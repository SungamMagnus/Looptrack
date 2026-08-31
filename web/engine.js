// Web Audio port of the plugin's single-track engine. Native nodes do
// everything except capturing raw input samples (recorder-worklet.js) --
// there's no built-in "record this stream" primitive, but delays, filters,
// waveshaping and the reverb network all map cleanly onto native nodes.

const LOOKAHEAD_SEC = 0.35;
const SCHEDULER_INTERVAL_MS = 100;
const WRAP_FADE_SEC = 0.005;

function dbToGain (db) { return Math.pow(10, db / 20); }
function clamp (v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

/** cents of peak pitch deviation -> delay-modulation amplitude in seconds,
    for a sinusoidal LFO at freqHz. Same derivation as the plugin's
    centsToDelaySamples, just expressed in seconds instead of samples. */
function centsToDelaySeconds (cents, freqHz) {
  const deltaRatio = Math.pow(2, cents / 1200) - 1;
  return deltaRatio / (2 * Math.PI * freqHz);
}

function makeBitcrushCurve (steps) {
  const n = 1024;
  const curve = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const x = (i / (n - 1)) * 2 - 1;
    curve[i] = Math.round(x * steps) / steps;
  }
  return curve;
}

function makeSoftClipCurve () {
  const n = 1024;
  const curve = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let x = (i / (n - 1)) * 2 - 1;
    x = clamp(x, -1.5, 1.5);
    curve[i] = x <= -1 ? -2 / 3 : x >= 1 ? 2 / 3 : x - (x * x * x) / 3;
  }
  return curve;
}

function makeNoiseBuffer (ctx, seconds) {
  const buf = ctx.createBuffer(2, Math.floor(ctx.sampleRate * seconds), ctx.sampleRate);
  for (let ch = 0; ch < 2; ch++) {
    const d = buf.getChannelData(ch);
    for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
  }
  return buf;
}

export class TapeEngine {
  constructor (ctx) {
    this.ctx = ctx;
    this.state = 'idle'; // idle | armed | recording | playing
    this.recordedBuffer = null;
    this.recordedSeconds = 0;
    this.trackPlayEnabled = true; // the track's own Play switch, independent of transport

    this.params = {
      bars: 2, source: 'input',
      inLow: 0, inHigh: 0, preamp: 0,
      volume: 0, pan: 0, hiss: 0.25,
      eqLow: 0, eqMid: 0, eqHigh: 0, filter: 0,
      sendDelay: -60, sendReverb: -60,
      speedSemis: 0, wowDepth: 0.3, wowRate: 1.0, flutterDepth: 0.25, flutterRate: 1.0,
      dlyMs: 250, dlyFb: 0.45, dlyTone: 0.5, dlyRet: 0,
      revSize: 0.55, revDamp: 0.5, revRet: 0,
      out: 1.5
    };

    this.bpm = 120;
    this.playing = false; // transport
    this.armed = false;
    this.transportStartCtxTime = 0;
    this.transportStartPpq = 0;

    this._recordStopScheduled = false;
    this._nextPassTime = 0;
    this._currentSource = null;
    this._schedulerTimer = null;
    this._recorderNode = null;
    this._recording = false;

    this._buildGraph();
  }

  // ---- transport (self-hosted -- no DAW here) ----

  get currentPpq () {
    if (!this.playing) return this.transportStartPpq;
    const elapsed = this.ctx.currentTime - this.transportStartCtxTime;
    return this.transportStartPpq + elapsed * (this.bpm / 60);
  }

  barSeconds () { return this.params.bars * 4 * (60 / this.bpm); }

  ppqToTime (ppq) {
    return this.transportStartCtxTime + (ppq - this.transportStartPpq) * (60 / this.bpm);
  }

  /** ctx time of the next bar boundary strictly after `fromTime`. */
  nextBoundaryTime (fromTime) {
    const barSec = this.barSeconds();
    const sinceStart = fromTime - this.transportStartCtxTime;
    const bars = Math.floor(sinceStart / barSec) + 1;
    return this.transportStartCtxTime + bars * barSec;
  }

  play () {
    if (this.playing) return;
    this.playing = true;
    this.transportStartCtxTime = this.ctx.currentTime;
    // transportStartPpq stays where it was -- resuming, not restarting

    if (this.state === 'armed') {
      // arming before playback started -- begin recording the instant it does
      this._beginRecording(this.ctx.currentTime);
    }
    this._tickScheduler();
    this._schedulerTimer = setInterval(() => this._tickScheduler(), SCHEDULER_INTERVAL_MS);
  }

  _updateMonitorGains () {
    const t = this.ctx.currentTime;
    const live = this.state === 'armed' || this.state === 'recording' ? 1 : 0;
    const loop = this.state === 'playing' && this.trackPlayEnabled ? 1 : 0;
    this._liveMonitorGain.gain.setTargetAtTime(live, t, 0.005);
    this._loopPlaybackGain.gain.setTargetAtTime(loop, t, 0.005);
  }

  setPlayEnabled (v) { this.trackPlayEnabled = v; this._updateMonitorGains(); }

  stop () {
    if (!this.playing) return;
    this.transportStartPpq = this.currentPpq;
    this.playing = false;
    if (this._schedulerTimer) { clearInterval(this._schedulerTimer); this._schedulerTimer = null; }
    // recording in progress when stopped: keep what's captured, but don't
    // finish the pass until the transport is playing again
  }

  // ---- record state machine ----

  toggleArm () {
    if (this.state === 'idle' || this.state === 'playing') {
      this.state = 'armed';
      if (this.playing) {
        // already rolling: quantize to the next bar (standard punch-in)
        const boundary = this.nextBoundaryTime(this.ctx.currentTime);
        this._scheduleArmedBoundary(boundary);
      }
      // if not playing yet, play() will catch the immediate-start case
    } else if (this.state === 'armed') {
      this.state = 'idle';
      this._armedTimeout && clearTimeout(this._armedTimeout);
    } else if (this.state === 'recording') {
      this._finishRecording(this.ctx.currentTime);
    }
    this._updateMonitorGains();
    this._onStateChange && this._onStateChange();
  }

  _scheduleArmedBoundary (time) {
    const delayMs = Math.max(0, (time - this.ctx.currentTime) * 1000);
    this._armedTimeout && clearTimeout(this._armedTimeout);
    this._armedTimeout = setTimeout(() => {
      if (this.state === 'armed') this._beginRecording(time);
    }, delayMs);
  }

  clear () {
    this.state = 'idle';
    this.recordedBuffer = null;
    this.recordedSeconds = 0;
    if (this._currentSource) { try { this._currentSource.stop(); } catch (e) {} this._currentSource = null; }
    this._updateMonitorGains();
    this._onStateChange && this._onStateChange();
  }

  async _beginRecording (atTime) {
    this.state = 'recording';
    this._recordStartTime = atTime;
    const capacitySeconds = 24; // matches the plugin: 40bpm, 4 bars
    this._recorderNode.port.postMessage({ type: 'start', capacity: Math.ceil(this.ctx.sampleRate * capacitySeconds) });
    this._recording = true;

    // stop recording at the following bar boundary
    const boundary = this.nextBoundaryTime(atTime);
    const delayMs = Math.max(0, (boundary - this.ctx.currentTime) * 1000);
    this._recordTimeout = setTimeout(() => {
      if (this.state === 'recording') this._finishRecording(boundary);
    }, delayMs);

    this._updateMonitorGains();
    this._onStateChange && this._onStateChange();
  }

  _finishRecording (atTime) {
    this._recordTimeout && clearTimeout(this._recordTimeout);
    if (!this._recording) return;
    this._recording = false;
    this._recorderNode.port.postMessage({ type: 'stop' });
    this._pendingFinishTime = atTime;
  }

  _onRecordedData (msg) {
    const sr = this.ctx.sampleRate;
    const len = msg.length;
    if (len < 8) { this.state = 'idle'; this._updateMonitorGains(); this._onStateChange && this._onStateChange(); return; }

    const buf = this.ctx.createBuffer(2, len, sr);
    const fadeLen = Math.min(len >> 1, Math.round(sr * WRAP_FADE_SEC));
    for (let ch = 0; ch < 2; ch++) {
      const src = ch === 0 ? msg.bufferL : msg.bufferR;
      const d = buf.getChannelData(ch);
      d.set(src);
      for (let i = 0; i < fadeLen; i++) {
        const g = i / fadeLen;
        d[i] *= g;
        d[len - 1 - i] *= g;
      }
    }
    this.recordedBuffer = buf;
    this.recordedSeconds = len / sr;
    this.state = 'playing';

    const startTime = this._pendingFinishTime || this.ctx.currentTime;
    this._nextPassTime = startTime;
    this._updateMonitorGains();
    this._onStateChange && this._onStateChange();
  }

  // ---- lookahead scheduler for loop passes ----

  _tickScheduler () {
    if (this.state !== 'playing' || !this.recordedBuffer) return;
    while (this._nextPassTime < this.ctx.currentTime + LOOKAHEAD_SEC) {
      this._schedulePass(this._nextPassTime);
      this._nextPassTime += this.barSeconds();
    }
  }

  _schedulePass (startTime) {
    const rate = Math.pow(2, this.params.speedSemis / 12);
    const src = this.ctx.createBufferSource();
    src.buffer = this.recordedBuffer;
    src.playbackRate.setValueAtTime(rate, startTime);
    src.connect(this._loopPlaybackGain);

    const barSec = this.barSeconds();
    const naturalDuration = this.recordedSeconds / rate;
    if (naturalDuration >= barSec) {
      src.start(startTime);
      src.stop(startTime + barSec);
    } else {
      src.start(startTime); // plays once, then silence until the next pass
    }
    this._currentSource = src;
    src.addEventListener('ended', () => { if (this._currentSource === src) this._currentSource = null; });
  }

  setSpeed (semis) {
    this.params.speedSemis = semis;
    if (this._currentSource) {
      const rate = Math.pow(2, semis / 12);
      this._currentSource.playbackRate.setTargetAtTime(rate, this.ctx.currentTime, 0.02);
    }
  }

  // ---- visualization state, for the UI to poll ----

  /** The input stage (preamp + in-EQ) is a record-chain stage: it shapes
      what goes onto the tape, and has nothing to act on once a loop is
      playing back. The UI greys those controls out when this is false. */
  isInputStageActive () { return this.state !== 'playing'; }

  getVizState () {
    const barSec = this.barSeconds();
    let filledFrac = 0, headFrac = null, label = 'IDLE';
    if (this.state === 'armed') label = 'ARMED';
    else if (this.state === 'recording') {
      label = 'RECORDING';
      const elapsed = this.ctx.currentTime - (this._recordStartTime || this.ctx.currentTime);
      filledFrac = clamp(elapsed / barSec, 0, 1);
      headFrac = filledFrac;
    } else if (this.state === 'playing') {
      label = 'PLAYING';
      filledFrac = clamp(this.recordedSeconds / barSec, 0, 1);
      const sinceLastBoundary = ((this.ctx.currentTime - this._nextPassTime) % barSec + barSec) % barSec;
      headFrac = clamp(sinceLastBoundary / barSec, 0, 1);
    }
    return { state: label, filledFrac, headFrac };
  }

  // ---- audio graph ----

  _buildGraph () {
    const ctx = this.ctx;

    // -- input stage: 2-band shelf EQ + preamp/drive --
    this.inLowShelf = new BiquadFilterNode(ctx, { type: 'lowshelf', frequency: 150, Q: 0.707 });
    this.inHighShelf = new BiquadFilterNode(ctx, { type: 'highshelf', frequency: 6000, Q: 0.707 });
    this.preampGain = new GainNode(ctx, { gain: 1 });
    this.preampDrive = new WaveShaperNode(ctx, { curve: makeSoftClipCurve(), oversample: '2x' });
    this.preampDryWet = new GainNode(ctx, { gain: 0 }); // crossfades in the driven path above 0dB
    this.preampDry = new GainNode(ctx, { gain: 1 });
    this.inputStageOut = new GainNode(ctx, { gain: 1 });

    this.inLowShelf.connect(this.inHighShelf).connect(this.preampGain);
    this.preampGain.connect(this.preampDry).connect(this.inputStageOut);
    this.preampGain.connect(this.preampDrive).connect(this.preampDryWet).connect(this.inputStageOut);

    // -- record/monitor vs loop-playback switch --
    this._liveMonitorGain = new GainNode(ctx, { gain: 1 });
    this._loopPlaybackGain = new GainNode(ctx, { gain: 0 });
    this.inputStageOut.connect(this._liveMonitorGain);
    const postSource = new GainNode(ctx, { gain: 1 });
    this._liveMonitorGain.connect(postSource);
    this._loopPlaybackGain.connect(postSource);

    // -- wow/flutter: modulated delay line --
    this.wfDelay = new DelayNode(ctx, { delayTime: 0.02, maxDelayTime: 0.05 });
    this._buildWowFlutterLfos();
    postSource.connect(this.wfDelay);

    // -- hiss --
    this.hissSource = new AudioBufferSourceNode(ctx, { buffer: makeNoiseBuffer(ctx, 4), loop: true });
    this.hissHp = new BiquadFilterNode(ctx, { type: 'highpass', frequency: 800, Q: 0.707 });
    this.hissLp = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 14000, Q: 0.707 });
    this.hissGain = new GainNode(ctx, { gain: 0 });
    this.hissSource.connect(this.hissHp).connect(this.hissLp).connect(this.hissGain);
    this.hissSource.start();

    const afterCharacter = new GainNode(ctx, { gain: 1 });
    this.wfDelay.connect(afterCharacter);
    this.hissGain.connect(afterCharacter);

    // -- 3-band EQ --
    this.eqLow = new BiquadFilterNode(ctx, { type: 'lowshelf', frequency: 120, Q: 0.707 });
    this.eqMid = new BiquadFilterNode(ctx, { type: 'peaking', frequency: 900, Q: 0.9 });
    this.eqHigh = new BiquadFilterNode(ctx, { type: 'highshelf', frequency: 3500, Q: 0.707 });
    afterCharacter.connect(this.eqLow).connect(this.eqMid).connect(this.eqHigh);

    // -- DJ filter: dry/filtered crossfade, hard-switched like the plugin --
    this.filterNode = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 18000, Q: 0.707 });
    this.filterDry = new GainNode(ctx, { gain: 1 });
    this.filterWet = new GainNode(ctx, { gain: 0 });
    const afterFilter = new GainNode(ctx, { gain: 1 });
    this.eqHigh.connect(this.filterDry).connect(afterFilter);
    this.eqHigh.connect(this.filterNode).connect(this.filterWet).connect(afterFilter);

    // -- volume (output fader, post-filter) + pan --
    this.volumeGain = new GainNode(ctx, { gain: 1 });
    afterFilter.connect(this.volumeGain);
    this.panNode = new StereoPannerNode(ctx, { pan: 0 });
    this.volumeGain.connect(this.panNode);

    // -- sends (post-EQ/filter/volume, pre-pan, mono) -- riding the fader
    // down takes the sends with it, same as a real console's post-fader aux
    this.sendDelayGain = new GainNode(ctx, { gain: 0 });
    this.sendReverbGain = new GainNode(ctx, { gain: 0 });
    this.volumeGain.connect(this.sendDelayGain);
    this.volumeGain.connect(this.sendReverbGain);

    // -- lofi bus --
    this._buildLofiBus();
    this.sendDelayGain.connect(this.lofiDelayIn);
    this.sendReverbGain.connect(this.lofiReverbIn);

    // -- master --
    this.outGain = new GainNode(ctx, { gain: dbToGain(this.params.out) });
    this.panNode.connect(this.outGain);
    this.lofiOut.connect(this.outGain);
    this.outGain.connect(ctx.destination);

    this.meterAnalyser = new AnalyserNode(ctx, { fftSize: 256 });
    this.outGain.connect(this.meterAnalyser);
  }

  _buildWowFlutterLfos () {
    const ctx = this.ctx;
    const bands = [
      { freq: 0.5, base: 'wowDepth', maxCents: 35, rateKey: 'wowRate' },
      { freq: 0.87, base: 'wowDepth', maxCents: 35, rateKey: 'wowRate' },
      { freq: 6.3, base: 'flutterDepth', maxCents: 12, rateKey: 'flutterRate' },
      { freq: 9.7, base: 'flutterDepth', maxCents: 12, rateKey: 'flutterRate' }
    ];
    this._wfLfos = bands.map((b) => {
      const osc = new OscillatorNode(ctx, { type: 'sine', frequency: b.freq });
      const depthGain = new GainNode(ctx, { gain: 0 });
      osc.connect(depthGain).connect(this.wfDelay.delayTime);
      osc.start();
      return { osc, depthGain, ...b };
    });
    this._updateWowFlutterDepths();
  }

  _updateWowFlutterDepths () {
    for (const lfo of this._wfLfos) {
      const depth01 = this.params[lfo.base];
      const rate = this.params[lfo.rateKey];
      const freq = lfo.freq * rate;
      lfo.osc.frequency.setTargetAtTime(freq, this.ctx.currentTime, 0.05);
      const cents = depth01 * lfo.maxCents;
      const seconds = centsToDelaySeconds(cents, freq);
      lfo.depthGain.gain.setTargetAtTime(seconds, this.ctx.currentTime, 0.05);
    }
  }

  _buildLofiBus () {
    const ctx = this.ctx;

    // -- delay: band-limited, bit-crushed, soft-clipped, ping-pong feedback --
    this.lofiDelayIn = new GainNode(ctx, { gain: 1 });
    this.dlyNodeL = new DelayNode(ctx, { delayTime: 0.25, maxDelayTime: 2.2 });
    this.dlyNodeR = new DelayNode(ctx, { delayTime: 0.25, maxDelayTime: 2.2 });
    this.dlyHpL = new BiquadFilterNode(ctx, { type: 'highpass', frequency: 250 });
    this.dlyHpR = new BiquadFilterNode(ctx, { type: 'highpass', frequency: 250 });
    this.dlyLpL = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 3200 });
    this.dlyLpR = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 3200 });
    this.dlyCrushL = new WaveShaperNode(ctx, { curve: makeBitcrushCurve(512) });
    this.dlyCrushR = new WaveShaperNode(ctx, { curve: makeBitcrushCurve(512) });
    this.dlyClipL = new WaveShaperNode(ctx, { curve: makeSoftClipCurve() });
    this.dlyClipR = new WaveShaperNode(ctx, { curve: makeSoftClipCurve() });
    this.dlyFbL = new GainNode(ctx, { gain: 0.45 });
    this.dlyFbR = new GainNode(ctx, { gain: 0.45 });
    this.dlyReturn = new GainNode(ctx, { gain: 1 });
    this.dlyMerge = new ChannelMergerNode(ctx, { numberOfInputs: 2 });

    this.lofiDelayIn.connect(this.dlyNodeL);
    this.lofiDelayIn.connect(this.dlyNodeR);
    this.dlyNodeL.connect(this.dlyHpL).connect(this.dlyLpL).connect(this.dlyCrushL).connect(this.dlyClipL);
    this.dlyNodeR.connect(this.dlyHpR).connect(this.dlyLpR).connect(this.dlyCrushR).connect(this.dlyClipR);
    // ping-pong: each channel's repeat feeds the OTHER channel's delay input
    this.dlyClipL.connect(this.dlyFbL).connect(this.dlyNodeR);
    this.dlyClipR.connect(this.dlyFbR).connect(this.dlyNodeL);
    this.dlyClipL.connect(this.dlyReturn);
    this.dlyClipR.connect(this.dlyReturn);
    this.dlyClipL.connect(this.dlyMerge, 0, 0);
    this.dlyClipR.connect(this.dlyMerge, 0, 1);

    // -- reverb: 8 combs + 4 allpasses per channel (Freeverb topology) --
    const combTuningL = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617].map((s) => s / 44100);
    const allpassTuningL = [556, 441, 341, 225].map((s) => s / 44100);
    const spread = 23 / 44100;

    this.lofiReverbIn = new GainNode(ctx, { gain: 1 });
    this.reverbInputLp = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 5500 });
    this.reverbCrush = new WaveShaperNode(ctx, { curve: makeBitcrushCurve(2048) });
    this.lofiReverbIn.connect(this.reverbInputLp).connect(this.reverbCrush);

    const buildChannel = (tunings) => {
      const combOuts = [];
      const combDamps = [];
      const combFbs = [];
      for (const t of tunings) {
        const d = new DelayNode(ctx, { delayTime: t, maxDelayTime: 0.06 });
        const damp = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 4000 });
        const fb = new GainNode(ctx, { gain: 0.84 });
        this.reverbCrush.connect(d);
        d.connect(damp).connect(fb).connect(d);
        combOuts.push(damp);
        combDamps.push(damp);
        combFbs.push(fb);
      }
      const sum = new GainNode(ctx, { gain: 1 / combOuts.length });
      combOuts.forEach((o) => o.connect(sum));
      let node = sum;
      const allpasses = [];
      for (const t of allpassTuningL) {
        const ap = this._makeAllpass(t);
        node.connect(ap.input);
        node = ap.output;
        allpasses.push(ap);
      }
      return { out: node, combDamps, combFbs };
    };

    this.reverbL = buildChannel(combTuningL);
    this.reverbR = buildChannel(combTuningL.map((t) => t + spread));

    this.revReturn = new GainNode(ctx, { gain: 1 });
    this.revMerge = new ChannelMergerNode(ctx, { numberOfInputs: 2 });
    this.reverbL.out.connect(this.revReturn);
    this.reverbR.out.connect(this.revReturn);
    this.reverbL.out.connect(this.revMerge, 0, 0);
    this.reverbR.out.connect(this.revMerge, 0, 1);

    this.lofiOut = new GainNode(ctx, { gain: 1 });
    this.dlyMerge.connect(this.lofiOut);
    this.revMerge.connect(this.lofiOut);
  }

  /** Schroeder allpass: y = -x + delayed(x); delayed = x + delayed*fb. */
  _makeAllpass (delaySeconds, feedback = 0.5) {
    const ctx = this.ctx;
    const input = new GainNode(ctx, { gain: 1 });
    const delay = new DelayNode(ctx, { delayTime: delaySeconds, maxDelayTime: 0.03 });
    const fb = new GainNode(ctx, { gain: feedback });
    const negIn = new GainNode(ctx, { gain: -1 });
    const output = new GainNode(ctx, { gain: 1 });

    input.connect(delay);
    delay.connect(fb).connect(delay);
    input.connect(negIn).connect(output);
    delay.connect(output);

    return { input, output };
  }

  /** Sets up the recorder worklet, tapping the input stage's OUTPUT -- what
      gets captured is the EQ'd/driven signal, same as the plugin, not raw
      input. Independent of whether a mic ever connects, so the transport,
      panel and test tone all work without microphone access. */
  async setupRecorder () {
    await this.ctx.audioWorklet.addModule('recorder-worklet.js');
    this._recorderNode = new AudioWorkletNode(this.ctx, 'recorder-processor', { numberOfInputs: 1, numberOfOutputs: 1, channelCount: 2, channelCountMode: 'explicit' });
    this.inputStageOut.connect(this._recorderNode);
    // the node outputs silence (it only listens); connect it to a muted sink
    // so the graph keeps pulling it -- an unconnected worklet output is never processed
    const sink = new GainNode(this.ctx, { gain: 0 });
    this._recorderNode.connect(sink).connect(this.ctx.destination);
    this._recorderNode.port.onmessage = (e) => {
      if (e.data.type === 'recorded') this._onRecordedData(e.data);
    };
  }

  async connectMic () {
    const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    const src = this.ctx.createMediaStreamSource(stream);
    src.connect(this.inLowShelf);
    return src;
  }

  /** A 220Hz oscillator into the same input point a mic would use -- lets
      the whole record/loop/character chain be tried (or verified) without
      microphone access. Off by default. */
  addTestTone () {
    const osc = new OscillatorNode(this.ctx, { type: 'sine', frequency: 220 });
    const gain = new GainNode(this.ctx, { gain: 0 });
    osc.connect(gain).connect(this.inLowShelf);
    osc.start();
    this._testToneGain = gain;
    return {
      setOn: (on) => this._testToneGain.gain.setTargetAtTime(on ? 0.3 : 0, this.ctx.currentTime, 0.02)
    };
  }

  onStateChange (cb) { this._onStateChange = cb; }

  // ---- parameter setters ----

  setBars (bars) { this.params.bars = bars; }
  setSource (src) { this.params.source = src; }

  setInLow (db) { this.params.inLow = db; this.inLowShelf.gain.setTargetAtTime(db, this.ctx.currentTime, 0.02); }
  setInHigh (db) { this.params.inHigh = db; this.inHighShelf.gain.setTargetAtTime(db, this.ctx.currentTime, 0.02); }

  setPreamp (db) {
    this.params.preamp = db;
    const gain = dbToGain(db);
    const driveAmount = clamp(db / 18, 0, 1);
    const t = this.ctx.currentTime;
    this.preampGain.gain.setTargetAtTime(gain, t, 0.02);
    this.preampDry.gain.setTargetAtTime(1 - driveAmount, t, 0.02);
    this.preampDryWet.gain.setTargetAtTime(driveAmount, t, 0.02);
  }

  setVolume (db) { this.params.volume = db; this.volumeGain.gain.setTargetAtTime(dbToGain(db), this.ctx.currentTime, 0.02); }
  setPan (v) { this.params.pan = v; this.panNode.pan.setTargetAtTime(v, this.ctx.currentTime, 0.02); }
  setHiss (v) { this.params.hiss = v; this.hissGain.gain.setTargetAtTime(dbToGain(-90 + v * 42), this.ctx.currentTime, 0.03); }

  setWowDepth (v) { this.params.wowDepth = v; this._updateWowFlutterDepths(); }
  setWowRate (v) { this.params.wowRate = v; this._updateWowFlutterDepths(); }
  setFlutterDepth (v) { this.params.flutterDepth = v; this._updateWowFlutterDepths(); }
  setFlutterRate (v) { this.params.flutterRate = v; this._updateWowFlutterDepths(); }

  setEqLow (db) { this.params.eqLow = db; this.eqLow.gain.setTargetAtTime(db, this.ctx.currentTime, 0.02); }
  setEqMid (db) { this.params.eqMid = db; this.eqMid.gain.setTargetAtTime(db, this.ctx.currentTime, 0.02); }
  setEqHigh (db) { this.params.eqHigh = db; this.eqHigh.gain.setTargetAtTime(db, this.ctx.currentTime, 0.02); }

  setFilter (k) {
    this.params.filter = k;
    const absK = Math.abs(k);
    const t = this.ctx.currentTime;
    if (absK < 0.02) {
      this.filterDry.gain.setTargetAtTime(1, t, 0.02);
      this.filterWet.gain.setTargetAtTime(0, t, 0.02);
      return;
    }
    const q = 0.707 + (absK - 0.02) / 0.98 * (2.0 - 0.707);
    const shaped = (absK - 0.02) / 0.98;
    const fc = k < 0 ? 18000 * Math.pow(200 / 18000, shaped) : 20 * Math.pow(6000 / 20, shaped);
    this.filterNode.type = k < 0 ? 'lowpass' : 'highpass';
    this.filterNode.frequency.setTargetAtTime(fc, t, 0.02);
    this.filterNode.Q.setTargetAtTime(q, t, 0.02);
    this.filterDry.gain.setTargetAtTime(0, t, 0.02);
    this.filterWet.gain.setTargetAtTime(1, t, 0.02);
  }

  setSendDelay (db) { this.params.sendDelay = db; this.sendDelayGain.gain.setTargetAtTime(dbToGain(db), this.ctx.currentTime, 0.02); }
  setSendReverb (db) { this.params.sendReverb = db; this.sendReverbGain.gain.setTargetAtTime(dbToGain(db), this.ctx.currentTime, 0.02); }

  setDelayMs (ms) { this.params.dlyMs = ms; this._updateDelayTime(); }
  setDelayFeedback (v) {
    this.params.dlyFb = v;
    this.dlyFbL.gain.setTargetAtTime(v, this.ctx.currentTime, 0.02);
    this.dlyFbR.gain.setTargetAtTime(v, this.ctx.currentTime, 0.02);
  }
  setDelayTone (v) {
    this.params.dlyTone = v;
    const hz = 1800 + v * (6000 - 1800);
    this.dlyLpL.frequency.setTargetAtTime(hz, this.ctx.currentTime, 0.02);
    this.dlyLpR.frequency.setTargetAtTime(hz, this.ctx.currentTime, 0.02);
  }
  setDelayReturn (db) { this.params.dlyRet = db; this.dlyReturn.gain.setTargetAtTime(dbToGain(db), this.ctx.currentTime, 0.02); }

  setReverbSize (v) {
    this.params.revSize = v;
    const fb = 0.70 + v * (0.98 - 0.70);
    for (const g of [...this.reverbL.combFbs, ...this.reverbR.combFbs]) g.gain.setTargetAtTime(fb, this.ctx.currentTime, 0.02);
  }
  setReverbDamp (v) {
    this.params.revDamp = v;
    const hz = 8000 - v * 7200;
    for (const f of [...this.reverbL.combDamps, ...this.reverbR.combDamps]) f.frequency.setTargetAtTime(hz, this.ctx.currentTime, 0.02);
  }
  setReverbReturn (db) { this.params.revRet = db; this.revReturn.gain.setTargetAtTime(dbToGain(db), this.ctx.currentTime, 0.02); }

  setOutput (db) { this.params.out = db; this.outGain.gain.setTargetAtTime(dbToGain(db), this.ctx.currentTime, 0.02); }

  setBpm (bpm) {
    // resync so the current musical position doesn't jump. The delay is
    // free-running in ms, so tempo no longer touches it.
    this.transportStartPpq = this.currentPpq;
    this.transportStartCtxTime = this.ctx.currentTime;
    this.bpm = bpm;
  }

  _updateDelayTime () {
    const seconds = clamp(this.params.dlyMs / 1000, 0.001, 2.0);
    this.dlyNodeL.delayTime.setTargetAtTime(seconds, this.ctx.currentTime, 0.05);
    this.dlyNodeR.delayTime.setTargetAtTime(seconds, this.ctx.currentTime, 0.05);
  }
}

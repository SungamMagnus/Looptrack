import { TapeEngine } from './engine.js';
import {
  createKnob, createSelector, createLatch, createLamp, createTerminal,
  createFrame, createMeter, createLoopView, createWordmark
} from './components.js';

const statusLine = document.getElementById('statusLine');
const btnEnable = document.getElementById('btnEnable');
const btnPlay = document.getElementById('btnPlay');
const bpmInput = document.getElementById('bpmInput');
const ltBody = document.getElementById('ltBody');

function db (v) { return v <= -59.9 ? 'OFF' : (v >= 0 ? '+' : '') + v.toFixed(1) + ' dB'; }
function pct (v) { return Math.round(v * 100) + '%'; }
function filterText (v) { return Math.abs(v) < 0.02 ? 'OFF' : (v < 0 ? 'LP' : 'HP'); }
function ratioText (v) { return v.toFixed(2) + 'x'; }
function panText (v) { return v.toFixed(2); }
function speedText (v) { return (v >= 0 ? '+' : '') + v.toFixed(1) + ' st (' + Math.pow(2, v / 12).toFixed(2) + 'x)'; }

function labeled (text, el) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;flex-direction:column;align-items:center;gap:1px';
  const l = document.createElement('span');
  l.className = 'lt-control-label';
  l.textContent = text;
  wrap.append(l, el);
  return wrap;
}

function buildTrack1 (engine, testTone) {
  const frame = createFrame({ title: 'TRACK 1', color: 'var(--coral)' });
  frame.el.classList.add('lt-track');
  const body = document.createElement('div');
  body.className = 'lt-track-body';
  frame.body.appendChild(body);

  const topRow = document.createElement('div');
  topRow.className = 'lt-row';

  const leftCol = document.createElement('div');
  leftCol.style.cssText = 'display:flex;flex-direction:column;gap:6px;width:184px';

  const selRow = document.createElement('div');
  selRow.className = 'lt-row';
  const barsSel = createSelector({ options: ['1', '2', '3', '4'], selected: 1, onChange: (i) => engine.setBars(i + 1) });
  const srcSel = createSelector({ options: ['Input', 'Sample'], selected: 0, onChange: (i) => engine.setSource(i === 0 ? 'input' : 'sample') });
  selRow.append(labeled('BARS', barsSel.el), labeled('SRC', srcSel.el));

  const stateRow = document.createElement('div');
  stateRow.style.cssText = 'display:flex;justify-content:space-between;align-items:center';
  const stateLabel = document.createElement('span');
  stateLabel.className = 'lt-state-label';
  stateLabel.textContent = 'IDLE';
  stateLabel.style.color = 'var(--ink-45)';
  const lampRow = document.createElement('div');
  lampRow.style.cssText = 'display:flex;gap:5px;align-items:center';
  const recLamp = createLamp({ color: '#ed8159' });
  const playLamp = createLamp({ color: '#52b0a4' });
  lampRow.append(recLamp.el, playLamp.el);
  stateRow.append(stateLabel, lampRow);

  const latchRow = document.createElement('div');
  latchRow.style.cssText = 'display:flex;gap:5px';
  const recLatch = createLatch({ label: 'REC', color: '#ed8159', onToggle: () => engine.toggleArm() });
  const playLatch = createLatch({ label: 'PLAY', color: '#52b0a4', on: true, onToggle: (on) => engine.setPlayEnabled(on) });
  const clrLatch = createLatch({
    label: 'CLR', color: '#4f7ea8',
    onToggle: () => { engine.clear(); clrLatch.setOn(false); }
  });
  const toneLatch = createLatch({
    label: 'TONE', color: '#6b5bc4',
    onToggle: (on) => testTone.setOn(on)
  });
  latchRow.append(recLatch.el, playLatch.el, clrLatch.el, toneLatch.el);

  leftCol.append(selRow, stateRow, latchRow);

  const loopView = createLoopView();
  loopView.el.style.flex = '1';
  topRow.append(leftCol, loopView.el);

  const chRow = document.createElement('div');
  chRow.style.cssText = 'width:288px;display:flex;justify-content:space-between;gap:2px';
  const inLowK = createKnob({ label: 'In Low', min: -12, max: 12, value: 0, format: db, bipolar: true, color: '#ed8159', onChange: (v) => engine.setInLow(v) });
  const inHighK = createKnob({ label: 'In High', min: -12, max: 12, value: 0, format: db, bipolar: true, color: '#ed8159', onChange: (v) => engine.setInHigh(v) });
  const hissK = createKnob({ label: 'Hiss', min: 0, max: 1, value: 0.25, format: pct, color: '#52b0a4', onChange: (v) => engine.setHiss(v) });
  chRow.append(inLowK.el, inHighK.el, hissK.el);

  const eqSection = document.createElement('div');
  eqSection.style.cssText = 'width:288px;display:flex;flex-direction:column;gap:8px';
  const eqRow = document.createElement('div');
  eqRow.style.cssText = 'display:flex;justify-content:space-between;gap:6px';

  const eqCol = document.createElement('div');
  eqCol.style.cssText = 'display:flex;flex-direction:column;gap:2px';
  const lowK = createKnob({ label: 'Low', min: -18, max: 18, value: 0, format: db, bipolar: true, color: '#4f7ea8', onChange: (v) => engine.setEqLow(v) });
  const midK = createKnob({ label: 'Mid', min: -18, max: 18, value: 0, format: db, bipolar: true, color: '#4f7ea8', onChange: (v) => engine.setEqMid(v) });
  const highK = createKnob({ label: 'High', min: -18, max: 18, value: 0, format: db, bipolar: true, color: '#4f7ea8', onChange: (v) => engine.setEqHigh(v) });
  eqCol.append(lowK.el, midK.el, highK.el);

  const fxCol = document.createElement('div');
  fxCol.style.cssText = 'display:flex;flex-direction:column;gap:2px';
  const filterK = createKnob({ label: 'Filter', min: -1, max: 1, value: 0, format: filterText, bipolar: true, color: '#4f7ea8', onChange: (v) => engine.setFilter(v) });
  const sendDlyK = createKnob({ label: 'To Dly', min: -60, max: 0, value: -60, format: db, color: '#4f7ea8', onChange: (v) => engine.setSendDelay(v) });
  const sendRevK = createKnob({ label: 'To Verb', min: -60, max: 0, value: -60, format: db, color: '#4f7ea8', onChange: (v) => engine.setSendReverb(v) });
  fxCol.append(filterK.el, sendDlyK.el, sendRevK.el);

  eqRow.append(eqCol, fxCol);

  const panRow = document.createElement('div');
  panRow.style.cssText = 'width:288px;display:flex;justify-content:center;gap:14px;padding-top:2px;border-top:1px solid var(--ink-13)';
  const panK = createKnob({ label: 'Pan', min: -1, max: 1, value: 0, format: panText, bipolar: true, color: '#4f7ea8', onChange: (v) => engine.setPan(v) });
  const preampK = createKnob({ label: 'Preamp', min: -24, max: 18, value: 0, format: db, color: '#ed8159', onChange: (v) => engine.setPreamp(v) });
  panRow.append(panK.el, preampK.el);

  eqSection.append(eqRow, panRow);
  body.append(topRow, chRow, eqSection);

  engine.onStateChange(() => {
    const st = engine.state;
    stateLabel.textContent = st.toUpperCase();
    stateLabel.style.color = st === 'recording' ? 'var(--coral)' : st === 'playing' ? 'var(--teal)' : st === 'armed' ? 'var(--amber)' : 'var(--ink-45)';
    recLamp.setOn(st === 'armed' || st === 'recording');
    playLamp.setOn(st === 'playing');
    recLatch.setOn(st === 'armed' || st === 'recording');
  });

  (function tick () {
    loopView.update(engine.getVizState());
    requestAnimationFrame(tick);
  })();

  engine.setInLow(0); engine.setInHigh(0); engine.setPreamp(0); engine.setPan(0); engine.setHiss(0.25);
  engine.setEqLow(0); engine.setEqMid(0); engine.setEqHigh(0); engine.setFilter(0);
  engine.setSendDelay(-60); engine.setSendReverb(-60);

  return frame.el;
}

function buildDummyTrack (label) {
  const frame = createFrame({ title: label, color: 'var(--coral)' });
  frame.el.classList.add('lt-track', 'lt-track-disabled');
  const body = document.createElement('div');
  body.className = 'lt-track-body';
  body.style.minHeight = '660px';
  body.innerHTML = '<div style="font-size:8px;color:var(--ink-45);padding-top:20px;text-align:center">not yet implemented —<br>identical to Track 1<br>once the plugin has it</div>';
  frame.body.appendChild(body);
  return frame.el;
}

function buildGlobal (engine) {
  const wrap = document.createElement('div');
  wrap.className = 'lt-global';

  const heading = document.createElement('div');
  heading.className = 'lt-global-heading';
  heading.textContent = 'GLOBAL';

  const speedRow = document.createElement('div');
  speedRow.style.cssText = 'display:flex;justify-content:center';
  const speedK = createKnob({ label: 'Speed', min: -12, max: 12, value: 0, format: speedText, bipolar: true, color: '#ed8159', onChange: (v) => engine.setSpeed(v) });
  speedRow.appendChild(speedK.el);

  const charFrame = createFrame({ title: 'Tape Character', color: 'var(--teal)' });
  const charRow = document.createElement('div');
  charRow.style.cssText = 'display:flex;justify-content:center;gap:12px;padding-top:10px;flex-wrap:wrap';
  const wowK = createKnob({ label: 'Wow', min: 0, max: 1, value: 0.3, format: pct, color: '#52b0a4', onChange: (v) => engine.setWowDepth(v) });
  const wowRateK = createKnob({ label: 'Wow Rate', min: 0.25, max: 4, value: 1, format: ratioText, color: '#52b0a4', onChange: (v) => engine.setWowRate(v) });
  const flutterK = createKnob({ label: 'Flutter', min: 0, max: 1, value: 0.25, format: pct, color: '#52b0a4', onChange: (v) => engine.setFlutterDepth(v) });
  const flutterRateK = createKnob({ label: 'Flutter Rate', min: 0.25, max: 4, value: 1, format: ratioText, color: '#52b0a4', onChange: (v) => engine.setFlutterRate(v) });
  charRow.append(wowK.el, wowRateK.el, flutterK.el, flutterRateK.el);
  charFrame.body.appendChild(charRow);

  const dlyFrame = createFrame({ title: 'Delay', color: 'var(--teal)' });
  const dlyWrap = document.createElement('div');
  dlyWrap.style.cssText = 'display:flex;flex-direction:column;align-items:center;gap:8px;padding-top:10px';
  const dlySel = createSelector({ options: ['1/16', '1/8T', '1/8', '1/8.', '1/4', '1/4.', '1/2'], selected: 2, onChange: (i) => engine.setDelayDiv(i) });
  const dlyKnobRow = document.createElement('div');
  dlyKnobRow.style.cssText = 'display:flex;gap:6px';
  const dlyFbK = createKnob({ label: 'Fb', min: 0, max: 0.95, value: 0.45, format: pct, color: '#52b0a4', onChange: (v) => engine.setDelayFeedback(v) });
  const dlyToneK = createKnob({ label: 'Tone', min: 0, max: 1, value: 0.5, format: pct, color: '#52b0a4', onChange: (v) => engine.setDelayTone(v) });
  const dlyRetK = createKnob({ label: 'Return', min: -60, max: 6, value: 0, format: db, bipolar: true, color: '#52b0a4', onChange: (v) => engine.setDelayReturn(v) });
  dlyKnobRow.append(dlyFbK.el, dlyToneK.el, dlyRetK.el);
  dlyWrap.append(dlySel.el, dlyKnobRow);
  dlyFrame.body.appendChild(dlyWrap);

  const revFrame = createFrame({ title: 'Reverb', color: 'var(--teal)' });
  const revRow = document.createElement('div');
  revRow.style.cssText = 'display:flex;justify-content:center;gap:6px;padding-top:10px';
  const revSizeK = createKnob({ label: 'Size', min: 0, max: 1, value: 0.55, format: pct, color: '#52b0a4', onChange: (v) => engine.setReverbSize(v) });
  const revDampK = createKnob({ label: 'Damp', min: 0, max: 1, value: 0.5, format: pct, color: '#52b0a4', onChange: (v) => engine.setReverbDamp(v) });
  const revRetK = createKnob({ label: 'Return', min: -60, max: 6, value: 0, format: db, bipolar: true, color: '#52b0a4', onChange: (v) => engine.setReverbReturn(v) });
  revRow.append(revSizeK.el, revDampK.el, revRetK.el);
  revFrame.body.appendChild(revRow);

  const outRow = document.createElement('div');
  outRow.style.cssText = 'display:flex;align-items:center;justify-content:space-between;margin-top:2px';
  const outLeft = document.createElement('div');
  outLeft.style.cssText = 'display:flex;flex-direction:column;gap:6px';
  const meterLabel = document.createElement('span');
  meterLabel.className = 'lt-control-label';
  meterLabel.textContent = 'OUT LEVEL';
  meterLabel.style.textAlign = 'left';
  const meter = createMeter();
  const termRow = document.createElement('div');
  termRow.style.cssText = 'display:flex;gap:10px;margin-top:6px';
  termRow.append(createTerminal('OUT L'), createTerminal('OUT R'));
  outLeft.append(meterLabel, meter.el, termRow);
  const outK = createKnob({ label: 'Output', min: -60, max: 6, value: 1.5, format: db, bipolar: true, color: '#4f7ea8', onChange: (v) => engine.setOutput(v) });
  outRow.append(outLeft, outK.el);

  const wordmark = createWordmark('LOOPTRACK');
  wordmark.style.marginTop = 'auto';
  wordmark.style.paddingTop = '10px';

  wrap.append(heading, speedRow, charFrame.el, dlyFrame.el, revFrame.el, outRow, wordmark);

  engine.setSpeed(0); engine.setWowDepth(0.3); engine.setWowRate(1); engine.setFlutterDepth(0.25); engine.setFlutterRate(1);
  engine.setDelayDiv(2); engine.setDelayFeedback(0.45); engine.setDelayTone(0.5); engine.setDelayReturn(0);
  engine.setReverbSize(0.55); engine.setReverbDamp(0.5); engine.setReverbReturn(0);
  engine.setOutput(1.5);

  const meterData = new Float32Array(engine.meterAnalyser.fftSize);
  (function tick () {
    engine.meterAnalyser.getFloatTimeDomainData(meterData);
    let peak = 0;
    for (let i = 0; i < meterData.length; i++) peak = Math.max(peak, Math.abs(meterData[i]));
    meter.setLevel(peak);
    requestAnimationFrame(tick);
  })();

  return wrap;
}

function withTimeout (promise, ms) {
  return Promise.race([
    promise,
    new Promise((_, reject) => setTimeout(() => reject(new Error('timed out')), ms))
  ]);
}

btnEnable.addEventListener('click', async () => {
  btnEnable.disabled = true;
  statusLine.textContent = 'Starting engine…';
  try {
    const ctx = new (window.AudioContext || window.webkitAudioContext)();
    const engine = new TapeEngine(ctx);
    await engine.setupRecorder();

    let micOk = false;
    statusLine.textContent = 'Requesting microphone access…';
    try {
      await withTimeout(engine.connectMic(), 8000);
      micOk = true;
    } catch (micErr) {
      console.warn('mic unavailable:', micErr);
    }

    const testTone = engine.addTestTone();

    ltBody.appendChild(buildTrack1(engine, testTone));
    ltBody.appendChild(buildDummyTrack('TRACK 2'));
    ltBody.appendChild(buildDummyTrack('TRACK 3'));
    ltBody.appendChild(buildDummyTrack('TRACK 4'));
    const divider = document.createElement('div');
    divider.className = 'lt-divider';
    ltBody.insertBefore(divider, ltBody.children[4]);
    ltBody.appendChild(buildGlobal(engine));

    btnPlay.disabled = false;
    bpmInput.disabled = false;
    engine.setBpm(Number(bpmInput.value) || 120);
    bpmInput.addEventListener('input', () => engine.setBpm(Number(bpmInput.value) || 120));

    btnPlay.addEventListener('click', () => {
      if (engine.playing) {
        engine.stop();
        btnPlay.textContent = 'Play';
        btnPlay.classList.remove('on');
      } else {
        ctx.resume();
        engine.play();
        btnPlay.textContent = 'Stop';
        btnPlay.classList.add('on');
      }
    });

    statusLine.textContent = micOk
      ? 'Engine running. Press Play to start the transport, then REC to arm Track 1.'
      : 'No microphone access -- use the Test Tone toggle on Track 1 to try recording, or reload and allow the mic.';
    if (!micOk) statusLine.classList.add('err');
  } catch (err) {
    console.error(err);
    statusLine.textContent = 'Error: ' + err.message + ' (microphone access is required to record)';
    statusLine.classList.add('err');
    btnEnable.disabled = false;
  }
});

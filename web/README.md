# Looptrack — web prototype

A Web Audio implementation of Track 1, in the panel's real visual language
(colors, knob geometry and layout pulled from `../design/panel-sketch.html`).
Tracks 2–4 are shown as dimmed placeholders — the engine underneath is
single-track today, same as the plugin.

Native Web Audio nodes do almost everything: the input EQ/preamp, wow/flutter
(a modulated `DelayNode`), hiss, the 3-band EQ, the DJ filter, and the lofi
delay/reverb bus (an 8-comb/4-allpass network built from `DelayNode`s). The
one exception is `recorder-worklet.js`, an `AudioWorkletProcessor` that
captures raw input samples into the loop buffer — there's no built-in
"record this stream" primitive.

There's no host here, so the page is its own transport: a Play/Stop button
and a BPM field drive the same tempo-boundary math as the plugin (`engine.js`
mirrors `Transport.h`/`LoopRecorder.cpp`), including the truncate-on-tempo-up
/ silence-on-tempo-down behavior, via a lookahead scheduler that re-derives
each loop pass from the current bpm/bars every ~100ms.

## Run it

```sh
cd web && python3 -m http.server 8777
```

Open <http://localhost:8777>, click **Enable Audio**, and allow microphone
access. No mic (or access denied)? Append `?tone=1` to the URL to inject a
220Hz test tone where a mic would connect, so recording and looping are
still testable end to end.

## Known simplifications vs. the plugin

- The lofi delay skips true sample-and-hold rate reduction (bit-crushing via
  a `WaveShaperNode` curve gets most of the character); the reverb skips the
  two modulated comb lengths that fight metallic ringing in the plugin.
- Varispeed changing an *already-playing* pass's rate can drift slightly from
  the visual playhead until the next scheduled pass resyncs it — a side
  effect of using the browser's native `playbackRate`, which the C++ engine
  doesn't have this constraint with.

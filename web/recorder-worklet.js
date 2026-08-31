// Captures raw input samples into a growable buffer while armed. Everything
// else in the engine uses native Web Audio nodes -- this is the one piece
// that genuinely needs sample-accurate custom code, since the browser has no
// built-in "record this stream into an array" primitive.
class RecorderProcessor extends AudioWorkletProcessor {
  constructor () {
    super();
    this.recording = false;
    this.bufferL = null;
    this.bufferR = null;
    this.writePos = 0;
    this.capacity = 0;
    this.progressCounter = 0;

    this.port.onmessage = (e) => {
      const msg = e.data;
      if (msg.type === 'start') {
        this.capacity = msg.capacity;
        this.bufferL = new Float32Array(this.capacity);
        this.bufferR = new Float32Array(this.capacity);
        this.writePos = 0;
        this.recording = true;
      } else if (msg.type === 'stop') {
        this.recording = false;
        const lenAtStop = this.writePos;
        this.port.postMessage({
          type: 'recorded',
          length: lenAtStop,
          bufferL: this.bufferL.slice(0, lenAtStop),
          bufferR: this.bufferR.slice(0, lenAtStop)
        });
      }
    };
  }

  process (inputs) {
    const input = inputs[0];
    if (this.recording && input.length > 0 && input[0].length > 0) {
      const inL = input[0];
      const inR = input.length > 1 ? input[1] : input[0];
      const n = inL.length;
      for (let i = 0; i < n && this.writePos < this.capacity; i++, this.writePos++) {
        this.bufferL[this.writePos] = inL[i];
        this.bufferR[this.writePos] = inR[i];
      }
      if (++this.progressCounter >= 8) {
        this.progressCounter = 0;
        this.port.postMessage({ type: 'progress', writePos: this.writePos });
      }
    }
    return true;
  }
}

registerProcessor('recorder-processor', RecorderProcessor);

const CAP = 8192;

class SilverIrisOut extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const opt = options && options.processorOptions;
    this.inRate = (opt && opt.inRate) || 22050;
    this.l = new Float32Array(CAP);
    this.r = new Float32Array(CAP);
    this.w = 0;
    this.rp = 0;
    this.n = 0;
    this.frac = 0;
    this.port.onmessage = (ev) => {
      const d = ev.data;
      if (!d || d.type !== "pcm" || !d.samples) return;
      const s = d.samples;
      const frames = s.length >> 1;
      for (let i = 0; i < frames; i++) {
        if (this.n >= CAP) break;
        this.l[this.w] = s[i * 2] / 32768;
        this.r[this.w] = s[i * 2 + 1] / 32768;
        this.w = (this.w + 1) % CAP;
        this.n++;
      }
    };
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    if (!out || !out[0]) return true;
    const outL = out[0];
    const outR = out[1] || out[0];
    const step = this.inRate / sampleRate;
    for (let i = 0; i < outL.length; i++) {
      if (this.n <= 0) {
        outL[i] = 0;
        outR[i] = 0;
        continue;
      }
      outL[i] = this.l[this.rp];
      outR[i] = this.r[this.rp];
      this.frac += step;
      while (this.frac >= 1 && this.n > 0) {
        this.frac -= 1;
        this.rp = (this.rp + 1) % CAP;
        this.n--;
      }
    }
    return true;
  }
}

registerProcessor("silveriris-out", SilverIrisOut);

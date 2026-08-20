/** Interleaved s16 stereo ring. Host float mix; no game RNG. */

export function s16StereoToChannels(
  interleaved: ArrayLike<number>,
  left: Float32Array,
  right: Float32Array,
): number {
  const n = Math.min(interleaved.length >> 1, left.length, right.length);
  for (let i = 0; i < n; i++) {
    left[i] = interleaved[i * 2] / 32768;
    right[i] = interleaved[i * 2 + 1] / 32768;
  }
  return n;
}

export class PcmRing {
  private readonly l: Float32Array;
  private readonly r: Float32Array;
  private readonly cap: number;
  private w = 0;
  private rp = 0;
  private n = 0;
  private frac = 0;

  constructor(capFrames: number) {
    this.cap = capFrames;
    this.l = new Float32Array(capFrames);
    this.r = new Float32Array(capFrames);
  }

  get length(): number {
    return this.n;
  }

  pushS16(interleaved: ArrayLike<number>): number {
    const frames = interleaved.length >> 1;
    let wrote = 0;
    for (let i = 0; i < frames; i++) {
      if (this.n >= this.cap) break;
      this.l[this.w] = interleaved[i * 2] / 32768;
      this.r[this.w] = interleaved[i * 2 + 1] / 32768;
      this.w = (this.w + 1) % this.cap;
      this.n++;
      wrote++;
    }
    return wrote;
  }

  readResampled(outL: Float32Array, outR: Float32Array, inRate: number, outRate: number): void {
    const step = outRate === 0 ? 1 : inRate / outRate;
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
        this.rp = (this.rp + 1) % this.cap;
        this.n--;
      }
    }
  }
}

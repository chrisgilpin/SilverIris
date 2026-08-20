import { PcmRing, s16StereoToChannels } from "./pcm.ts";

export type AudioFill = (out: Int16Array, nframes: number) => void;

type AudioContextCtor = new (opts?: AudioContextOptions) => AudioContext;

function audioContextCtor(): AudioContextCtor | null {
  const w = globalThis as typeof globalThis & {
    AudioContext?: AudioContextCtor;
    webkitAudioContext?: AudioContextCtor;
  };
  return w.AudioContext ?? w.webkitAudioContext ?? null;
}

let shared: AudioContext | null = null;
let lastError = "";

export function lastAudioError(): string {
  return lastError;
}

/** Create/resume the context in the same turn as a click or key. */
export function unlockAudio(rate = 22050): AudioContext | null {
  const Ctor = audioContextCtor();
  if (!Ctor) {
    lastError = "This browser has no Web Audio API";
    return null;
  }
  if (!shared || shared.state === "closed") {
    try {
      shared = new Ctor({ sampleRate: rate, latencyHint: "interactive" });
    } catch {
      try {
        shared = new Ctor({ latencyHint: "interactive" });
      } catch (err) {
        lastError = err instanceof Error ? err.message : String(err);
        shared = null;
        return null;
      }
    }
  }
  if (shared.state === "suspended") {
    void shared.resume();
  }
  return shared;
}

export class AudioPlayer {
  private timer: ReturnType<typeof setInterval> | null = null;
  private scratch: Int16Array;
  private readonly framesPerPump: number;
  private readonly mixRate: number;
  private readonly ring: PcmRing;
  private nextTime = 0;
  private stopped = false;
  private readonly dest: GainNode;

  private constructor(
    private readonly ctx: AudioContext,
    private readonly fill: AudioFill,
    rate: number,
  ) {
    this.mixRate = rate;
    this.framesPerPump = Math.max(128, Math.round((rate * 20) / 1000));
    this.scratch = new Int16Array(this.framesPerPump * 2);
    this.ring = new PcmRing(this.framesPerPump * 8);
    this.dest = ctx.createGain();
    this.dest.gain.value = 0.7;
    this.dest.connect(ctx.destination);
  }

  static create(fill: AudioFill, rate: number): AudioPlayer | null {
    const ctx = unlockAudio(rate);
    if (!ctx) return null;
    lastError = "";
    return new AudioPlayer(ctx, fill, rate);
  }

  get running(): boolean {
    return !this.stopped && this.ctx.state === "running";
  }

  private schedule(l: Float32Array, r: Float32Array): void {
    const n = l.length;
    if (n <= 0) return;
    const buf = this.ctx.createBuffer(2, n, this.ctx.sampleRate);
    buf.getChannelData(0).set(l);
    buf.getChannelData(1).set(r);
    const src = this.ctx.createBufferSource();
    src.buffer = buf;
    src.connect(this.dest);
    const now = this.ctx.currentTime;
    if (this.nextTime < now - 0.05) this.nextTime = now + 0.03;
    const t = Math.max(now + 0.02, this.nextTime);
    src.start(t);
    this.nextTime = t + buf.duration;
  }

  private pump(): void {
    if (this.stopped || this.ctx.state !== "running") return;
    if (this.nextTime > this.ctx.currentTime + 0.08) return;
    this.fill(this.scratch, this.framesPerPump);
    const outRate = this.ctx.sampleRate;
    if (outRate === this.mixRate) {
      const l = new Float32Array(this.framesPerPump);
      const r = new Float32Array(this.framesPerPump);
      s16StereoToChannels(this.scratch, l, r);
      this.schedule(l, r);
      return;
    }
    this.ring.pushS16(this.scratch);
    const outN = Math.max(1, Math.round((this.framesPerPump * outRate) / this.mixRate));
    const l = new Float32Array(outN);
    const r = new Float32Array(outN);
    this.ring.readResampled(l, r, this.mixRate, outRate);
    this.schedule(l, r);
  }

  private ensureTimer(): void {
    if (this.stopped || this.timer != null) return;
    this.timer = setInterval(() => this.pump(), 20);
  }

  async resume(): Promise<boolean> {
    if (this.stopped) return false;
    if (this.ctx.state === "suspended") {
      try {
        await this.ctx.resume();
      } catch (err) {
        lastError = err instanceof Error ? err.message : String(err);
        return false;
      }
    }
    if (this.ctx.state === "running") this.ensureTimer();
    else lastError = `audio state is ${this.ctx.state}`;
    return this.ctx.state === "running";
  }

  kick(): void {
    if (this.nextTime > this.ctx.currentTime + 0.04) return;
    this.pump();
  }

  stop(): void {
    this.stopped = true;
    if (this.timer != null) clearInterval(this.timer);
    this.timer = null;
    try {
      this.dest.disconnect();
    } catch {
      /* already down */
    }
  }
}

export async function startAudio(fill: AudioFill, rate: number): Promise<AudioPlayer | null> {
  return AudioPlayer.create(fill, rate);
}

/** Keep the match tab from being background-throttled. There is no good fix. */

export const HOLD_WORKLET_SOURCE = [
  "class SilverIrisHold extends AudioWorkletProcessor {",
  "  process() { return true; }",
  "}",
  'registerProcessor("silveriris-hold", SilverIrisHold);',
  "",
].join("\n");

type WakeLockSentinelLike = { released: boolean; release(): Promise<void> };

let wake: WakeLockSentinelLike | null = null;
let holdNode: AudioNode | null = null;
let visHandler: (() => void) | null = null;

async function requestWake(): Promise<void> {
  const nav = navigator as Navigator & {
    wakeLock?: { request(type: "screen"): Promise<WakeLockSentinelLike> };
  };
  try {
    wake = (await nav.wakeLock?.request("screen")) ?? null;
  } catch {
    wake = null;
  }
}

async function startSilentHold(): Promise<void> {
  const w = globalThis as typeof globalThis & {
    AudioContext?: new (opts?: AudioContextOptions) => AudioContext;
    webkitAudioContext?: new (opts?: AudioContextOptions) => AudioContext;
  };
  const Ctor = w.AudioContext ?? w.webkitAudioContext;
  if (!Ctor) return;
  let ctx: AudioContext;
  try {
    ctx = new Ctor({ latencyHint: "interactive" });
  } catch {
    return;
  }
  if (ctx.state === "suspended") {
    try { await ctx.resume(); } catch { /* gesture required */ }
  }
  try {
    const blob = new Blob([HOLD_WORKLET_SOURCE], { type: "application/javascript" });
    const url = URL.createObjectURL(blob);
    try {
      await ctx.audioWorklet.addModule(url);
      const node = new AudioWorkletNode(ctx, "silveriris-hold");
      const g = ctx.createGain();
      g.gain.value = 0;
      node.connect(g).connect(ctx.destination);
      holdNode = node;
    } finally {
      URL.revokeObjectURL(url);
    }
  } catch {
    try {
      const osc = ctx.createOscillator();
      const g = ctx.createGain();
      g.gain.value = 0;
      osc.frequency.value = 20;
      osc.connect(g).connect(ctx.destination);
      osc.start();
      holdNode = osc;
    } catch {
      holdNode = null;
    }
  }
}

export async function startMatchHold(): Promise<void> {
  if (typeof document === "undefined") return;
  await requestWake();
  if (!holdNode) await startSilentHold();
  if (!visHandler) {
    visHandler = () => {
      if (document.visibilityState === "visible")
        void requestWake();
    };
    document.addEventListener("visibilitychange", visHandler);
  }
}

export async function releaseMatchHold(): Promise<void> {
  if (visHandler && typeof document !== "undefined") {
    document.removeEventListener("visibilitychange", visHandler);
    visHandler = null;
  }
  try { await wake?.release(); } catch { /* already released */ }
  wake = null;
  try { holdNode?.disconnect(); } catch { /* */ }
  holdNode = null;
}

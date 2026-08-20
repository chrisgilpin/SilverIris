import type { ExtractedFile, ExtractProgress } from "./extract.ts";

export type { ExtractedFile, ExtractProgress };

export function extractRom(
  z64: Uint8Array,
  region: "U" | "J" | "E",
  onProgress: (p: ExtractProgress) => void,
): Promise<ExtractedFile[]> {
  if (region !== "U") {
    return Promise.reject(new Error("US dump required."));
  }
  return new Promise((resolve, reject) => {
    const worker = new Worker(new URL("./worker.ts", import.meta.url), {
      type: "module",
    });
    worker.onmessage = (ev: MessageEvent) => {
      const msg = ev.data as
        | { type: "progress"; done: number; total: number; current: string }
        | { type: "done"; files: ExtractedFile[] }
        | { type: "error"; message: string };
      if (msg.type === "progress") {
        onProgress({ done: msg.done, total: msg.total, current: msg.current });
        return;
      }
      worker.terminate();
      if (msg.type === "error") {
        reject(new Error(msg.message));
        return;
      }
      resolve(msg.files);
    };
    worker.onerror = (err) => {
      worker.terminate();
      reject(err.error ?? new Error(err.message));
    };
    const copy = z64.slice();
    worker.postMessage({ type: "start", z64: copy.buffer, region }, [copy.buffer]);
  });
}

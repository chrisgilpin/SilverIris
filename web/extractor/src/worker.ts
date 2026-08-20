import { EXTRACT_RUNTIME, extractRom } from "./extract.ts";
import { extractMapsU } from "./maps.ts";

/* EXTRACT_RUNTIME must change whenever extract.ts packing rules change;
 * Vite workers do not always HMR. */

type StartMsg = { type: "start"; z64: ArrayBuffer; region: "U" | "J" | "E" };

self.onmessage = async (ev: MessageEvent<StartMsg>) => {
  const msg = ev.data;
  try {
    if (msg.region !== "U") {
      throw new Error("US dump required.");
    }
    const z64 = new Uint8Array(msg.z64);
    const files = await extractRom(z64, {
      ...extractMapsU,
      includeUcode: false,
      onProgress: (p) => {
        self.postMessage({
          type: "progress",
          done: p.done,
          total: p.total,
          current: p.current,
        });
      },
    });
    const transfer: Transferable[] = files.map((f) => f.bytes.buffer as ArrayBuffer);
    (self as unknown as Worker).postMessage({ type: "done", files }, transfer);
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    self.postMessage({ type: "error", message });
  }
};

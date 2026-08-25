import { describe, expect, it } from "vitest";
import { DEFAULT_BINDS, formatBind, loadBinds, saveBinds, setBind } from "./binds.ts";

describe("binds", () => {
  it("defaults WASD + Z/Space fire", () => {
    expect(loadBinds({ getItem: () => null })).toEqual(DEFAULT_BINDS);
    expect(formatBind(DEFAULT_BINDS.fire)).toBe("Z / Space");
  });

  it("round-trips through storage", () => {
    const store: Record<string, string> = {};
    const next = setBind(DEFAULT_BINDS, "up", "ArrowUp");
    saveBinds(next, { setItem: (k, v) => { store[k] = v; } });
    const loaded = loadBinds({ getItem: (k) => store[k] ?? null });
    expect(loaded.up).toEqual(["ArrowUp"]);
    expect(loaded.down).toEqual(DEFAULT_BINDS.down);
  });

  it("keeps Space as a fire alias when remapping fire", () => {
    const next = setBind(DEFAULT_BINDS, "fire", "KeyF");
    expect(next.fire).toEqual(["KeyF", "Space"]);
  });
});

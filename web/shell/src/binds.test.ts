import { describe, expect, it } from "vitest";
import { DEFAULT_BINDS, formatBind, loadBinds, saveBinds, setBind } from "./binds.ts";

describe("binds", () => {
  it("defaults WASD + Z/Space use, mouse fire", () => {
    expect(loadBinds({ getItem: () => null })).toEqual(DEFAULT_BINDS);
    expect(formatBind(DEFAULT_BINDS.use)).toBe("Z / Space");
    expect(formatBind(DEFAULT_BINDS.fire)).toBe("mouse");
  });

  it("round-trips through storage", () => {
    const store: Record<string, string> = {};
    const next = setBind(DEFAULT_BINDS, "up", "ArrowUp");
    saveBinds(next, { setItem: (k, v) => { store[k] = v; } });
    const loaded = loadBinds({ getItem: (k) => store[k] ?? null });
    expect(loaded.up).toEqual(["ArrowUp"]);
    expect(loaded.down).toEqual(DEFAULT_BINDS.down);
    expect(loaded.use).toEqual(DEFAULT_BINDS.use);
  });

  it("remaps fire without forcing Space", () => {
    const next = setBind(DEFAULT_BINDS, "fire", "KeyF");
    expect(next.fire).toEqual(["KeyF"]);
    expect(next.use).toEqual(["KeyZ", "Space"]);
  });

  it("migrates old Z/Space fire binds to use", () => {
    const loaded = loadBinds({
      getItem: () => JSON.stringify({ fire: ["KeyZ", "Space"] }),
    });
    expect(loaded.use).toEqual(["KeyZ", "Space"]);
    expect(loaded.fire).toEqual([]);
  });
});

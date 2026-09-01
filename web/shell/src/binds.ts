/** Remappable P1 / netplay-seat keys. Mouse look stays pointer-lock.
 * N64 Facility 1P: click = B / Z-trig fire; Z/Space = A use-door. */

export type BindAction = "up" | "down" | "left" | "right" | "fire" | "use" | "lookUp" | "lookDown";

export const BIND_ACTIONS: BindAction[] = [
  "up",
  "down",
  "left",
  "right",
  "fire",
  "use",
  "lookUp",
  "lookDown",
];

export const BIND_LABELS: Record<BindAction, string> = {
  up: "Forward",
  down: "Back",
  left: "Left / strafe",
  right: "Right / strafe",
  fire: "Fire (N64 B / Z-trig)",
  use: "Use door (N64 A)",
  lookUp: "Look up",
  lookDown: "Look down",
};

export const DEFAULT_BINDS: Record<BindAction, string[]> = {
  up: ["KeyW"],
  down: ["KeyS"],
  left: ["KeyA"],
  right: ["KeyD"],
  fire: [],
  use: ["KeyZ", "Space"],
  lookUp: ["KeyI"],
  lookDown: ["KeyK"],
};

const STORAGE_KEY = "si_binds";

function cloneDefaults(): Record<BindAction, string[]> {
  const out = {} as Record<BindAction, string[]>;
  BIND_ACTIONS.forEach((a) => {
    out[a] = [...DEFAULT_BINDS[a]];
  });
  return out;
}

function parseStored(raw: string | null): Record<BindAction, string[]> {
  const out = cloneDefaults();
  if (!raw) return out;
  try {
    const parsed = JSON.parse(raw) as Partial<Record<BindAction, unknown>>;
    BIND_ACTIONS.forEach((a) => {
      const v = parsed[a];
      if (Array.isArray(v) && v.every((c) => typeof c === "string"))
        out[a] = v as string[];
    });
    /* Pre-split shells stored Z/Space as fire. Move them to use. */
    if (!("use" in parsed) && Array.isArray(parsed.fire)) {
      const old = parsed.fire.filter((c): c is string => typeof c === "string");
      if (old.includes("KeyZ") || old.includes("Space")) {
        out.use = old.filter((c) => c === "KeyZ" || c === "Space");
        out.fire = old.filter((c) => c !== "KeyZ" && c !== "Space");
      }
    }
  } catch {
    /* keep defaults */
  }
  return out;
}

export function loadBinds(
  storage: Pick<Storage, "getItem"> | null = typeof localStorage !== "undefined" ? localStorage : null,
): Record<BindAction, string[]> {
  return parseStored(storage?.getItem(STORAGE_KEY) ?? null);
}

export function saveBinds(
  binds: Record<BindAction, string[]>,
  storage: Pick<Storage, "setItem"> | null = typeof localStorage !== "undefined" ? localStorage : null,
): void {
  storage?.setItem(STORAGE_KEY, JSON.stringify(binds));
}

export function codeLabel(code: string): string {
  if (code === "Space") return "Space";
  if (code.startsWith("Key") && code.length === 4) return code.slice(3);
  if (code.startsWith("Arrow")) return code.slice(5);
  if (code.startsWith("Digit") && code.length === 6) return code.slice(5);
  return code;
}

export function formatBind(codes: string[]): string {
  if (codes.length === 0) return "mouse";
  return codes.map(codeLabel).join(" / ");
}

export function setBind(
  binds: Record<BindAction, string[]>,
  action: BindAction,
  code: string,
): Record<BindAction, string[]> {
  const next = cloneDefaults();
  BIND_ACTIONS.forEach((a) => {
    next[a] = binds[a].filter((c) => c !== code);
    if (a !== "fire" && next[a].length === 0) next[a] = [...DEFAULT_BINDS[a]];
  });
  next[action] = [code];
  return next;
}

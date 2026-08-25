/** Remappable P1 / netplay-seat keys. Mouse look stays pointer-lock. */

export type BindAction = "up" | "down" | "left" | "right" | "fire" | "lookUp" | "lookDown";

export const BIND_ACTIONS: BindAction[] = [
  "up",
  "down",
  "left",
  "right",
  "fire",
  "lookUp",
  "lookDown",
];

export const BIND_LABELS: Record<BindAction, string> = {
  up: "Forward",
  down: "Back",
  left: "Left / strafe",
  right: "Right / strafe",
  fire: "Fire / use door",
  lookUp: "Look up",
  lookDown: "Look down",
};

export const DEFAULT_BINDS: Record<BindAction, string[]> = {
  up: ["KeyW"],
  down: ["KeyS"],
  left: ["KeyA"],
  right: ["KeyD"],
  fire: ["KeyZ", "Space"],
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
      if (Array.isArray(v) && v.every((c) => typeof c === "string" && c.length > 0))
        out[a] = v as string[];
    });
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
    if (next[a].length === 0) next[a] = [...DEFAULT_BINDS[a]];
  });
  next[action] = [code];
  if (action === "fire" && code !== "Space") next[action] = [code, "Space"];
  return next;
}

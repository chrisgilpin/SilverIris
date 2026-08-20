/** Feature flags. Query `?ff_name=0|1` overrides localStorage `ff_name`. */

export type FlagName =
  | "netplay"
  | "turnForce"
  | "wsRelay"
  | "widescreen"
  | "lan"
  | "campaign";

const DEFAULTS: Record<FlagName, boolean> = {
  netplay: false,
  turnForce: false,
  wsRelay: false,
  widescreen: true,
  lan: false,
  campaign: false,
};

function parseBool(raw: string | null): boolean | undefined {
  if (raw == null) return undefined;
  if (raw === "1" || raw === "true") return true;
  if (raw === "0" || raw === "false") return false;
  return undefined;
}

export function readFlags(search = ""): Record<FlagName, boolean> {
  const params = new URLSearchParams(search.startsWith("?") ? search.slice(1) : search);
  const out = { ...DEFAULTS };
  (Object.keys(DEFAULTS) as FlagName[]).forEach((name) => {
    const q = parseBool(params.get(`ff_${name}`));
    if (q !== undefined) {
      out[name] = q;
      return;
    }
    if (typeof localStorage !== "undefined") {
      const ls = parseBool(localStorage.getItem(`ff_${name}`));
      if (ls !== undefined) out[name] = ls;
    }
  });
  return out;
}

export const flags = readFlags(
  typeof location !== "undefined" ? location.search : "",
);

/** Feature flags. Query `?ff_name=0|1` overrides localStorage `ff_name`.
 *
 *   netplay     -- lobby + lockstep mesh. Default on (M6). ?ff_netplay=0 for solo.
 *   lan         -- delay 1 instead of 2.
 *   turnForce   -- ICE relay-only (TURN path). Default off.
 *   wsRelay     -- force /ws inp+ctl relay. Also auto-on after ICE fail.
 *   widescreen  -- Hor+ camera. Default on.
 *   campaign    -- not v1. Default off.
 */

export type FlagName =
  | "netplay"
  | "turnForce"
  | "wsRelay"
  | "widescreen"
  | "lan"
  | "campaign";

const DEFAULTS: Record<FlagName, boolean> = {
  netplay: true,
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

import { hexToBytes } from "../pack.ts";
import { blitRgbaToCanvas, stageHasDrawableRooms } from "./view.ts";

export const PORT_DRAW_NONE = 0;
export const PORT_DRAW_STAGE = 1;
export const PORT_DRAW_FALLBACK = 2;

export function lastDrawLabel(d: number): string {
  return d === PORT_DRAW_STAGE ? "STAGE" : d === PORT_DRAW_FALLBACK ? "FALLBACK" : "NONE";
}

export function formatStageDebug(opts: {
  lastDraw: number;
  rooms: number;
  gdlC0: boolean;
  fbNonzero: number;
}): string {
  return `last_draw ${lastDrawLabel(opts.lastDraw)}  rooms ${opts.rooms}  gdlC0 ${opts.gdlC0 ? 1 : 0}  fbNonzero ${opts.fbNonzero}`;
}

/** Live canvas blits G1 only when the pack produced a drawable room GDL. */
export function shouldBlitStageFb(info: { gdlRaw: boolean; gdlC0: boolean }): boolean {
  return stageHasDrawableRooms(info);
}

export type GameModule = {
  _malloc: (n: number) => number;
  _free: (p: number) => void;
  _port_api_init: (packPtr: number, packLen: number, hashPtr: number) => number;
  _port_api_shutdown: () => void;
  _port_api_fb: () => number;
  _port_api_fb_width: () => number;
  _port_api_fb_height: () => number;
  _port_api_draw: () => void;
  _port_api_last_draw?: () => number;
  _port_api_last_error: () => number;
  _port_api_ready: () => number;
  _port_api_audio_cb: (ptr: number, nframes: number) => void;
  _port_api_audio_play_gun: () => void;
  _port_api_audio_set_music: (on: number) => void;
  _port_api_audio_rate: () => number;
  _port_api_load_stage: (levelId: number) => number;
  _port_api_sim_tick: (tick: number) => number;
  _port_api_clock_timer: () => number;
  _port_api_stage_rooms: () => number;
  _port_api_bg_rooms: () => number;
  _port_api_gdl_raw: () => number;
  _port_api_gdl_c0: () => number;
  _port_api_gdl_vtx?: () => number;
  _port_api_fb_nonzero?: () => number;
  _port_api_pack_files: () => number;
  _port_api_set_pad: (seat: number, x: number, y: number, buttons: number) => void;
  _port_api_set_player_count: (n: number) => void;
  _port_api_player_count: () => number;
  _port_api_env_players: () => number;
  _port_api_player_x: () => number;
  _port_api_player_x_at: (seat: number) => number;
  _port_api_player_z_at: (seat: number) => number;
  _port_api_player_theta_at: (seat: number) => number;
  _port_api_vp_left: (seat: number) => number;
  _port_api_vp_top: (seat: number) => number;
  _port_api_vp_width: (seat: number) => number;
  _port_api_vp_height: (seat: number) => number;
  _port_api_player_y: () => number;
  _port_api_player_z: () => number;
  _port_api_player_theta: () => number;
  _port_api_gun_mag: () => number;
  _port_api_gun_reserve: () => number;
  _port_api_gun_hits: () => number;
  _port_api_gun_have_hit: () => number;
  _port_api_gun_hit_x: () => number;
  _port_api_gun_hit_y: () => number;
  _port_api_gun_hit_z: () => number;
  _port_api_crc_players: () => number;
  _port_api_chr_count: () => number;
  _port_api_chr_x: () => number;
  _port_api_chr_z: () => number;
  _port_api_chr_theta: () => number;
  _port_api_chr_action: () => number;
  _port_api_crc_chrs: () => number;
  _port_api_kills: () => number;
  _port_api_crc_objectives: () => number;
  _port_api_rng_lo: () => number;
  _port_api_chr_rng_lo: () => number;
  _port_api_begin_match?: (nseats: number, rngSeed: number) => void;
  _port_api_set_view_seat?: (seat: number) => void;
  _port_api_view_seat?: () => number;
  _port_api_view_unsplit?: () => number;
  _port_api_set_screen_size?: (w: number, h: number) => void;
  _port_api_set_screen_position?: (l: number, t: number) => void;
  _port_api_set_perspective?: (near: number, fovy: number, aspect: number) => void;
  _port_api_view_hfov?: () => number;
  HEAPU8: Uint8Array;
  HEAP16?: Int16Array;
  UTF8ToString?: (p: number) => string;
};

export type GameBridge = {
  init(pack: Uint8Array, packHash: Uint8Array): Promise<void>;
  shutdown(): void;
  draw(canvas: HTMLCanvasElement): void;
  rasterStage(): void;
  stageFb(): { rgba: Uint8Array; w: number; h: number } | null;
  lastDraw(): number;
  hasDrawableStage(): boolean;
  ready(): boolean;
  audioCb(out: Int16Array, nframes: number): void;
  audioPlayGun(): void;
  audioSetMusic(on: boolean): void;
  audioRate(): number;
  loadStage(levelId: number): number;
  simTick(tick: number): number;
  clockTimer(): number;
  stageRooms(): number;
  bgRooms(): number;
  gdlRaw(): boolean;
  gdlC0(): boolean;
  gdlVtx(): boolean;
  fbNonzero(): number;
  lastDrawName(): string;
  packFiles(): number;
  lastError(): string;
  setPad(seat: number, x: number, y: number, buttons: number): void;
  setPlayerCount(n: number): void;
  playerCount(): number;
  envPlayers(): number;
  playerX(): number;
  playerXAt(seat: number): number;
  playerZAt(seat: number): number;
  playerThetaAt(seat: number): number;
  vpLeft(seat: number): number;
  vpTop(seat: number): number;
  vpWidth(seat: number): number;
  vpHeight(seat: number): number;
  playerY(): number;
  playerZ(): number;
  playerTheta(): number;
  gunMag(): number;
  gunReserve(): number;
  gunHits(): number;
  gunHaveHit(): boolean;
  gunHitX(): number;
  gunHitY(): number;
  gunHitZ(): number;
  crcPlayers(): number;
  chrCount(): number;
  chrX(): number;
  chrZ(): number;
  chrTheta(): number;
  chrAction(): number;
  crcChrs(): number;
  kills(): number;
  crcObjectives(): number;
  rngLo(): number;
  chrRngLo(): number;
  beginMatch(nseats: number, rngSeed: number): void;
  setViewSeat(seat: number): void;
  viewSeat(): number;
  viewUnsplit(): boolean;
  setScreenSize(w: number, h: number): void;
  setScreenPosition(l: number, t: number): void;
  setPerspective(near: number, fovy: number, aspect: number): void;
  viewHfov(): number;
};

type Factory = (opts?: Record<string, unknown>) => Promise<GameModule>;

function cstr(mod: GameModule, p: number): string {
  if (mod.UTF8ToString) return mod.UTF8ToString(p);
  if (!p) return "";
  let s = "";
  for (let i = p; mod.HEAPU8[i]; i++) s += String.fromCharCode(mod.HEAPU8[i]);
  return s;
}

/**
 * Load an ES module from Vite's public/ dir.
 * `import("/game.js")` is rewritten to `game.js?import` and 404s; fetch the
 * static file and import a blob URL instead.
 */
async function importPublicModule(path: string): Promise<{ default?: Factory; createSilverIris?: Factory }> {
  const res = await fetch(path, { credentials: "same-origin" });
  if (!res.ok) {
    throw new Error(`failed to load ${path}: ${res.status}`);
  }
  const text = await res.text();
  const blob = new Blob([text], { type: "text/javascript" });
  const blobUrl = URL.createObjectURL(blob);
  return import(/* @vite-ignore */ blobUrl) as Promise<{
    default?: Factory;
    createSilverIris?: Factory;
  }>;
}

export async function loadGame(url = "/game.js"): Promise<GameBridge> {
  const modNs = await importPublicModule(url);
  const factory = modNs.default ?? modNs.createSilverIris;
  if (typeof factory !== "function") {
    throw new Error("game.js did not export a module factory");
  }
  const M = await factory({
    locateFile: (path: string) => (path.endsWith(".wasm") ? "/game.wasm" : `/${path}`),
  });

  let alive = false;
  let audioPtr = 0;
  let audioCap = 0;

  const heapS16 = (ptr: number, samples: number): Int16Array => {
    const heap = M.HEAPU8;
    return new Int16Array(heap.buffer, heap.byteOffset + ptr, samples);
  };

  return {
    async init(pack: Uint8Array, packHash: Uint8Array): Promise<void> {
      if (packHash.byteLength !== 32) throw new Error("packHash must be 32 bytes");
      const p = M._malloc(pack.byteLength);
      const h = M._malloc(32);
      if (!p || !h) throw new Error("wasm malloc failed");
      M.HEAPU8.set(pack, p);
      M.HEAPU8.set(packHash, h);
      const rc = M._port_api_init(p, pack.byteLength, h);
      M._free(p);
      M._free(h);
      if (rc !== 0) {
        const err = cstr(M, M._port_api_last_error());
        throw new Error(err || `port_api_init ${rc}`);
      }
      alive = true;
    },
    shutdown(): void {
      if (alive) M._port_api_shutdown();
      alive = false;
      if (audioPtr) {
        M._free(audioPtr);
        audioPtr = 0;
        audioCap = 0;
      }
    },
    draw(canvas: HTMLCanvasElement): void {
      if (!alive || !M._port_api_ready()) return;
      if (!this.hasDrawableStage()) return;
      this.rasterStage();
      const fb = this.stageFb();
      const ctx = canvas.getContext("2d");
      if (!fb || !ctx) return;
      if (!canvas.width || !canvas.height) {
        canvas.width = fb.w;
        canvas.height = fb.h;
      }
      blitRgbaToCanvas(ctx, fb.rgba, fb.w, fb.h);
    },
    rasterStage(): void {
      if (alive && M._port_api_ready()) M._port_api_draw();
    },
    stageFb(): { rgba: Uint8Array; w: number; h: number } | null {
      if (!alive || !M._port_api_ready()) return null;
      const w = M._port_api_fb_width();
      const h = M._port_api_fb_height();
      const ptr = M._port_api_fb();
      if (!ptr || w <= 0 || h <= 0) return null;
      return { rgba: M.HEAPU8.subarray(ptr, ptr + w * h * 4), w, h };
    },
    lastDraw(): number {
      return alive && M._port_api_last_draw ? M._port_api_last_draw() : 0;
    },
    hasDrawableStage(): boolean {
      return !!(alive && ((M._port_api_gdl_raw && M._port_api_gdl_raw()) || (M._port_api_gdl_c0 && M._port_api_gdl_c0())));
    },
    ready(): boolean {
      return alive && M._port_api_ready() !== 0;
    },
    audioCb(out: Int16Array, nframes: number): void {
      if (!alive || nframes <= 0) {
        out.fill(0);
        return;
      }
      const bytes = nframes * 4;
      if (!audioPtr || audioCap < bytes) {
        if (audioPtr) M._free(audioPtr);
        audioPtr = M._malloc(bytes);
        audioCap = bytes;
      }
      if (!audioPtr) {
        out.fill(0);
        return;
      }
      M._port_api_audio_cb(audioPtr, nframes);
      const src = heapS16(audioPtr, nframes * 2);
      const n = Math.min(out.length, src.length);
      out.set(src.subarray(0, n));
      if (n < out.length) out.fill(0, n);
    },
    audioPlayGun(): void {
      if (alive) M._port_api_audio_play_gun();
    },
    audioSetMusic(on: boolean): void {
      if (alive) M._port_api_audio_set_music(on ? 1 : 0);
    },
    audioRate(): number {
      return alive ? M._port_api_audio_rate() : 22050;
    },
    loadStage(levelId: number): number {
      if (!alive) return -1;
      return M._port_api_load_stage(levelId);
    },
    simTick(tick: number): number {
      if (!alive) return -1;
      return M._port_api_sim_tick(tick);
    },
    clockTimer(): number {
      return alive ? M._port_api_clock_timer() : 0;
    },
    stageRooms(): number {
      return alive ? M._port_api_stage_rooms() : 0;
    },
    bgRooms(): number {
      return alive && M._port_api_bg_rooms ? M._port_api_bg_rooms() : 0;
    },
    gdlRaw(): boolean {
      return !!(alive && M._port_api_gdl_raw && M._port_api_gdl_raw());
    },
    gdlC0(): boolean {
      return !!(alive && M._port_api_gdl_c0 && M._port_api_gdl_c0());
    },
    gdlVtx(): boolean {
      return !!(alive && M._port_api_gdl_vtx && M._port_api_gdl_vtx());
    },
    fbNonzero(): number {
      return alive && M._port_api_fb_nonzero ? M._port_api_fb_nonzero() : 0;
    },
    lastDrawName(): string {
      return lastDrawLabel(this.lastDraw());
    },
    packFiles(): number {
      return alive ? M._port_api_pack_files() : 0;
    },
    lastError(): string {
      return cstr(M, M._port_api_last_error());
    },
    setPad(seat: number, x: number, y: number, buttons: number): void {
      if (alive) M._port_api_set_pad(seat, x, y, buttons);
    },
    setPlayerCount(n: number): void {
      if (alive) M._port_api_set_player_count(n);
    },
    playerCount(): number {
      return alive ? M._port_api_player_count() : 1;
    },
    envPlayers(): number {
      return alive ? M._port_api_env_players() : 0;
    },
    playerXAt(seat: number): number {
      return alive ? M._port_api_player_x_at(seat) : 0;
    },
    playerZAt(seat: number): number {
      return alive ? M._port_api_player_z_at(seat) : 0;
    },
    playerThetaAt(seat: number): number {
      return alive ? M._port_api_player_theta_at(seat) : 0;
    },
    vpLeft(seat: number): number {
      return alive ? M._port_api_vp_left(seat) : 0;
    },
    vpTop(seat: number): number {
      return alive ? M._port_api_vp_top(seat) : 0;
    },
    vpWidth(seat: number): number {
      return alive ? M._port_api_vp_width(seat) : 320;
    },
    vpHeight(seat: number): number {
      return alive ? M._port_api_vp_height(seat) : 240;
    },
    playerX(): number {
      return alive ? M._port_api_player_x() : 0;
    },
    playerY(): number {
      return alive ? M._port_api_player_y() : 0;
    },
    playerZ(): number {
      return alive ? M._port_api_player_z() : 0;
    },
    playerTheta(): number {
      return alive ? M._port_api_player_theta() : 0;
    },
    gunMag(): number {
      return alive ? M._port_api_gun_mag() : 0;
    },
    gunReserve(): number {
      return alive ? M._port_api_gun_reserve() : 0;
    },
    gunHits(): number {
      return alive ? M._port_api_gun_hits() : 0;
    },
    gunHaveHit(): boolean {
      return alive ? M._port_api_gun_have_hit() !== 0 : false;
    },
    gunHitX(): number {
      return alive ? M._port_api_gun_hit_x() : 0;
    },
    gunHitY(): number {
      return alive ? M._port_api_gun_hit_y() : 0;
    },
    gunHitZ(): number {
      return alive ? M._port_api_gun_hit_z() : 0;
    },
    crcPlayers(): number {
      return alive ? M._port_api_crc_players() >>> 0 : 0;
    },
    chrCount(): number {
      return alive ? M._port_api_chr_count() : 0;
    },
    chrX(): number {
      return alive ? M._port_api_chr_x() : 0;
    },
    chrZ(): number {
      return alive ? M._port_api_chr_z() : 0;
    },
    chrTheta(): number {
      return alive ? M._port_api_chr_theta() : 0;
    },
    chrAction(): number {
      return alive ? M._port_api_chr_action() : 0;
    },
    crcChrs(): number {
      return alive ? M._port_api_crc_chrs() >>> 0 : 0;
    },
    kills(): number {
      return alive ? M._port_api_kills() : 0;
    },
    crcObjectives(): number {
      return alive ? M._port_api_crc_objectives() >>> 0 : 0;
    },
    rngLo(): number {
      return alive ? M._port_api_rng_lo() >>> 0 : 0;
    },
    chrRngLo(): number {
      return alive ? M._port_api_chr_rng_lo() >>> 0 : 0;
    },
    beginMatch(nseats: number, rngSeed: number): void {
      if (!alive)
        return;
      if (M._port_api_begin_match)
        M._port_api_begin_match(nseats, rngSeed >>> 0);
      else
        M._port_api_set_player_count(nseats);
    },
    setViewSeat(seat: number): void {
      if (alive && M._port_api_set_view_seat)
        M._port_api_set_view_seat(seat);
    },
    viewSeat(): number {
      return alive && M._port_api_view_seat ? M._port_api_view_seat() : 0;
    },
    viewUnsplit(): boolean {
      return !!(alive && M._port_api_view_unsplit && M._port_api_view_unsplit());
    },
    setScreenSize(w: number, h: number): void {
      if (alive && M._port_api_set_screen_size)
        M._port_api_set_screen_size(w, h);
    },
    setScreenPosition(l: number, t: number): void {
      if (alive && M._port_api_set_screen_position)
        M._port_api_set_screen_position(l, t);
    },
    setPerspective(near: number, fovy: number, aspect: number): void {
      if (alive && M._port_api_set_perspective)
        M._port_api_set_perspective(near, fovy, aspect);
    },
    viewHfov(): number {
      return alive && M._port_api_view_hfov ? M._port_api_view_hfov() : 0;
    },
  };
}

export function packHashBytes(hex: string): Uint8Array {
  return hexToBytes(hex);
}

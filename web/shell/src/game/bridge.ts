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
  settex?: number;
  texOk?: number;
  texMiss?: number;
  walked?: number;
  cur?: number;
}): string {
  let s = `last_draw ${lastDrawLabel(opts.lastDraw)}  rooms ${opts.rooms}  gdlC0 ${opts.gdlC0 ? 1 : 0}  fbNonzero ${opts.fbNonzero}`;
  if (opts.settex !== undefined) {
    s += `  settex ${opts.settex}  texOk ${opts.texOk ?? 0}  texMiss ${opts.texMiss ?? 0}`;
  }
  if (opts.walked !== undefined) {
    s += `  walked ${opts.walked}  cur ${opts.cur ?? 0}`;
  }
  return s;
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
  _port_api_audio_play_dry?: () => void;
  _port_api_audio_last_sfx?: () => number;
  _port_api_audio_seq_on?: () => number;
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
  _port_api_portal_count?: () => number;
  _port_api_current_room?: () => number;
  _port_api_rooms_walked?: () => number;
  _port_api_fb_nonzero?: () => number;
  _port_api_settex?: () => number;
  _port_api_tex_ok?: () => number;
  _port_api_tex_miss?: () => number;
  _port_api_tex_miss_absent?: () => number;
  _port_api_tex_miss_decode?: () => number;
  _port_api_pack_files: () => number;
  _port_api_set_pad: (seat: number, x: number, y: number, buttons: number) => void;
  _port_api_set_player_count: (n: number) => void;
  _port_api_player_count: () => number;
  _port_api_env_players: () => number;
  _port_api_player_x: () => number;
  _port_api_player_x_at: (seat: number) => number;
  _port_api_player_z_at: (seat: number) => number;
  _port_api_player_theta_at: (seat: number) => number;
  _port_api_player_phi_at: (seat: number) => number;
  _port_api_vp_left: (seat: number) => number;
  _port_api_vp_top: (seat: number) => number;
  _port_api_vp_width: (seat: number) => number;
  _port_api_vp_height: (seat: number) => number;
  _port_api_player_y: () => number;
  _port_api_player_z: () => number;
  _port_api_player_theta: () => number;
  _port_api_player_phi: () => number;
  _port_api_set_look_delta: (seat: number, yaw: number, pitch: number) => void;
  _port_api_gun_mag: () => number;
  _port_api_gun_reserve: () => number;
  _port_api_gun_weapon?: () => number;
  _port_api_gun_hits: () => number;
  _port_api_gun_flash_frames?: () => number;
  _port_api_gun_last_action?: () => number;
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
  _port_api_kill_counts?: (seat: number) => number;
  _port_api_configure_match?: (scenario: number, gameLength: number) => void;
  _port_api_score_remain?: () => number;
  _port_api_score_over?: () => number;
  _port_api_score_winner?: () => number;
  _port_api_dead_ticks?: () => number;
  _port_api_hud_i32?: () => number;
  _port_api_health?: () => number;
  _port_api_guard_los?: () => number;
  _port_api_guard_shots?: () => number;
  _port_api_setup_guards?: () => number;
  _port_api_setup_guard_x?: (i: number) => number;
  _port_api_setup_guard_z?: (i: number) => number;
  _port_api_setup_guard_dead?: (i: number) => number;
  _port_api_stan_tiles?: () => number;
  _port_api_stan_on_tile?: () => number;
  _port_api_crc_objectives: () => number;
  _port_api_crc_props?: () => number;
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
  HEAP32?: Int32Array;
  UTF8ToString?: (p: number) => string;
  wasmMemory?: WebAssembly.Memory;
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
  audioLastSfx(): number;
  audioSeqOn(): boolean;
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
  portalCount(): number;
  currentRoom(): number;
  roomsWalked(): number;
  fbNonzero(): number;
  settex(): number;
  texOk(): number;
  texMiss(): number;
  texMissAbsent(): number;
  texMissDecode(): number;
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
  playerPhiAt(seat: number): number;
  vpLeft(seat: number): number;
  vpTop(seat: number): number;
  vpWidth(seat: number): number;
  vpHeight(seat: number): number;
  playerY(): number;
  playerZ(): number;
  playerTheta(): number;
  playerPhi(): number;
  setLookDelta(seat: number, yawDeg: number, pitchDeg: number): void;
  gunMag(): number;
  gunReserve(): number;
  gunWeapon(): number;
  gunHits(): number;
  gunFlashFrames(): number;
  gunLastAction(): number;
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
  killCounts(seat: number): number;
  configureMatch(scenario: number, gameLength: number): void;
  scoreRemain(): number;
  scoreOver(): boolean;
  scoreWinner(): number;
  deadTicks(): number;
  health(): number;
  armour(): number;
  guardLos(): number;
  guardShots(): number;
  setupGuards(): { x: number; z: number; dead: boolean }[];
  stanTiles(): number;
  stanOnTile(): boolean;
  crcObjectives(): number;
  crcProps(): number;
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


/**
 * HUD counters are i32 in C. Read HEAP32 / DataView.getInt32 — never HEAPF32.
 * 1.0f bits are 1065353216; |0 is not enough if glue delivered an f32 payload.
 */
export function readHeapI32(heap: Uint8Array, ptr: number): number {
  if (ptr < 0 || ptr + 4 > heap.byteLength) return 0;
  return new DataView(heap.buffer, heap.byteOffset + ptr, 4).getInt32(0, true);
}

/** wasm ALLOW_MEMORY_GROWTH detaches HEAPU8; always rebind from wasmMemory. */
export function liveHeapU8(mod: GameModule): Uint8Array {
  const mem = mod.wasmMemory;
  if (mem && (mod.HEAPU8.buffer !== mem.buffer || mod.HEAPU8.byteLength !== mem.buffer.byteLength))
    mod.HEAPU8 = new Uint8Array(mem.buffer);
  return mod.HEAPU8;
}

function hudSlot(mod: GameModule, slot: 0 | 1 | 2 | 3 | 4, fallback: () => number): number {
  const heap = liveHeapU8(mod);
  const p = mod._port_api_hud_i32?.();
  if (p && p + slot * 4 + 4 <= heap.byteLength) return readHeapI32(heap, p + slot * 4);
  return fallback() | 0;
}

type Factory = (opts?: Record<string, unknown>) => Promise<GameModule>;

function cstr(mod: GameModule, p: number): string {
  if (mod.UTF8ToString) return mod.UTF8ToString(p);
  if (!p) return "";
  const heap = liveHeapU8(mod);
  let s = "";
  for (let i = p; heap[i]; i++) s += String.fromCharCode(heap[i]);
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
    const heap = liveHeapU8(M);
    return new Int16Array(heap.buffer, heap.byteOffset + ptr, samples);
  };

  return {
    async init(pack: Uint8Array, packHash: Uint8Array): Promise<void> {
      if (packHash.byteLength !== 32) throw new Error("packHash must be 32 bytes");
      const p = M._malloc(pack.byteLength);
      const h = M._malloc(32);
      if (!p || !h) throw new Error("wasm malloc failed");
      liveHeapU8(M).set(pack, p);
      liveHeapU8(M).set(packHash, h);
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
      const heap = liveHeapU8(M);
      return { rgba: heap.subarray(ptr, ptr + w * h * 4), w, h };
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
    audioLastSfx(): number {
      return alive && M._port_api_audio_last_sfx ? M._port_api_audio_last_sfx() : 0;
    },
    audioSeqOn(): boolean {
      return !!(alive && M._port_api_audio_seq_on && M._port_api_audio_seq_on());
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
    portalCount(): number {
      return alive && M._port_api_portal_count ? M._port_api_portal_count() : 0;
    },
    currentRoom(): number {
      return alive && M._port_api_current_room ? M._port_api_current_room() : 0;
    },
    roomsWalked(): number {
      return alive && M._port_api_rooms_walked ? M._port_api_rooms_walked() : 0;
    },
    fbNonzero(): number {
      return alive && M._port_api_fb_nonzero ? M._port_api_fb_nonzero() : 0;
    },
    settex(): number {
      return alive && M._port_api_settex ? M._port_api_settex() : 0;
    },
    texOk(): number {
      return alive && M._port_api_tex_ok ? M._port_api_tex_ok() : 0;
    },
    texMiss(): number {
      return alive && M._port_api_tex_miss ? M._port_api_tex_miss() : 0;
    },
    texMissAbsent(): number {
      return alive && M._port_api_tex_miss_absent ? M._port_api_tex_miss_absent() : 0;
    },
    texMissDecode(): number {
      return alive && M._port_api_tex_miss_decode ? M._port_api_tex_miss_decode() : 0;
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
    playerPhiAt(seat: number): number {
      return alive ? M._port_api_player_phi_at(seat) : 0;
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
    playerPhi(): number {
      return alive ? M._port_api_player_phi() : 0;
    },
    setLookDelta(seat: number, yawDeg: number, pitchDeg: number): void {
      if (alive) M._port_api_set_look_delta(seat, yawDeg, pitchDeg);
    },
    gunMag(): number {
      return alive ? hudSlot(M, 0, () => M._port_api_gun_mag()) : 0;
    },
    gunReserve(): number {
      return alive ? hudSlot(M, 1, () => M._port_api_gun_reserve()) : 0;
    },
    gunWeapon(): number {
      return alive && M._port_api_gun_weapon ? M._port_api_gun_weapon() : 0;
    },
    gunHits(): number {
      return alive ? hudSlot(M, 2, () => M._port_api_gun_hits()) : 0;
    },
    gunFlashFrames(): number {
      return alive && M._port_api_gun_flash_frames ? M._port_api_gun_flash_frames() : 0;
    },
    gunLastAction(): number {
      return alive && M._port_api_gun_last_action ? M._port_api_gun_last_action() : 0;
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
      return alive ? hudSlot(M, 3, () => M._port_api_kills()) : 0;
    },
    killCounts(seat: number): number {
      return alive && M._port_api_kill_counts ? M._port_api_kill_counts(seat) | 0 : 0;
    },
    configureMatch(scenario: number, gameLength: number): void {
      if (alive && M._port_api_configure_match)
        M._port_api_configure_match(scenario | 0, gameLength >>> 0);
    },
    scoreRemain(): number {
      return alive && M._port_api_score_remain ? M._port_api_score_remain() | 0 : 0;
    },
    scoreOver(): boolean {
      return !!(alive && M._port_api_score_over && M._port_api_score_over());
    },
    scoreWinner(): number {
      return alive && M._port_api_score_winner ? M._port_api_score_winner() | 0 : -1;
    },
    deadTicks(): number {
      return alive && M._port_api_dead_ticks ? M._port_api_dead_ticks() | 0 : 0;
    },
    health(): number {
      return alive ? hudSlot(M, 4, () => (M._port_api_health ? M._port_api_health() : 8)) : 0;
    },
    armour(): number {
      return alive && M._port_api_armour ? M._port_api_armour() | 0 : 0;
    },
    guardLos(): number {
      return alive && M._port_api_guard_los ? M._port_api_guard_los() | 0 : 0;
    },
    guardShots(): number {
      return alive && M._port_api_guard_shots ? M._port_api_guard_shots() | 0 : 0;
    },
    setupGuards(): { x: number; z: number; dead: boolean }[] {
      if (!alive || !M._port_api_setup_guards) return [];
      const n = Math.max(0, M._port_api_setup_guards() | 0);
      const out: { x: number; z: number; dead: boolean }[] = [];
      for (let i = 0; i < n; i++) {
        out.push({
          x: M._port_api_setup_guard_x ? M._port_api_setup_guard_x(i) : 0,
          z: M._port_api_setup_guard_z ? M._port_api_setup_guard_z(i) : 0,
          dead: !!(M._port_api_setup_guard_dead && M._port_api_setup_guard_dead(i)),
        });
      }
      return out;
    },
    stanTiles(): number {
      return alive && M._port_api_stan_tiles ? M._port_api_stan_tiles() | 0 : 0;
    },
    stanOnTile(): boolean {
      return !!(alive && M._port_api_stan_on_tile && M._port_api_stan_on_tile());
    },
    crcObjectives(): number {
      return alive ? M._port_api_crc_objectives() >>> 0 : 0;
    },
    crcProps(): number {
      return alive && M._port_api_crc_props ? M._port_api_crc_props() >>> 0 : 0;
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

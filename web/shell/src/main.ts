import { extractRom } from "../../extractor/src/index.ts";
import type { ExtractedFile } from "../../extractor/src/extract.ts";
import { AudioPlayer, lastAudioError, unlockAudio } from "./audio/player.ts";
import { buildReport, encodeTapeExcerpt } from "./det/report.ts";
import { flags } from "./flags.ts";
import { loadGame, packHashBytes, type GameBridge } from "./game/bridge.ts";
import { drawPortView, horPlusHfovDeg, PORT_NATIVE_FOVY, presentLiveView, stageHasDrawableRooms, type PortChr, type PortHit } from "./game/view.ts";
import { kvGet, kvSet, packGet, packPut } from "./idb.ts";
import { decodeInputDatagram, encodeInputDatagram, emptyPad, lookDegFromQ, quantizeLookDeg } from "./net/datagram.ts";
import { defaultSignalUrl, LOBBY_BUILD_ID, packedLobbyCfg, SignalClient } from "./net/lobby.ts";
import { ICE_FAIL_OVERLAY, LockstepSession, type LockstepEngine, type LockstepEvent } from "./net/lockstep.ts";
import { bytesFromHex, decodeMatchConfig, hexBytes } from "./net/match_config.ts";
import { releaseMatchHold, startMatchHold } from "./net/hold_tab.ts";
import { dataChannelsPerPeer, meshOfferTargets, PeerMesh, type CtlMsg } from "./net/rtc.ts";
import { buildPack } from "./pack.ts";
import { pickRomFile, romFromDrop } from "./rom/pick.ts";
import { verifyRom } from "./rom/verify.ts";
import "./style.css";

const drop = document.querySelector<HTMLDivElement>("#drop");
const pickBtn = document.querySelector<HTMLButtonElement>("#pick");
const statusEl = document.querySelector<HTMLParagraphElement>("#status");
const bar = document.querySelector<HTMLDivElement>("#bar");
const barfill = document.querySelector<HTMLSpanElement>("#barfill");
const view = document.querySelector<HTMLCanvasElement>("#view");
const reportBtn = document.querySelector<HTMLButtonElement>("#report");
const lobbyEl = document.querySelector<HTMLElement>("#lobby");
const nickEl = document.querySelector<HTMLInputElement>("#nick");
const createRoomBtn = document.querySelector<HTMLButtonElement>("#create-room");
const joinCodeEl = document.querySelector<HTMLInputElement>("#join-code");
const joinRoomBtn = document.querySelector<HTMLButtonElement>("#join-room");
const lobbyStatus = document.querySelector<HTMLParagraphElement>("#lobby-status");
const rosterEl = document.querySelector<HTMLUListElement>("#roster");
const readyBtn = document.querySelector<HTMLButtonElement>("#ready-btn");
const startBtn = document.querySelector<HTMLButtonElement>("#start-btn");
const rttEl = document.querySelector<HTMLParagraphElement>("#rtt");

if (!drop || !pickBtn || !statusEl || !bar || !barfill || !view) {
  throw new Error("shell markup missing");
}

const dropEl = drop;
const status = statusEl;
const progressBar = bar;
const fill = barfill;
const canvas = view;

let game: GameBridge | null = null;
let player: AudioPlayer | null = null;
let raf = 0;
let lastStageNote = "stage not loaded";
let lookYawAcc = 0;
let lookPitchAcc = 0;
const C_UP = 0x0008;
const C_DOWN = 0x0004;
const STRAFE = 0x0020;
let lastHp = 8;
let hurtFlash = 0;
const held = new Set<string>();
let simN = 1;
let accMs = 0;
let lastPaint = 0;
let seenHits = 0;
const hitMarks: PortHit[] = [];
const checksumLog: Array<{
  tick: number;
  rng_lo: number;
  chr_rng_lo: number;
  crc_players: number;
  crc_chrs: number;
  crc_props: number;
  crc_objectives: number;
  pads: Array<{ x: number; y: number; buttons: number }>;
}> = [];
let lastPackHash = "";
const BUILD_ID = LOBBY_BUILD_ID;
const MOUSE_LOOK_DEG = 0.12;
let signal: SignalClient | null = null;
let lobbyReady = false;
let mySeat = 0;
let lastCfgHash = "";
let lastRosterSeats: number[] = [];
let mesh: PeerMesh | null = null;
let lastCfgHex = "";
let lastTurn: { username: string; credential: string } | null = null;
let iceFailSent = false;
let netLock: LockstepSession | null = null;
let lastNackAt = 0;
const pendingSdp: Array<{ from: number; desc: { type: "offer" | "answer"; sdp: string } }> = [];
const pendingIce: Array<{ from: number; cand: { candidate: string; sdpMid?: string; sdpMLineIndex?: number } }> = [];

function stickPad(opts: {
  up: boolean;
  down: boolean;
  left: boolean;
  right: boolean;
  fire: boolean;
}): ReturnType<typeof emptyPad> {
  const pad = emptyPad();
  if (opts.up) pad.y -= 70;
  if (opts.down) pad.y += 70;
  if (opts.left) pad.x -= 70;
  if (opts.right) pad.x += 70;
  if (opts.fire) pad.buttons |= 0x2000;
  return pad;
}

function padFromGamepad(gp: Gamepad): ReturnType<typeof emptyPad> {
  const ax = gp.axes[0] ?? 0;
  const ay = gp.axes[1] ?? 0;
  const fire = !!(gp.buttons[0]?.pressed || gp.buttons[6]?.pressed || gp.buttons[7]?.pressed);
  return {
    ...emptyPad(),
    x: Math.max(-70, Math.min(70, Math.round(ax * 70))),
    y: Math.max(-70, Math.min(70, Math.round(ay * 70))),
    buttons: fire ? 0x2000 : 0,
  };
}

function consumeMouseLook(): { lookYaw: number; lookPitch: number } {
  const yawQ = quantizeLookDeg(lookYawAcc * MOUSE_LOOK_DEG);
  const pitchQ = quantizeLookDeg(-lookPitchAcc * MOUSE_LOOK_DEG);
  lookYawAcc = 0;
  lookPitchAcc = 0;
  return { lookYaw: yawQ, lookPitch: pitchQ };
}

function padP1Move(): ReturnType<typeof emptyPad> {
  const pad = stickPad({
    up: held.has("KeyW"),
    down: held.has("KeyS"),
    left: held.has("KeyA"),
    right: held.has("KeyD"),
    fire: held.has("KeyZ") || held.has("Space"),
  });
  const oneP = !game || game.playerCount() <= 1 || !!netLock;
  if (held.has("KeyI") || (oneP && held.has("ArrowUp"))) pad.buttons |= C_UP;
  if (held.has("KeyK") || (oneP && held.has("ArrowDown"))) pad.buttons |= C_DOWN;
  if (typeof document !== "undefined" && document.pointerLockElement)
    pad.buttons |= STRAFE;
  return pad;
}

function padP2Keys(): ReturnType<typeof emptyPad> {
  return stickPad({
    up: held.has("ArrowUp"),
    down: held.has("ArrowDown"),
    left: held.has("ArrowLeft"),
    right: held.has("ArrowRight"),
    fire: held.has("Enter") || held.has("ShiftRight"),
  });
}

function syncHits(): void {
  if (!game?.ready()) return;
  const n = game.gunHits();
  if (n < seenHits) {
    hitMarks.length = 0;
    seenHits = n;
  }
  if (n > seenHits && game.gunHaveHit()) {
    hitMarks.push({ x: game.gunHitX(), y: game.gunHitY(), z: game.gunHitZ() });
    seenHits = n;
  }
}

function drawHud(): void {
  if (!game?.ready()) return;
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  if (game.playerCount() > 1 && !netLock) return;
  const x = game.playerX();
  const z = game.playerZ();
  const th = game.playerTheta();
  ctx.fillStyle = "rgba(18,20,24,0.72)";
  ctx.fillRect(0, 0, canvas.width, 62);
  ctx.fillStyle = "#e8e6e1";
  ctx.font = "11px ui-sans-serif, system-ui, sans-serif";
  ctx.fillText(`x ${x.toFixed(1)}  z ${z.toFixed(1)}  y ${game.playerY().toFixed(1)}  θ ${th.toFixed(0)}°  φ ${game.playerPhi().toFixed(0)}°  stan ${game.stanTiles()}${game.stanOnTile() ? "+" : "-"}`, 8, 14);
  const hp = game.health();
  if (hp < lastHp)
    hurtFlash = 12;
  lastHp = hp;
  ctx.fillStyle = hp <= 0 ? "#e07070" : hp < 8 || hurtFlash > 0 ? "#e07070" : "#e8e6e1";
  ctx.fillText(
    `PP7 ${game.gunMag()}/${game.gunReserve()}  hp ${hp}${game.armour() ? " arm " + game.armour() : ""}${hp <= 0 ? " DEAD" : ""}${hurtFlash > 0 && hp > 0 ? "  UNDER FIRE" : ""}  hits ${game.gunHits()}  crc ${game.crcPlayers().toString(16).padStart(8, "0")}`,
    8,
    28,
  );
  ctx.fillStyle = "#e8e6e1";
  {
    const los = game.guardLos();
    const gline =
      game.chrCount() > 0
        ? `kills ${game.kills()}  grd ${game.chrX().toFixed(0)},${game.chrZ().toFixed(0)}  act ${game.chrAction()}`
        : `kills ${game.kills()}`;
    ctx.fillText(
      `${gline}${los ? `  los ${los} shots ${game.guardShots()}` : ""}`,
      8,
      42,
    );
  }
  ctx.fillText(
    `fb ${game.fbNonzero()}  last ${game.lastDrawName()}  rm ${game.bgRooms()} wlk ${game.roomsWalked()} cur ${game.currentRoom()} c0 ${game.gdlC0() ? 1 : 0} vtx ${game.gdlVtx() ? 1 : 0} tex ${game.settex()}/${game.texOk()}/${game.texMiss()}`,
    8,
    56,
  );
}

function drawHurtFlash(): void {
  if (!game?.ready()) return;
  if (hurtFlash <= 0) return;
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  ctx.fillStyle = `rgba(140, 16, 16, ${0.22 + 0.04 * hurtFlash})`;
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  hurtFlash -= 1;
}

function drawDeathCue(): void {
  if (!game?.ready()) return;
  if (game.health() > 0) return;
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  ctx.fillStyle = "rgba(10, 6, 6, 0.48)";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "#e07070";
  ctx.font = "bold 48px ui-sans-serif, system-ui, sans-serif";
  ctx.fillText("DEAD", 16, Math.max(96, canvas.height * 0.42));
  ctx.fillStyle = "#e8e6e1";
  ctx.font = "14px ui-sans-serif, system-ui, sans-serif";
  ctx.fillText("hp 0", 18, Math.max(120, canvas.height * 0.42 + 26));
}

function flashPew(): void {
  canvas.classList.remove("pew");
  void canvas.offsetWidth;
  canvas.classList.add("pew");
  window.setTimeout(() => canvas.classList.remove("pew"), 200);
}

function ensurePlayer(): AudioPlayer | null {
  if (player) return player;
  if (!game?.ready()) return null;
  player = AudioPlayer.create((out, n) => {
    game?.audioCb(out, n);
  }, game.audioRate());
  /* Placeholder 196/294 Hz triangle drone stays off. Gun SFX still pumps. */
  if (player) game.audioSetMusic(false);
  return player;
}

async function bang(): Promise<void> {
  flashPew();
  const p = ensurePlayer();
  if (!p) {
    const why = lastAudioError() || (!game?.ready() ? "engine not ready" : "unknown");
    setStatus("err", `Audio did not start (${why}).`);
    return;
  }
  const ok = await p.resume();
  if (!ok) {
    setStatus("err", `Audio is still suspended (${lastAudioError() || "retry the click"}).`);
    return;
  }
  game?.audioPlayGun();
  p.kick();
}

function onAudioGesture(): void {
  unlockAudio(game?.audioRate() ?? 22050);
  void bang();
}

function paint(now: number): void {
  if (!game?.ready()) return;
  if (!lastPaint) lastPaint = now;
  accMs += now - lastPaint;
  lastPaint = now;
  const n = game.playerCount();
  const pads = [padP1Move()];
  const gps = typeof navigator !== "undefined" ? navigator.getGamepads?.() ?? [] : [];
  for (let seat = 1; seat < n; seat++) {
    const gp = gps[seat - 1];
    if (gp) pads[seat] = padFromGamepad(gp);
    else if (seat === 1) pads[seat] = padP2Keys();
    else pads[seat] = emptyPad();
  }
  while (accMs >= 50) {
    const local = { ...pads[0], ...consumeMouseLook() };
    if (netLock) {
      const evs = netLock.step(now, local, typeof document !== "undefined" && document.hidden);
      for (const ev of evs)
        onLockEvent(ev);
      if (netLock.history.length)
        sendInpAll(encodeInputDatagram(mySeat, netLock.history));
      if (rttEl && mesh)
        rttEl.textContent = `${mesh.rttLine()}  lock t=${netLock.nextTick} d=${netLock.delay}${netLock.stalled ? " STALL" : ""}`;
    } else {
      for (let seat = 0; seat < n; seat++) {
        const p = seat === 0 ? local : pads[seat];
        game.setPad(seat, p.x, p.y, p.buttons);
        if (p.lookYaw || p.lookPitch)
          game.setLookDelta(seat, lookDegFromQ(p.lookYaw), lookDegFromQ(p.lookPitch));
      }
      game.simTick(simN++);
      checksumLog.push({
        tick: simN - 1,
        rng_lo: game.rngLo(),
        chr_rng_lo: game.chrRngLo(),
        crc_players: game.crcPlayers(),
        crc_chrs: game.crcChrs(),
        crc_props: game.crcProps(),
        crc_objectives: game.crcObjectives(),
        pads: pads.map((p) => ({ x: p.x, y: p.y, buttons: p.buttons })),
      });
      if (checksumLog.length > 32)
        checksumLog.splice(0, checksumLog.length - 32);
    }
    accMs -= 50;
    if (netLock?.halted)
      break;
  }
  syncHits();
  const ctx = canvas.getContext("2d");
  if (ctx) {
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    const guard: PortChr[] =
      game.chrCount() > 0
        ? [{ x: game.chrX(), z: game.chrZ(), theta: game.chrTheta(), dead: game.chrAction() === 5 }]
        : [];
    const seats = netLock ? [mySeat] : Array.from({ length: n }, (_, i) => i);
    const hfov = netLock
      ? (horPlusHfovDeg(PORT_NATIVE_FOVY, canvas.width / canvas.height) * Math.PI) / 180
      : undefined;
    const drawable = stageHasDrawableRooms({ gdlRaw: game.gdlRaw(), gdlC0: game.gdlC0() });
    let stageFb: { rgba: Uint8Array; w: number; h: number } | undefined;
    if (drawable) {
      game.rasterStage();
      stageFb = game.stageFb() ?? undefined;
      /* A cleared FB is not a picture — keep the PORT mesh (never black). */
      if (stageFb && game.fbNonzero() === 0)
        stageFb = undefined;
    }
    if (drawable && stageFb) {
      const viewSeat = netLock ? mySeat : game.viewSeat();
      const overlay: PortChr[] = guard.slice();
      for (let j = 0; j < n; j++) {
        if (j === viewSeat) continue;
        overlay.push({
          x: game.playerXAt(j),
          z: game.playerZAt(j),
          theta: game.playerThetaAt(j),
          peer: true,
        });
      }
      presentLiveView(ctx, {
        gdlRaw: game.gdlRaw(),
        gdlC0: game.gdlC0(),
        fb: stageFb,
        cam: {
          x: game.playerXAt(viewSeat),
          z: game.playerZAt(viewSeat),
          theta: game.playerThetaAt(viewSeat),
          phi: game.playerPhiAt(viewSeat),
        },
        hits: hitMarks,
        chrs: overlay,
        box: { x: 0, y: 0, w: canvas.width, h: canvas.height },
        hfov,
      });
    }
    for (const seat of seats) {
      const box = netLock
        ? { x: 0, y: 0, w: canvas.width, h: canvas.height }
        : {
            x: game.vpLeft(seat),
            y: game.vpTop(seat),
            w: game.vpWidth(seat),
            h: game.vpHeight(seat),
          };
      if (!(drawable && stageFb)) {
        const peers: PortChr[] = [];
        for (let j = 0; j < n; j++) {
          if (j === seat) continue;
          peers.push({
            x: game.playerXAt(j),
            z: game.playerZAt(j),
            theta: game.playerThetaAt(j),
            peer: true,
          });
        }
        drawPortView(
          ctx,
          { x: game.playerXAt(seat), z: game.playerZAt(seat), theta: game.playerThetaAt(seat), phi: game.playerPhiAt(seat) },
          seat === mySeat ? hitMarks : [],
          guard.concat(peers),
          box,
          hfov,
        );
      }
      ctx.fillStyle = "rgba(18,20,24,0.72)";
      ctx.fillRect(box.x, box.y, box.w, 14);
      ctx.fillStyle = "#e8e6e1";
      ctx.font = "10px ui-sans-serif, system-ui, sans-serif";
      ctx.fillText(
        `P${seat + 1}  ${game.playerXAt(seat).toFixed(0)},${game.playerZAt(seat).toFixed(0)}`,
        box.x + 4,
        box.y + 11,
      );
    }
  }
  drawHurtFlash();
  drawHud();
  drawDeathCue();
  if (netLock?.overlay) {
    const ctx2 = canvas.getContext("2d");
    if (ctx2) {
      ctx2.fillStyle = "rgba(0,0,0,0.62)";
      ctx2.fillRect(0, canvas.height / 2 - 20, canvas.width, 40);
      ctx2.fillStyle = "#e8e6e1";
      ctx2.font = "13px ui-sans-serif, system-ui, sans-serif";
      ctx2.fillText(netLock.overlay, 10, canvas.height / 2 + 5);
    }
  }
  raf = requestAnimationFrame(paint);
}

function afterLoadStatus(packHash: string, fromIdb: boolean): string {
  const src = fromIdb ? "from this browser" : "loaded";
  const prefix = fromIdb ? "" : "NTSC-U verified. ";
  const net = flags.netplay
    ? "Netplay lobby is on (opt-in). Campaign is not v1."
    : "Solo. Netplay is opt-in (?ff_netplay=1). Campaign is not v1.";
  return prefix + "Pack " + packHash.slice(0, 16) + "… " + src + ". Live G1 blit if this pack\'s room GDL is drawable, else PORT mesh. " + lastStageNote + " Click picture or Z/Space for audio. " + net;
}

async function startEngine(packBytes: Uint8Array, packHashHex: string): Promise<void> {
  setStatus("", "Compiling engine…");
  if (!game) {
    game = await loadGame("/game.js");
  }
  lastPackHash = packHashHex;
  await game.init(packBytes, packHashBytes(packHashHex));
  lastHp = 8;
  hurtFlash = 0;
  if (player) {
    player.stop();
    player = null;
  }
  /* Do not construct AudioContext here: extract already consumed the
   * user gesture. The first click / Z / Space unlocks Web Audio. */
  /* Keep mixer ambient off on load. Click/Z unlocks Web Audio for one-shot SFX only. */
  game.audioSetMusic(false);
  {
    const FACILITY = 34;
    const rc = game.loadStage(FACILITY);
    if (rc === 0) {
      game.simTick(0);
      lastStageNote = game.gdlRaw()
        ? `Facility header + synthetic Fast3D room GDL — live canvas blits that G1 FB. Keys 1-4 split-screen (ENV ${game.envPlayers()}). P1 WASD+Z (Z/Space opens a facing door; click canvas + mouse or I/K / 1P arrows look), P2 arrows+Enter. (g_ClockTimer=${game.clockTimer()}).`
        : game.gdlC0()
        ? `Inflated 1172 C0 + vtx + player look-at + G_SETTEX (IA/RGBA16-64 + RGB15 lookup + Huffman/RLE-lookup). last_draw=${game.lastDraw()} rooms=${game.bgRooms()} walked=${game.roomsWalked()} cur=${game.currentRoom()} gdlC0=1 vtx=${game.gdlVtx() ? 1 : 0}. G1 walks the current room plus portal neighbors (depth 2). Clip is w/±x/±y/±z. A 64x64 RGBA16 SETTEX no longer misses the 4KB TMEM cap. Keys 1-4 split-screen (ENV ${game.envPlayers()}). P1 WASD+Z (Z/Space opens a facing door; click canvas + mouse or I/K / 1P arrows look), P2 arrows+Enter.`
        : `Facility bg/stan loaded (${game.bgRooms()} bg rooms). Rare GDL not drawable — PORT mesh kept (no black screen). Keys 1-4 split-screen (ENV ${game.envPlayers()}). P1 WASD+Z (Z/Space opens a facing door; click canvas + mouse or I/K / 1P arrows look), P2 arrows+Enter. (g_ClockTimer=${game.clockTimer()}).`;
    } else {
      lastStageNote = `Stage load rc=${rc} packFiles=${game.packFiles()}. ${game.lastError()} Hard-refresh (Ctrl+Shift+R) then drop the ROM so extract shows dma-v3.`;
    }
  }
  canvas.hidden = false;
  simN = 1;
  accMs = 0;
  lastPaint = 0;
  seenHits = 0;
  hitMarks.length = 0;
  checksumLog.length = 0;
  if (reportBtn)
    reportBtn.hidden = false;
  if (flags.netplay && lobbyEl)
    lobbyEl.hidden = false;
  cancelAnimationFrame(raf);
  raf = requestAnimationFrame(paint);
}

/** Last extract in this tab. Pack lives in IndexedDB. */
let lastExtract: ExtractedFile[] | null = null;

function setStatus(kind: "ok" | "err" | "", text: string): void {
  status.className = `status${kind ? ` ${kind}` : ""}`;
  status.textContent = text;
}

async function ingest(name: string, bytes: Uint8Array): Promise<void> {
  progressBar.hidden = true;
  fill.style.width = "0%";
  setStatus("", `Checking ${name}…`);
  await new Promise<void>((resolve) => {
    requestAnimationFrame(() => resolve());
  });
  try {
    const result = await verifyRom(bytes);
    if (!result.ok) {
      setStatus("err", result.message);
      return;
    }
    try {
      await kvSet("lastRegion", result.region);
    } catch {
      // IndexedDB is optional for verify; don't block the gate message.
    }
    setStatus("", "Extracting assets in this tab…");
    progressBar.hidden = false;
    fill.style.width = "0%";
    lastExtract = await extractRom(result.z64, "U", (p) => {
      const pct = p.total === 0 ? 0 : Math.round((100 * p.done) / p.total);
      fill.style.width = `${pct}%`;
      setStatus("", `Extracting ${p.done}/${p.total}  ${p.current}`);
    });
    setStatus("", "Building c0pack…");
    const pack = await buildPack(lastExtract, "U", 0);
    try {
      await packPut(pack.packHash, {
        region: "U",
        romSha1: result.romSha1,
        created: Date.now(),
        blob: pack.bytes.buffer as ArrayBuffer,
      });
      await kvSet("lastPackHash", pack.packHash);
      if (navigator.storage?.persist) {
        void navigator.storage.persist();
      }
    } catch {
      /* pack still usable in-memory this session */
    }
    const totalBytes = lastExtract.reduce((n, f) => n + f.bytes.byteLength, 0);
    const hasArk = lastExtract.some((f) => f.path.includes("bg_ark_all_p"));
    setStatus(
      "ok",
      `NTSC-U verified. Extracted ${lastExtract.length} files (${(totalBytes / (1024 * 1024)).toFixed(1)} MiB, ark=${hasArk ? "yes" : "no"}). Pack ${pack.packHash.slice(0, 16)}… stored. Starting engine…`,
    );
    try {
      await startEngine(pack.bytes, pack.packHash);
      setStatus(
        "ok",
        afterLoadStatus(pack.packHash, false),
      );
    } catch (eng) {
      const msg = eng instanceof Error ? eng.message : String(eng);
      setStatus("err", `Pack stored, but engine init failed (${msg}).`);
    }
  } catch (err) {
    progressBar.hidden = true;
    const msg = err instanceof Error ? err.message : String(err);
    setStatus("err", `Could not verify or extract that file (${msg}).`);
  }
}

canvas.addEventListener("pointerdown", () => {
  onAudioGesture();
  if (document.pointerLockElement !== canvas)
    void canvas.requestPointerLock();
});
document.addEventListener("mousemove", (ev) => {
  if (document.pointerLockElement !== canvas) return;
  lookYawAcc += ev.movementX;
  lookPitchAcc += ev.movementY;
});
window.addEventListener("keydown", (ev) => {
  if (ev.code === "Digit1" || ev.code === "Digit2" || ev.code === "Digit3" || ev.code === "Digit4") {
    ev.preventDefault();
    if (netLock)
      return;
    const n = Number(ev.code.slice(5));
    if (game?.ready()) {
      game.setPlayerCount(n);
      lastStageNote = `${n}P local  ENV ${game.envPlayers()}. P1 WASD+Z; P2 arrows+Enter or gamepad.`;
      setStatus("ok", lastStageNote);
    }
    return;
  }
  if (ev.code === "Space" || ev.code === "KeyZ" || ev.code === "Enter") {
    ev.preventDefault();
    held.add(ev.code);
    onAudioGesture();
    return;
  }
  if (
    ev.code === "KeyW" ||
    ev.code === "KeyA" ||
    ev.code === "KeyS" ||
    ev.code === "KeyD" ||
    ev.code === "ArrowUp" ||
    ev.code === "ArrowDown" ||
    ev.code === "ArrowLeft" ||
    ev.code === "ArrowRight" ||
    ev.code === "KeyI" ||
    ev.code === "KeyK" ||
    ev.code === "ShiftRight"
  ) {
    ev.preventDefault();
    held.add(ev.code);
  }
});
window.addEventListener("keyup", (ev) => {
  held.delete(ev.code);
});


function meshNeedsRelay(): boolean {
  return !mesh || !mesh.meshFullyUp();
}

function shouldWsRelay(): boolean {
  return flags.wsRelay || meshNeedsRelay();
}

function declareIceFail(): void {
  if (iceFailSent)
    return;
  iceFailSent = true;
  signal?.send({ v: 1, t: "ice_fail" });
  void postIceMetric("ice_fail");
}

function postIceMetric(name: string): void {
  const body: Record<string, number> = { [name]: 1 };
  void fetch("/api/m", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  }).catch(() => undefined);
}

function sendInpAll(buf: Uint8Array): void {
  if (mesh)
    mesh.sendInp(buf);
  if (shouldWsRelay()) {
    declareIceFail();
    signal?.send({ v: 1, t: "relay", from: mySeat, kind: "inp", data: hexBytes(buf) });
  }
}

function sendCtlAll(msg: CtlMsg): void {
  if (mesh)
    mesh.sendCtl(msg);
  if (shouldWsRelay()) {
    declareIceFail();
    signal?.send({ v: 1, t: "relay", from: mySeat, kind: "ctl", data: JSON.stringify(msg) });
  }
}

function handleCtl(from: number, msg: CtlMsg): void {
  if (!netLock)
    return;
  if (msg.t === "ck") {
    const ev = netLock.acceptRemoteCk(msg);
    if (ev)
      onLockEvent(ev);
  } else if (msg.t === "nack") {
    if (netLock.history.length)
      sendInpAll(encodeInputDatagram(mySeat, netLock.history));
  } else if (msg.t === "bye") {
    if (netLock.halted)
      return;
    if (!netLock.meshLinked)
      onLockEvent(netLock.endMatch(ICE_FAIL_OVERLAY, from));
    else if (from === 0)
      onLockEvent(netLock.endMatch("Host disconnected. Match ended.", 0));
    else
      onLockEvent(netLock.endMatch(`P${from + 1} left. Match ended.`, from));
  } else if (msg.t === "desync") {
    netLock.desynced = true;
    netLock.overlay = `DESYNC at tick ${msg.tick}`;
    onLockEvent({
      t: "desync",
      tick: msg.tick,
      local: netLock.localCk.get(msg.tick) ?? {
        tick: msg.tick,
        rng_lo: 0,
        chr_rng_lo: 0,
        crc_players: 0,
        crc_chrs: 0,
        crc_props: 0,
        crc_objectives: 0,
      },
      remote: {
        tick: msg.tick,
        rng_lo: 0,
        chr_rng_lo: 0,
        crc_players: 0,
        crc_chrs: 0,
        crc_props: 0,
        crc_objectives: 0,
      },
    });
  }
}

function ensureSignal(): SignalClient {
  if (signal)
    return signal;
  signal = new SignalClient({
    onStatus(text) {
      if (lobbyStatus)
        lobbyStatus.textContent = text;
    },
    onCode(code, seat) {
      mySeat = seat;
      lastRosterSeats = [seat];
      if (lobbyStatus)
        lobbyStatus.textContent = `Room ${code} seat ${seat}`;
      if (joinCodeEl)
        joinCodeEl.value = code;
      if (readyBtn)
        readyBtn.hidden = false;
      if (startBtn)
        startBtn.hidden = seat !== 0;
      if (seat === 0 && lastPackHash)
        publishLobbyCfg(Math.max(2, lastRosterSeats.length || 2));
    },
    onRoster(seats) {
      lastRosterSeats = seats.map((s) => s.seat);
      if (mySeat === 0 && lastPackHash && !netLock)
        publishLobbyCfg(seats.length);
      if (netLock && !seats.some((s) => s.seat === 0))
        onLockEvent(netLock.endMatch("Host disconnected. Match ended.", 0));
      if (!rosterEl)
        return;
      rosterEl.replaceChildren();
      for (const s of seats) {
        const li = document.createElement("li");
        li.textContent = `P${s.seat + 1} ${s.nick}${s.ready ? " ready" : ""}`;
        rosterEl.append(li);
      }
    },
    onError(code, msg) {
      if (lobbyStatus) {
        lobbyStatus.className = "status err";
        lobbyStatus.textContent = `${code}: ${msg}`;
      }
      if (code === "EXPIRED" && netLock && !netLock.halted) {
        const hostGone = /host/i.test(msg);
        onLockEvent(netLock.endMatch(hostGone ? "Host disconnected. Match ended." : `${msg}. Match ended.`, 0));
      }
    },
    onCfg(cfg, cfgHash) {
      lastCfgHash = cfgHash;
      lastCfgHex = cfg;
    },
    onStart() {
      void beginMesh();
    },
    onSdp(from, to, desc) {
      if (to !== mySeat)
        return;
      if (mesh)
        void mesh.handleSdp(from, desc);
      else
        pendingSdp.push({ from, desc });
    },
    onIce(from, to, cand) {
      if (to !== mySeat)
        return;
      if (mesh)
        void mesh.handleIce(from, cand);
      else
        pendingIce.push({ from, cand });
    },
    onTurn(turn) {
      if (turn.username && turn.credential)
        lastTurn = { username: turn.username, credential: turn.credential };
    },
    onRelay(from, kind, data) {
      if (from === mySeat)
        return;
      if (kind === "inp") {
        const dg = decodeInputDatagram(bytesFromHex(data));
        if (dg)
          netLock?.ingest(dg.blocks);
        return;
      }
      try {
        handleCtl(from, JSON.parse(data) as CtlMsg);
      } catch {
        /* ignore bad ctl */
      }
    },
  });
  signal.connect(defaultSignalUrl());
  return signal;
}

createRoomBtn?.addEventListener("click", () => {
  if (!lastPackHash) {
    setStatus("err", "Load a ROM first.");
    return;
  }
  const nick = nickEl?.value.trim() || "Player";
  ensureSignal().send({ v: 1, t: "create", nick, packHash: lastPackHash, region: "U", buildId: BUILD_ID });
});

joinRoomBtn?.addEventListener("click", () => {
  if (!lastPackHash) {
    setStatus("err", "Load a ROM first.");
    return;
  }
  const nick = nickEl?.value.trim() || "Player";
  const code = (joinCodeEl?.value || "").trim().toUpperCase();
  ensureSignal().send({ v: 1, t: "join", code, nick, packHash: lastPackHash, region: "U", buildId: BUILD_ID });
});

readyBtn?.addEventListener("click", () => {
  lobbyReady = !lobbyReady;
  ensureSignal().send({ v: 1, t: "ready", seat: mySeat, ready: lobbyReady });
  readyBtn.textContent = lobbyReady ? "Unready" : "Ready";
});

startBtn?.addEventListener("click", () => {
  if (!lastCfgHash) {
    setStatus("err", "No MatchConfig yet.");
    return;
  }
  const n = lastRosterSeats.length;
  if (n < 2 || n > 4) {
    setStatus("err", "Need 2–4 players before Start.");
    return;
  }
  if (lastPackHash)
    publishLobbyCfg(n);
  ensureSignal().send({ v: 1, t: "start", cfgHash: lastCfgHash });
});

async function beginMesh(): Promise<void> {
  iceFailSent = false;
  if (mesh)
    mesh.close();
  const sig = ensureSignal();
  mesh = new PeerMesh(
    mySeat,
    {
      sendSdp(to, desc) {
        if (desc.type === "offer" || desc.type === "answer")
          sig.send({ v: 1, t: "sdp", from: mySeat, to, desc: { type: desc.type, sdp: desc.sdp || "" } });
      },
      sendIce(to, cand) {
        sig.send({
          v: 1,
          t: "ice",
          from: mySeat,
          to,
          cand: { candidate: cand.candidate || "", sdpMid: cand.sdpMid ?? undefined, sdpMLineIndex: cand.sdpMLineIndex ?? undefined },
        });
      },
    },
    flags.turnForce,
    (s) => {
      if (netLock && rttEl) {
        rttEl.textContent = s;
        return;
      }
      if (lobbyStatus)
        lobbyStatus.textContent = s;
    },
    lastTurn,
  );
  mesh.onInp = (_from, data) => {
    const dg = decodeInputDatagram(data);
    if (dg)
      netLock?.ingest(dg.blocks);
  };
  mesh.onCtlMsg = (from, msg) => handleCtl(from, msg);
  mesh.onIcePath = (path) => {
    signal?.send({ v: 1, t: "ice_ok", path });
    void postIceMetric(`ice_ok_${path}`);
  };
  mesh.onInpOpen = () => {
    if (netLock)
      netLock.markLinked();
    if (netLock?.history.length)
      sendInpAll(encodeInputDatagram(mySeat, netLock.history));
  };
  mesh.onPeerLost = (seat, state) => {
    if (!netLock || netLock.halted)
      return;
    /* ICE connecting/checking/failed before inp opened is not "player left". */
    if (!mesh?.inpEverOpened(seat)) {
      if (rttEl)
        rttEl.textContent = `P${seat + 1} WebRTC ${state}`;
      return;
    }
    if (seat === 0)
      onLockEvent(netLock.endMatch("Host disconnected. Match ended.", 0));
  };
  for (const seat of meshOfferTargets(mySeat, lastRosterSeats))
    await mesh.offerTo(seat);
  for (const p of pendingSdp)
    await mesh.handleSdp(p.from, p.desc);
  pendingSdp.length = 0;
  for (const p of pendingIce)
    await mesh.handleIce(p.from, p.cand);
  pendingIce.length = 0;
  mesh.startPing();
  startLockstep();
  if (netLock?.history.length)
    sendInpAll(encodeInputDatagram(mySeat, netLock.history));
  if (lobbyStatus)
    lobbyStatus.textContent = `Lockstep ${netLock?.nseats ?? 2}P delay ${netLock?.delay ?? 2} (${dataChannelsPerPeer(netLock?.nseats ?? 2)} DataChannels). WASD+Z is this seat. Keep tab visible.`;
}

function publishLobbyCfg(nseats: number): void {
  if (!lastPackHash)
    return;
  const packed = packedLobbyCfg(lastPackHash, nseats, flags.lan ? 1 : 2);
  lastCfgHash = packed.cfgHash;
  lastCfgHex = packed.cfg;
  signal?.send({ v: 1, t: "cfg", cfg: packed.cfg, cfgHash: packed.cfgHash });
}

function applyRemoteView(): void {
  if (!game?.ready() || !netLock)
    return;
  if (flags.widescreen) {
    canvas.width = 640;
    canvas.height = 360;
  } else {
    canvas.width = 320;
    canvas.height = 240;
  }
  const w = canvas.width;
  const h = canvas.height;
  game.setViewSeat(mySeat);
  game.setScreenSize(w, h);
  game.setScreenPosition(0, 0);
  game.setPerspective(30, PORT_NATIVE_FOVY, w / h);
}

function lockEngine(): LockstepEngine {
  return {
    beginMatch(nseats, seed) {
      game?.beginMatch(nseats, seed);
      lastHp = 8;
      hurtFlash = 0;
    },
    applyTick(tick, pads) {
      if (!game)
        return -1;
      for (let i = 0; i < pads.length; i++) {
        game.setPad(i, pads[i].x, pads[i].y, pads[i].buttons);
        if (pads[i].lookYaw || pads[i].lookPitch)
          game.setLookDelta(i, lookDegFromQ(pads[i].lookYaw), lookDegFromQ(pads[i].lookPitch));
      }
      return game.simTick(tick);
    },
    snapshot(tick) {
      return {
        tick,
        rng_lo: game?.rngLo() ?? 0,
        chr_rng_lo: game?.chrRngLo() ?? 0,
        crc_players: game?.crcPlayers() ?? 0,
        crc_chrs: game?.crcChrs() ?? 0,
        crc_props: game?.crcProps() ?? 0,
        crc_objectives: game?.crcObjectives() ?? 0,
      };
    },
  };
}

function startLockstep(): void {
  if (!game?.ready())
    return;
  const cfg = lastCfgHex ? decodeMatchConfig(bytesFromHex(lastCfgHex)) : null;
  const nseats = Math.min(4, Math.max(2, lastRosterSeats.length || 0, cfg?.nseats ?? 0));
  const delay = Math.min(3, Math.max(1, cfg?.delayTicks ?? 2));
  const seed = cfg?.rngSeed ?? 1;
  netLock = new LockstepSession(mySeat, nseats, delay, lockEngine());
  netLock.start(seed);
  simN = 0;
  accMs = 0;
  lastPaint = 0;
  seenHits = 0;
  hitMarks.length = 0;
  checksumLog.length = 0;
  applyRemoteView();
  void startMatchHold();
  lastStageNote = `${nseats}P lockstep delay ${delay} Hor+ seat ${mySeat}. WASD+Z this seat (P${mySeat + 1}). Keep tab visible.`;
  setStatus("ok", lastStageNote);
}

function onLockEvent(ev: LockstepEvent): void {
  if (ev.t === "ran") {
    simN = ev.tick + 1;
    checksumLog.push({
      tick: ev.tick,
      rng_lo: ev.ck.rng_lo,
      chr_rng_lo: ev.ck.chr_rng_lo,
      crc_players: ev.ck.crc_players,
      crc_chrs: ev.ck.crc_chrs,
      crc_props: ev.ck.crc_props,
      crc_objectives: ev.ck.crc_objectives,
      pads: [],
    });
    if (checksumLog.length > 32)
      checksumLog.splice(0, checksumLog.length - 32);
    sendCtlAll({
      t: "ck",
      tick: ev.ck.tick,
      rng_lo: ev.ck.rng_lo,
      chr_rng_lo: ev.ck.chr_rng_lo,
      crc_players: ev.ck.crc_players,
      crc_chrs: ev.ck.crc_chrs,
      crc_props: ev.ck.crc_props,
      crc_objectives: ev.ck.crc_objectives,
    });
    if (lobbyStatus && (lobbyStatus.textContent || "").includes("waiting"))
      lobbyStatus.textContent = `Lockstep ${netLock?.nseats ?? 2}P delay ${netLock?.delay ?? 2}. WASD+Z is this seat.`;
  } else if (ev.t === "stall") {
    sendCtlAll({ t: "stall", seat: ev.seat });
    const t = performance.now();
    if (t - lastNackAt > 200) {
      lastNackAt = t;
      sendCtlAll({ t: "nack", fromTick: netLock?.nextTick ?? 0, toTick: netLock?.nextTick ?? 0 });
    }
    if (lobbyStatus)
      lobbyStatus.textContent = netLock?.overlay ?? "";
  } else if (ev.t === "drop") {
    void releaseMatchHold();
    sendCtlAll({ t: "bye" });
    mesh?.close();
    if (lobbyStatus)
      lobbyStatus.textContent = netLock?.overlay ?? "";
    setStatus("err", netLock?.overlay ?? "peer dropped");
  } else if (ev.t === "desync") {
    void releaseMatchHold();
    sendCtlAll({ t: "desync", tick: ev.tick });
    if (lobbyStatus)
      lobbyStatus.textContent = netLock?.overlay ?? "";
    setStatus("err", `${netLock?.overlay ?? "DESYNC"}. Copy the debug report (no ROM).`);
  } else if (ev.t === "hidden") {
    sendCtlAll({ t: "stall", seat: mySeat });
    if (lobbyStatus)
      lobbyStatus.textContent = netLock?.overlay ?? "tab must stay visible.";
  }
}

reportBtn?.addEventListener("click", async () => {
  if (!game?.ready()) return;
  const json = JSON.stringify(
    buildReport({
      buildId: "shell",
      packHash: lastPackHash,
      nseats: netLock?.nseats ?? game.playerCount(),
      seat: mySeat,
      tick: simN,
      delayTicks: netLock?.delay ?? 0,
      checksums: checksumLog.map(({ pads: _p, ...cs }) => cs),
      tapeExcerpt: encodeTapeExcerpt(
        game.playerCount(),
        checksumLog.map((s) => ({ tick: s.tick, pads: s.pads })),
      ),
      flags: {
        netplay: flags.netplay,
        turnForce: flags.turnForce,
        wsRelay: flags.wsRelay,
        widescreen: flags.widescreen,
      },
    }),
    null,
    2,
  );
  try {
    await navigator.clipboard.writeText(json);
    setStatus("ok", "Copied silveriris-report/1 (no ROM).");
  } catch {
    setStatus("err", "Could not copy debug report.");
  }
});

pickBtn.addEventListener("pointerdown", () => {
  unlockAudio();
});
pickBtn.addEventListener("click", () => {
  void pickRomFile().then((picked) => {
    if (picked) return ingest(picked.name, picked.bytes);
  });
});

dropEl.addEventListener("dragover", (ev) => {
  ev.preventDefault();
  dropEl.classList.add("dragover");
});
dropEl.addEventListener("dragleave", () => dropEl.classList.remove("dragover"));
dropEl.addEventListener("drop", async (ev) => {
  ev.preventDefault();
  unlockAudio();
  dropEl.classList.remove("dragover");
  const file = romFromDrop(ev.dataTransfer);
  if (!file) return;
  const bytes = new Uint8Array(await file.arrayBuffer());
  void ingest(file.name, bytes);
});

void (async () => {
  try {
    const hash = await kvGet<string>("lastPackHash");
    if (!hash) return;
    const stored = await packGet(hash);
    if (!stored) return;
    setStatus("", "Loading pack already in this browser…");
    await startEngine(new Uint8Array(stored.blob), hash);
    setStatus(
      "ok",
      afterLoadStatus(hash, true),
    );
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    setStatus("", `Pack is in this browser. Engine not started (${msg}).`);
  }
})();
void flags;

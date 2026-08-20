/** PORT first-person view of the z=-50 wall (gun.h PORT_WALL_Z). Not Facility mesh. */

export const PORT_WALL_Z = -50;
export const PORT_VIEW_FOV = (70 * Math.PI) / 180;

export type PortCam = { x: number; z: number; theta: number };
export type PortHit = { x: number; y: number; z: number };
export type PortChr = { x: number; z: number; theta: number; dead?: boolean; peer?: boolean };
export type PortViewBox = { x: number; y: number; w: number; h: number };

export function wrapPi(a: number): number {
  while (a > Math.PI) a -= Math.PI * 2;
  while (a < -Math.PI) a += Math.PI * 2;
  return a;
}

/** Look dir at theta=0 is (0,-1) in XZ — same as gun.c / move.c. */
export function lookDir(thetaDeg: number): { dx: number; dz: number } {
  const th = (thetaDeg * Math.PI) / 180;
  return { dx: Math.sin(th), dz: -Math.cos(th) };
}

export function wallHitscan(cam: PortCam): PortHit | null {
  const { dx, dz } = lookDir(cam.theta);
  if (dz === 0) return null;
  const t = (PORT_WALL_Z - cam.z) / dz;
  if (t < 0.05 || t > 4000) return null;
  return { x: cam.x + dx * t, y: 0, z: cam.z + dz * t };
}

export function projectWorld(
  wx: number,
  wy: number,
  wz: number,
  cam: PortCam,
  w: number,
  h: number,
): { sx: number; sy: number; dist: number } | null {
  const th = (cam.theta * Math.PI) / 180;
  const hitAng = Math.atan2(wx - cam.x, -(wz - cam.z));
  const dAng = wrapPi(hitAng - th);
  if (Math.abs(dAng) > PORT_VIEW_FOV * 0.55) return null;
  const dx = wx - cam.x;
  const dz = wz - cam.z;
  const dist = dx * Math.sin(th) - dz * Math.cos(th);
  if (dist < 0.2) return null;
  const sx = w / 2 + (dAng / PORT_VIEW_FOV) * w;
  const sy = h / 2 - (wy / dist) * (h * 0.35);
  return { sx, sy, dist };
}

function shadeRgb(r: number, g: number, b: number, shade: number): string {
  const s = Math.max(0.18, Math.min(1, shade));
  return `rgb(${(r * s) | 0},${(g * s) | 0},${(b * s) | 0})`;
}

function drawRadar(
  ctx: CanvasRenderingContext2D,
  cam: PortCam,
  hits: readonly PortHit[],
  chrs: readonly PortChr[],
  viewH: number,
): void {
  const size = viewH >= 180 ? 72 : 36;
  const pad = 4;
  const x0 = pad;
  const y0 = viewH - size - pad;
  ctx.fillStyle = "rgba(10,12,14,0.78)";
  ctx.fillRect(x0, y0, size, size);
  ctx.strokeStyle = "rgba(196,181,154,0.45)";
  ctx.strokeRect(x0 + 0.5, y0 + 0.5, size - 1, size - 1);

  const scale = size / 280;
  const mapX = (wx: number) => x0 + size / 2 + wx * scale;
  /* +Z is down so the z=-50 wall sits above spawn (look-forward). */
  const mapY = (wz: number) => y0 + size / 2 + wz * scale;

  ctx.strokeStyle = "#6a7a88";
  ctx.beginPath();
  ctx.moveTo(mapX(-140), mapY(PORT_WALL_Z));
  ctx.lineTo(mapX(140), mapY(PORT_WALL_Z));
  ctx.stroke();

  ctx.strokeStyle = "rgba(160,64,48,0.55)";
  ctx.beginPath();
  ctx.moveTo(mapX(-80), mapY(-20));
  ctx.lineTo(mapX(80), mapY(-20));
  ctx.lineTo(mapX(80), mapY(80));
  ctx.lineTo(mapX(-80), mapY(80));
  ctx.closePath();
  ctx.stroke();

  for (const hit of hits) {
    ctx.fillStyle = "#e8c14a";
    ctx.fillRect(mapX(hit.x) - 1, mapY(hit.z) - 1, 2, 2);
  }

  for (const chr of chrs) {
    const { dx: cdx, dz: cdz } = lookDir(chr.theta);
    const cx = mapX(chr.x);
    const cz = mapY(chr.z);
    ctx.fillStyle = chr.peer ? "#7ec8e3" : "#c45a48";
    ctx.beginPath();
    ctx.moveTo(cx + cdx * 6, cz + cdz * 6);
    ctx.lineTo(cx - cdx * 3 + cdz * 2.5, cz - cdz * 3 - cdx * 2.5);
    ctx.lineTo(cx - cdx * 3 - cdz * 2.5, cz - cdz * 3 + cdx * 2.5);
    ctx.closePath();
    ctx.fill();
  }

  const px = mapX(cam.x);
  const pz = mapY(cam.z);
  const { dx, dz } = lookDir(cam.theta);
  ctx.fillStyle = "#e8e6e1";
  ctx.beginPath();
  ctx.moveTo(px + dx * 7, pz + dz * 7);
  ctx.lineTo(px - dx * 4 + dz * 3, pz - dz * 4 - dx * 3);
  ctx.lineTo(px - dx * 4 - dz * 3, pz - dz * 4 + dx * 3);
  ctx.closePath();
  ctx.fill();
}

export function drawPortView(
  ctx: CanvasRenderingContext2D,
  cam: PortCam,
  hits: readonly PortHit[],
  chrs: readonly PortChr[] = [],
  box?: PortViewBox,
): void {
  const ox = box?.x ?? 0;
  const oy = box?.y ?? 0;
  const w = box?.w ?? ctx.canvas.width;
  const h = box?.h ?? ctx.canvas.height;
  ctx.save();
  if (box) {
    ctx.beginPath();
    ctx.rect(ox, oy, w, h);
    ctx.clip();
    ctx.translate(ox, oy);
  }
  ctx.fillStyle = "#1a2430";
  ctx.fillRect(0, 0, w, h / 2);
  ctx.fillStyle = "#2a2218";
  ctx.fillRect(0, h / 2, w, h / 2);

  const th0 = (cam.theta * Math.PI) / 180;
  for (let col = 0; col < w; col++) {
    const ndc = (col + 0.5) / w - 0.5;
    const ang = th0 + ndc * PORT_VIEW_FOV;
    const dx = Math.sin(ang);
    const dz = -Math.cos(ang);
    if (Math.abs(dz) < 1e-5) continue;
    const t = (PORT_WALL_Z - cam.z) / dz;
    if (t < 0.2) continue;
    const hx = cam.x + dx * t;
    const zcorr = t * Math.cos(ndc * PORT_VIEW_FOV);
    if (zcorr < 0.2) continue;
    const sliceH = Math.min(h, (140 / zcorr) * (h * 0.5));
    const y0 = (h - sliceH) / 2;
    const stripe = Math.abs(Math.floor(hx / 32)) % 2;
    const shade = 90 / zcorr;
    ctx.fillStyle = stripe ? shadeRgb(62, 74, 58, shade) : shadeRgb(96, 108, 88, shade);
    ctx.fillRect(col, y0, 1, sliceH);
  }

  ctx.lineWidth = 1;
  for (const chr of chrs) {
    const feet = projectWorld(chr.x, 0, chr.z, cam, w, h);
    const headY = chr.dead ? 8 : 80;
    const head = projectWorld(chr.x, headY, chr.z, cam, w, h);
    if (!feet || !head) continue;
    const bw = Math.max(4, 220 / feet.dist);
    ctx.fillStyle = chr.peer ? "#5aa0b8" : chr.dead ? "#4a3030" : "#a04030";
    ctx.fillRect(head.sx - bw * 0.5, head.sy, bw, Math.max(4, feet.sy - head.sy));
  }
  for (const hit of hits) {
    const p = projectWorld(hit.x, hit.y, hit.z, cam, w, h);
    if (!p) continue;
    const s = Math.max(2, 18 / p.dist);
    ctx.strokeStyle = "#e8c14a";
    ctx.beginPath();
    ctx.moveTo(p.sx - s, p.sy);
    ctx.lineTo(p.sx + s, p.sy);
    ctx.moveTo(p.sx, p.sy - s);
    ctx.lineTo(p.sx, p.sy + s);
    ctx.stroke();
  }

  ctx.strokeStyle = "rgba(232,230,225,0.85)";
  ctx.beginPath();
  ctx.moveTo(w / 2 - 5, h / 2);
  ctx.lineTo(w / 2 + 5, h / 2);
  ctx.moveTo(w / 2, h / 2 - 5);
  ctx.lineTo(w / 2, h / 2 + 5);
  ctx.stroke();

  ctx.fillStyle = "#3a3a38";
  ctx.beginPath();
  ctx.moveTo(w * 0.42, h);
  ctx.lineTo(w * 0.5, h * 0.74);
  ctx.lineTo(w * 0.58, h);
  ctx.fill();
  ctx.fillStyle = "#2a2a28";
  ctx.fillRect(w * 0.48, h * 0.74, w * 0.04, h * 0.08);

  if (h >= 100)
    drawRadar(ctx, cam, hits, chrs, h);
  ctx.restore();
}

/** InputBlock + InputDatagram packing. Magic matches src/port/net/input_block.h. */

export const INPUT_MAGIC = 0x49314e42; /* BIN1 */
export const DATAGRAM_MAGIC = 0x524e4942; /* BINR */
export const INPUT_REDUNDANCY = 8;
export const INPUT_BLOCK_BYTES = 20;
export const DATAGRAM_BYTES = 8 + INPUT_REDUNDANCY * INPUT_BLOCK_BYTES;

export type PortPad = { x: number; y: number; buttons: number };

export type InputBlock = {
  tick: number;
  seat: number;
  nseats: number;
  delay: number;
  pad: PortPad;
  simCrc: number;
};

function wr32(b: Uint8Array, o: number, v: number): void {
  b[o] = v & 0xff;
  b[o + 1] = (v >>> 8) & 0xff;
  b[o + 2] = (v >>> 16) & 0xff;
  b[o + 3] = (v >>> 24) & 0xff;
}

function rd32(b: Uint8Array, o: number): number {
  return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24);
}

export function encodeInputBlock(b: InputBlock, out: Uint8Array, o = 0): void {
  wr32(out, o, INPUT_MAGIC);
  wr32(out, o + 4, b.tick >>> 0);
  out[o + 8] = b.seat & 0xff;
  out[o + 9] = b.nseats & 0xff;
  out[o + 10] = b.delay & 0xff;
  out[o + 11] = 0;
  out[o + 12] = b.pad.x;
  out[o + 13] = b.pad.y;
  out[o + 14] = b.pad.buttons & 0xff;
  out[o + 15] = (b.pad.buttons >>> 8) & 0xff;
  wr32(out, o + 16, b.simCrc >>> 0);
}

export function decodeInputBlock(buf: Uint8Array, o = 0): InputBlock | null {
  if (rd32(buf, o) !== INPUT_MAGIC)
    return null;
  return {
    tick: rd32(buf, o + 4) >>> 0,
    seat: buf[o + 8],
    nseats: buf[o + 9],
    delay: buf[o + 10],
    pad: { x: (buf[o + 12] << 24) >> 24, y: (buf[o + 13] << 24) >> 24, buttons: buf[o + 14] | (buf[o + 15] << 8) },
    simCrc: rd32(buf, o + 16) >>> 0,
  };
}

export function encodeInputDatagram(seat: number, blocks: InputBlock[]): Uint8Array {
  const out = new Uint8Array(DATAGRAM_BYTES);
  wr32(out, 0, DATAGRAM_MAGIC);
  out[4] = seat & 0xff;
  const n = Math.max(0, Math.min(INPUT_REDUNDANCY, blocks.length));
  out[5] = n;
  for (let i = 0; i < n; i++)
    encodeInputBlock(blocks[i], out, 8 + i * INPUT_BLOCK_BYTES);
  return out;
}

export function decodeInputDatagram(buf: Uint8Array): { seat: number; blocks: InputBlock[] } | null {
  if (buf.byteLength < 8 || rd32(buf, 0) !== DATAGRAM_MAGIC)
    return null;
  const count = buf[5];
  if (count < 1 || count > INPUT_REDUNDANCY)
    return null;
  const blocks: InputBlock[] = [];
  for (let i = 0; i < count; i++) {
    const b = decodeInputBlock(buf, 8 + i * INPUT_BLOCK_BYTES);
    if (b)
      blocks.push(b);
  }
  return { seat: buf[4], blocks };
}

import { rtcConfiguration } from "./ice.ts";
import { validateIce, validateSdp } from "./wire.ts";

/** Lower seat offers to every higher seat: C(n,2) links, 2 channels each. */
export function meshOfferTargets(mySeat: number, seats: readonly number[]): number[] {
  return seats.filter((s) => s > mySeat).sort((a, b) => a - b);
}

/** inp + ctl per remote. 4P full mesh = 3 remotes × 2 = 6 DataChannels. */
export function dataChannelsPerPeer(nseats: number): number {
  const n = Math.max(0, (nseats | 0) - 1);
  return n * 2;
}

export type CtlMsg =
  | { t: "ping"; t0: number }
  | { t: "pong"; t0: number }
  | { t: "nack"; fromTick: number; toTick: number }
  | { t: "ck"; tick: number; rng_lo: number; chr_rng_lo: number; crc_players: number; crc_chrs: number; crc_objectives: number }
  | { t: "stall"; seat: number }
  | { t: "desync"; tick: number }
  | { t: "bye" };

type SignalSink = {
  sendSdp(to: number, desc: RTCSessionDescriptionInit): void;
  sendIce(to: number, cand: RTCIceCandidateInit): void;
};

export class PeerMesh {
  private pcs = new Map<number, RTCPeerConnection>();
  private inp = new Map<number, RTCDataChannel>();
  private ctl = new Map<number, RTCDataChannel>();
  rttMs = new Map<number, number>();
  onInp: ((from: number, data: Uint8Array) => void) | null = null;
  onCtlMsg: ((from: number, msg: CtlMsg) => void) | null = null;
  onInpOpen: (() => void) | null = null;
  onPeerLost: ((seat: number, state: string) => void) | null = null;
  private pingTimer = 0;

  constructor(
    readonly mySeat: number,
    private readonly signal: SignalSink,
    private readonly turnForce: boolean,
    private readonly onLog: (s: string) => void,
  ) {}

  private pc(remote: number): RTCPeerConnection {
    let pc = this.pcs.get(remote);
    if (pc)
      return pc;
    pc = new RTCPeerConnection(rtcConfiguration({ turnForce: this.turnForce }));
    this.pcs.set(remote, pc);
    pc.onicecandidate = (ev) => {
      if (ev.candidate && validateIce({ candidate: ev.candidate.candidate }))
        this.signal.sendIce(remote, ev.candidate.toJSON());
    };
    pc.onconnectionstatechange = () => {
      const st = pc!.connectionState;
      this.onLog(`P${remote} ${st}`);
      if (st === "failed" || st === "closed")
        this.onPeerLost?.(remote, st);
    };
    pc.ondatachannel = (ev) => this.bindChan(remote, ev.channel);
    return pc;
  }

  private bindChan(remote: number, ch: RTCDataChannel): void {
    ch.binaryType = "arraybuffer";
    if (ch.label === "inp") {
      this.inp.set(remote, ch);
      ch.onmessage = (ev) => {
        const raw = ev.data;
        const buf =
          raw instanceof ArrayBuffer
            ? new Uint8Array(raw)
            : raw instanceof Uint8Array
              ? raw
              : null;
        if (buf)
          this.onInp?.(remote, buf);
      };
    }
    if (ch.label === "ctl") {
      this.ctl.set(remote, ch);
      ch.onmessage = (ev) => this.onCtl(remote, ev.data);
    }
    ch.onopen = () => {
      this.onLog(`${ch.label} open → P${remote}`);
      if (ch.label === "inp")
        this.onInpOpen?.();
    };
  }

  async offerTo(remote: number): Promise<void> {
    const pc = this.pc(remote);
    const inp = pc.createDataChannel("inp", { ordered: false, maxRetransmits: 0 });
    const ctl = pc.createDataChannel("ctl", { ordered: true });
    this.bindChan(remote, inp);
    this.bindChan(remote, ctl);
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    if (offer.type && offer.sdp && validateSdp({ type: offer.type, sdp: offer.sdp }))
      this.signal.sendSdp(remote, offer);
  }

  async handleSdp(from: number, desc: RTCSessionDescriptionInit): Promise<void> {
    if (!desc.type || !desc.sdp || !validateSdp({ type: desc.type, sdp: desc.sdp }))
      return;
    const pc = this.pc(from);
    await pc.setRemoteDescription(desc);
    if (desc.type === "offer") {
      const answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      if (answer.type && answer.sdp && validateSdp({ type: answer.type, sdp: answer.sdp }))
        this.signal.sendSdp(from, answer);
    }
  }

  async handleIce(from: number, cand: RTCIceCandidateInit): Promise<void> {
    if (!cand.candidate || !validateIce({ candidate: cand.candidate }))
      return;
    const pc = this.pcs.get(from);
    if (pc)
      await pc.addIceCandidate(cand);
  }

  sendInp(buf: Uint8Array): void {
    for (const ch of this.inp.values()) {
      if (ch.readyState === "open") {
        const copy = new Uint8Array(buf.byteLength);
        copy.set(buf);
        ch.send(copy);
      }
    }
  }

  sendCtl(msg: CtlMsg, to?: number): void {
    const s = JSON.stringify(msg);
    const send = (ch: RTCDataChannel) => {
      if (ch.readyState === "open")
        ch.send(s);
    };
    if (to != null) {
      const ch = this.ctl.get(to);
      if (ch)
        send(ch);
      return;
    }
    for (const ch of this.ctl.values())
      send(ch);
  }

  sendNack(fromTick: number, toTick: number): void {
    this.sendCtl({ t: "nack", fromTick, toTick });
  }

  startPing(): void {
    this.stopPing();
    this.pingTimer = window.setInterval(() => this.sendCtl({ t: "ping", t0: performance.now() }), 1000);
  }

  stopPing(): void {
    if (this.pingTimer)
      window.clearInterval(this.pingTimer);
    this.pingTimer = 0;
  }

  private onCtl(from: number, data: unknown): void {
    let msg: CtlMsg | null = null;
    try {
      msg = JSON.parse(String(data)) as CtlMsg;
    } catch {
      return;
    }
    if (msg.t === "ping")
      this.sendCtl({ t: "pong", t0: msg.t0 }, from);
    else if (msg.t === "pong")
      this.rttMs.set(from, performance.now() - msg.t0);
    else
      this.onCtlMsg?.(from, msg);
  }

  remoteSeats(): number[] {
    return [...this.pcs.keys()].sort((a, b) => a - b);
  }

  openDataChannelCount(): number {
    let n = 0;
    for (const ch of this.inp.values()) {
      if (ch.readyState === "open")
        n += 1;
    }
    for (const ch of this.ctl.values()) {
      if (ch.readyState === "open")
        n += 1;
    }
    return n;
  }

  expectedDataChannelCount(): number {
    return this.pcs.size * 2;
  }

  inpOpen(): boolean {
    for (const ch of this.inp.values()) {
      if (ch.readyState === "open")
        return true;
    }
    return false;
  }

  rttLine(): string {
    const parts: string[] = [];
    for (const [seat, ms] of this.rttMs)
      parts.push(`P${seat} ${ms.toFixed(0)}ms`);
    return parts.length ? `RTT ${parts.join(" ")}` : "RTT …";
  }

  close(): void {
    this.stopPing();
    for (const pc of this.pcs.values())
      pc.close();
    this.pcs.clear();
  }
}

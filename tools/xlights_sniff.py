import argparse
import sys
from collections import Counter

from scapy.all import IP, UDP, Raw, sniff


def describe_payload(payload: bytes) -> str:
    if payload.startswith(b"Art-Net\x00") and len(payload) >= 10:
        opcode = payload[8] | (payload[9] << 8)
        return f"ArtNet opcode=0x{opcode:04X}"

    if len(payload) >= 126 and payload[4:12] == b"ASC-E1.17":
        vector = int.from_bytes(payload[18:22], "big")
        return f"E131 root_vector=0x{vector:08X}"

    if len(payload) >= 4:
        head = payload[:8].hex(" ")
        return f"raw[{len(payload)}]={head}"

    return f"raw[{len(payload)}]"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iface", required=True)
    parser.add_argument("--host")
    parser.add_argument("--bpf")
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--limit", type=int, default=40)
    args = parser.parse_args()

    if args.bpf:
        bpf = args.bpf
    elif args.host:
        bpf = f"host {args.host} and udp"
    else:
        parser.error("use --host or --bpf")

    print(f"sniff iface={args.iface} filter={bpf} seconds={args.seconds}", flush=True)

    packets = sniff(iface=args.iface, filter=bpf, timeout=args.seconds, store=True)

    print(f"captured={len(packets)}", flush=True)

    counts: Counter[tuple[str, int, str, int]] = Counter()
    shown = 0
    for pkt in packets:
        if IP not in pkt or UDP not in pkt:
            continue

        ip = pkt[IP]
        udp = pkt[UDP]
        payload = bytes(pkt[Raw].load) if Raw in pkt else b""
        counts[(ip.src, int(udp.sport), ip.dst, int(udp.dport))] += 1

        if shown < args.limit:
            print(
                f"{ip.src}:{int(udp.sport)} -> {ip.dst}:{int(udp.dport)} "
                f"len={len(payload)} {describe_payload(payload)}",
                flush=True,
            )
            shown += 1

    if counts:
        print("flows:", flush=True)
        for (src, sport, dst, dport), count in counts.most_common():
            print(f"  {src}:{sport} -> {dst}:{dport} count={count}", flush=True)
    else:
        print("flows: none", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

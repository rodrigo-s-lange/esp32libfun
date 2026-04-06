#!/usr/bin/env python3
from __future__ import annotations

import argparse
import signal
import socket
import sys
import time


ARTNET_HEADER = b"Art-Net\x00"
ARTDMX_OPCODE_LE = b"\x00\x50"
ARTNET_PROTOCOL = b"\x00\x0e"
DMX_MAX = 512


def build_packet(universe: int, sequence: int, payload: bytes, physical: int = 0) -> bytes:
    length = len(payload)
    if length == 0 or length > DMX_MAX:
        raise ValueError("payload must be between 1 and 512 bytes")

    packet = bytearray(18 + length)
    packet[0:8] = ARTNET_HEADER
    packet[8:10] = ARTDMX_OPCODE_LE
    packet[10:12] = ARTNET_PROTOCOL
    packet[12] = sequence & 0xFF
    packet[13] = physical & 0xFF
    packet[14] = universe & 0xFF
    packet[15] = (universe >> 8) & 0xFF
    packet[16] = (length >> 8) & 0xFF
    packet[17] = length & 0xFF
    packet[18:] = payload
    return bytes(packet)


def make_payload(length: int, universe: int, frame: int, mode: str) -> bytes:
    if mode == "zero":
        return bytes(length)

    if mode == "solid":
        value = (universe + frame) & 0xFF
        return bytes([value]) * length

    data = bytearray(length)
    for i in range(length):
        data[i] = (universe * 17 + frame + i) & 0xFF
    return bytes(data)


def run(args: argparse.Namespace) -> int:
    stop = False

    def on_signal(_signum, _frame):
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    target = (args.ip, args.port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)

    payload_len = min(max(args.payload, 1), DMX_MAX)
    frame_period = 1.0 / args.fps if args.fps > 0 else 0.0
    sequence = args.sequence_start & 0xFF
    frame = 0
    total_packets = 0
    total_payload = 0
    start = time.perf_counter()
    next_frame = start
    last_report = start
    report_packets = 0
    report_payload = 0

    print(
        f"Art-Net stress -> {args.ip}:{args.port} universes={args.universes} fps={args.fps} "
        f"payload={payload_len} mode={args.mode} broadcast={args.broadcast}",
        flush=True,
    )

    if args.broadcast:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    while not stop:
        frame_start = time.perf_counter()

        for universe in range(args.universes):
            payload = make_payload(payload_len, universe + args.universe_base, frame, args.mode)
            packet = build_packet(universe + args.universe_base, sequence, payload, args.physical)
            sock.sendto(packet, target)
            total_packets += 1
            total_payload += payload_len
            report_packets += 1
            report_payload += payload_len

        frame += 1
        if args.sequence_mode == "increment":
            sequence = (sequence + 1) & 0xFF
            if sequence == 0:
                sequence = 1
        elif args.sequence_mode == "zero":
            sequence = 0

        now = time.perf_counter()
        if now - last_report >= 1.0:
            elapsed = now - last_report
            pps = report_packets / elapsed
            kbps = (report_payload * 8.0) / elapsed / 1000.0
            print(
                f"tx fps={frame / max(now - start, 1e-9):.2f} pps={pps:.1f} kbps={kbps:.1f} "
                f"frames={frame} packets={total_packets}",
                flush=True,
            )
            last_report = now
            report_packets = 0
            report_payload = 0

        if frame_period > 0.0:
            next_frame += frame_period
            sleep_time = next_frame - time.perf_counter()
            if sleep_time > 0:
                time.sleep(sleep_time)
            else:
                next_frame = time.perf_counter()

    total_time = max(time.perf_counter() - start, 1e-9)
    avg_pps = total_packets / total_time
    avg_kbps = (total_payload * 8.0) / total_time / 1000.0
    print(
        f"done frames={frame} packets={total_packets} avg_pps={avg_pps:.1f} avg_kbps={avg_kbps:.1f}",
        flush=True,
    )
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send sustained Art-Net ArtDMX traffic for ESP/W5500 stress tests.")
    parser.add_argument("--ip", default="192.168.0.250", help="Target IP address")
    parser.add_argument("--port", type=int, default=6454, help="Target UDP port")
    parser.add_argument("--universes", type=int, default=60, help="Universes per frame")
    parser.add_argument("--universe-base", type=int, default=0, help="Starting universe number")
    parser.add_argument("--fps", type=float, default=30.0, help="Frames per second")
    parser.add_argument("--payload", type=int, default=512, help="DMX payload bytes per universe")
    parser.add_argument("--physical", type=int, default=0, help="ArtDMX physical port field")
    parser.add_argument("--mode", choices=("gradient", "solid", "zero"), default="gradient", help="Payload pattern")
    parser.add_argument("--sequence-mode", choices=("increment", "zero"), default="increment", help="Art-Net sequence field behavior")
    parser.add_argument("--sequence-start", type=int, default=1, help="Initial Art-Net sequence value")
    parser.add_argument("--broadcast", action="store_true", help="Enable UDP broadcast mode")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.universes <= 0:
        print("universes must be > 0", file=sys.stderr)
        return 2
    if args.fps <= 0:
        print("fps must be > 0", file=sys.stderr)
        return 2
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

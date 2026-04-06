#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time


STATS_RE = re.compile(
    r"^protocol=(?P<protocol>\S+) "
    r"total=(?P<total>\d+) dmx=(?P<dmx>\d+) artnet=(?P<artnet>\d+) e131=(?P<e131>\d+) "
    r"bytes=(?P<bytes>\d+) frames=(?P<frames>\d+) avg_univ=(?P<avg_i>\d+)\.(?P<avg_f>\d+) "
    r"last_univ=(?P<last_frame_univ>\d+) first=(?P<first>\d+) last=(?P<last>\d+)$"
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run UDP DMX stress and query receiver stats over UDP.")
    parser.add_argument("--protocol", choices=("artnet", "e131"), required=True)
    parser.add_argument("--ip", required=True)
    parser.add_argument("--universes", type=int, default=32)
    parser.add_argument("--fps", type=float, default=40.0)
    parser.add_argument("--duration", type=float, default=12.0)
    parser.add_argument("--payload", type=int, default=512)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--control-port", type=int, default=9001)
    parser.add_argument("--query-timeout", type=float, default=1.5)
    parser.add_argument("--settle", type=float, default=0.25)
    return parser.parse_args(argv)


def sender_command(args: argparse.Namespace) -> list[str]:
    script = "artnet_stress.py" if args.protocol == "artnet" else "e131_stress.py"
    return [
        args.python,
        os.path.join("tools", script),
        "--ip",
        args.ip,
        "--universes",
        str(args.universes),
        "--fps",
        str(args.fps),
        "--payload",
        str(args.payload),
    ]


def udp_command(ip: str, port: int, payload: str, timeout_s: float) -> str:
    import socket

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.settimeout(timeout_s)
        sock.sendto(payload.encode("ascii"), (ip, port))
        data, _ = sock.recvfrom(512)
        return data.decode("utf-8", errors="ignore").strip()
    finally:
        sock.close()


def parse_stats(line: str, duration_s: float) -> dict[str, float]:
    match = STATS_RE.match(line)
    if not match:
        raise RuntimeError(f"unexpected stats reply: {line!r}")

    avg_univ = float(f"{match.group('avg_i')}.{match.group('avg_f')}")
    frames = float(match.group("frames"))
    payload_bytes = float(match.group("bytes"))
    dmx_packets = float(match.group("dmx"))
    seconds = max(duration_s, 1e-9)

    result = {
        "protocol_label": match.group("protocol"),
        "total": float(match.group("total")),
        "dmx": dmx_packets,
        "artnet": float(match.group("artnet")),
        "e131": float(match.group("e131")),
        "bytes": payload_bytes,
        "frames": frames,
        "avg_univ": avg_univ,
        "last_frame_univ": float(match.group("last_frame_univ")),
        "first_univ": float(match.group("first")),
        "last_univ": float(match.group("last")),
        "pps": dmx_packets / seconds,
        "kbps": (payload_bytes * 8.0) / seconds / 1000.0,
        "fps": frames / seconds,
        "px_frame": avg_univ * 170.0,
    }
    return result


def stop_process(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is not None:
        return

    proc.terminate()
    try:
        proc.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5.0)


def query_stats(ip: str, port: int, timeout_s: float, attempts: int = 5) -> str:
    last_error: Exception | None = None
    for _ in range(attempts):
        try:
            return udp_command(ip, port, "stats", timeout_s)
        except Exception as exc:  # noqa: BLE001
            last_error = exc
            time.sleep(0.15)
    raise RuntimeError(f"failed to query stats: {last_error}")


def main(argv: list[str]) -> int:
    global args
    args = parse_args(argv)

    clear_reply = udp_command(args.ip, args.control_port, "clear", args.query_timeout)
    if not clear_reply.startswith("ok clear"):
        raise RuntimeError(f"unexpected clear reply: {clear_reply!r}")

    time.sleep(args.settle)

    cmd = sender_command(args)
    proc = subprocess.Popen(
        cmd,
        cwd=os.getcwd(),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    try:
        time.sleep(args.duration)
    finally:
        stop_process(proc)

    sender_output = ""
    if proc.stdout is not None:
        sender_output = proc.stdout.read()
        if sender_output:
            sys.stdout.write(sender_output)

    stats_line = query_stats(args.ip, args.control_port, args.query_timeout)
    stats = parse_stats(stats_line, args.duration)
    print(
        f"summary protocol={args.protocol} label={stats['protocol_label']} "
        f"universes={args.universes} fps_target={args.fps:.2f} duration={args.duration:.2f} "
        f"pps={stats['pps']:.1f} kbps={stats['kbps']:.1f} fps={stats['fps']:.2f} "
        f"avg_univ={stats['avg_univ']:.2f} px_frame={stats['px_frame']:.0f} "
        f"dmx={stats['dmx']:.0f} frames={stats['frames']:.0f} "
        f"artnet={stats['artnet']:.0f} e131={stats['e131']:.0f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

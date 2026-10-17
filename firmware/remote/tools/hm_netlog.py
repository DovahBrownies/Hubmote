#!/usr/bin/env python3
"""
Records the remote's log over Wi-Fi (developer builds, TCP port 2323): the
same as scripts/hm-netlog.ps1, for any OS. Standard library only.

    python tools/hm_netlog.py --remote hubmote.local                 # live, until Ctrl+C
    python tools/hm_netlog.py --remote 192.168.1.50 --seconds 600 --out test.log

A new connection first gets the last 32 KB of the log, so recent start-up
lines are included. TCP keepalive probes notice a dead connection (the
remote restarted, went to sleep or out of range), and the script connects
again by itself. The PC connects out, so no firewall rule is needed.
"""

import argparse
import re
import socket
import sys
import time
from datetime import datetime

CONTROL = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f-\x9f]")  # all but tab and line ends

IDLE_S, INTERVAL_S, PROBES = 5, 2, 5


def keepalive(sock):
    """Probes after IDLE_S of silence, every INTERVAL_S. A restarted remote
    answers a probe for the old connection with a reset at once; one that's
    gone doesn't answer, and the connection fails after the probes."""
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    if hasattr(socket, "SIO_KEEPALIVE_VALS"):  # Windows (it sends 10 probes)
        sock.ioctl(socket.SIO_KEEPALIVE_VALS, (1, IDLE_S * 1000, INTERVAL_S * 1000))
        return
    for name, value in (("TCP_KEEPIDLE", IDLE_S), ("TCP_KEEPALIVE", IDLE_S),  # Linux / macOS
                        ("TCP_KEEPINTVL", INTERVAL_S), ("TCP_KEEPCNT", PROBES)):
        if hasattr(socket, name):
            sock.setsockopt(socket.IPPROTO_TCP, getattr(socket, name), value)


def stamp():
    return datetime.now().strftime("%Y-%m-%dT%H:%M:%S")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--remote", required=True, help="name or address (Settings > About)")
    ap.add_argument("--port", type=int, default=2323)
    ap.add_argument("--seconds", type=int, default=0, help="stop after this long (0: until Ctrl+C)")
    ap.add_argument("--out", default="hm-netlog.log")
    ap.add_argument("--quiet", action="store_true", help="file only, no console echo")
    args = ap.parse_args()

    end = time.monotonic() + args.seconds if args.seconds > 0 else float("inf")
    with open(args.out, "w", encoding="utf-8", newline="") as log:
        while time.monotonic() < end:
            try:
                sock = socket.create_connection((args.remote, args.port), timeout=3)
            except OSError:
                time.sleep(3)  # asleep, restarting or out of range: try again
                continue
            with sock:
                keepalive(sock)
                sock.settimeout(1)
                log.write(f"\n[connected to {args.remote}:{args.port} at {stamp()}]\n")
                log.flush()
                while time.monotonic() < end:
                    try:
                        data = sock.recv(4096)
                    except socket.timeout:
                        continue
                    except OSError:
                        break  # reset, or the keepalive probes went unanswered
                    if not data:
                        break
                    # Control characters out: anything posing as the remote
                    # could otherwise send terminal escape sequences.
                    text = CONTROL.sub("", data.decode("utf-8", errors="replace"))
                    log.write(text)
                    log.flush()
                    if not args.quiet:
                        sys.stdout.write(text)
                        sys.stdout.flush()
                log.write(f"\n[disconnected at {stamp()}]\n")
                log.flush()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass

#!/usr/bin/env python3
# Minimal OSC sender for testing crutchfield-machine without external deps.
#
# Usage:
#   ./osc_send.py /cma/decay 0.85
#   ./osc_send.py /cma/layer/noise 1
#   ./osc_send.py --host 127.0.0.1 --port 7700 /cma/shot
#
# Writes a single OSC 1.0 UDP datagram. Numeric args become floats; bare
# tokens become strings.

import argparse
import socket
import struct
import sys


def osc_pad(s: bytes) -> bytes:
    """Pad a bytes value to a multiple of 4."""
    pad = (-len(s)) % 4
    return s + (b"\0" * pad)


def osc_string(s: str) -> bytes:
    """Encode an OSC string: utf-8 bytes + null + 4-byte align."""
    raw = s.encode("utf-8") + b"\0"
    return osc_pad(raw)


def build_message(address: str, args) -> bytes:
    """Build a single OSC message: address + type tag + packed args."""
    tag = ","
    payload = b""
    for a in args:
        if isinstance(a, int) and not isinstance(a, bool):
            tag += "i"
            payload += struct.pack(">i", a)
        elif isinstance(a, bool):
            tag += "T" if a else "F"
        elif isinstance(a, float):
            tag += "f"
            payload += struct.pack(">f", a)
        else:
            tag += "s"
            payload += osc_string(str(a))
    return osc_string(address) + osc_string(tag) + payload


def parse_arg(tok: str):
    """Cast string tokens to int/float/bool/str by heuristic."""
    if tok in ("true", "T"): return True
    if tok in ("false", "F"): return False
    try:
        if "." in tok or "e" in tok or "E" in tok:
            return float(tok)
        return int(tok)
    except ValueError:
        try:
            return float(tok)
        except ValueError:
            return tok


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=7700)
    p.add_argument("address")
    p.add_argument("args", nargs="*")
    a = p.parse_args()

    if not a.address.startswith("/"):
        sys.exit("address must start with '/'")

    parsed = [parse_arg(t) for t in a.args]
    # By default, treat any numeric arg as float (matches what control surfaces
    # send). To force int, prefix with `i:` e.g. `i:42`.
    cleaned = []
    for tok, val in zip(a.args, parsed):
        if tok.startswith("i:"):
            cleaned.append(int(tok[2:]))
        elif isinstance(val, int):
            cleaned.append(float(val))
        else:
            cleaned.append(val)

    pkt = build_message(a.address, cleaned)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.sendto(pkt, (a.host, a.port))
    s.close()
    print(f"sent {len(pkt)} bytes to {a.host}:{a.port} → {a.address} {cleaned}")


if __name__ == "__main__":
    main()

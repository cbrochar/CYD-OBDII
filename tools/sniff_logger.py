#!/usr/bin/env python3
"""
Laptop version of the CYD's SNIFF page, for adapters that support monitor mode.

The CYD's Bluetooth ELM327 v2.1 rejects ATMA, so with the Y-cable the OBDLink EX does the
listening here while FORScan runs on the other adapter. The file is in the same format as
the CYD's SNIFnnn.TXT.

It only listens. It refuses to start unless the adapter confirms silent monitoring
(ATCSM1), so it can't acknowledge frames or disturb the bus.

    python sniff_logger.py --port COM3 --mode engine

Press Enter (or m + Enter) for MARK, q + Enter to stop.
Captures can hold the VIN: they are git-ignored, keep them out of the public repo.
"""
import argparse
import datetime
import pathlib
import re
import sys
import threading
import time

import serial

MODES = {
    # name: (label, protocol commands, filter commands)
    "engine": ("CAN ENGINE", ["ATSP6", "ATCAF0"], ["ATCF7E0", "ATCM7F0"]),
    "diag":   ("CAN DIAG",   ["ATSP6", "ATCAF0"], ["ATCF700", "ATCM700"]),
    "all":    ("CAN ALL",   ["ATSP6", "ATCAF0"], []),
    "kline":  ("K-LINE",     ["ATSP3"],           []),
}
RECENT_LIMIT = 200   # lines printed to the screen before going quiet


def cmd(ser, text, timeout=3.0):
    """Send one command, return its reply as one string (echo and prompt removed)."""
    ser.reset_input_buffer()
    ser.write((text + "\r").encode())
    t0, buf = time.time(), b""
    while time.time() - t0 < timeout and b">" not in buf:
        buf += ser.read(ser.in_waiting or 1)
    lines = [s.strip() for s in re.split(r"[\r\n]+", buf.decode(errors="replace").replace(">", "")) if s.strip()]
    if lines and lines[0].upper().replace(" ", "") == text.upper().replace(" ", ""):
        lines = lines[1:]
    return " ".join(lines)


def next_name(folder):
    for n in range(1, 1000):
        path = folder / f"SNIF{n:03d}.TXT"
        if not path.exists():
            return path
    sys.exit("SNIF001-999 exist: delete some")


def stop_monitor(ser):
    """Any character ends monitor mode. Returns what was still in the adapter."""
    ser.write(b"\r")
    t0, buf = time.time(), b""
    while time.time() - t0 < 2.0 and b">" not in buf:
        buf += ser.read(ser.in_waiting or 1)
    return buf


def main():
    ap = argparse.ArgumentParser(description="Record OBD bus traffic to a SNIFnnn.TXT file")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--mode", choices=MODES, default="engine")
    ap.add_argument("--out", default=".", help="folder for the capture")
    ap.add_argument("--seconds", type=float, help="stop after this long (default: q + Enter)")
    args = ap.parse_args()
    sys.stdout.reconfigure(errors="replace")

    label, protocol, filters = MODES[args.mode]
    folder = pathlib.Path(args.out)
    folder.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    ser.write(b"\r")
    time.sleep(0.3)
    reply = cmd(ser, "ATZ", 4.0)
    if "ELM" not in reply.upper():
        sys.exit(f"No ELM327 answer on {args.port} at {args.baud}: {reply!r}")

    setup = [("ATZ", reply)]
    for c in ["ATE0", "ATL0", "ATS1", "ATH1", "ATCSM1"] + protocol + filters:
        answer = cmd(ser, c)
        setup.append((c, answer))
        print(f"{c:8s} -> {answer}")
        if "OK" not in answer:
            ser.close()
            sys.exit(f"{c} was not accepted: not recording")

    path = next_name(folder)
    state = {"marks": 0, "stop": False}
    lock = threading.Lock()

    def keyboard():
        while True:
            try:
                key = input().strip().lower()
            except EOFError:
                return
            if key == "q":
                state["stop"] = True
                return
            with lock:
                state["marks"] += 1
                pending.append(f"MARK {state['marks']}")

    pending = []
    lines = restarts = 0
    with open(path, "w", newline="\n") as f:
        f.write(f"# CYD-OBDII bus sniffer (sniff_logger.py, {datetime.datetime.now():%Y-%m-%d %H:%M:%S})\n")
        f.write(f"# Mode: {label}\n")
        f.write("# Lines: seconds since START, then what the adapter heard (ID/header first)\n")
        for c, answer in setup:
            f.write(f"# {c} -> {answer}\n")
        f.write("# Recording\n")
        f.flush()

        threading.Thread(target=keyboard, daemon=True).start()
        print(f"Recording {label} to {path}. Enter = MARK, q + Enter = stop.")
        t0 = last_flush = time.time()
        ser.reset_input_buffer()
        ser.write(b"ATMA\r")
        line = b""

        def write(text):
            f.write(f"{time.time() - t0:.3f} {text}\n")

        try:
            while not state["stop"] and not (args.seconds and time.time() - t0 >= args.seconds):
                with lock:
                    while pending:
                        mark = pending.pop(0)
                        write(mark)
                        print(mark)
                for byte in ser.read(ser.in_waiting or 1):
                    if byte == ord(">"):
                        if line:
                            write(line.decode(errors="replace"))
                            lines += 1
                            line = b""
                        restarts += 1
                        write(f"# monitor stopped, restarted ({restarts})")
                        ser.write(b"ATMA\r")
                    elif byte in (13, 10):
                        text = line.decode(errors="replace").strip()
                        line = b""
                        if text and text != "ATMA":
                            if text == "?":
                                write("? (ATMA rejected)")
                                raise SystemExit("The adapter rejected ATMA: not recording")
                            write(text)
                            lines += 1
                            if lines <= RECENT_LIMIT:
                                print(text)
                    else:
                        line += bytes([byte])
                if time.time() - last_flush > 2:
                    last_flush = time.time()
                    f.flush()
        except KeyboardInterrupt:
            pass
        finally:
            for text in re.split(r"[\r\n>]+", stop_monitor(ser).decode(errors="replace")):
                if text.strip():
                    write(text.strip())
                    lines += 1
            cmd(ser, "ATZ", 4.0)
            ser.close()
            write(f"# Stopped. {lines} lines, {state['marks']} marks, {restarts} restarts")
    print(f"Saved {path}: {lines} lines, {state['marks']} marks, {restarts} restarts")


if __name__ == "__main__":
    main()

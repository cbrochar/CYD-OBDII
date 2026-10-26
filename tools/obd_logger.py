#!/usr/bin/env python3
"""
OBD-II / ELM327 data logger for the CYD project.

What it does
  1. Connects to a USB ELM327-style adapter on a COM port (auto-tries common baud rates).
  2. Detects the protocol and every module (ECU) that answers.
  3. Finds every standard Mode 01 PID each module supports (0100, 0120, 0140 ...).
  4. Reads every supported PID, decodes the ones it knows, and times each reply.
  5. Samples the core PIDs several times (values + response times).
  6. Repeats the core PIDs with headers/spaces OFF, which is how an Arduino sketch
     normally sees the data, and tests whether you can isolate a single module.
  7. Optionally reads Ford-specific PIDs you list in CUSTOM_PIDS below.
  8. With --kline: probes the K-line (OBD pin 7, ISO 9141 / KWP2000) for modules that
     aren't on CAN, such as the ABS and 4WD modules, and lists which addresses answer.
  9. Writes everything, plus a full raw transcript, to one .txt file.

Only read requests are sent. Add only read requests to CUSTOM_PIDS.

Usage (Windows, close PuTTY / FORScan / any other terminal first):
    pip install pyserial
    python obd_logger.py --note "key ON, engine idling, warm"

Options:
    --port COM5       skip the port prompt
    --baud 38400      skip baud auto-detect
    --samples 10      how many times to sample the core PIDs
    --kline           also probe the K-line (pin 7) for modules; adds several minutes
    --kline-addrs 28,40,60   only probe these K-line addresses (hex) instead of 01-7F
"""

import argparse
import datetime
import platform
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # checked again in main()
    serial = None
    list_ports = None

SCRIPT_VERSION = "1.1"
BAUDS = [38400, 115200, 9600, 57600, 230400, 500000]
CORE_PIDS = [0x0C, 0x0D, 0x05, 0x04, 0x0B, 0x0F, 0x10]
PROTOCOL_FALLBACK_ORDER = [1, 2, 6, 3, 4, 5, 7, 8, 9]
PROTOCOL_NAMES = {
    1: "SAE J1850 PWM", 2: "SAE J1850 VPW", 3: "ISO 9141-2", 4: "ISO 14230-4 KWP (5 baud init)",
    5: "ISO 14230-4 KWP (fast init)", 6: "ISO 15765-4 CAN 11bit 500k", 7: "ISO 15765-4 CAN 29bit 500k",
    8: "ISO 15765-4 CAN 11bit 250k", 9: "ISO 15765-4 CAN 29bit 250k", 10: "SAE J1939 CAN",
}

# ---------------------------------------------------------------------------
# Optional: Ford-specific (Mode 22) PIDs to read, e.g. the ones you found with FORScan.
# Leave empty to skip. Each entry:
#   {"name": "label for the log", "header": "", "request": "22XXXX"}
# "header" is optional. If you know the module request header (ATSH value) for that PID,
# put it here without spaces; leave "" to keep the adapter's default header.
# Only add READ requests.
# ---------------------------------------------------------------------------
CUSTOM_PIDS = [
    # {"name": "Example - replace me", "header": "", "request": "22XXXX"},
]


def u16(d):
    return d[0] * 256 + d[1]


# pid: (name, unit, number of data bytes, function(data bytes) -> value)
DECODERS = {
    0x04: ("Calculated engine load", "%", 1, lambda d: d[0] * 100 / 255),
    0x05: ("Coolant temperature", "C", 1, lambda d: d[0] - 40),
    0x06: ("Short-term fuel trim B1", "%", 1, lambda d: (d[0] - 128) * 100 / 128),
    0x07: ("Long-term fuel trim B1", "%", 1, lambda d: (d[0] - 128) * 100 / 128),
    0x08: ("Short-term fuel trim B2", "%", 1, lambda d: (d[0] - 128) * 100 / 128),
    0x09: ("Long-term fuel trim B2", "%", 1, lambda d: (d[0] - 128) * 100 / 128),
    0x0A: ("Fuel pressure (gauge)", "kPa", 1, lambda d: d[0] * 3),
    0x0B: ("Intake manifold pressure (MAP)", "kPa", 1, lambda d: d[0]),
    0x0C: ("Engine RPM", "rpm", 2, lambda d: u16(d) / 4),
    0x0D: ("Vehicle speed", "km/h", 1, lambda d: d[0]),
    0x0E: ("Timing advance", "deg", 1, lambda d: d[0] / 2 - 64),
    0x0F: ("Intake air temperature", "C", 1, lambda d: d[0] - 40),
    0x10: ("Mass air flow (MAF)", "g/s", 2, lambda d: u16(d) / 100),
    0x11: ("Throttle position", "%", 1, lambda d: d[0] * 100 / 255),
    0x1F: ("Run time since engine start", "s", 2, lambda d: u16(d)),
    0x21: ("Distance with MIL on", "km", 2, lambda d: u16(d)),
    0x2C: ("Commanded EGR", "%", 1, lambda d: d[0] * 100 / 255),
    0x2F: ("Fuel level", "%", 1, lambda d: d[0] * 100 / 255),
    0x30: ("Warm-ups since DTCs cleared", "count", 1, lambda d: d[0]),
    0x31: ("Distance since DTCs cleared", "km", 2, lambda d: u16(d)),
    0x33: ("Barometric pressure", "kPa", 1, lambda d: d[0]),
    0x42: ("Control module voltage", "V", 2, lambda d: u16(d) / 1000),
    0x43: ("Absolute load", "%", 2, lambda d: u16(d) * 100 / 255),
    0x46: ("Ambient air temperature", "C", 1, lambda d: d[0] - 40),
    0x5C: ("Engine oil temperature", "C", 1, lambda d: d[0] - 40),
    0x5D: ("Fuel injection timing", "deg", 2, lambda d: u16(d) / 128 - 210),
    0x5E: ("Engine fuel rate", "L/h", 2, lambda d: u16(d) / 20),
}

HEXRE = re.compile(r"^[0-9A-F]+$")


# ---------------------------------------------------------------------------
# Parsing helpers (no hardware needed)
# ---------------------------------------------------------------------------
def split_tokens(line):
    t = line.upper().split()
    if len(t) == 1 and len(t[0]) > 3 and len(t[0]) % 2 == 0:
        s = t[0]
        t = [s[i:i + 2] for i in range(0, len(s), 2)]
    return t


def parse_reply(lines, proto, service, pid, expected_n=None):
    """Return a list of (header, payload_bytes) for every module that answered.
    Expects headers ON (ATH1) and spaces ON (ATS1)."""
    frames = []
    for line in lines:
        t = split_tokens(line)
        if not t or not all(HEXRE.match(x) for x in t):
            continue
        try:
            if proto in (6, 8) and len(t[0]) == 3 and len(t) > 2:
                hdr = t[0]
                n = int(t[1], 16)
                rem = t[2:2 + n]
            elif proto in (7, 9) and len(t) > 5:
                hdr = " ".join(t[:4])
                n = int(t[4], 16)
                rem = t[5:5 + n]
            elif len(t) > 3:
                hdr = " ".join(t[:3])
                rem = t[3:]
            else:
                continue
            vals = [int(x, 16) for x in rem]
        except ValueError:
            continue
        payload = None
        for i in range(len(vals) - 1):
            if vals[i] == service + 0x40 and vals[i + 1] == pid:
                payload = vals[i + 2:]
                break
        if payload is None:
            continue
        if expected_n is not None:
            payload = payload[:expected_n]
        elif proto in (1, 2, 3, 4, 5) and payload:
            payload = payload[:-1]  # strip checksum byte
        frames.append((hdr, payload))
    return frames


def decode(pid, payload):
    if pid in DECODERS:
        name, unit, n, fn = DECODERS[pid]
        if len(payload) >= n:
            return name, fn(payload), unit
    return None


def hexs(payload):
    return " ".join(f"{b:02X}" for b in payload)


def fmt(v):
    return f"{v:.2f}" if isinstance(v, float) else str(v)


def proto_from_dpn(reply_lines):
    for ln in reply_lines:
        s = ln.strip().upper()
        if s.startswith("A") and len(s) > 1:
            s = s[1:]
        try:
            return int(s, 16)
        except ValueError:
            continue
    return None


# ---------------------------------------------------------------------------
# ELM327 connection
# ---------------------------------------------------------------------------
class Elm:
    def __init__(self, ser):
        self.ser = ser
        self.transcript = []

    def cmd(self, text, timeout=5.0):
        self.ser.reset_input_buffer()
        self.ser.write((text + "\r").encode())
        t0 = time.time()
        buf = b""
        while time.time() - t0 < timeout:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                buf += chunk
            if b">" in buf:
                break
        ms = (time.time() - t0) * 1000
        raw = buf.decode(errors="replace").replace(">", "")
        lines = [ln.strip() for ln in re.split(r"[\r\n]+", raw) if ln.strip()]
        if lines and lines[0].upper().replace(" ", "") == text.upper().replace(" ", ""):
            lines = lines[1:]  # drop echo
        stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.transcript.append(f"[{stamp}] > {text}")
        for ln in lines:
            self.transcript.append(f"            {ln}")
        self.transcript.append(f"            ({ms:.0f} ms)")
        return lines, ms


def open_adapter(port, baud_opt):
    for baud in ([baud_opt] if baud_opt else BAUDS):
        print(f"  trying {baud} baud ...", end=" ", flush=True)
        ser = serial.Serial(port, baud, timeout=0.1)
        ser.write(b"\r")
        time.sleep(0.3)
        ser.reset_input_buffer()
        elm = Elm(ser)
        lines, _ = elm.cmd("ATZ", timeout=4.0)
        if any("ELM" in ln.upper() for ln in lines):
            print("OK")
            return elm, baud, lines
        print("no ELM reply")
        ser.close()
    return None, None, []


def choose_port(arg_port):
    if arg_port:
        return arg_port
    ports = list(list_ports.comports())
    if not ports:
        print("No COM ports found. Plug in the adapter and try again.")
        sys.exit(1)
    print("Available ports:")
    for i, p in enumerate(ports, 1):
        print(f"  {i}) {p.device} - {p.description}")
    if len(ports) == 1:
        print(f"Using {ports[0].device}")
        return ports[0].device
    choice = input("Pick the OBD adapter (number or COM name): ").strip()
    if choice.isdigit() and 1 <= int(choice) <= len(ports):
        return ports[int(choice) - 1].device
    return choice


# ---------------------------------------------------------------------------
# Main run
# ---------------------------------------------------------------------------
def run(elm, baud, port, args, out, warn):
    def sec(title):
        out.append("")
        out.append("=" * 70)
        out.append(title)
        out.append("=" * 70)

    # ---- adapter info + init ----
    sec("ADAPTER AND INIT")
    out.append(f"Port: {port}   Baud that worked: {baud}")
    for c in ["ATI", "AT@1", "ATRV"]:
        lines, ms = elm.cmd(c)
        out.append(f"{c:8s} -> {' | '.join(lines)}")
    init_cmds = ["ATE0", "ATL0", "ATS1", "ATH1", "ATAT1", "ATSTFF", "ATSP0"]
    for c in init_cmds:
        lines, ms = elm.cmd(c)
        out.append(f"{c:8s} -> {' | '.join(lines)}")
        if lines and "OK" not in " ".join(lines).upper():
            warn.append(f"{c} did not return OK: {lines}")

    # ---- protocol / first contact ----
    print("Connecting to vehicle (key must be ON) ...")
    lines, ms = elm.cmd("0100", timeout=25.0)
    out.append(f"0100 first contact -> {' | '.join(lines)}  ({ms:.0f} ms)")
    proto = proto_from_dpn(elm.cmd("ATDPN")[0])
    if proto is None:
        warn.append("Could not read the protocol number from ATDPN; parsing assumes a 3-byte header.")
    frames = parse_reply(lines, proto, 1, 0x00, 4)
    if not frames:
        warn.append("Auto protocol got no usable answer to 0100. Trying protocols one by one.")
        for p in PROTOCOL_FALLBACK_ORDER:
            elm.cmd(f"ATSP{p}")
            lines, ms = elm.cmd("0100", timeout=15.0)
            frames = parse_reply(lines, p, 1, 0x00, 4)
            out.append(f"ATSP{p} 0100 -> {' | '.join(lines)}")
            if frames:
                proto = p
                break
    if not frames:
        warn.append("No protocol produced a valid reply. Check key ON, plug seated, adapter type.")
        return None
    dp, _ = elm.cmd("ATDP")
    dpn, _ = elm.cmd("ATDPN")
    out.append(f"ATDP  -> {' | '.join(dp)}")
    out.append(f"ATDPN -> {' | '.join(dpn)}   (parsed protocol number: {proto})")

    # ---- supported PIDs per module ----
    sec("SUPPORTED STANDARD PIDS PER MODULE (headers shown as received)")
    supported = {}
    page = 0x00
    while page <= 0xC0:
        lines, ms = elm.cmd(f"01{page:02X}", timeout=8.0)
        fr = parse_reply(lines, proto, 1, page, 4)
        if not fr:
            break
        more = False
        for hdr, d in fr:
            if len(d) < 4:
                continue
            mask = int.from_bytes(bytes(d[:4]), "big")
            out.append(f"01{page:02X} from [{hdr}] data bytes: {hexs(d)}")
            for i in range(32):
                if mask & (1 << (31 - i)):
                    supported.setdefault(hdr, set()).add(page + 1 + i)
            if mask & 1:
                more = True
        if not more:
            break
        page += 0x20
    for hdr in sorted(supported):
        pl = sorted(supported[hdr])
        out.append(f"Module [{hdr}] supports {len(pl)} PIDs: " + " ".join(f"{p:02X}" for p in pl))
    modules = sorted(supported)
    page_marks = {0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0}
    all_pids = sorted(set().union(*supported.values()) - page_marks) if supported else []

    # ---- read every supported PID once ----
    sec("DATA TABLE: EVERY SUPPORTED PID READ ONCE")
    print(f"Reading {len(all_pids)} PIDs ...")
    out.append("PID  | Name                           | Module header | Raw data bytes | Value | ms")
    for pid in all_pids:
        n = DECODERS[pid][2] if pid in DECODERS else None
        lines, ms = elm.cmd(f"01{pid:02X}", timeout=6.0)
        fr = parse_reply(lines, proto, 1, pid, n)
        name = DECODERS[pid][0] if pid in DECODERS else "(no decoder in script)"
        if not fr:
            out.append(f"{pid:02X}   | {name:30s} | NO REPLY: {' | '.join(lines)} | | {ms:.0f}")
            continue
        for hdr, d in fr:
            dec = decode(pid, d)
            val = f"{fmt(dec[1])} {dec[2]}" if dec else "raw only"
            out.append(f"{pid:02X}   | {name:30s} | {hdr} | {hexs(d)} | {val} | {ms:.0f}")

    # ---- sampling ----
    core = [p for p in CORE_PIDS if p in all_pids]
    sec(f"SAMPLING: CORE PIDS x {args.samples}")
    print(f"Sampling {len(core)} core PIDs x {args.samples} ...")
    stats = {}
    for _ in range(args.samples):
        for pid in core:
            n = DECODERS[pid][2]
            lines, ms = elm.cmd(f"01{pid:02X}", timeout=6.0)
            for hdr, d in parse_reply(lines, proto, 1, pid, n):
                dec = decode(pid, d)
                s = stats.setdefault((pid, hdr), {"vals": [], "ms": []})
                s["ms"].append(ms)
                if dec:
                    s["vals"].append(dec[1])
            time.sleep(0.05)
    out.append("PID  | Name                           | Module header | min | max | avg | avg ms | max ms")
    for (pid, hdr), s in sorted(stats.items()):
        v, m = s["vals"], s["ms"]
        if v:
            out.append(f"{pid:02X}   | {DECODERS[pid][0]:30s} | {hdr} | {fmt(min(v))} | {fmt(max(v))} | "
                       f"{fmt(sum(v) / len(v))} | {sum(m) / len(m):.0f} | {max(m):.0f}")

    # ---- Arduino-style output (headers off, spaces off) ----
    sec("ARDUINO-STYLE TEST (ATH0, ATS0): raw lines exactly as a sketch would receive them")
    elm.cmd("ATH0")
    elm.cmd("ATS0")
    for pid in core:
        lines, ms = elm.cmd(f"01{pid:02X}", timeout=6.0)
        out.append(f"01{pid:02X} -> {len(lines)} line(s): {' | '.join(lines)}  ({ms:.0f} ms)")
    elm.cmd("ATH0")
    elm.cmd("ATS1")

    # ---- single-module isolation ----
    if len(modules) > 1 and core:
        sec("MODULE ISOLATION TEST (can the adapter be told to listen to one module?)")
        test_pid = 0x0C if 0x0C in core else core[0]
        elm.cmd("ATH1")
        for hdr in modules:
            if proto in (1, 2, 3, 4, 5):
                src = hdr.split()[2]
                lines, _ = elm.cmd(f"ATRA{src}")
                reset = "ATAR"
                setcmd = f"ATRA{src}"
            elif proto in (6, 8):
                lines, _ = elm.cmd(f"ATCRA{hdr}")
                reset = "ATCRA"
                setcmd = f"ATCRA{hdr}"
            else:
                out.append(f"[{hdr}] isolation test skipped for protocol {proto}")
                continue
            r, ms = elm.cmd(f"01{test_pid:02X}", timeout=6.0)
            out.append(f"{setcmd} -> {' | '.join(lines)}  then 01{test_pid:02X} -> {' | '.join(r)}  ({ms:.0f} ms)")
            elm.cmd(reset)
        elm.cmd("ATH0")

    # ---- custom PIDs ----
    if CUSTOM_PIDS:
        sec("CUSTOM PIDS (raw, headers ON)")
        elm.cmd("ATH1")
        for item in CUSTOM_PIDS:
            if item.get("header"):
                lines, _ = elm.cmd("ATSH" + item["header"].replace(" ", ""))
                out.append(f"ATSH{item['header']} -> {' | '.join(lines)}")
            lines, ms = elm.cmd(item["request"].replace(" ", ""), timeout=8.0)
            out.append(f"{item['name']}  request {item['request']} -> {' | '.join(lines)}  ({ms:.0f} ms)")
        elm.cmd("ATZ", timeout=4.0)

    return {"proto": proto, "modules": modules, "supported": supported, "all_pids": all_pids}


# ---------------------------------------------------------------------------
# K-line probe (--kline)
# ---------------------------------------------------------------------------
# On the 2005 Excursion the PCM is on CAN, but Ford's workshop manual (section 418-00, as
# quoted on a forum) puts the ABS and 4WD control modules on ISO 9141 (OBD pin 7). A
# K-line module only answers a 5-baud "slow init" sent to its own address, so this tries
# every address and records which ones complete the handshake and their key bytes. The
# init is only a handshake; for each address that answers it then sends read-only
# requests (OBD service 03 and KWP service 18, read trouble codes). Nothing is changed.

def kline_probe(elm, args, out, warn):
    out.append("")
    out.append("=" * 70)
    out.append("K-LINE PROBE (OBD pin 7: ISO 9141-2 / ISO 14230 KWP, 5-baud init)")
    out.append("=" * 70)
    if args.kline_addrs:
        addrs = [int(a, 16) for a in args.kline_addrs.split(",") if a.strip()]
    else:
        addrs = list(range(0x01, 0x80))
    out.append("Addresses tried: " + " ".join(f"{a:02X}" for a in addrs))
    elm.cmd("ATZ", timeout=4.0)
    for c in ["ATE0", "ATL0", "ATS1", "ATH1", "ATAT1"]:
        elm.cmd(c)

    found = []
    for proto in (3, 4):
        elm.cmd(f"ATSP{proto}")
        out.append(f"\n-- Protocol {proto}: {PROTOCOL_NAMES[proto]} --")
        print(f"K-line probe, protocol {proto}: trying {len(addrs)} addresses (about 3 s each) ...")
        si_works = True
        for a in addrs:
            elm.cmd(f"ATIIA{a:02X}")          # 5-baud init address
            if si_works:
                lines, ms = elm.cmd("ATSI", timeout=8.0)   # slow init now
                text = " ".join(lines).upper()
                if text.strip() == "?":            # older / clone adapter without ATSI
                    si_works = False
                    warn.append("Adapter doesn't support ATSI; using a request to trigger each init instead.")
            if not si_works:
                lines, ms = elm.cmd("03", timeout=8.0)
                text = " ".join(lines).upper()
            ok = "ERROR" not in text and "UNABLE" not in text and text.strip() not in ("", "?", "NO DATA")
            if not ok:
                print(f"  {a:02X}: no answer", end="\r", flush=True)
                continue
            kw, _ = elm.cmd("ATKW")
            out.append(f"Address {a:02X}: ANSWERED init ({' | '.join(lines)})  key bytes: {' | '.join(kw)}  ({ms:.0f} ms)")
            print(f"  {a:02X}: ANSWERED, key bytes {' '.join(kw)}            ")
            found.append((proto, a))
            # Read-only requests: the reply (or a 7F negative reply) shows how the module talks
            for hdr in (f"C1{a:02X}F1", f"81{a:02X}F1", f"68{a:02X}F1"):
                elm.cmd(f"ATSH{hdr}")
                for req in ("03", "1800FF00"):
                    r, rms = elm.cmd(req, timeout=6.0)
                    out.append(f"    ATSH{hdr} {req} -> {' | '.join(r)}  ({rms:.0f} ms)")
        print()
        if found and any(p == proto for p, _ in found):
            break   # protocol 3 found modules: no need to repeat the scan with protocol 4

    if found:
        out.append("\nK-line modules found: " + ", ".join(f"{a:02X} (protocol {p})" for p, a in found))
    else:
        out.append("\nNo K-line module answered a 5-baud init. Either nothing is on pin 7, the modules")
        out.append("need a fast init (protocol 5), or the adapter can't do K-line (some clones).")
        warn.append("K-line probe: no module answered.")
    elm.cmd("ATZ", timeout=4.0)
    return found


def write_report(path, header_lines, summary_lines, body, warnings, transcript):
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(header_lines + summary_lines + body))
        f.write("\n\n" + "=" * 70 + "\nWARNINGS\n" + "=" * 70 + "\n")
        f.write("\n".join(warnings) if warnings else "(none)")
        f.write("\n\n" + "=" * 70 + "\nFULL RAW TRANSCRIPT\n" + "=" * 70 + "\n")
        f.write("\n".join(transcript))
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description="ELM327 OBD-II logger")
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int)
    ap.add_argument("--samples", type=int, default=10)
    ap.add_argument("--note", default="", help="e.g. 'key ON, engine idling, warm'")
    ap.add_argument("--kline", action="store_true", help="also probe the K-line (pin 7) for modules")
    ap.add_argument("--kline-addrs", help="comma-separated hex addresses to probe, default 01-7F")
    args = ap.parse_args()

    if serial is None:
        print("pyserial is not installed. Run:  pip install pyserial")
        sys.exit(1)

    port = choose_port(args.port)
    print(f"Opening {port} ...")
    try:
        elm, baud, _ = open_adapter(port, args.baud)
    except serial.SerialException as e:
        print(f"Could not open {port}: {e}\nClose PuTTY, FORScan and any serial monitor, then retry.")
        sys.exit(1)
    if not elm:
        print("No ELM327 answer at any baud rate. Check the port and that nothing else is using it.")
        sys.exit(1)

    now = datetime.datetime.now()
    path = f"obd_log_{now:%Y%m%d_%H%M%S}.txt"
    header = [
        "OBD-II LOG",
        f"Script version: {SCRIPT_VERSION}",
        f"Date: {now:%Y-%m-%d %H:%M:%S}",
        f"PC: {platform.system()} {platform.release()}  Python {platform.python_version()}",
        f"Vehicle: 2005 Ford Excursion 6.0L diesel",
        f"Vehicle state note: {args.note or '(none given)'}",
    ]
    body, warnings, result = [], [], None
    try:
        result = run(elm, baud, port, args, body, warnings)
        if args.kline:
            kline_found = kline_probe(elm, args, body, warnings)
            if result is not None:
                result["kline"] = kline_found
    except KeyboardInterrupt:
        warnings.append("Run interrupted by user (Ctrl+C). Report is partial.")
    except Exception as e:  # keep the partial log
        warnings.append(f"Script error: {type(e).__name__}: {e}")
    finally:
        try:
            elm.ser.close()
        except Exception:
            pass

    summary = ["", "=" * 70, "SUMMARY", "=" * 70]
    if result:
        p = result["proto"]
        summary.append(f"Protocol: {p} ({PROTOCOL_NAMES.get(p, 'unknown')})")
        summary.append(f"Modules that answered: {len(result['modules'])}  -> {', '.join(result['modules'])}")
        summary.append(f"Distinct supported PIDs (excluding page markers): {len(result['all_pids'])}")
        if "kline" in result:
            k = result["kline"]
            summary.append("K-line modules: " + (", ".join(f"{a:02X} (protocol {p})" for p, a in k) if k else "none answered"))
    else:
        summary.append("No vehicle data was collected. See warnings and transcript.")

    write_report(path, header, summary, body, warnings, elm.transcript)
    print(f"\nDone. Report written to: {path}")
    print("Use the PIDs, formulas and timings in it to configure src/obd_engine.h.")


if __name__ == "__main__":
    main()

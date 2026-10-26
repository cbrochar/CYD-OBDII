# PC tools

Scripts that run on a PC, not on the CYD.

## obd_logger.py

A Python script that connects to a **USB (COM-port) ELM327-style adapter**, surveys what the
truck supports, and writes everything to one text file. Use the log to work out
the PIDs, formulas and timing to use in the CYD firmware. This is the tool that confirmed
the truck uses CAN 11-bit 500k, with the engine module at 7E0/7E8.

It sends **read-only** requests (AT commands and Mode 01 PIDs, plus any custom reads you
add).

### What it logs

1. Adapter info (`ATI`, `AT@1`, `ATRV`) and the result of each init command
2. The protocol it detected (it tries each protocol in turn if auto-detect fails)
3. Every module that answered, and each module's supported Mode 01 PIDs
4. Every supported PID read once, with raw bytes, decoded value and response time
5. The core PIDs (RPM, speed, coolant, load, MAP, intake air temp, MAF) sampled several
   times, with min/max/average and response times
6. The same PIDs with headers and spaces off, exactly as the CYD firmware receives them
7. A test of whether the adapter can listen to only one module (`ATCRA`)
8. Optional custom PIDs (see below)
9. Warnings, and a full raw transcript of every command and reply

### Requirements

- Python 3.6 or newer
- `pip install pyserial`
- A USB ELM327 adapter that shows up as a COM port (Windows) or `/dev/ttyUSB*` (Linux).
  The script doesn't work over Bluetooth unless the adapter is paired as a serial port.

### Usage

Close FORScan, PuTTY and any serial monitor first, because only one program can use the port.
Turn the key on, ideally with the engine running and warm.

```bash
python obd_logger.py --note "engine idling, warm"
```

| Option | Default | Meaning |
|---|---|---|
| `--port COM5` | asks | Serial port; skips the port picker |
| `--baud 38400` | auto | Baud rate; otherwise it tries 38400, 115200, 9600, 57600, 230400, 500000 |
| `--samples 10` | 10 | How many times to sample the core PIDs |
| `--note "..."` | none | Free-text note saved in the log header (engine state, etc.) |
| `--kline` | off | Also probe the K-line (OBD pin 7) for modules that aren't on CAN (adds several minutes) |
| `--kline-addrs 28,40` | 01-7F | Only probe these K-line addresses (hex) |

The log is saved as `obd_log_<date>_<time>.txt` in the folder you run the command from.

### K-line probe (`--kline`)

Ford's 2005 Excursion workshop manual puts the **ABS** and **4WD control** modules on the K-line
(ISO 9141, OBD pin 7) rather than CAN. A K-line module only answers a wake-up (the ISO 9141
"5-baud init") sent to its own address, so `--kline` tries each address from `01` to `7F` and
records which ones complete the handshake, with their key bytes. For each address that answers
it then sends two read-only requests (`03` and `1800FF00`, read trouble codes) with three header
styles, to show how the module expects to be addressed.

```bash
python obd_logger.py --kline --note "key ON, engine off"
```

- It takes about 3 seconds per address, so a full scan takes 6–12 minutes. Use `--kline-addrs`
  to retry just the addresses that answered.
- The results are in the **K-LINE PROBE** section of the log and in the summary
  ("K-line modules: 28 (protocol 3), ..."). Send that section along when asking about PIDs.
- The wake-up is only a handshake and the requests only read; nothing in the modules is changed.
- Some cheap ELM327 clones can't do the K-line at all. If nothing answers, the log says so.

### Reading Ford-specific PIDs

To log Mode 22 PIDs (for example ones found with FORScan), add them to `CUSTOM_PIDS` near
the top of the script:

```python
CUSTOM_PIDS = [
    {"name": "FICM main power", "header": "7E0", "request": "2209D0"},
]
```

`header` is the module's request address (the `ATSH` value), or `""` to keep the default.
Their replies are logged raw, with headers on.

> **Only add read requests** (service `22`). Requests with services `2E`, `2F`, `31` or `10`
> can command modules or change settings.

### Note

The log header has the vehicle hard-coded as "2005 Ford Excursion 6.0L diesel". Edit that
line in `main()` if you use the script on another vehicle.

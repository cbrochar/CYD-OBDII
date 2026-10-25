# Docs

Reference captures from the truck, and photos of the display.

- [`images/`](images/): photos of the dash used in the main README (demo mode). They've been
  resized, and their metadata, including GPS location, has been removed.

- [`obd_log_20261024_132310.txt`](obd_log_20261024_132310.txt): the first
  [`tools/obd_logger.py`](../tools/) capture, taken at warm idle on 2026-10-24 through a USB
  ELM327 v1.4b. It confirmed the CAN protocol (ISO 15765-4, 11-bit, 500k), the two modules that
  answer (7E8 engine, 7E9) and the supported PIDs listed in the main [README](../README.md).
  It also showed that `ATCRA7E8` makes the adapter listen to the engine module only.
- Put new logs from the script here too (`obd_log_<date>_<time>.txt`).
- See [`../test/serialLog.txt`](../test/serialLog.txt) for a serial log from the CYD itself.

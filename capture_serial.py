"""Bounded USB boot-log capture; no flash writes."""
import argparse
from pathlib import Path
import re
import time
import serial

p = argparse.ArgumentParser()
p.add_argument("--port", default="COM3")
p.add_argument("--seconds", type=int, default=65)
p.add_argument("--reset", action="store_true")
p.add_argument("--output", default="device.log")
args = p.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.5)
# Keep DTR inactive throughout passive capture. Closing an active-DTR handle
# otherwise produces a falling edge that can reboot ESP32 USB-Serial/JTAG.
port.dtr = False
port.port = args.port
port.open()
with port:
    if args.reset:
        port.dtr = True
        time.sleep(0.2)
        port.dtr = False
    end = time.monotonic() + args.seconds
    with Path(args.output).open("w", encoding="utf-8") as out:
        while time.monotonic() < end:
            line = port.readline().decode("utf-8", errors="replace")
            line = re.sub(r"\x1b\[[0-9;]*[mK]", "", line)
            out.write(line)
            out.flush()
            if any(key in line for key in ("nina_voice", "IP Address", "XMOS firmware", "[E]", "Guru Meditation", "Rebooting")):
                print(line.rstrip(), flush=True)

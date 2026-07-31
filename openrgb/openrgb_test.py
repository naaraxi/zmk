#!/usr/bin/env python3
"""
OpenRGB test harness for the custom Keychron Ultra firmware (ZMK issue #893).

Talks the raw-HID command channel (usage page 0xFF60) and exercises our
id_openrgb (0x16) command. Proves our firmware is running (stock ignores 0x16)
and drives the per-key LEDs directly. Works on any Ultra board: the LED count
comes from the device, it is not assumed.

Needs permission to open /dev/hidraw: either run it with sudo, or install the
udev rule next to this script (61-keychron-ultra-openrgb.rules).

  sudo ./openrgb_test.py count   # GET_LED_COUNT - proof of firmware (no visible change)
  sudo ./openrgb_test.py demo    # enter direct mode, cycle R/G/B for ~30s, hand back
"""
import os, sys, glob, select, time

RID = 0x16                     # id_openrgb
SUB_GET_VERSION = 0x00
SUB_GET_LED_COUNT = 0x01
SUB_SET_DIRECT = 0x02
SUB_SET_LEDS = 0x03
EPSIZE = 32
MAX_LEDS = 1024                # sanity ceiling only; real boards are far below this
VENDOR_ID = 0x3434             # Keychron; product id differs per model

def find_cmd_hidraw():
    # Matched by vendor id plus the 0xFF60 usage page. The product id is not
    # checked: every Ultra model has its own, and the usage page already picks
    # out the right interface on whichever board is plugged in.
    found = []
    for path in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            ue = open(f"{path}/device/uevent").read().upper()
            if f"{VENDOR_ID:04X}" not in ue:
                continue
            rd = open(f"{path}/device/report_descriptor", "rb").read()
            if b"\x06\x60\xff" in rd:          # USAGE_PAGE 0xFF60 (VIA/launcher raw)
                found.append("/dev/" + os.path.basename(path))
        except OSError:
            continue
    if len(found) > 1:
        print(f"   note: several Keychron boards attached, using {found[0]} "
              f"(also saw {', '.join(found[1:])})")
    return found[0] if found else None

class Cmd:
    def __init__(self, dev):
        self.fd = os.open(dev, os.O_RDWR)
    def xfer(self, payload, timeout=1.0):
        # OpenRGB may have this same interface open, and hidraw hands every reply
        # to every reader. So keep reading until we see the reply to the command
        # we just sent: the firmware echoes our command and subcommand back in
        # bytes 0 and 1.
        buf = bytes(payload) + b"\x00" * (EPSIZE - len(payload))
        os.write(self.fd, b"\x00" + buf)        # report id 0 (unnumbered)
        want_cmd, want_sub = payload[0], payload[1]
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            r, _, _ = select.select([self.fd], [], [], left)
            if not r:
                return None
            resp = os.read(self.fd, EPSIZE)
            if len(resp) < 2:
                continue
            if resp[0] == 0xFF:                 # "unhandled", i.e. stock firmware
                return resp
            if resp[0] == want_cmd and resp[1] == want_sub:
                return resp
    def close(self):
        os.close(self.fd)

def set_all(c, r, g, b, n):
    # n is required on purpose: it must be the count the device reported, so this
    # works on every board instead of only the one it was written on.
    # SET_LEDS in runs of 9 (fits a 32-byte packet: 4 header + 27 rgb)
    i = 0
    while i < n:
        cnt = min(9, n - i)
        pkt = [RID, SUB_SET_LEDS, i, cnt]
        for _ in range(cnt):
            pkt += [r, g, b]
        c.xfer(pkt, timeout=0.3)
        i += cnt

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "count"
    dev = find_cmd_hidraw()
    if not dev:
        print("!! 0xFF60 command interface not found"); sys.exit(2)
    print(f"command interface: {dev}")
    c = Cmd(dev)
    try:
        resp = c.xfer([RID, SUB_GET_LED_COUNT])
        if resp is None:
            print("!! no response"); sys.exit(1)
        if resp[0] == 0xFF:
            print("!! device returned 'unhandled' (0xFF) — this is NOT our OpenRGB firmware")
            sys.exit(1)
        count = resp[2] | (resp[3] << 8)
        print(f"   GET_LED_COUNT -> {count}   (echo cmd=0x{resp[0]:02x} sub=0x{resp[1]:02x})")
        # Any sane count is fine; boards differ. Only an impossible one is a fault.
        # The proof of our firmware is the reply itself, not the number.
        if count < 1 or count > MAX_LEDS:
            print(f"!! implausible LED count {count} — device did not answer properly")
            sys.exit(1)
        print("   >>> OpenRGB firmware CONFIRMED running (stock would have said 0xFF).")
        if count > 256:
            print(f"   note: SET_LEDS addresses LEDs 0-255, so only the first 256 of "
                  f"{count} can be driven.")
        if mode == "demo":
            print("   entering direct mode; cycling RED/GREEN/BLUE ~30s ...")
            c.xfer([RID, SUB_SET_DIRECT, 1])
            end = 30
            colors = [(255,0,0),(0,255,0),(0,0,255),(255,255,255)]
            t = 0
            while t < end:
                for (r,g,b) in colors:
                    set_all(c, r, g, b, count)
                    time.sleep(1.2)
                    t += 1.2
                    if t >= end: break
            print("   handing control back to onboard lighting ...")
            c.xfer([RID, SUB_SET_DIRECT, 0])
        elif mode == "hold":
            secs = float(sys.argv[2]) if len(sys.argv) > 2 else 45
            print(f"   direct mode, all keys BLUE, holding {secs:.0f}s (feeding, no repaint) ...")
            c.xfer([RID, SUB_SET_DIRECT, 1])
            set_all(c, 0, 0, 255, count)
            t = 0.0
            while t < secs:
                time.sleep(1.5); t += 1.5
                c.xfer([RID, SUB_SET_DIRECT, 1])  # feed watchdog WITHOUT repainting keys
            c.xfer([RID, SUB_SET_DIRECT, 0])
            print("   handed back.")
    finally:
        c.close()

if __name__ == "__main__":
    main()

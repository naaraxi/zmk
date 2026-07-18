#!/usr/bin/env python3
"""
OpenRGB test harness for the custom V6 Ultra firmware (ZMK issue #893).

Talks the raw-HID command channel (usage page 0xFF60) and exercises our
id_openrgb (0x16) command. Proves our firmware is running (stock ignores 0x16)
and drives the per-key LEDs directly.

  sudo ./openrgb_test.py count        # GET_LED_COUNT — proof of firmware (no visible change)
  sudo ./openrgb_test.py demo         # enter direct mode, cycle R/G/B for ~30s, hand back
"""
import os, sys, glob, select, time

RID = 0x16                     # id_openrgb
SUB_GET_VERSION = 0x00
SUB_GET_LED_COUNT = 0x01
SUB_SET_DIRECT = 0x02
SUB_SET_LEDS = 0x03
EPSIZE = 32

def find_cmd_hidraw():
    for path in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            ue = open(f"{path}/device/uevent").read().upper()
            if "3434" not in ue or "0C60" not in ue:
                continue
            rd = open(f"{path}/device/report_descriptor", "rb").read()
            if b"\x06\x60\xff" in rd:          # USAGE_PAGE 0xFF60 (VIA/launcher raw)
                return "/dev/" + os.path.basename(path)
        except OSError:
            continue
    return None

class Cmd:
    def __init__(self, dev):
        self.fd = os.open(dev, os.O_RDWR)
    def xfer(self, payload, timeout=1.0):
        buf = bytes(payload) + b"\x00" * (EPSIZE - len(payload))
        os.write(self.fd, b"\x00" + buf)        # report id 0 (unnumbered)
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return None
        return os.read(self.fd, EPSIZE)
    def close(self):
        os.close(self.fd)

def set_all(c, r, g, b, n=108):
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
        if count != 108:
            print("!! unexpected LED count"); sys.exit(1)
        print("   >>> OpenRGB firmware CONFIRMED running (stock would have said 0xFF).")
        if mode == "demo":
            print("   entering direct mode; cycling RED/GREEN/BLUE ~30s ...")
            c.xfer([RID, SUB_SET_DIRECT, 1])
            end = 30
            colors = [(255,0,0),(0,255,0),(0,0,255),(255,255,255)]
            t = 0
            while t < end:
                for (r,g,b) in colors:
                    set_all(c, r, g, b)
                    time.sleep(1.2)
                    t += 1.2
                    if t >= end: break
            print("   handing control back to onboard lighting ...")
            c.xfer([RID, SUB_SET_DIRECT, 0])
        elif mode == "hold":
            secs = float(sys.argv[2]) if len(sys.argv) > 2 else 45
            print(f"   direct mode, all keys BLUE, holding {secs:.0f}s (feeding, no repaint) ...")
            c.xfer([RID, SUB_SET_DIRECT, 1])
            set_all(c, 0, 0, 255)
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

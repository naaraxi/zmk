#!/usr/bin/env python3
"""
Keychron V6 Ultra 8K — custom firmware flasher (Realtek SC-FWU over HID).

Faithful port of the Keychron Launcher's Realtek OTA routine, extracted from
the Launcher JS + the device-side app/src/dfu/tdfu.c. Talks the DFU HID
interface (usage page 0x8C, OUT report 0xB2 / IN report 0xB1) directly over
/dev/hidraw (needs root).

SAFETY: the device stages the image to the "OTA Tmp" bank and only activates on
IMAGE_SWITCH *after* VERIFY_CRC32 succeeds. This script ABORTS before
IMAGE_SWITCH if the CRC doesn't match, so a bad upload cannot activate. The
running firmware is untouched until a verified image is switched in.

Usage:
  sudo ./flash.py handshake                 # read-only: identify + query (NO write)
  sudo ./flash.py flash <image.bin>         # full flash (START->SEND->VERIFY->SWITCH)
"""
import os, sys, glob, select, time

OUT_REPORT_ID = 0xB2
IN_REPORT_ID  = 0xB1
HDR0, HDR1_ACK = 0xAA, 0x56          # SC_FWU_HEADER_ACK = 0xaa56
REPORT_LEN = 32                      # payload bytes after the report id
CHUNK = 16                           # Launcher uses 16-byte SEND_BIN chunks
OP_GET_MODEL_INFO = 0x60
OP_GET_DFU_VERSION = 0x61
OP_START = 0x63
OP_SEND_BIN = 0x64
OP_VERIFY_CRC32 = 0x65
OP_IMAGE_SWITCH = 0x66
FWU_NAME = b"KCZKV68K"

def crc32_rtk(buf, crc=0xFFFFFFFF):
    # Matches tdfu.c CRC32(): reflected, poly 0xEDB88320, init 0xFFFFFFFF, NO final xor.
    for b in buf:
        t = (crc ^ b) & 0xFF
        for _ in range(8):
            t = (t >> 1) ^ 0xEDB88320 if (t & 1) else (t >> 1)
        crc = (crc >> 8) ^ t
    return crc & 0xFFFFFFFF

def build(opcode, data=b""):
    length = len(data) + 3                       # len_of_dfu(=1+dlen) + 2
    pkt = bytearray([HDR0, HDR1_ACK, length, (~length) & 0xFF, 0, opcode])
    pkt += data
    s = (opcode + sum(data)) & 0xFFFF
    pkt += bytes([s & 0xFF, (s >> 8) & 0xFF])
    return pkt

def find_dfu_hidraw():
    """Return /dev/hidrawX whose report descriptor declares usage page 0x8C."""
    for path in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        node = os.path.basename(path)
        try:
            uevent = open(f"{path}/device/uevent").read()
            if "3434" not in uevent.upper() or "0C60" not in uevent.upper():
                continue
            rd = open(f"{path}/device/report_descriptor", "rb").read()
            if b"\x05\x8c" in rd:                # USAGE_PAGE 0x8C (DFU)
                return f"/dev/{node}"
        except OSError:
            continue
    return None

class Dfu:
    def __init__(self, dev):
        self.fd = os.open(dev, os.O_RDWR)
        self.sn = 0
    def _write(self, pkt, sn):
        pkt = bytearray(pkt); pkt[4] = sn
        report = bytes([OUT_REPORT_ID]) + bytes(pkt) + b"\x00" * (REPORT_LEN - len(pkt))
        os.write(self.fd, report)
    def _read(self, timeout=1.0):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return None
        return os.read(self.fd, REPORT_LEN + 1)   # [0xB1] + payload
    def cmd(self, opcode, data=b"", sn=0, timeout=1.0):
        self._write(build(opcode, data), sn)
        return self._read(timeout)
    def close(self):
        os.close(self.fd)

def parse_model(resp):
    # resp = [0xB1][hdr_l,hdr_h,len,len_n,sn,rsp_cmd,ack_sn,ack_cmd,ack_status][payload...]
    if not resp or resp[0] != IN_REPORT_ID:
        return None
    payload = resp[10:]                            # skip report id(1)+header(9)
    return payload

def handshake(d):
    print(">> GET_MODEL_INFO (0x60)...")
    r = d.cmd(OP_GET_MODEL_INFO, sn=1)
    p = parse_model(r)
    if not p:
        print("!! no response — wrong interface or device not in a state to answer"); return False
    model = bytes(p[:10]).split(b"\x00")[0]
    fw = bytes(p[12:22]).split(b"\x00")[0]
    print(f"   model = {model!r}   fw = {fw!r}")
    r = d.cmd(OP_GET_DFU_VERSION, sn=2)
    p = parse_model(r)
    if p:
        print(f"   dfu_version = {p[0]:#04x} enc_mode = {p[2]}")
    ok = model == FWU_NAME
    print(f"   framing/identity {'VALID (matches KCZKV68K)' if ok else 'MISMATCH'}")
    return ok

def flash(d, image):
    data = open(image, "rb").read()
    crc = crc32_rtk(data)
    print(f">> image {image}: {len(data)} bytes, crc32=0x{crc:08x}")
    print(">> START (0x63) sn=3 ..."); r = d.cmd(OP_START, b"\x00", sn=3)
    if not r or parse_model(r) is None:
        print("!! START not acked"); return False
    sn = 4
    total = (len(data) + CHUNK - 1) // CHUNK
    for i in range(0, len(data), CHUNK):
        chunk = data[i:i+CHUNK]
        r = d.cmd(OP_SEND_BIN, chunk, sn=sn)
        if not r:
            print(f"!! no ack at chunk {i//CHUNK}/{total} (sn={sn})"); return False
        sn += 1
        if sn > 255: sn = 1
        if (i // CHUNK) % 256 == 0:
            print(f"   sent {i//CHUNK}/{total} chunks", end="\r")
    print(f"   sent {total}/{total} chunks           ")
    print(">> VERIFY_CRC32 (0x65) ...")
    vdata = crc.to_bytes(4, "little") * 2          # ciphertext crc + plaintext crc (both = crc)
    r = d.cmd(OP_VERIFY_CRC32, vdata, sn=sn, timeout=3.0); sn = sn + 1 if sn < 255 else 1
    p = parse_model(r)
    if not p or p[0] != 0:
        print(f"!! CRC VERIFY FAILED (status={p[0] if p else 'none'}) — NOT switching. Running fw is untouched.")
        return False
    print("   CRC verified OK by device.")
    print(">> IMAGE_SWITCH (0x66) — device will reboot into the new image ...")
    d._write(build(OP_IMAGE_SWITCH), sn); time.sleep(0.2)
    print("   switch command sent.")
    return True

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "handshake"
    dev = find_dfu_hidraw()
    if not dev:
        print("!! DFU HID interface (usage 0x8C) not found. Keyboard connected & wired?"); sys.exit(2)
    print(f"DFU interface: {dev}")
    d = Dfu(dev)
    try:
        if mode == "handshake":
            ok = handshake(d)
            sys.exit(0 if ok else 1)
        elif mode == "flash":
            if not handshake(d):
                print("!! handshake failed — aborting before any write."); sys.exit(1)
            ok = flash(d, sys.argv[2])
            sys.exit(0 if ok else 1)
        else:
            print("usage: flash.py [handshake | flash <image.bin>]"); sys.exit(2)
    finally:
        d.close()

if __name__ == "__main__":
    main()

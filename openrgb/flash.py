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

# Every Ultra shield the release CI builds, keyed by the model string the device
# reports. Values are each shield's CONFIG_KEYCHRON_FWU_STRING_NAME.
KNOWN_MODELS = {
    b"KCZKV08K": "V0 Ultra 8K - ANSI",
    b"KCZKV18K": "V1 Ultra 8K - ANSI",
    b"KCZKV18I": "V1 Ultra 8K - ISO",
    b"KCZKV18J": "V1 Ultra 8K - JIS",
    b"KCZKV28K": "V2 Ultra 8K - ANSI",
    b"KCZKV38K": "V3 Ultra 8K - ANSI",
    b"KCZKV58K": "V5 Ultra 8K - ANSI",
    b"KCZKV68K": "V6 Ultra 8K - ANSI",
    b"KCZKVA8K": "V10 Ultra 8K - ANSI",
    b"KCZKQ18K": "Q1 Ultra 8K - ANSI",
    b"KCZKQ38K": "Q3 Ultra 8K - ANSI",
    b"KCZKQ68K": "Q6 Ultra 8K - ANSI",
    b"KCZ270U":  "Z2-70 Ultra 8K - ANSI",
}

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

def keychron_hidraw_nodes():
    """Every hidraw node belonging to the keyboard, not just the DFU one."""
    nodes = []
    for path in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            uevent = open(f"{path}/device/uevent").read().upper()
        except OSError:
            continue
        if "3434" in uevent and "0C60" in uevent:
            nodes.append("/dev/" + os.path.basename(path))
    return nodes

def processes_holding(nodes):
    """[(pid, comm, node)] for other processes holding any of these nodes open."""
    holders, me = [], os.getpid()
    for fddir in glob.glob("/proc/[0-9]*/fd"):
        try:
            pid = int(fddir.split("/")[2])
        except (IndexError, ValueError):
            continue
        if pid == me:
            continue
        try:
            fds = os.listdir(fddir)
        except OSError:
            continue                          # exited, or not ours to inspect
        for fd in fds:
            try:
                target = os.readlink(os.path.join(fddir, fd))
            except OSError:
                continue
            if target in nodes:
                try:
                    comm = open(f"/proc/{pid}/comm").read().strip()
                except OSError:
                    comm = "?"
                holders.append((pid, comm, target))
    return holders

def preflight(force=False):
    """Refuse to flash while something else is talking to the keyboard.

    A flash attempted while OpenRGB and Artemis were streaming to the raw-HID
    interface died with a bare "no ack at chunk 5665/19014": the firmware was too
    busy servicing that traffic to ack DFU packets. The same image flashed cleanly
    with nothing else attached. That failure gives no hint of its cause, so check
    up front instead of finding out 5000 chunks in.
    """
    holders = processes_holding(keychron_hidraw_nodes())
    if not holders:
        return True

    print("!! another process is talking to the keyboard:")
    for pid, comm, node in sorted(set(holders)):
        print(f"     pid {pid:>7}  {comm:<20} {node}")
    print("   Flashing while it streams starves the DFU acks, and the upload dies")
    print("   partway through with a bare 'no ack at chunk N'.")
    print("   Stop it first:  systemctl --user stop artemis openrgb")
    print("   ...or flash from a machine that is not driving this keyboard.")
    if force:
        print("   --force given, continuing anyway.")
        return True
    print("   Refusing to start. Pass --force to override.")
    return False

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
        self._drain()
    def _write(self, pkt, sn):
        pkt = bytearray(pkt); pkt[4] = sn
        report = bytes([OUT_REPORT_ID]) + bytes(pkt) + b"\x00" * (REPORT_LEN - len(pkt))
        os.write(self.fd, report)
    def _read(self, timeout=1.0):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return None
        return os.read(self.fd, REPORT_LEN + 1)   # [0xB1] + payload
    def _drain(self):
        """Discard reports left over from an earlier command or run."""
        while True:
            r, _, _ = select.select([self.fd], [], [], 0)
            if not r:
                return
            os.read(self.fd, REPORT_LEN + 1)
    def cmd(self, opcode, data=b"", sn=0, timeout=1.0):
        # GET_MODEL_INFO's ack spans two input reports, so taking the next
        # report desyncs every later read. Match our sn (byte 7) and opcode
        # (byte 8) instead.
        self._write(build(opcode, data), sn)
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            resp = self._read(left)
            if resp is None:
                return None
            if (len(resp) > 9 and resp[0] == IN_REPORT_ID
                    and resp[7] == sn and resp[8] == opcode):
                return resp
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
    if model in KNOWN_MODELS:
        print(f"   framing/identity VALID (Keychron {KNOWN_MODELS[model]})")
        return True
    print(f"   framing/identity MISMATCH ({model!r} is not a known Keychron Ultra)")
    return False

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
    force = "--force" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    mode = args[0] if args else "handshake"

    # Checked before opening the DFU node so the advice lands even if the DFU
    # interface is missing. handshake is read-only, so it is exempt.
    if mode == "flash" and not preflight(force):
        sys.exit(1)

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
            if len(args) < 2:
                print("usage: flash.py flash <image.bin> [--force]"); sys.exit(2)
            if not handshake(d):
                print("!! handshake failed — aborting before any write."); sys.exit(1)
            ok = flash(d, args[1])
            sys.exit(0 if ok else 1)
        else:
            print("usage: flash.py [handshake | flash <image.bin> [--force]]"); sys.exit(2)
    finally:
        d.close()

if __name__ == "__main__":
    main()

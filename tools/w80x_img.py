#!/usr/bin/env python3
"""
w80x_img.py - Build WinnerMicro W80x (W800/W801/W806) firmware images.

Replaces the image-packing part of the vendor `wm_tool`.

  img : raw .bin  -> .img  (64-byte header + payload), flashable over the
        stock secure bootloader with w80x_flash.py.
  fls : secboot.img + app.img -> .fls (full flash image incl. bootloader).
        The secboot header's `next_boot` is re-pointed to the app header.
  info: dump the header(s) of an .img / .fls file.

Header layout (little endian, 64 bytes), from wm_tool.c:
  u32 magic (0xA0FFFF9F)  u16 img_type  u16 zip_type
  u32 run_img_addr  u32 run_img_len  u32 img_header_addr  u32 upgrade_img_addr
  u32 run_org_checksum  u32 upd_no  char ver[16]  u32 reserved0  u32 reserved1
  u32 next_boot  u32 hd_checksum
Checksums: CRC-32 (reflected, poly 0xEDB88320), init 0xFFFFFFFF, *no* final XOR.

SPDX-License-Identifier: MIT
"""
import argparse
import struct
import sys
import zlib

MAGIC = 0xA0FFFF9F
HDR_FMT = "<IHHIIIIII16sIIII"
HDR_LEN = struct.calcsize(HDR_FMT)  # 64
FIELDS = ("magic", "img_type", "zip_type", "run_img_addr", "run_img_len",
          "img_header_addr", "upgrade_img_addr", "run_org_checksum", "upd_no",
          "ver", "reserved0", "reserved1", "next_boot", "hd_checksum")

IMG_TYPE_SECBOOT = 0
IMG_TYPE_APP = 1  # "flash layout 1" in the vendor SDK


def wm_crc32(data: bytes) -> int:
    # zlib.crc32 applies a final XOR with 0xFFFFFFFF; the W80x tools don't.
    return zlib.crc32(data) ^ 0xFFFFFFFF


def build_header(payload: bytes, *, img_type, run, header, upd, version="",
                 upd_no=0, next_boot=0) -> bytes:
    ver = version.encode()[:15].ljust(16, b"\0")
    body = struct.pack(HDR_FMT[:-1], MAGIC, img_type, 0, run, len(payload),
                       header, upd, wm_crc32(payload), upd_no, ver, 0, 0,
                       next_boot)
    return body + struct.pack("<I", wm_crc32(body))


def pad4(data: bytes) -> bytes:
    return data + b"\0" * (-len(data) % 4)


def parse_header(blob: bytes, off=0) -> dict:
    h = dict(zip(FIELDS, struct.unpack_from(HDR_FMT, blob, off)))
    if h["magic"] != MAGIC:
        raise ValueError(f"bad magic 0x{h['magic']:08X} at offset {off}")
    h["hd_ok"] = wm_crc32(blob[off:off + HDR_LEN - 4]) == h["hd_checksum"]
    payload = blob[off + HDR_LEN: off + HDR_LEN + h["run_img_len"]]
    h["payload_ok"] = (len(payload) == h["run_img_len"]
                       and wm_crc32(payload) == h["run_org_checksum"])
    return h


def cmd_img(a):
    payload = pad4(open(a.bin, "rb").read())
    hdr = build_header(payload, img_type=a.type, run=a.run, header=a.header,
                       upd=a.upd, version=a.version, next_boot=a.next_boot)
    with open(a.output, "wb") as f:
        f.write(hdr + payload)
    print(f"{a.output}: {len(payload)} bytes, header @0x{a.header:08X}, "
          f"run @0x{a.run:08X}, crc 0x{wm_crc32(payload):08X}")


def cmd_fls(a):
    sec = open(a.secboot, "rb").read()
    app = open(a.app, "rb").read()
    sh = parse_header(sec)
    ah = parse_header(app)
    if not (sh["hd_ok"] and sh["payload_ok"]):
        sys.exit("secboot image is corrupt")
    if not (ah["hd_ok"] and ah["payload_ok"]):
        sys.exit("app image is corrupt")
    sec_payload = sec[HDR_LEN:HDR_LEN + sh["run_img_len"]]
    new_sec_hdr = build_header(
        sec_payload, img_type=sh["img_type"], run=sh["run_img_addr"],
        header=sh["img_header_addr"], upd=ah["upgrade_img_addr"],
        version=sh["ver"].rstrip(b"\0").decode(errors="ignore"),
        upd_no=sh["upd_no"], next_boot=ah["img_header_addr"])
    with open(a.output, "wb") as f:
        f.write(new_sec_hdr + sec_payload + app)
    print(f"{a.output}: secboot -> app header @0x{ah['img_header_addr']:08X}")


def cmd_info(a):
    blob = open(a.file, "rb").read()
    off = 0
    while off + HDR_LEN <= len(blob):
        try:
            h = parse_header(blob, off)
        except ValueError:
            break
        print(f"-- header at file offset {off}")
        for k in FIELDS + ("hd_ok", "payload_ok"):
            v = h[k]
            if isinstance(v, int) and not isinstance(v, bool):
                v = f"0x{v:08X}"
            elif isinstance(v, bytes):
                v = v.rstrip(b"\0").decode(errors="replace")
            print(f"   {k:18s} {v}")
        off += HDR_LEN + h["run_img_len"]


def main():
    num = lambda s: int(s, 0)
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("img", help="pack a raw binary into an .img")
    s.add_argument("bin")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--header", type=num, default=0x080D0000, help="image header address")
    s.add_argument("--run", type=num, default=None, help="run address (default header+0x400)")
    s.add_argument("--upd", type=num, default=0x08010000, help="OTA update area address")
    s.add_argument("--type", type=num, default=IMG_TYPE_APP)
    s.add_argument("--next-boot", type=num, default=0)
    s.add_argument("--version", default="1.0.0")
    s.set_defaults(func=cmd_img)

    s = sub.add_parser("fls", help="combine secboot.img + app.img into a .fls")
    s.add_argument("secboot")
    s.add_argument("app")
    s.add_argument("-o", "--output", required=True)
    s.set_defaults(func=cmd_fls)

    s = sub.add_parser("info", help="print headers of an .img/.fls")
    s.add_argument("file")
    s.set_defaults(func=cmd_info)

    a = p.parse_args()
    if a.cmd == "img" and a.run is None:
        a.run = a.header + 0x400
    a.func(a)


if __name__ == "__main__":
    main()

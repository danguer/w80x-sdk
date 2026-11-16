#!/usr/bin/env python3
"""
w80x_flash.py - Flash WinnerMicro W80x (W800/W801/W806) over UART0.

Protocol (as implemented by the vendor `wm_tool`):
  1. reset the chip (RTS/DTR pulse, or press RESET by hand),
  2. spam ESC (0x1B) on UART0 @115200 until the bootloader prints 'C'/'P' x3,
  3. optionally switch baud rate with a bootloader command,
  4. send the .img / .fls file with XMODEM-1K (CRC16),
  5. reset again to run the new firmware.

Wiring: UART0 is PB19 (TX) / PB20 (RX). Most dev boards route it to the
on-board USB-serial chip with RTS -> RESET.

Requires pyserial:  pip install pyserial

SPDX-License-Identifier: MIT
"""
import argparse
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

ESC, STX, EOT, ACK, NAK = 0x1B, 0x02, 0x04, 0x06, 0x15

# Bootloader commands, copied from wm_tool.c / wm_iot_sdk tools/wm/flash.py
# (framing: 0x21, len16, crc16, cmd...).
CMD_REBOOT = bytes([0x21, 0x06, 0x00, 0xC7, 0x7C, 0x3F, 0x00, 0x00, 0x00])
CMD_GET_MAC = bytes([0x21, 0x06, 0x00, 0xEA, 0x2D, 0x38, 0x00, 0x00, 0x00])
CMD_ERASE_ALL = bytes([0x21, 0x0A, 0x00, 0xC3, 0x35, 0x32, 0x00, 0x00, 0x00,
                       0x02, 0x00, 0xFE, 0x01])
CMD_BAUD = {
    115200:  bytes([0x21, 0x0A, 0x00, 0x97, 0x4B, 0x31, 0x00, 0x00, 0x00, 0x00, 0xC2, 0x01, 0x00]),
    460800:  bytes([0x21, 0x0A, 0x00, 0x07, 0x00, 0x31, 0x00, 0x00, 0x00, 0x00, 0x08, 0x07, 0x00]),
    921600:  bytes([0x21, 0x0A, 0x00, 0x5D, 0x50, 0x31, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00]),
    1000000: bytes([0x21, 0x0A, 0x00, 0x5E, 0x3D, 0x31, 0x00, 0x00, 0x00, 0x40, 0x42, 0x0F, 0x00]),
    2000000: bytes([0x21, 0x0A, 0x00, 0xEF, 0x2A, 0x31, 0x00, 0x00, 0x00, 0x80, 0x84, 0x1E, 0x00]),
}


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def reset_chip(ser):
    # Same sequence as wm_tool "-rs rts".
    ser.dtr, ser.rts = False, True
    time.sleep(0.05)
    ser.dtr, ser.rts = True, False
    time.sleep(0.05)
    ser.dtr = False


def sync(ser, timeout=60.0):
    """Hold the bootloader in download mode; wait for 3 consecutive 'C'/'P'."""
    print("waiting for bootloader (press RESET if nothing happens)...", flush=True)
    for _ in range(50):                    # ~500 ms of ESC right after reset
        ser.write(bytes([ESC]))
        time.sleep(0.01)
    ser.timeout = 0.03
    count, t0 = 0, time.time()
    while count < 3:
        c = ser.read(1)
        if c:
            count = count + 1 if c in (b"C", b"P") else 0
        else:
            for _ in range(3):
                ser.write(bytes([ESC]))
                time.sleep(0.01)
        if time.time() - t0 > timeout:
            sys.exit("sync timeout")
    print("bootloader synced")


def query_mac(ser):
    ser.reset_input_buffer()
    ser.write(CMD_GET_MAC)
    ser.timeout = 1.0
    line = ser.read_until(b"\n", 32).decode(errors="replace").strip()
    print(f"chip says: {line or '(no answer)'}")
    # ROM answers "Mac:", secboot answers "MAC:". ROM can only take .fls files.
    return line


def wait_c(ser, need=3, timeout=30.0):
    ser.timeout = 0.5
    count, t0 = 0, time.time()
    while count < need:
        c = ser.read(1)
        if c:
            count = count + 1 if c in (b"C", b"P") else 0
        if time.time() - t0 > timeout:
            sys.exit("timeout waiting for bootloader")


def xmodem_send(ser, data: bytes):
    ser.timeout = 5.0
    ser.reset_input_buffer()
    total = (len(data) + 1023) // 1024
    for n in range(total):
        block = data[n * 1024:(n + 1) * 1024].ljust(1024, b"\0")
        seq = (n + 1) & 0xFF
        crc = crc16_xmodem(block)
        frame = bytes([STX, seq, 0xFF - seq]) + block + bytes([crc >> 8, crc & 0xFF])
        for _attempt in range(100):
            ser.write(frame)
            r = ser.read(1)
            if r and r[0] == ACK:
                break
            if r and r[0] not in (NAK,):
                sys.exit(f"\nunexpected reply 0x{r[0]:02X}, download aborted")
        else:
            sys.exit("\ntoo many retries")
        pct = (n + 1) * 100 // total
        print(f"\r  {pct:3d}% [{'#' * (pct // 4):25s}]", end="", flush=True)
    for _ in range(10):
        ser.write(bytes([EOT]))
        r = ser.read(1)
        if r and r[0] == ACK:
            print("\ndownload complete")
            return
    sys.exit("\nno ACK for EOT")


def monitor(ser, baud):
    ser.baudrate = baud
    ser.timeout = 0.1
    print(f"--- monitor @ {baud} baud, Ctrl-C to quit ---")
    try:
        while True:
            d = ser.read(256)
            if d:
                sys.stdout.write(d.decode(errors="replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        print()


def main():
    p = argparse.ArgumentParser(description="Flash W80x firmware over UART0")
    p.add_argument("image", nargs="?", help=".img (app only) or .fls (with secboot)")
    p.add_argument("-p", "--port", default="/dev/ttyUSB0")
    p.add_argument("-b", "--baud", type=int, default=2000000, choices=sorted(CMD_BAUD),
                   help="download baud rate")
    p.add_argument("--no-reset", action="store_true", help="don't toggle RTS/DTR; reset by hand")
    p.add_argument("--erase", action="store_true", help="erase whole flash first (then use a .fls!)")
    p.add_argument("--monitor", action="store_true", help="open a serial monitor after flashing")
    p.add_argument("--monitor-only", action="store_true", help="just reset and open the monitor")
    p.add_argument("--monitor-baud", type=int, default=115200)
    a = p.parse_args()

    ser = serial.Serial(a.port, 115200, timeout=0.1)
    time.sleep(0.2)

    if a.monitor_only:
        if not a.no_reset:
            reset_chip(ser)
        monitor(ser, a.monitor_baud)
        return

    if not a.image:
        p.error("image is required")
    data = open(a.image, "rb").read()
    is_fls = a.image.endswith(".fls")

    if not a.no_reset:
        reset_chip(ser)
    sync(ser)
    who = query_mac(ser)
    if who.startswith("Mac:") and not is_fls:
        sys.exit("chip is in ROM mode (no secboot): flash a .fls image instead (make fls)")

    if a.erase:
        print("erasing flash...")
        ser.write(CMD_ERASE_ALL)
        wait_c(ser)

    if a.baud != 115200:
        ser.write(CMD_BAUD[a.baud])
        time.sleep(1.0)
        ser.baudrate = a.baud
        ser.reset_input_buffer()
        wait_c(ser, need=1)

    print(f"sending {os.path.basename(a.image)} ({len(data)} bytes) @ {a.baud} baud")
    xmodem_send(ser, data)

    if a.no_reset:
        print("press RESET to start the new firmware")
    else:
        # The reboot command works even when RTS isn't wired to RESET.
        # The RTS pulse afterwards is a fallback.
        ser.write(CMD_REBOOT)
        ser.flush()
        time.sleep(0.1)
        reset_chip(ser)
        print("chip reset")

    if a.baud != 115200:
        ser.baudrate = 115200

    if a.monitor:
        monitor(ser, a.monitor_baud)


if __name__ == "__main__":
    main()

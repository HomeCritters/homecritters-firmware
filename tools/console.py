#!/usr/bin/env python3
"""Send one or more commands to the Ball V2 serial debug console.

    ~/.platformio/penv/bin/python tools/console.py games
    ~/.platformio/penv/bin/python tools/console.py "stats:80,20,50,10" pet
    ~/.platformio/penv/bin/python tools/console.py help

Opens the port without toggling DTR/RTS, so the board keeps its state.
See src/main.cpp (dispatchSerialCmd) for the command list.
"""
import glob
import sys
import time

import serial


def find_port() -> str:
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyUSB*") +
                   glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("No serial port found.")
    return ports[0]


def main() -> int:
    cmds = sys.argv[1:]
    if not cmds:
        print("usage: console.py <cmd> [cmd ...]")
        return 1
    ser = serial.Serial()
    ser.port = find_port()
    ser.baudrate = 115200
    ser.dtr = False
    ser.rts = False
    ser.timeout = 0.3
    ser.open()
    time.sleep(0.2)
    for c in cmds:
        ser.write((c + "\n").encode())
        ser.flush()
        time.sleep(0.15)
    time.sleep(0.3)
    out = ser.read(4096).decode(errors="replace").strip()
    if out:
        print(out)
    ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

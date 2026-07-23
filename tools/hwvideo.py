#!/usr/bin/env python3
"""Record the Ball V2 screen as an animated GIF (or PNG frames) over WiFi.

The serial screenshot is ~10s/frame at 115200 baud - fine for stills, useless
for motion. This polls the firmware's /shot.bmp HTTP endpoint instead (fast
over the LAN) and assembles the frames into a looping GIF, so animations
(swaying flames, the flying sleigh, blinking lights, green cauldron smoke,
the voice ring, weather FX...) can actually be reviewed.

Auth mirrors the portal's 'camera': a bare GET 401s with a one-shot nonce,
then you retry signed with ?sig=HMAC-SHA256(token, "shot:<nonce>").

    PY=~/.platformio/penv/bin/python
    $PY tools/hwvideo.py --host critter.local --token <tok> --secs 4 -o clip.gif
    $PY tools/hwvideo.py --token <tok> --fps 6 --secs 3 --cmd fest:natal

--cmd runs a serial console command first (e.g. force a festive theme), then
records. Token: serial `token`, or Seguranca > Senha on the device.
"""
import argparse
import glob
import hashlib
import hmac
import io
import sys
import time
import urllib.request

from PIL import Image, ImageDraw


def find_serial():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyUSB*") +
                   glob.glob("/dev/ttyACM*"))
    return ports[0] if ports else None


def serial_cmd(cmd):
    """Fire a console command (best-effort) without resetting the board."""
    import serial
    port = find_serial()
    if not port:
        print("  (no serial port; skipping --cmd)")
        return
    s = serial.Serial()
    s.port, s.baudrate, s.dtr, s.rts, s.timeout = port, 115200, False, False, 0.3
    s.open()
    time.sleep(0.2)
    s.write((cmd + "\n").encode())
    s.flush()
    time.sleep(0.3)
    s.close()


def grab_frame(host, token, timeout=4.0):
    """One screenshot: GET -> nonce -> signed GET -> BMP bytes -> Image."""
    base = f"http://{host}/shot.bmp"
    # The bare GET answers 401 with `nonce:<hex>` in the body (by design);
    # urllib raises on 401, so read the nonce off the exception.
    try:
        with urllib.request.urlopen(base + f"?t={time.time()}", timeout=timeout) as r:
            body = r.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
    nonce = body.replace("nonce:", "").strip()
    sig = hmac.new(token.encode(), f"shot:{nonce}".encode(), hashlib.sha256).hexdigest()
    with urllib.request.urlopen(base + f"?sig={sig}&t={time.time()}", timeout=timeout) as r:
        return Image.open(io.BytesIO(r.read())).convert("RGB")


def add_bezel(img):
    d = ImageDraw.Draw(img)
    w, h = img.size
    d.ellipse([0, 0, w - 1, h - 1], outline=(255, 60, 60), width=2)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="critter.local")
    ap.add_argument("--token", required=True, help="device token (serial `token`)")
    ap.add_argument("--secs", type=float, default=4.0, help="clip length")
    ap.add_argument("--fps", type=float, default=6.0, help="target capture rate")
    ap.add_argument("-o", "--out", default="hwvideo.gif")
    ap.add_argument("--cmd", default=None, help="serial console cmd to run first")
    ap.add_argument("--no-circle", action="store_true")
    ap.add_argument("--frames-dir", default=None, help="also dump PNG frames here")
    args = ap.parse_args()

    if args.cmd:
        print(f"cmd: {args.cmd}")
        serial_cmd(args.cmd)
        time.sleep(1.0)

    period = 1.0 / args.fps
    n = max(1, int(args.secs * args.fps))
    frames, times = [], []
    print(f"recording {n} frames from {args.host} ...")
    t0 = time.time()
    for i in range(n):
        due = t0 + i * period
        now = time.time()
        if now < due:
            time.sleep(due - now)
        try:
            img = grab_frame(args.host, args.token)
        except Exception as e:
            print(f"  frame {i}: {e}")
            continue
        if not args.no_circle:
            add_bezel(img)
        frames.append(img)
        times.append(time.time())
        sys.stdout.write(f"\r  {len(frames)}/{n}")
        sys.stdout.flush()
    print()
    if not frames:
        sys.exit("no frames captured")

    # Real inter-frame delay (capture isn't perfectly paced) -> honest playback.
    if len(times) > 1:
        deltas = [max(40, int((times[k + 1] - times[k]) * 1000))
                  for k in range(len(times) - 1)]
        deltas.append(deltas[-1])
    else:
        deltas = [int(period * 1000)]
    avg = sum(deltas) / len(deltas)
    print(f"captured {len(frames)} frames, ~{1000 / avg:.1f} fps effective")

    if args.frames_dir:
        import os
        os.makedirs(args.frames_dir, exist_ok=True)
        for i, f in enumerate(frames):
            f.save(os.path.join(args.frames_dir, f"frame_{i:03d}.png"))

    frames[0].save(args.out, save_all=True, append_images=frames[1:],
                   duration=deltas, loop=0, disposal=2, optimize=True)
    print(f"saved {args.out} ({len(frames)} frames)")


if __name__ == "__main__":
    main()

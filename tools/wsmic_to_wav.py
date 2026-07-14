#!/usr/bin/env python3
"""Capture mic audio from a HomeCritters device over WebSocket -> WAV.

Connects to ws://<host>:81, sends "mic:on", collects binary PCM frames
(16 kHz mono 16-bit) for N seconds, sends "mic:off", writes a WAV.
Usage: wsmic_to_wav.py [host] [seconds] [out.wav]
"""
import sys, socket, base64, os, struct, time, wave

host = sys.argv[1] if len(sys.argv) > 1 else "critter.local"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
out  = sys.argv[3] if len(sys.argv) > 3 else "mic.wav"

s = socket.create_connection((host, 81), timeout=8)
key = base64.b64encode(os.urandom(16)).decode()
s.sendall((f"GET / HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\n"
           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
           f"Sec-WebSocket-Version: 13\r\n\r\n").encode())
buf = b""
while b"\r\n\r\n" not in buf:
    buf += s.recv(1024)
rest = buf.split(b"\r\n\r\n", 1)[1]

def send_text(msg):
    p = msg.encode(); m = os.urandom(4)
    s.sendall(bytes([0x81, 0x80 | len(p)]) + m +
              bytes(b ^ m[i % 4] for i, b in enumerate(p)))

def frames():
    global rest
    def rd(n):
        global rest
        while len(rest) < n:
            rest += s.recv(4096)
        o = rest[:n]; rest = rest[n:]; return o
    while True:
        h = rd(2); op = h[0] & 0x0F; ln = h[1] & 0x7F
        if ln == 126: ln = int.from_bytes(rd(2), "big")
        elif ln == 127: ln = int.from_bytes(rd(8), "big")
        payload = rd(ln)
        yield op, payload

send_text("mic:on")
pcm = bytearray(); peak = 0; t0 = time.time()
for op, payload in frames():
    if op == 0x2:  # binary
        pcm += payload
        for i in range(0, len(payload) - 1, 2):
            v = struct.unpack_from("<h", payload, i)[0]
            if abs(v) > peak: peak = abs(v)
    if time.time() - t0 > secs:
        break
send_text("mic:off")
s.close()

with wave.open(out, "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    w.writeframes(bytes(pcm))
dur = len(pcm) / 2 / 16000
print(f"wrote {out}: {len(pcm)} bytes, {dur:.1f}s, peak={peak} "
      f"({100*peak/32767:.0f}% FS)")

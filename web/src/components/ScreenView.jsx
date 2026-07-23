import React, { useEffect, useRef } from 'react';

// The live mirror of the device screen, embedded in the main view (replacing
// the old CSS ferret sprite). While mounted it asks the firmware to stream
// (screen:on, resent every 3s as a keepalive) and decodes the incoming
// DELTA-RLE frames onto a persistent canvas; on unmount it stops (screen:off).
// The square canvas is masked into the round bezel.
const W = 240;
const H = 240;

const ScreenView = React.memo(function ScreenView({ send, setFrameHandler, online, size = 220, onFps }) {
  const canvasRef = useRef(null);
  const imgRef = useRef(null);
  const frameTimes = useRef([]);

  useEffect(() => {
    if (!online) return undefined;
    const ctx = canvasRef.current.getContext('2d');
    imgRef.current = ctx.createImageData(W, H);

    // Decode a delta frame: records of {rowIdx, RLE of that row's 240 px}.
    // Only the rows present are updated; the canvas persists between frames.
    const onFrame = (buf) => {
      const b = new Uint8Array(buf);
      const out = imgRef.current.data;
      let k = 0;
      while (k < b.length) {
        const row = b[k++];
        let di = row * W * 4;
        let filled = 0;
        while (filled < W && k + 2 < b.length) {
          const run = b[k];
          const v = (b[k + 1] << 8) | b[k + 2];   // RGB565, big-endian
          k += 3;
          const r5 = (v >> 11) & 0x1f;
          const g6 = (v >> 5) & 0x3f;
          const b5 = v & 0x1f;
          const R = (r5 << 3) | (r5 >> 2);
          const G = (g6 << 2) | (g6 >> 4);
          const B = (b5 << 3) | (b5 >> 2);
          for (let n = 0; n < run && filled < W; n++, filled++) {
            out[di++] = R; out[di++] = G; out[di++] = B; out[di++] = 255;
          }
        }
      }
      ctx.putImageData(imgRef.current, 0, 0);
      // rolling fps over the last second, reported at most 1x/s
      const nowT = performance.now();
      const t = frameTimes.current;
      t.push(nowT);
      while (t.length && nowT - t[0] > 1000) t.shift();
      if (onFps && (!onFps.lastAt || nowT - onFps.lastAt > 1000)) {
        onFps.lastAt = nowT;
        onFps(t.length);
      }
    };

    setFrameHandler(onFrame);
    send('screen:on');
    const ka = setInterval(() => send('screen:on'), 3000);
    return () => {
      clearInterval(ka);
      send('screen:off');
      setFrameHandler(null);
      if (onFps) onFps(0);
    };
  }, [online, send, setFrameHandler, onFps]);

  // --- touch control: the mirror IS the remote. Pointer down/move/up map to
  // device coords (0..239) and go out as "touch:<1|0>:x:y"; the firmware feeds
  // them into the same touch pipeline as the physical screen, so taps, swipes
  // and edge-pulls all work exactly like a finger on the glass. ---
  const lastMove = useRef(0);
  const toDevice = (e) => {
    const r = canvasRef.current.getBoundingClientRect();
    const x = Math.max(0, Math.min(239, Math.round((e.clientX - r.left) / r.width * W)));
    const y = Math.max(0, Math.min(239, Math.round((e.clientY - r.top) / r.height * H)));
    return { x, y };
  };
  const onDown = (e) => {
    if (!online) return;
    const { x, y } = toDevice(e);
    // Ignore presses that land outside the round bezel (corners) so a stray
    // click can't kick off an edge-swipe; drags may still travel to the edge.
    if ((x - 120) ** 2 + (y - 120) ** 2 > 118 * 118) return;
    e.currentTarget.setPointerCapture(e.pointerId);
    send(`touch:1:${x}:${y}`);
  };
  const onMove = (e) => {
    if (!online || !e.currentTarget.hasPointerCapture?.(e.pointerId)) return;
    const now = performance.now();
    if (now - lastMove.current < 33) return;  // ~30/s, like the game joystick
    lastMove.current = now;
    const { x, y } = toDevice(e);
    send(`touch:1:${x}:${y}`);
  };
  const onUp = (e) => {
    if (!online) return;
    const { x, y } = toDevice(e);
    send(`touch:0:${x}:${y}`);
  };

  return (
    <div
      style={{
        width: size,
        height: size,
        margin: '0 auto',
        borderRadius: '50%',
        overflow: 'hidden',
        background: '#000',
        boxShadow: '0 0 0 3px #2a2340',
      }}
    >
      <canvas
        ref={canvasRef}
        width={W}
        height={H}
        onPointerDown={onDown}
        onPointerMove={onMove}
        onPointerUp={onUp}
        onPointerCancel={onUp}
        style={{
          width: '100%', height: '100%', imageRendering: 'pixelated',
          touchAction: 'none', userSelect: 'none', cursor: online ? 'pointer' : 'default',
        }}
      />
    </div>
  );
});

export default ScreenView;

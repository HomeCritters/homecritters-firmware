import React, { useEffect, useRef, useState } from 'react';
import { Button, Modal, Typography } from 'antd';

const { Text } = Typography;

// Live mirror of the device screen. On open it asks the firmware to stream
// (screen:on, resent every 3s as a keepalive) and decodes the incoming RLE
// frames onto a canvas; on close it stops (screen:off). The round bezel is
// masked with a circular clip so it looks like the real display.
const W = 240;
const H = 240;

export default function LiveScreen({ open, onClose, send, setFrameHandler }) {
  const canvasRef = useRef(null);
  const imgDataRef = useRef(null);
  const [fps, setFps] = useState(0);
  const frameTimes = useRef([]);

  useEffect(() => {
    if (!open) return undefined;
    const ctx = canvasRef.current.getContext('2d');
    imgDataRef.current = ctx.createImageData(W, H);

    // Decode one RLE frame ({count, hi, lo} triples) into the ImageData.
    const onFrame = (buf) => {
      const b = new Uint8Array(buf);
      const out = imgDataRef.current.data;
      let di = 0;
      for (let k = 0; k + 2 < b.length; k += 3) {
        const run = b[k];
        const v = (b[k + 1] << 8) | b[k + 2];   // RGB565, big-endian
        const r5 = (v >> 11) & 0x1f;
        const g6 = (v >> 5) & 0x3f;
        const b5 = v & 0x1f;
        const R = (r5 << 3) | (r5 >> 2);
        const G = (g6 << 2) | (g6 >> 4);
        const B = (b5 << 3) | (b5 >> 2);
        for (let n = 0; n < run && di < out.length; n++) {
          out[di++] = R; out[di++] = G; out[di++] = B; out[di++] = 255;
        }
      }
      ctx.putImageData(imgDataRef.current, 0, 0);
      // rolling fps over the last ~1s
      const now = performance.now();
      const t = frameTimes.current;
      t.push(now);
      while (t.length && now - t[0] > 1000) t.shift();
      setFps(t.length);
    };

    setFrameHandler(onFrame);
    send('screen:on');
    const ka = setInterval(() => send('screen:on'), 3000);  // keepalive
    return () => {
      clearInterval(ka);
      send('screen:off');
      setFrameHandler(null);
    };
  }, [open, send, setFrameHandler]);

  return (
    <Modal
      title="📺 Tela ao vivo"
      open={open}
      onCancel={onClose}
      footer={[
        <Text key="f" type="secondary" style={{ fontSize: 12, marginRight: 'auto' }}>
          {fps > 0 ? `${fps} fps` : 'conectando…'}
        </Text>,
        <Button key="c" type="primary" onClick={onClose}>Fechar</Button>,
      ]}
      styles={{ footer: { display: 'flex', alignItems: 'center' } }}
    >
      <div
        style={{
          width: '100%',
          aspectRatio: '1 / 1',
          borderRadius: '50%',        // mask the square canvas into the bezel
          overflow: 'hidden',
          background: '#000',
          boxShadow: '0 0 0 3px #2a2340',
        }}
      >
        <canvas
          ref={canvasRef}
          width={W}
          height={H}
          style={{ width: '100%', height: '100%', imageRendering: 'pixelated' }}
        />
      </div>
    </Modal>
  );
}

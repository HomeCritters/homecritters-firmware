import React, { useRef } from 'react';
import { Button, Card, Col, Row, Typography } from 'antd';

const { Text } = Typography;

// Phone game controllers, one per mini-game. All memoized: they receive a
// stable `send`/`onBack` and only the score changes while playing.

// Doodle Jump controller: a drag strip that steers the ferret on the hardware.
// Dragging sends the finger's normalized x (0=left, 1=right) as "game:x:<v>";
// the firmware makes the ferret follow it. Releasing stops steering.
export const GamePad = React.memo(function GamePad({ send, score, onBack }) {
  const stripRef = useRef(null);
  const dragging = useRef(false);
  const lastSent = useRef(0);

  const steer = (clientX) => {
    const el = stripRef.current;
    if (!el) return;
    const r = el.getBoundingClientRect();
    let n = (clientX - r.left) / r.width;
    n = n < 0 ? 0 : n > 1 ? 1 : n;
    const t = performance.now();
    if (t - lastSent.current >= 33) {
      // throttle to ~30/s
      send('game:x:' + n.toFixed(3));
      lastSent.current = t;
    }
  };

  return (
    <Card size="small" style={{ marginTop: 8 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 10 }}>
        <Text strong style={{ fontSize: 16 }}>🕹️ Jump!</Text>
        <Text strong style={{ fontSize: 18 }}>{score ?? 0}</Text>
      </div>
      <div
        ref={stripRef}
        onPointerDown={(e) => {
          dragging.current = true;
          e.currentTarget.setPointerCapture(e.pointerId);
          steer(e.clientX);
        }}
        onPointerMove={(e) => dragging.current && steer(e.clientX)}
        onPointerUp={() => (dragging.current = false)}
        onPointerCancel={() => (dragging.current = false)}
        style={{
          height: 140,
          borderRadius: 14,
          background: 'linear-gradient(90deg,#2c2247,#3a2f55,#2c2247)',
          border: '1px solid #4a3d6b',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          touchAction: 'none',
          userSelect: 'none',
          cursor: 'ew-resize',
        }}
      >
        <Text type="secondary">← arraste para mover →</Text>
      </div>
      <Button block onClick={onBack} style={{ marginTop: 12, height: 46 }}>
        ← Voltar
      </Button>
    </Card>
  );
});

// Bolinha controller: swipe up on the pad to throw the ball on the hardware.
// The swipe vector is normalized by the pad size and sent as "ball:t:<nx>:<ny>".
export const BallPad = React.memo(function BallPad({ send, score, onBack }) {
  const padRef = useRef(null);
  const start = useRef(null);

  return (
    <Card size="small" style={{ marginTop: 8 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 10 }}>
        <Text strong style={{ fontSize: 16 }}>🎾 Bolinha</Text>
        <Text strong style={{ fontSize: 18 }}>{score ?? 0}</Text>
      </div>
      <div
        ref={padRef}
        onPointerDown={(e) => {
          start.current = { x: e.clientX, y: e.clientY };
          e.currentTarget.setPointerCapture(e.pointerId);
        }}
        onPointerUp={(e) => {
          const s = start.current;
          start.current = null;
          const el = padRef.current;
          if (!s || !el) return;
          const r = el.getBoundingClientRect();
          const nx = (e.clientX - s.x) / r.width;
          const ny = (e.clientY - s.y) / r.height;
          if (ny < -0.1) send(`ball:t:${nx.toFixed(3)}:${ny.toFixed(3)}`);
        }}
        onPointerCancel={() => (start.current = null)}
        style={{
          height: 180,
          borderRadius: 14,
          background: 'linear-gradient(180deg,#2c2247,#264a2e)',
          border: '1px solid #4a3d6b',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          touchAction: 'none',
          userSelect: 'none',
          cursor: 'grab',
        }}
      >
        <Text type="secondary">↑ arraste para cima para lançar ↑</Text>
      </div>
      <Button block onClick={onBack} style={{ marginTop: 12, height: 46 }}>
        ← Voltar
      </Button>
    </Card>
  );
});

// Genius controller: 4 color pads mirroring the hardware arcs. Each press
// sends "simon:<i>"; the device lights the arc + LED and plays the tone.
const SIMON_PAD_COLORS = ['#3fca5e', '#e6483a', '#efd23e', '#4a7be6'];
const SIMON_PAD_LABELS = ['Verde', 'Vermelho', 'Amarelo', 'Azul'];
export const SimonPad = React.memo(function SimonPad({ send, score, onBack }) {
  return (
    <Card size="small" style={{ marginTop: 8 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 10 }}>
        <Text strong style={{ fontSize: 16 }}>🧠 Genius</Text>
        <Text strong style={{ fontSize: 18 }}>{score ?? 0}</Text>
      </div>
      <Row gutter={[10, 10]}>
        {SIMON_PAD_COLORS.map((c, i) => (
          <Col span={12} key={i}>
            <button
              type="button"
              aria-label={SIMON_PAD_LABELS[i]}
              onPointerDown={() => send('simon:' + i)}
              style={{
                width: '100%',
                height: 96,
                borderRadius: 14,
                border: '2px solid rgba(0,0,0,0.25)',
                background: c,
                cursor: 'pointer',
                touchAction: 'manipulation',
                userSelect: 'none',
              }}
            />
          </Col>
        ))}
      </Row>
      <Button block onClick={onBack} style={{ marginTop: 12, height: 46 }}>
        ← Voltar
      </Button>
    </Card>
  );
});

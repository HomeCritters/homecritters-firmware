import React, { useEffect, useRef, useState } from 'react';
import {
  ConfigProvider,
  theme,
  Card,
  Row,
  Col,
  Progress,
  Button,
  Drawer,
  Input,
  Slider,
  Switch,
  Select,
  Segmented,
  Divider,
  Typography,
  Badge,
  Space,
  Modal,
  Spin,
} from 'antd';
import ferretSheet from './ferret-sheet.png';

const { Title, Text } = Typography;

// Timezones: POSIX TZ (applied by the firmware) + IANA names (to match
// what the browser reports).
const TZ_OPTIONS = [
  { label: 'São Paulo (UTC-3)', value: '<-03>3', iana: ['America/Sao_Paulo', 'America/Bahia', 'America/Fortaleza'] },
  { label: 'UTC', value: 'UTC0', iana: ['UTC', 'Etc/UTC'] },
  { label: 'Lisboa', value: 'WET0WEST,M3.5.0/1,M10.5.0', iana: ['Europe/Lisbon'] },
  { label: 'Londres', value: 'GMT0BST,M3.5.0/1,M10.5.0', iana: ['Europe/London'] },
  { label: 'Nova York', value: 'EST5EDT,M3.2.0,M11.1.0', iana: ['America/New_York', 'America/Toronto'] },
  { label: 'Los Angeles', value: 'PST8PDT,M3.2.0,M11.1.0', iana: ['America/Los_Angeles', 'America/Vancouver'] },
  { label: 'Berlim / Paris', value: 'CET-1CEST,M3.5.0,M10.5.0/3', iana: ['Europe/Berlin', 'Europe/Paris', 'Europe/Madrid', 'Europe/Rome'] },
  { label: 'Tóquio', value: 'JST-9', iana: ['Asia/Tokyo'] },
  { label: 'Sydney', value: 'AEST-10AEDT,M10.1.0,M4.1.0/3', iana: ['Australia/Sydney'] },
];

const IDLE_OPTIONS = [
  { label: '15 segundos', value: 15 },
  { label: '30 segundos', value: 30 },
  { label: '1 minuto', value: 60 },
  { label: '2 minutos', value: 120 },
  { label: '5 minutos', value: 300 },
];

const MENU_TIMEOUT_OPTIONS = [
  { label: 'Desativado', value: 0 },
  { label: '5 segundos', value: 5 },
  { label: '10 segundos', value: 10 },
  { label: '15 segundos', value: 15 },
  { label: '30 segundos', value: 30 },
  { label: '1 minuto', value: 60 },
];

const STATS = [
  { key: 'hunger', label: 'Fome' },
  { key: 'energy', label: 'Energia' },
  { key: 'joy', label: 'Alegria' },
  { key: 'hygiene', label: 'Higiene' },
];

const ACTIONS = [
  { id: 'feed', label: '🍎 Alimentar' },
  { id: 'pat', label: '🐾 Carinho' },
  { id: 'clean', label: '💧 Banho' },
  { id: 'sleep', label: '🌙 Dormir' },
];

// Same color rule as the hardware: red low, yellow mid, green high.
const barColor = (v) => (v < 25 ? '#e04640' : v < 55 ? '#f0c846' : '#46c85a');

// Small battery pill: an SVG icon whose fill/level mirrors the hardware.
function BatteryTag({ pct }) {
  const color = pct <= 20 ? '#e0563c' : pct <= 50 ? '#f0be40' : '#5ac86e';
  return (
    <span style={{ display: 'inline-flex', alignItems: 'center', gap: 5, opacity: 0.9 }}>
      <svg width="26" height="13" viewBox="0 0 26 13" aria-label="bateria">
        <rect x="0.5" y="0.5" width="21" height="12" rx="2.5" fill="none" stroke="#8a8a99" />
        <rect x="22.5" y="4" width="2.5" height="5" rx="1" fill="#8a8a99" />
        <rect x="2" y="2" width={Math.max(0, (17 * pct) / 100)} height="9" rx="1" fill={color} />
      </svg>
      <span style={{ fontSize: 12, color: '#b9b3c8' }}>{pct}%</span>
    </span>
  );
}

// Detects the browser timezone -> POSIX TZ string. Tries to match the IANA
// name against the list; otherwise falls back to a fixed offset (no DST).
function detectPosixTz() {
  try {
    const iana = Intl.DateTimeFormat().resolvedOptions().timeZone;
    const opt = TZ_OPTIONS.find((o) => o.iana && o.iana.includes(iana));
    if (opt) return opt.value;
  } catch (e) {
    /* fall through to the offset fallback */
  }
  const offH = new Date().getTimezoneOffset() / 60; // positive going west
  const utcH = -offH;
  const name = `${utcH >= 0 ? '+' : '-'}${String(Math.abs(utcH)).padStart(2, '0')}`;
  return `<${name}>${offH}`;
}

// Mirrors the hardware animations. Sprite sheet: 8 cols x 9 rows (32px).
const ANIM_ROW = {
  idle: 0,
  idle2: 1,
  walk: 2,
  dig: 3,
  disappear: 4,
  jump: 5,
  emerge: 6,
  sleep: 7,
};
const ANIM_DUR = {
  idle: 1.3,
  idle2: 1.45,
  walk: 0.72,
  dig: 0.72,
  disappear: 0.68,
  jump: 0.56,
  emerge: 0.68,
  sleep: 1.6,
};

// Animations that play ONCE and hold the last frame (no looping).
const ONCE = new Set(['jump', 'disappear', 'emerge']);

// One sprite-sheet clip (an animated row). Using key={seq} at the call site
// restarts the animation from frame 0 on every change (fixes jump/burrow).
function FerretSprite({ anim = 'idle', flip = false, size = 96 }) {
  const row = ANIM_ROW[anim] ?? 0;
  const scale = size / 32;
  const frame = 32 * scale;
  const once = ONCE.has(anim);
  // Loop: steps through 8 frames (overflows and wraps to 0, never holds).
  // One-shot: steps 7 frames with jump-none -> 8 aligned frames, holding
  // the real 8th frame at the end (no blank overflow frame).
  return (
    <div
      className="sprite"
      style={{
        width: size,
        height: size,
        backgroundImage: `url(${ferretSheet})`,
        backgroundSize: `${256 * scale}px ${288 * scale}px`,
        backgroundPositionY: `${-row * frame}px`,
        transform: flip ? 'scaleX(-1)' : 'none',
        animationDuration: `${ANIM_DUR[anim] ?? 1}s`,
        animationTimingFunction: once ? 'steps(8, jump-none)' : 'steps(8)',
        animationIterationCount: once ? 1 : 'infinite',
        animationFillMode: once ? 'forwards' : 'none',
        '--end': `${once ? -7 * frame : -8 * frame}px`,
      }}
    />
  );
}

// Doodle Jump controller: a drag strip that steers the ferret on the hardware.
// Dragging sends the finger's normalized x (0=left, 1=right) as "game:x:<v>";
// the firmware makes the ferret follow it. Releasing stops steering.
function GamePad({ send, score, onBack }) {
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
}

// Bolinha controller: swipe up on the pad to throw the ball on the hardware.
// The swipe vector is normalized by the pad size and sent as "ball:t:<nx>:<ny>".
function BallPad({ send, score, onBack }) {
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
}

// The "stage" where the ferret walks sideways, mirroring the hardware
// position. The transition only runs while WALKING; standing still it
// snaps (otherwise it would keep sliding during idle).
function FerretStage({ anim, flip, x, seq, size = 96 }) {
  // Responsive stage (full width, capped). The wider it is, the more
  // screen the ferret covers in the same time -> faster/wider motion.
  const t = typeof x === 'number' ? x : 0.5;
  return (
    <div style={{ width: '100%', maxWidth: 340, height: size, position: 'relative', margin: '0 auto' }}>
      <div
        style={{
          position: 'absolute',
          top: 0,
          left: `calc(${t} * (100% - ${size}px))`,
          // short transition matches the ~10x/s broadcast -> near 1:1, no jumps
          transition: anim === 'walk' ? 'left 0.12s linear' : 'none',
        }}
      >
        <FerretSprite key={seq} anim={anim} flip={flip} size={size} />
      </div>
    </div>
  );
}

export default function App() {
  const [state, setState] = useState(null);
  const [online, setOnline] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [shotSrc, setShotSrc] = useState(null);   // /shot.bmp?t=... when the modal is open
  const [shotLoading, setShotLoading] = useState(false);
  const takeShot = () => {
    setShotLoading(true);
    setShotSrc(`/shot.bmp?t=${Date.now()}`);
  };
  const [name, setName] = useState('');
  const [vol, setVol] = useState(80);
  const [ledBright, setLedBright] = useState(50);
  const wsRef = useRef(null);
  const nameDirty = useRef(false);
  const volDirty = useRef(false);
  const ledDirty = useRef(false);
  const autoTz = useRef(false);

  // WebSocket: state arrives by push (no polling).
  useEffect(() => {
    let ws;
    let retry;
    const connect = () => {
      ws = new WebSocket(`ws://${location.hostname}:81/`);
      wsRef.current = ws;
      ws.onopen = () => setOnline(true);
      ws.onclose = () => {
        setOnline(false);
        retry = setTimeout(connect, 1500);
      };
      ws.onmessage = (ev) => {
        try {
          const j = JSON.parse(ev.data);
          setState(j);
          if (!nameDirty.current) setName(j.name);
          if (!volDirty.current && typeof j.volume === 'number') setVol(j.volume);
          if (!ledDirty.current && typeof j.ledBright === 'number') setLedBright(j.ledBright);
          // Device has no timezone yet -> detect it here and enable the clock.
          if (!autoTz.current && (!j.tz || j.tz === '')) {
            autoTz.current = true;
            const tz = detectPosixTz();
            if (tz && ws.readyState === WebSocket.OPEN) {
              ws.send('tz:' + tz);
              ws.send('clock:on');
            }
          }
        } catch (e) {
          /* ignore malformed frames */
        }
      };
    };
    connect();
    return () => {
      clearTimeout(retry);
      if (ws) ws.close();
    };
  }, []);

  const send = (m) => {
    const ws = wsRef.current;
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(m);
  };

  const val = (k) => Math.round(state ? state[k] : 0);
  const playing = state?.screen === 'doodle';
  const playingBall = state?.screen === 'ball';

  return (
    <ConfigProvider
      theme={{
        algorithm: theme.darkAlgorithm,
        token: { colorPrimary: '#7a5cc0', borderRadius: 14 },
      }}
    >
      <div style={{ maxWidth: 460, margin: '0 auto', padding: 16 }}>
        {/* Header: name on top, ferret below, settings gear in the corner */}
        <div style={{ position: 'relative', textAlign: 'center', paddingTop: 2 }}>
          {typeof state?.battery === 'number' && state.battery >= 0 && (
            <div style={{ position: 'absolute', left: 0, top: 4 }}>
              <BatteryTag pct={state.battery} />
            </div>
          )}
          <Button
            shape="circle"
            size="large"
            disabled={!online}
            onClick={takeShot}
            style={{ position: 'absolute', right: 44, top: 0 }}
            title="Print da tela do hardware"
          >
            📸
          </Button>
          <Button
            shape="circle"
            size="large"
            onClick={() => setSettingsOpen(true)}
            style={{ position: 'absolute', right: 0, top: 0 }}
          >
            ⚙️
          </Button>
          <Title level={2} style={{ margin: '0 0 8px' }}>
            {state ? state.name : 'Furão'} {state && (state.sleeping ? '😴' : '😊')}
          </Title>
          <FerretStage
            anim={state ? state.anim : 'idle'}
            flip={state ? state.flip : false}
            x={state ? state.x : 0.5}
            seq={state ? state.seq : 0}
            size={96}
          />
        </div>

        {playing ? (
          /* Game controller: steer Doodle Jump on the hardware from the phone */
          <GamePad send={send} score={state?.score} onBack={() => send('game:back')} />
        ) : playingBall ? (
          /* Bolinha controller: swipe up to throw the ball on the hardware */
          <BallPad send={send} score={state?.score} onBack={() => send('game:back')} />
        ) : (
          <>
            {/* 2x2 stat bars, hardware style */}
            <Card size="small" style={{ marginTop: 8 }}>
              <Row gutter={[16, 8]}>
                {STATS.map((s) => (
                  <Col span={12} key={s.key}>
                    <Text type="secondary" style={{ fontSize: 12 }}>
                      {s.label}
                    </Text>
                    <Progress
                      percent={val(s.key)}
                      strokeColor={barColor(val(s.key))}
                      trailColor="#3a2f55"
                      strokeWidth={12}
                      format={(p) => `${p}%`}
                    />
                  </Col>
                ))}
              </Row>
            </Card>

            {/* Actions: 4 equally sized buttons (2x2) */}
            <Card size="small" style={{ marginTop: 12 }}>
              <Row gutter={[12, 12]}>
                {ACTIONS.map((a) => (
                  <Col span={12} key={a.id}>
                    <Button block disabled={!online} onClick={() => send(a.id)} style={{ height: 54, fontSize: 16 }}>
                      {a.label}
                    </Button>
                  </Col>
                ))}
              </Row>
            </Card>

            {/* Game launchers: open a game on the hardware, play from here */}
            <Card size="small" style={{ marginTop: 12 }}>
              <Row gutter={[12, 12]}>
                <Col span={12}>
                  <Button block disabled={!online} onClick={() => send('game:start')} style={{ height: 48, fontSize: 16 }}>
                    🎮 Jump!
                  </Button>
                </Col>
                <Col span={12}>
                  <Button block disabled={!online} onClick={() => send('game:ball')} style={{ height: 48, fontSize: 16 }}>
                    🎾 Bolinha
                  </Button>
                </Col>
              </Row>
            </Card>
          </>
        )}

        <div style={{ textAlign: 'center', marginTop: 14 }}>
          <Badge status={online ? 'success' : 'error'} text={online ? 'Ao vivo' : 'Reconectando…'} />
        </div>

        {/* Settings: name + volume + clock */}
        <Drawer
          title="Configurações"
          placement="right"
          width="min(92vw, 360px)"
          styles={{ body: { padding: 20, overflowX: 'hidden' } }}
          open={settingsOpen}
          onClose={() => setSettingsOpen(false)}
        >
          <Text strong>Nome do bichinho</Text>
          <Space.Compact style={{ width: '100%', marginTop: 8 }}>
            <Input
              value={name}
              maxLength={14}
              onChange={(e) => {
                setName(e.target.value);
                nameDirty.current = true;
              }}
            />
            <Button
              type="primary"
              onClick={() => {
                send('name:' + name);
                nameDirty.current = false;
              }}
            >
              Salvar
            </Button>
          </Space.Compact>

          <div style={{ marginTop: 28 }}>
            <Text strong>Volume: {vol}%</Text>
            <Slider
              min={0}
              max={100}
              step={5}
              value={vol}
              onChange={(v) => {
                setVol(v);
                volDirty.current = true;
              }}
              onChangeComplete={(v) => {
                send('vol:' + v);
                volDirty.current = false;
              }}
            />
          </div>

          <div style={{ marginTop: 20 }}>
            <Text strong>Brilho do LED: {ledBright}%</Text>
            <Slider
              min={0}
              max={100}
              step={5}
              value={ledBright}
              onChange={(v) => {
                setLedBright(v);
                ledDirty.current = true;
              }}
              onChangeComplete={(v) => {
                send('led:' + v);
                ledDirty.current = false;
              }}
            />
          </div>

          <div style={{ marginTop: 20 }}>
            <Text strong>Fechar menus sozinho</Text>
            <Text type="secondary" style={{ fontSize: 12, display: 'block', marginTop: 2, marginBottom: 4 }}>
              Fecha config/jogos sem interação
            </Text>
            <Select
              style={{ width: '100%' }}
              value={state?.menuSec}
              options={MENU_TIMEOUT_OPTIONS}
              onChange={(v) => send('menu:' + v)}
            />
          </div>

          <Divider />

          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <Text strong>Relógio quando ocioso</Text>
            <Switch
              checked={!!state?.clockOn}
              onChange={(v) => send(v ? 'clock:on' : 'clock:off')}
            />
          </div>

          <div style={{ marginTop: 16 }}>
            <Text type="secondary" style={{ fontSize: 12 }}>
              Fuso horário
            </Text>
            <Select
              style={{ width: '100%', marginTop: 4 }}
              value={state?.tz}
              options={TZ_OPTIONS}
              disabled={!state?.clockOn}
              onChange={(v) => send('tz:' + v)}
            />
          </div>

          <div style={{ marginTop: 16 }}>
            <Text type="secondary" style={{ fontSize: 12 }}>
              Aparece após ocioso
            </Text>
            <Select
              style={{ width: '100%', marginTop: 4 }}
              value={state?.idleSec}
              options={IDLE_OPTIONS}
              disabled={!state?.clockOn}
              onChange={(v) => send('idle:' + v)}
            />
          </div>

          <div style={{ marginTop: 16 }}>
            <Text type="secondary" style={{ fontSize: 12, display: 'block', marginBottom: 4 }}>
              Formato da hora
            </Text>
            <Segmented
              block
              value={state?.h24 ? '24h' : '12h'}
              options={['24h', '12h']}
              disabled={!state?.clockOn}
              onChange={(v) => send(v === '24h' ? 'fmt:24' : 'fmt:12')}
            />
          </div>

          <div style={{ marginTop: 16 }}>
            <Text type="secondary" style={{ fontSize: 12, display: 'block', marginBottom: 4 }}>
              Formato da data
            </Text>
            <Segmented
              block
              value={state?.dmy === false ? 'MM/DD/AAAA' : 'DD/MM/AAAA'}
              options={['DD/MM/AAAA', 'MM/DD/AAAA']}
              disabled={!state?.clockOn}
              onChange={(v) => send(v === 'DD/MM/AAAA' ? 'date:dmy' : 'date:mdy')}
            />
          </div>
        </Drawer>

        {/* Hardware screenshot: /shot.bmp rendered by the firmware on demand */}
        <Modal
          title="📸 Tela do hardware"
          open={!!shotSrc}
          onCancel={() => setShotSrc(null)}
          footer={[
            <Button key="r" loading={shotLoading} onClick={takeShot}>
              Atualizar
            </Button>,
            <Button key="c" type="primary" onClick={() => setShotSrc(null)}>
              Fechar
            </Button>,
          ]}
        >
          <div
            style={{
              position: 'relative',
              width: '100%',
              aspectRatio: '1 / 1',
              borderRadius: 12,
              overflow: 'hidden',
              background: '#241c3a',
            }}
          >
            {shotLoading && (
              <div
                style={{
                  position: 'absolute',
                  inset: 0,
                  display: 'flex',
                  flexDirection: 'column',
                  alignItems: 'center',
                  justifyContent: 'center',
                  gap: 12,
                }}
              >
                <Spin size="large" />
                <span style={{ color: '#b9b3c8', fontSize: 13 }}>Capturando…</span>
              </div>
            )}
            {shotSrc && (
              <img
                src={shotSrc}
                alt="tela do furão"
                onLoad={() => setShotLoading(false)}
                onError={() => setShotLoading(false)}
                style={{
                  width: '100%',
                  height: '100%',
                  objectFit: 'contain',
                  imageRendering: 'pixelated',
                  opacity: shotLoading ? 0 : 1,
                  transition: 'opacity 0.2s',
                }}
              />
            )}
          </div>
        </Modal>
      </div>
    </ConfigProvider>
  );
}

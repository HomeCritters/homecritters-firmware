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
  const [name, setName] = useState('');
  const [vol, setVol] = useState(80);
  const wsRef = useRef(null);
  const nameDirty = useRef(false);
  const volDirty = useRef(false);
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

        <div style={{ textAlign: 'center', marginTop: 14 }}>
          <Badge status={online ? 'success' : 'error'} text={online ? 'ao vivo' : 'reconectando…'} />
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
              Formato
            </Text>
            <Segmented
              block
              value={state?.h24 ? '24h' : '12h'}
              options={['24h', '12h']}
              disabled={!state?.clockOn}
              onChange={(v) => send(v === '24h' ? 'fmt:24' : 'fmt:12')}
            />
          </div>
        </Drawer>
      </div>
    </ConfigProvider>
  );
}

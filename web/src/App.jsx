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
  Popconfirm,
  Spin,
} from 'antd';
import ferretSheet from './ferret-sheet.png';
import { hmacSha256Hex } from './hmac.js';

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
// A friendly name for THIS browser, shown in the connections manager.
function browserLabel() {
  const ua = navigator.userAgent;
  const os = /iPhone|iPad/.test(ua) ? 'iPhone' : /Android/.test(ua) ? 'Android'
    : /Macintosh/.test(ua) ? 'Mac' : /Windows/.test(ua) ? 'Windows' : 'Navegador';
  const br = /Edg\//.test(ua) ? 'Edge' : /Chrome\//.test(ua) ? 'Chrome'
    : /Firefox\//.test(ua) ? 'Firefox' : /Safari\//.test(ua) ? 'Safari' : '';
  return `Portal ${os}${br ? ' · ' + br : ''}`.slice(0, 24);
}

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

// Genius controller: 4 color pads mirroring the hardware arcs. Each press
// sends "simon:<i>"; the device lights the arc + LED and plays the tone.
const SIMON_PAD_COLORS = ['#3fca5e', '#e6483a', '#efd23e', '#4a7be6'];
const SIMON_PAD_LABELS = ['Verde', 'Vermelho', 'Amarelo', 'Azul'];
function SimonPad({ send, score, onBack }) {
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
  const takeShot = async () => {
    setShotLoading(true);
    // Challenge-response for the screenshot (no token in the URL): a bare GET
    // 401s with a one-shot nonce; retry signed.
    const tok = localStorage.getItem('token') || '';
    try {
      const r = await fetch(`/shot.bmp?t=${Date.now()}`);
      const nonce = (await r.text()).replace('nonce:', '').trim();
      const sig = hmacSha256Hex(tok, 'shot:' + nonce);
      setShotSrc(`/shot.bmp?sig=${sig}&t=${Date.now()}`);
    } catch {
      setShotLoading(false);
    }
  };
  // Pairing (F-Sec 1): without a stored token we send "pair:start" - a
  // 6-digit PIN pops on the device screen by itself; the user types it here
  // and the device hands us the long-lived token (localStorage).
  const [needToken, setNeedToken] = useState(!localStorage.getItem('token'));
  const [pinDraft, setPinDraft] = useState('');
  const [clients, setClients] = useState([]);   // connections manager list
  const gotState = useRef(false);
  // antd's Input.OTP doesn't forward input attributes: patch the underlying
  // inputs to type=tel so phones reliably open the numeric keypad.
  const otpWrapRef = useRef(null);
  useEffect(() => {
    if (!needToken || !otpWrapRef.current) return;
    otpWrapRef.current.querySelectorAll('input').forEach((i) => {
      i.type = 'tel';
      i.inputMode = 'numeric';
      i.pattern = '[0-9]*';
      i.autocomplete = 'one-time-code';
    });
  }, [needToken]);
  const [name, setName] = useState('');
  const [vol, setVol] = useState(80);
  const [ledBright, setLedBright] = useState(50);
  // Weather city search (geocoding runs in the browser; device only stores
  // the chosen lat/lon/city via the wxloc command).
  const [citySearch, setCitySearch] = useState('');
  const [cityResults, setCityResults] = useState([]);
  const [citySearching, setCitySearching] = useState(false);
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
      let opened = false;
      const cnonce = Array.from(crypto.getRandomValues(new Uint8Array(16)))
        .map((x) => x.toString(16).padStart(2, '0')).join('');
      ws.onopen = () => {
        // The device greets with "challenge:<nonce>" (handled in onmessage).
        // Without a stored token we ask to pair right away instead.
        opened = true;
        gotState.current = false;
        if (!localStorage.getItem('token')) ws.send('pair:start');
      };
      ws.onclose = () => {
        setOnline(false);
        // Socket opened but closed before any state = our token was rejected
        // (device re-paired/reset): drop it and fall back to PIN pairing.
        if (opened && !gotState.current) {
          if (localStorage.getItem('token')) localStorage.removeItem('token');
          setNeedToken(true);
        }
        retry = setTimeout(connect, 1500);
      };
      ws.onmessage = (ev) => {
        if (typeof ev.data === 'string') {
          // Challenge-response (F-Sec 2): prove we know the token without
          // sending it, and require the device to prove itself back (mutual).
          if (ev.data.startsWith('challenge:')) {
            const tok = localStorage.getItem('token');
            if (tok) {
              ws.send('auth:' + hmacSha256Hex(tok, ev.data.slice(10)) + ':' + cnonce);
            }
            return;
          }
          if (ev.data.startsWith('proof:')) {
            const tok = localStorage.getItem('token');
            if (!tok || ev.data.slice(6) !== hmacSha256Hex(tok, cnonce)) {
              ws.close();  // not the real device (mDNS spoof) - bail
            }
            return;
          }
          if (ev.data.startsWith('token:')) {
            localStorage.setItem('token', ev.data.slice(6));  // paired!
            setNeedToken(false);
            return;
          }
          if (ev.data.startsWith('clients:')) {
            try { setClients(JSON.parse(ev.data.slice(8))); } catch { /* ignore */ }
            return;
          }
        }
        try {
          const j = JSON.parse(ev.data);
          if (!gotState.current) {
            gotState.current = true;  // authenticated
            setOnline(true);
            setNeedToken(false);
            // Name this connection for the manager, then fetch the list.
            ws.send('label:' + browserLabel());
            ws.send('clients?');
          }
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
  const playingSimon = state?.screen === 'simon';

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
        ) : playingSimon ? (
          /* Genius controller: 4 color pads, presses mirror on the hardware */
          <SimonPad send={send} score={state?.score} onBack={() => send('game:back')} />
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
              <Row gutter={[10, 10]}>
                <Col span={8}>
                  <Button block disabled={!online} onClick={() => send('game:start')} style={{ height: 48, fontSize: 14 }}>
                    🎮 Jump!
                  </Button>
                </Col>
                <Col span={8}>
                  <Button block disabled={!online} onClick={() => send('game:ball')} style={{ height: 48, fontSize: 14 }}>
                    🎾 Bolinha
                  </Button>
                </Col>
                <Col span={8}>
                  <Button block disabled={!online} onClick={() => send('game:simon')} style={{ height: 48, fontSize: 14 }}>
                    🧠 Genius
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

          <div>
            <Text strong>🔒 Conexões pareadas</Text>
            <Text type="secondary" style={{ fontSize: 12, display: 'block', marginBottom: 8 }}>
              Aparelhos com acesso ao bichinho agora
            </Text>
            {clients.length === 0 && (
              <Text type="secondary" style={{ fontSize: 13 }}>Nenhuma conexão.</Text>
            )}
            {clients.map((c) => (
              <div
                key={c.slot + '-' + c.ip}
                style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}
              >
                <div style={{ minWidth: 0 }}>
                  <Text style={{ fontSize: 13 }}>
                    {c.label || 'Aparelho'} {c.me && <Text type="secondary">(este)</Text>}
                  </Text>
                  <Text type="secondary" style={{ fontSize: 11, display: 'block' }}>{c.ip}</Text>
                </div>
                <Popconfirm
                  title="Encerrar esta conexão?"
                  description={c.me ? 'Você precisará parear de novo.' : 'O aparelho precisará parear de novo.'}
                  okText="Encerrar"
                  cancelText="Cancelar"
                  okButtonProps={{ danger: true }}
                  onConfirm={() => send('revoke:' + c.slot)}
                >
                  <Button size="small" danger>Encerrar</Button>
                </Popconfirm>
              </div>
            ))}
            {clients.length > 1 && (
              <Popconfirm
                title="Encerrar TODAS as conexões?"
                description="Todos os aparelhos (inclusive o HA) terão que parear de novo."
                okText="Encerrar todas"
                cancelText="Cancelar"
                okButtonProps={{ danger: true }}
                onConfirm={() => send('revoke:all')}
              >
                <Button size="small" danger block style={{ marginTop: 6 }}>
                  Encerrar todas
                </Button>
              </Popconfirm>
            )}
          </div>

          <Divider />

          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <div>
              <Text strong>🎙️ Microfone</Text>
              <Text type="secondary" style={{ fontSize: 12, display: 'block' }}>
                Desligado = assistente não escuta (ícone na tela)
              </Text>
            </div>
            <Switch
              checked={!state?.micMuted}
              onChange={(v) => send(v ? 'mute:off' : 'mute:on')}
            />
          </div>

          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 16 }}>
            <div>
              <Text strong>🌙 Modo noite</Text>
              <Text type="secondary" style={{ fontSize: 12, display: 'block' }}>
                Apaga tela e LED, Leon dorme
              </Text>
            </div>
            <Switch
              checked={!!state?.fullSleep}
              onChange={(v) => send(v ? 'fullsleep:on' : 'fullsleep:off')}
            />
          </div>

          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 10, paddingLeft: 16 }}>
            <Text type="secondary" style={{ fontSize: 13 }}>Som ao dormir</Text>
            <Switch
              size="small"
              checked={!!state?.sleepSnd}
              onChange={(v) => send(v ? 'sleepsnd:on' : 'sleepsnd:off')}
            />
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 8, paddingLeft: 16 }}>
            <Text type="secondary" style={{ fontSize: 13 }}>Som ao acordar</Text>
            <Switch
              size="small"
              checked={!!state?.wakeSnd}
              onChange={(v) => send(v ? 'wakesnd:on' : 'wakesnd:off')}
            />
          </div>

          <div style={{ marginTop: 20 }}>
            <Text strong>🌤️ Clima</Text>
            <Text type="secondary" style={{ fontSize: 12, display: 'block', marginTop: 2 }}>
              Cena e previsão seguem o tempo real (Open-Meteo).
              Cidade atual: <b>{state?.wxCity || 'não configurada'}</b>
            </Text>
            <Input.Search
              style={{ marginTop: 8 }}
              placeholder="Buscar cidade..."
              value={citySearch}
              loading={citySearching}
              onChange={(e) => setCitySearch(e.target.value)}
              onSearch={async (q) => {
                if (!q || q.length < 2) return;
                setCitySearching(true);
                try {
                  const r = await fetch(
                    'https://geocoding-api.open-meteo.com/v1/search?name=' +
                    encodeURIComponent(q) + '&count=6&language=pt&format=json');
                  const j = await r.json();
                  setCityResults(j.results || []);
                } catch { setCityResults([]); }
                setCitySearching(false);
              }}
            />
            {cityResults.map((c) => (
              <Button
                key={c.id}
                block
                size="small"
                style={{ marginTop: 6, textAlign: 'left' }}
                onClick={() => {
                  // Device font is ASCII-only: strip accents before sending.
                  const ascii = c.name.normalize('NFD').replace(/[\u0300-\u036f]/g, '');
                  send(`wxloc:${c.latitude.toFixed(4)},${c.longitude.toFixed(4)},${ascii}`);
                  setCityResults([]);
                  setCitySearch('');
                }}
              >
                {c.name}{c.admin1 ? `, ${c.admin1}` : ''} ({c.country_code})
              </Button>
            ))}
          </div>

          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 16 }}>
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

        {/* Pairing (F-Sec 1): a 6-digit PIN pops on the device screen */}
        <Modal title="🔑 Parear com o bichinho" open={needToken} closable={false} footer={null}>
          <Text>
            Um código de <b>6 dígitos</b> apareceu na tela do bichinho. Digite
            ele aqui:
          </Text>
          <div ref={otpWrapRef} style={{ display: 'flex', justifyContent: 'center', marginTop: 16 }}>
            <Input.OTP
              length={6}
              size="large"
              value={pinDraft}
              formatter={(v) => v.replace(/\D/g, '')}
              onChange={(v) => setPinDraft(v)}
            />
          </div>
          <Button
            type="primary"
            block
            style={{ marginTop: 16 }}
            disabled={pinDraft.length !== 6}
            onClick={() => {
              send('pair:' + pinDraft);
              setPinDraft('');
            }}
          >
            Parear
          </Button>
          <Text type="secondary" style={{ fontSize: 12, display: 'block', marginTop: 10 }}>
            Sem código na tela? Aguarde reconectar, ou no aparelho: deslize
            para baixo → Seguranca → Parear.
          </Text>
        </Modal>

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

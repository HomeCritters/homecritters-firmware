import React, { useCallback, useState } from 'react';
import {
  Badge, Button, Card, Col, ConfigProvider, Progress, Row, Typography, theme,
} from 'antd';
import { hmacSha256Hex } from './hmac.js';
import { ACTIONS, STATS, barColor } from './options.js';
import { useDeviceSocket } from './useDeviceSocket.js';
import ScreenView from './components/ScreenView.jsx';
import { GamePad, BallPad, SimonPad } from './components/GamePads.jsx';
import { BatteryTag } from './components/Widgets.jsx';
import SettingsDrawer from './components/SettingsDrawer.jsx';
import { PairingModal, ShotModal } from './components/Modals.jsx';

const { Title, Text } = Typography;

// App = composition only: the socket lives in useDeviceSocket, every screen
// area is its own component. The WS pushes state ~10x/s; the leaf components
// are memoized and the heavy drawer/modals are mounted only while open.
export default function App() {
  const {
    state, online, clients, needToken, topInfo, send, requestTop, setFrameHandler,
    name, setName, nameDirty,
    vol, setVol, volDirty,
    ledBright, setLedBright, ledDirty,
  } = useDeviceSocket();

  const [settingsOpen, setSettingsOpen] = useState(false);
  const [wxForce, setWxForce] = useState('');    // dev panel: forced weather
  const [festForce, setFestForce] = useState(''); // dev panel: forced festive theme
  const [shotSrc, setShotSrc] = useState(null);  // /shot.bmp?... while open
  const [shotLoading, setShotLoading] = useState(false);
  const takeShot = useCallback(async () => {
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
  }, []);
  const backToPet = useCallback(() => send('game:back'), [send]);

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
            title="Salvar um print da tela"
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
          {/* The pet's live screen (mirrors the device 1:1) - not a game */}
          {!playing && !playingBall && !playingSimon && (
            <ScreenView
              send={send}
              setFrameHandler={setFrameHandler}
              online={online}
              size={220}
            />
          )}
        </div>

        {playing ? (
          /* Game controller: steer Doodle Jump on the hardware from the phone */
          <GamePad send={send} score={state?.score} onBack={backToPet} />
        ) : playingBall ? (
          /* Bolinha controller: swipe up to throw the ball on the hardware */
          <BallPad send={send} score={state?.score} onBack={backToPet} />
        ) : playingSimon ? (
          /* Genius controller: 4 color pads, presses mirror on the hardware */
          <SimonPad send={send} score={state?.score} onBack={backToPet} />
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

        {/* Heavy subtrees are mounted only while open: the ~10Hz state push
            shouldn't pay for them the other 99% of the time. */}
        {settingsOpen && (
          <SettingsDrawer
            open
            onClose={() => setSettingsOpen(false)}
            send={send}
            state={state}
            clients={clients}
            name={name} setName={setName} nameDirty={nameDirty}
            vol={vol} setVol={setVol} volDirty={volDirty}
            ledBright={ledBright} setLedBright={setLedBright} ledDirty={ledDirty}
            wxForce={wxForce} setWxForce={setWxForce}
            festForce={festForce} setFestForce={setFestForce}
            topInfo={topInfo} onTopRefresh={requestTop}
          />
        )}
        {needToken && <PairingModal open send={send} />}
        {(!!shotSrc || shotLoading) && (
          <ShotModal
            shotSrc={shotSrc}
            shotLoading={shotLoading}
            setShotLoading={setShotLoading}
            onRefresh={takeShot}
            onClose={() => setShotSrc(null)}
          />
        )}
      </div>
    </ConfigProvider>
  );
}

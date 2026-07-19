import React, { useEffect, useState } from 'react';
import {
  Button, Divider, Drawer, Input, Popconfirm, Progress, Segmented, Select,
  Slider, Space, Switch, Typography,
} from 'antd';
import {
  IDLE_OPTIONS, MENU_TIMEOUT_OPTIONS, TZ_OPTIONS, WX_FORCE_OPTIONS,
} from '../options.js';

const { Text } = Typography;

// Linux-top mirror (dev panel): renders the device's "top:{...}" snapshot.
// While mounted with data it auto-refreshes every 5s (the device samples for
// ~1.1s per request on a side task, so this stays cheap).
function TopPanel({ topInfo, onRefresh }) {
  const [auto, setAuto] = useState(false);
  useEffect(() => {
    if (!auto) return undefined;
    onRefresh();
    const t = setInterval(onRefresh, 5000);
    return () => clearInterval(t);
  }, [auto, onRefresh]);

  return (
    <div style={{ marginTop: 16 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <Text strong>📊 Top (CPU / tasks)</Text>
        <Space>
          <Text type="secondary" style={{ fontSize: 12 }}>auto</Text>
          <Switch size="small" checked={auto} onChange={setAuto} />
        </Space>
      </div>
      <Button block size="small" style={{ marginTop: 8 }} onClick={onRefresh}>
        Medir agora (~1s)
      </Button>
      {topInfo && (
        <div style={{ marginTop: 10 }}>
          {[['CPU 0 (rádio/rede)', topInfo.c0], ['CPU 1 (render/áudio)', topInfo.c1]].map(([l, v]) => (
            <div key={l}>
              <Text type="secondary" style={{ fontSize: 11 }}>{l}</Text>
              <Progress
                percent={v}
                size="small"
                strokeColor={v > 85 ? '#e04640' : v > 60 ? '#f0c846' : '#46c85a'}
                format={(p) => `${p}%`}
              />
            </div>
          ))}
          <Text type="secondary" style={{ fontSize: 11, display: 'block', marginTop: 4 }}>
            heap {topInfo.heap}KB livre (mín {topInfo.min}KB, maior bloco {topInfo.big}KB)
            · psram {topInfo.psram}KB · uptime {Math.floor(topInfo.up / 60)}min
          </Text>
          <div
            style={{
              marginTop: 8,
              fontFamily: 'ui-monospace, Menlo, monospace',
              fontSize: 11,
              lineHeight: 1.7,
              background: '#241c3a',
              borderRadius: 8,
              padding: '6px 10px',
              overflowX: 'auto',
            }}
          >
            <div style={{ color: '#8a8a99' }}>task        core prio stack st</div>
            {(topInfo.tasks || []).map((t) => (
              <div key={t.n} style={{ whiteSpace: 'pre', color: t.s < 512 ? '#e04640' : '#d8d2e8' }}>
                {t.n.padEnd(11)} {String(t.c).padStart(3)} {String(t.p).padStart(4)}
                {' '}{String(t.s).padStart(5)}B {t.st}
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}

// The full settings drawer. Mounted by App only while open, so none of this
// tree costs anything during the ~10Hz state pushes with the drawer closed.
export default function SettingsDrawer({
  open, onClose, send, state, clients,
  name, setName, nameDirty,
  vol, setVol, volDirty,
  ledBright, setLedBright, ledDirty,
  wxForce, setWxForce,
  topInfo, onTopRefresh,
}) {
  // City search is transient UI: it lives (and resets) with the drawer.
  const [citySearch, setCitySearch] = useState('');
  const [cityResults, setCityResults] = useState([]);
  const [citySearching, setCitySearching] = useState(false);

  return (
    <Drawer
      title="Configurações"
      placement="right"
      width="min(92vw, 360px)"
      styles={{ body: { padding: 20, overflowX: 'hidden' } }}
      open={open}
      onClose={onClose}
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

      <div>
        <Text strong>🛠️ Desenvolvimento</Text>
        <Text type="secondary" style={{ fontSize: 12, display: 'block', marginTop: 2, marginBottom: 4 }}>
          Força um clima na cena pra testar os visuais (não afeta a
          previsão real).
        </Text>
        <Select
          style={{ width: '100%' }}
          value={wxForce}
          options={WX_FORCE_OPTIONS}
          onChange={(v) => {
            setWxForce(v);
            send('wxset:' + v);
          }}
        />
        <Button
          block
          style={{ marginTop: 8 }}
          onClick={() => send('wxbolt')}
        >
          ⚡ Raio agora
        </Button>
        <TopPanel topInfo={topInfo} onRefresh={onTopRefresh} />
      </div>
    </Drawer>
  );
}

import React, { useEffect, useState } from 'react';
import {
  Button, Divider, Drawer, Input, Popconfirm, Progress, Segmented, Select,
  Slider, Space, Switch, Typography,
} from 'antd';
import {
  FEST_FORCE_OPTIONS, IDLE_OPTIONS, MENU_TIMEOUT_OPTIONS, TZ_OPTIONS,
  WX_FORCE_OPTIONS,
} from '../options.js';

const { Text } = Typography;

// Linux-top mirror (dev panel), REAL TIME: each reply triggers the next
// sample (the device measures for ~1.1s per request, so this self-clocks at
// ~1.2s like a live top), with a 3s fallback timer in case a reply is lost.
// The device ignores overlapping requests while one is in flight.
function TopPanel({ topInfo, onRefresh }) {
  useEffect(() => {
    onRefresh();  // start streaming as soon as the drawer section mounts
    const t = setInterval(onRefresh, 3000);  // lost-reply fallback
    return () => clearInterval(t);
  }, [onRefresh]);
  useEffect(() => {
    if (topInfo) onRefresh();  // reply landed -> ask for the next sample
  }, [topInfo, onRefresh]);

  const memUsed = topInfo && topInfo.heapT ? topInfo.heapT - topInfo.heap : 0;
  const memPct = topInfo && topInfo.heapT ? Math.round((100 * memUsed) / topInfo.heapT) : 0;
  const psUsed = topInfo && topInfo.psramT ? topInfo.psramT - topInfo.psram : 0;
  const psPct = topInfo && topInfo.psramT ? Math.round((100 * psUsed) / topInfo.psramT) : 0;
  const barColor = (v) => (v > 85 ? '#e04640' : v > 60 ? '#f0c846' : '#46c85a');
  const temp = topInfo?.temp;
  const tempColor = temp > 75 ? '#e04640' : temp > 60 ? '#f0c846' : '#5ac86e';

  return (
    <div style={{ marginTop: 16 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <Text strong>📊 Top</Text>
        {topInfo ? (
          <Space size={10}>
            <Text style={{ fontSize: 13, color: tempColor }}>🌡️ {temp?.toFixed(1)}°C</Text>
            <Text type="secondary" style={{ fontSize: 11 }}>
              up {Math.floor(topInfo.up / 60)}min
            </Text>
          </Space>
        ) : (
          <Text type="secondary" style={{ fontSize: 12 }}>medindo…</Text>
        )}
      </div>
      {topInfo && (
        <div style={{ marginTop: 6 }}>
          {[
            ['CPU 0 (rádio/rede)', topInfo.c0, `${topInfo.c0}%`],
            ['CPU 1 (render/áudio)', topInfo.c1, `${topInfo.c1}%`],
            [`Memória (${memUsed}/${topInfo.heapT}KB · mín livre ${topInfo.min}KB)`, memPct, `${memPct}%`],
            [`PSRAM (${psUsed}/${topInfo.psramT}KB)`, psPct, `${psPct}%`],
          ].map(([l, v, txt]) => (
            <div key={l}>
              <Text type="secondary" style={{ fontSize: 11 }}>{l}</Text>
              <Progress
                percent={v}
                size="small"
                strokeColor={barColor(v)}
                format={() => txt}
              />
            </div>
          ))}
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
            {/* Header and rows share ONE fixed-width template so the columns
                can't drift (hand-spaced header + per-cell padding wobbled). */}
            {(() => {
              const row = (n, c, p, s, st) =>
                `${n.padEnd(10)}${c.padStart(5)}${p.padStart(5)}${s.padStart(8)}  ${st}`;
              return (
                <>
                  <div style={{ whiteSpace: 'pre', color: '#8a8a99' }}>
                    {row('task', 'core', 'prio', 'stack', 'estado')}
                  </div>
                  {(topInfo.tasks || []).map((t) => (
                    <div key={t.n} style={{ whiteSpace: 'pre', color: t.s < 512 ? '#e04640' : '#d8d2e8' }}>
                      {row(t.n, String(t.c), String(t.p), `${t.s}B`, t.st)}
                    </div>
                  ))}
                </>
              );
            })()}
          </div>
          <div
            style={{
              marginTop: 8,
              fontSize: 11,
              lineHeight: 1.6,
              color: '#9a93ad',
              background: '#1d1731',
              borderRadius: 8,
              padding: '8px 12px',
            }}
          >
            <b style={{ color: '#b9b3c8' }}>Legenda</b><br />
            <b>CPU 0</b>: núcleo do rádio — WiFi, rede, clima, mic ·{' '}
            <b>CPU 1</b>: núcleo da tela — desenho da cena e decodificação de
            áudio. Ocupação medida em amostras de ~1s.<br />
            <b>Memória</b>: RAM interna do chip (335KB) — a crítica: DMA,
            stacks e WiFi só vivem nela. <i>mín livre</i> = o pior momento
            desde ligar (se encostar em ~10KB, é aperto de verdade).<br />
            <b>PSRAM</b>: RAM externa de 8MB, mais lenta — buffers grandes
            (1MB do streaming de música, prints de tela).<br />
            <b>🌡️</b>: temperatura interna do SoC (do chip, não do ambiente;
            verde &lt;60°, amarelo &lt;75°).<br />
            <b>Tabela</b>: as tasks do firmware — <i>core</i> onde roda,{' '}
            <i>prio</i>ridade, <i>stack</i> = pilha que sobrou de reserva
            (vermelho &lt;512B = perigo), <i>estado</i>: run = executando,
            ready = na fila, block = dormindo à espera de trabalho.
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
  wxForce, setWxForce, festForce, setFestForce,
  topInfo, onTopRefresh,
}) {
  // City search is transient UI: it lives (and resets) with the drawer.
  const [citySearch, setCitySearch] = useState('');
  const [cityResults, setCityResults] = useState([]);
  // Birthday draft mirrors the device value until the user edits it.
  const [bday, setBday] = useState(state?.bday || '');
  useEffect(() => { setBday(state?.bday || ''); }, [state?.bday]);
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

      <div style={{ marginTop: 20 }}>
        <Text strong>🎂 Aniversário</Text>
        <Text type="secondary" style={{ fontSize: 12, display: 'block', marginTop: 2, marginBottom: 6 }}>
          {`No dia, o ${state?.name || 'bichinho'} ganha bolo, balões e "Parabéns pra Você".`}
        </Text>
        <Space.Compact style={{ width: '100%' }}>
          <Input
            type="date"
            // Full date incl. year (used for the age below); the device stores
            // YYYY-MM-DD and fires the party on the MM-DD part.
            value={/^\d{4}-\d{2}-\d{2}$/.test(bday) ? bday : (/^\d{2}-\d{2}$/.test(bday) ? `2026-${bday}` : '')}
            onChange={(e) => setBday(e.target.value)}
          />
          <Button
            type="primary"
            disabled={!/^\d{4}-\d{2}-\d{2}$/.test(bday)}
            onClick={() => send('bday:' + bday)}
          >
            Salvar
          </Button>
        </Space.Compact>
        {/^\d{4}-\d{2}-\d{2}$/.test(bday) && (() => {
          const [y, m, d] = bday.split('-').map(Number);
          const now = new Date();
          let age = now.getFullYear() - y;
          if (now.getMonth() + 1 < m || (now.getMonth() + 1 === m && now.getDate() < d)) age -= 1;
          return (
            <Text type="secondary" style={{ fontSize: 12, display: 'block', marginTop: 6 }}>
              {age > 0 ? `${state?.name || 'Ele'} tem ${age} ano${age > 1 ? 's' : ''} 🎂`
                       : `${state?.name || 'Ele'} nasceu esse ano 🎂`}
            </Text>
          );
        })()}
      </div>

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
            Apaga tela e LED, o bichinho dorme
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

        <Text type="secondary" style={{ fontSize: 12, display: 'block', marginTop: 12, marginBottom: 4 }}>
          Força um tema festivo na cena (não altera o calendário real).
        </Text>
        <Select
          style={{ width: '100%' }}
          value={festForce}
          options={FEST_FORCE_OPTIONS}
          onChange={(v) => {
            setFestForce(v);
            send('fest:' + (v || 'auto'));
          }}
        />
        <TopPanel topInfo={topInfo} onRefresh={onTopRefresh} />
      </div>
    </Drawer>
  );
}

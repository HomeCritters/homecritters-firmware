// Shared option lists + small pure helpers (no React here).

// Timezones: POSIX TZ (applied by the firmware) + IANA names (to match
// what the browser reports).
export const TZ_OPTIONS = [
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

export const IDLE_OPTIONS = [
  { label: '15 segundos', value: 15 },
  { label: '30 segundos', value: 30 },
  { label: '1 minuto', value: 60 },
  { label: '2 minutos', value: 120 },
  { label: '5 minutos', value: 300 },
];

export const MENU_TIMEOUT_OPTIONS = [
  { label: 'Desativado', value: 0 },
  { label: '5 segundos', value: 5 },
  { label: '10 segundos', value: 10 },
  { label: '15 segundos', value: 15 },
  { label: '30 segundos', value: 30 },
  { label: '1 minuto', value: 60 },
];

// Dev panel: forceable weather (value = firmware wxset name; '' = real).
export const WX_FORCE_OPTIONS = [
  { label: '🌐 Automático (clima real)', value: '' },
  { label: '☀️ Limpo', value: 'clear' },
  { label: '🌤️ Quase limpo', value: 'mclear' },
  { label: '⛅ Parcial nublado', value: 'partly' },
  { label: '☁️ Encoberto', value: 'cloudy' },
  { label: '🌫️ Neblina', value: 'fog' },
  { label: '🌦️ Garoa', value: 'drizzle' },
  { label: '🧊 Garoa gelada', value: 'frizzle' },
  { label: '🌧️ Chuva', value: 'rainy' },
  { label: '🧊 Chuva gelada', value: 'frain' },
  { label: '⛈️ Temporal', value: 'pouring' },
  { label: '❄️ Neve', value: 'snow' },
  { label: '🌨️ Grãos de neve', value: 'grains' },
  { label: '🌨️ Pancada de neve', value: 'snowshower' },
  { label: '🌩️ Tempestade', value: 'storm' },
  { label: '🧊 Granizo', value: 'hail' },
];

export const STATS = [
  { key: 'hunger', label: 'Fome' },
  { key: 'energy', label: 'Energia' },
  { key: 'joy', label: 'Alegria' },
  { key: 'hygiene', label: 'Higiene' },
];

export const ACTIONS = [
  { id: 'feed', label: '🍎 Alimentar' },
  { id: 'pat', label: '🐾 Carinho' },
  { id: 'clean', label: '💧 Banho' },
  { id: 'sleep', label: '🌙 Dormir' },
];

// Same color rule as the hardware: red low, yellow mid, green high.
export const barColor = (v) => (v < 25 ? '#e04640' : v < 55 ? '#f0c846' : '#46c85a');

// A friendly name for THIS browser, shown in the connections manager.
export function browserLabel() {
  const ua = navigator.userAgent;
  const os = /iPhone|iPad/.test(ua) ? 'iPhone' : /Android/.test(ua) ? 'Android'
    : /Macintosh/.test(ua) ? 'Mac' : /Windows/.test(ua) ? 'Windows' : 'Navegador';
  const br = /Edg\//.test(ua) ? 'Edge' : /Chrome\//.test(ua) ? 'Chrome'
    : /Firefox\//.test(ua) ? 'Firefox' : /Safari\//.test(ua) ? 'Safari' : '';
  return `Portal ${os}${br ? ' · ' + br : ''}`.slice(0, 24);
}

// Detects the browser timezone -> POSIX TZ string. Tries to match the IANA
// name against the list; otherwise falls back to a fixed offset (no DST).
export function detectPosixTz() {
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

import { useCallback, useEffect, useRef, useState } from 'react';
import { hmacSha256Hex, timingSafeEqualHex } from './hmac.js';
import { browserLabel, detectPosixTz } from './options.js';

// The device WebSocket in one hook: connection + exponential-backoff
// reconnect, challenge-response auth (mutual), pairing fallback, state
// ingestion (with dirty-guards for the inputs the user may be editing) and
// the `top` dev snapshot. Everything socket-synced lives here; App composes.
export function useDeviceSocket() {
  const [state, setState] = useState(null);
  const [online, setOnline] = useState(false);
  const [clients, setClients] = useState([]);   // connections manager list
  const [topInfo, setTopInfo] = useState(null); // dev panel `top` snapshot
  // Pairing (F-Sec 1): without a stored token we send "pair:start" - a
  // 6-digit PIN pops on the device screen by itself; the user types it and
  // the device hands us the long-lived token (localStorage).
  const [needToken, setNeedToken] = useState(!localStorage.getItem('token'));

  // Inputs mirrored from the state UNLESS the user is mid-edit (dirty).
  const [name, setName] = useState('');
  const [vol, setVol] = useState(80);
  const [ledBright, setLedBright] = useState(50);
  const nameDirty = useRef(false);
  const volDirty = useRef(false);
  const ledDirty = useRef(false);

  const wsRef = useRef(null);
  const gotState = useRef(false);
  const autoTz = useRef(false);
  // Live screen: a component registers a handler here; when a binary RLE
  // frame arrives we hand it over (raw ArrayBuffer) to decode onto a canvas.
  const frameCb = useRef(null);

  useEffect(() => {
    let ws;
    let retry;
    // Reconnect backoff: 1.5s after a blip, doubling to 30s while the device
    // stays away (phone left on the page overnight shouldn't hammer it).
    let retryMs = 1500;
    const connect = () => {
      ws = new WebSocket(`ws://${location.hostname}:81/`);
      ws.binaryType = 'arraybuffer';  // live screen frames arrive as binary
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
        retry = setTimeout(connect, retryMs);
        retryMs = Math.min(retryMs * 2, 30000);
      };
      ws.onmessage = (ev) => {
        if (ev.data instanceof ArrayBuffer) {
          if (frameCb.current) frameCb.current(ev.data);  // live screen frame
          return;
        }
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
            // Constant-time compare (hygiene parity with the HA plugin's
            // hmac.compare_digest; remote timing over WS is impractical, but
            // there's no reason to hand out an early-exit oracle either).
            if (!tok || !timingSafeEqualHex(ev.data.slice(6), hmacSha256Hex(tok, cnonce))) {
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
          if (ev.data.startsWith('top:')) {
            try { setTopInfo(JSON.parse(ev.data.slice(4))); } catch { /* ignore */ }
            return;
          }
        }
        try {
          const j = JSON.parse(ev.data);
          if (!gotState.current) {
            gotState.current = true;  // authenticated
            retryMs = 1500;           // healthy again: reset the backoff
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

  // Stable identity (useCallback): the memoized pads receive `send` as a
  // prop - a fresh closure per render would defeat their React.memo.
  const send = useCallback((m) => {
    const s = wsRef.current;
    if (s && s.readyState === WebSocket.OPEN) s.send(m);
  }, []);
  const requestTop = useCallback(() => send('top'), [send]);
  // Live screen: register/clear the frame handler (LiveScreen owns decoding).
  const setFrameHandler = useCallback((fn) => { frameCb.current = fn; }, []);

  return {
    state, online, clients, needToken, topInfo, send, requestTop, setFrameHandler,
    name, setName, nameDirty,
    vol, setVol, volDirty,
    ledBright, setLedBright, ledDirty,
  };
}

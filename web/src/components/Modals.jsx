import React, { useEffect, useRef, useState } from 'react';
import { Button, Input, Modal, Spin, Typography } from 'antd';

const { Text } = Typography;

// Pairing (F-Sec 1): a 6-digit PIN pops on the device screen; the user types
// it here and the device answers with the long-lived token (handled by the
// socket hook). Mounted by App only while pairing is needed.
export function PairingModal({ open, send }) {
  const [pinDraft, setPinDraft] = useState('');
  // antd's Input.OTP doesn't forward input attributes: patch the underlying
  // inputs to type=tel so phones reliably open the numeric keypad.
  const otpWrapRef = useRef(null);
  useEffect(() => {
    if (!open || !otpWrapRef.current) return;
    otpWrapRef.current.querySelectorAll('input').forEach((i) => {
      i.type = 'tel';
      i.inputMode = 'numeric';
      i.pattern = '[0-9]*';
      i.autocomplete = 'one-time-code';
    });
  }, [open]);

  return (
    <Modal title="🔑 Parear com o bichinho" open={open} closable={false} footer={null}>
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
  );
}

// Hardware screenshot: /shot.bmp rendered by the firmware on demand.
export function ShotModal({ shotSrc, shotLoading, setShotLoading, onRefresh, onClose }) {
  return (
    <Modal
      title="📸 Tela do hardware"
      open={!!shotSrc}
      onCancel={onClose}
      footer={[
        <Button key="r" loading={shotLoading} onClick={onRefresh}>
          Atualizar
        </Button>,
        <Button key="c" type="primary" onClick={onClose}>
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
  );
}

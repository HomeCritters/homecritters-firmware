import React from 'react';

// Small battery pill: an SVG icon whose fill/level mirrors the hardware.
export const BatteryTag = React.memo(function BatteryTag({ pct }) {
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
});

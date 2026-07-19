import React from 'react';
import ferretSheet from '../ferret-sheet.png';

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
export function FerretSprite({ anim = 'idle', flip = false, size = 96 }) {
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
// snaps (otherwise it would keep sliding during idle). Memoized: the app
// re-renders ~10x/s on WS pushes, this only repaints when its props change.
export const FerretStage = React.memo(function FerretStage({
  anim, flip, x, seq, size = 96,
}) {
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
});

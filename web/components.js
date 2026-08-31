// Original implementations of the Sungam visual language (colors, spacing,
// knob sweep geometry) for a standalone page -- the design sketch's own
// component JS is bundled/proprietary to the design tool and isn't reused.

const KNOB_SWEEP_DEG = 317.2;
const KNOB_ARC_START_DEG = 111.4;

function knobAngleRad(frac) {
  return (KNOB_ARC_START_DEG + frac * KNOB_SWEEP_DEG) * Math.PI / 180;
}

/** A drag-vertical rotary knob, canvas-drawn, matching the design's 317.2°
    sweep / 111.4° start geometry. Bipolar knobs grow their arc from noon. */
export function createKnob ({ label, min, max, value, format, bipolar = false, color = '#4f7ea8', onChange }) {
  const wrap = document.createElement('div');
  wrap.className = 'lt-knob-wrap';

  const labelEl = document.createElement('div');
  labelEl.className = 'lt-knob-label';
  labelEl.textContent = label;

  const size = 44;
  const dpr = window.devicePixelRatio || 1;
  const canvas = document.createElement('canvas');
  canvas.width = size * dpr;
  canvas.height = size * dpr;
  canvas.style.width = size + 'px';
  canvas.style.height = size + 'px';
  const ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);

  const valueEl = document.createElement('div');
  valueEl.className = 'lt-knob-value';

  const defaultValue = value;
  let current = value;

  function toFrac (v) { return Math.max(0, Math.min(1, (v - min) / (max - min))); }

  function draw () {
    ctx.clearRect(0, 0, size, size);
    const cx = size / 2, cy = size / 2, r = size / 2 - 6;

    ctx.lineWidth = 3;
    ctx.lineCap = 'round';

    // full-sweep background track
    ctx.beginPath();
    ctx.arc(cx, cy, r, knobAngleRad(0), knobAngleRad(1));
    ctx.strokeStyle = 'rgba(26,26,23,0.16)';
    ctx.stroke();

    // value arc
    const frac = toFrac(current);
    ctx.beginPath();
    if (bipolar) {
      const a0 = knobAngleRad(0.5);
      const a1 = knobAngleRad(frac);
      if (frac >= 0.5) ctx.arc(cx, cy, r, a0, a1);
      else ctx.arc(cx, cy, r, a1, a0);
    } else {
      ctx.arc(cx, cy, r, knobAngleRad(0), knobAngleRad(frac));
    }
    ctx.strokeStyle = color;
    ctx.stroke();

    // pointer
    const a = knobAngleRad(frac);
    ctx.beginPath();
    ctx.moveTo(cx + Math.cos(a) * (r - 8), cy + Math.sin(a) * (r - 8));
    ctx.lineTo(cx + Math.cos(a) * (r + 2), cy + Math.sin(a) * (r + 2));
    ctx.strokeStyle = '#1a1a17';
    ctx.lineWidth = 2;
    ctx.stroke();
  }

  function setValue (v, notify = true) {
    current = Math.max(min, Math.min(max, v));
    valueEl.textContent = format(current);
    draw();
    if (notify && onChange) onChange(current);
  }

  let dragStartY = null, dragStartVal = null;
  canvas.style.cursor = 'ns-resize';
  canvas.addEventListener('pointerdown', (e) => {
    dragStartY = e.clientY;
    dragStartVal = current;
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener('pointermove', (e) => {
    if (dragStartY === null) return;
    const dy = dragStartY - e.clientY;
    const range = max - min;
    const sensitivity = e.shiftKey ? range / 800 : range / 200;
    setValue(dragStartVal + dy * sensitivity);
  });
  canvas.addEventListener('pointerup', () => { dragStartY = null; });
  canvas.addEventListener('dblclick', () => setValue(defaultValue));
  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    const range = max - min;
    setValue(current + (e.deltaY < 0 ? 1 : -1) * range / 100);
  }, { passive: false });

  wrap.append(labelEl, canvas, valueEl);
  setValue(value, false);

  return { el: wrap, setValue: (v) => setValue(v, false), getValue: () => current };
}

export function createSelector ({ options, selected = 0, onChange }) {
  const el = document.createElement('div');
  el.className = 'lt-selector';
  let current = selected;
  const buttons = options.map((opt, i) => {
    const b = document.createElement('button');
    b.textContent = opt;
    b.type = 'button';
    if (i === current) b.classList.add('active');
    b.addEventListener('click', () => {
      current = i;
      buttons.forEach((btn, j) => btn.classList.toggle('active', j === i));
      if (onChange) onChange(i);
    });
    el.appendChild(b);
    return b;
  });
  return { el, getIndex: () => current, setIndex: (i) => { current = i; buttons.forEach((b, j) => b.classList.toggle('active', j === i)); } };
}

export function createLatch ({ label, color = '#ed8159', on = false, onToggle }) {
  const el = document.createElement('button');
  el.type = 'button';
  el.className = 'lt-latch';
  el.textContent = label;
  function paint (state) {
    el.classList.toggle('on', state);
    el.style.background = state ? color : '';
    el.style.borderColor = state ? color : '';
  }
  paint(on);
  el.addEventListener('click', () => {
    on = !on;
    paint(on);
    if (onToggle) onToggle(on);
  });
  return { el, setOn: (v) => { on = v; paint(v); }, isOn: () => on };
}

export function createLamp ({ color = '#ed8159' } = {}) {
  const el = document.createElement('div');
  el.className = 'lt-lamp';
  el.style.color = color;
  function setOn (on) {
    el.classList.toggle('on', on);
    el.style.background = on ? color : '';
    el.style.borderColor = on ? color : '';
  }
  setOn(false);
  return { el, setOn };
}

export function createTerminal (label) {
  const el = document.createElement('div');
  el.className = 'lt-terminal';
  const dot = document.createElement('div');
  dot.className = 'lt-terminal-dot';
  const lbl = document.createElement('div');
  lbl.className = 'lt-terminal-label';
  lbl.textContent = label;
  el.append(dot, lbl);
  return el;
}

export function createFrame ({ title, color = '#4f7ea8' }) {
  const el = document.createElement('div');
  el.className = 'lt-frame';
  el.style.borderColor = 'var(--ink-22)';
  const titleEl = document.createElement('div');
  titleEl.className = 'lt-frame-title';
  titleEl.textContent = title;
  titleEl.style.color = color;
  const body = document.createElement('div');
  el.append(titleEl, body);
  return { el, body };
}

export function createMeter () {
  const el = document.createElement('div');
  el.className = 'lt-meter';
  const fill = document.createElement('div');
  fill.className = 'lt-meter-fill';
  el.appendChild(fill);
  return { el, setLevel: (frac) => { fill.style.width = Math.max(0, Math.min(1, frac)) * 100 + '%'; } };
}

/** Loop position view: a filled bar (what's recorded, relative to the
    current musical loop length) with a moving playhead -- red while
    recording, teal while playing. */
export function createLoopView () {
  const el = document.createElement('div');
  el.className = 'lt-wave';
  const fill = document.createElement('div');
  fill.style.cssText = 'position:absolute;left:0;top:0;bottom:0;width:0%;background:#2f6478;';
  const head = document.createElement('div');
  head.className = 'lt-wave-head';
  head.style.display = 'none';
  const label = document.createElement('div');
  label.style.cssText = 'position:absolute;top:6px;left:8px;font:700 12px var(--font-mono);letter-spacing:.08em;color:rgba(255,255,255,0.9);background:rgba(0,0,0,0.4);padding:2px 6px;';
  el.append(fill, head, label);

  function update ({ state, filledFrac, headFrac }) {
    label.textContent = state;
    const recColor = '#e06060', playColor = '#60e0a8', recFill = '#7a3535', playFill = '#2f6478';
    if (state === 'RECORDING') {
      fill.style.background = recFill;
      head.style.background = recColor;
      head.style.boxShadow = `0 0 4px ${recColor}`;
    } else {
      fill.style.background = playFill;
      head.style.background = playColor;
      head.style.boxShadow = `0 0 4px ${playColor}`;
    }
    fill.style.width = Math.max(0, Math.min(1, filledFrac)) * 100 + '%';
    if (headFrac === null || headFrac === undefined) {
      head.style.display = 'none';
    } else {
      head.style.display = '';
      head.style.left = Math.max(0, Math.min(1, headFrac)) * 100 + '%';
    }
  }

  return { el, update };
}

export function createWordmark (text) {
  const wrap = document.createElement('div');
  wrap.style.textAlign = 'right';
  const word = document.createElement('div');
  word.className = 'lt-wordmark';
  word.textContent = text;
  const tag = document.createElement('div');
  tag.className = 'lt-tagline';
  tag.textContent = 'TAPE MULTITRACK';
  wrap.append(word, tag);
  return wrap;
}

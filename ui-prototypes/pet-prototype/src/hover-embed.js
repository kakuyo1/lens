/**
 * The hover interaction study: seven pets on one desk, each with its own proximity rings. A pointer near
 * the pet moves its eyes first; holding nearby earns a greeting and then a fuller reaction. State text is
 * part of the study, not the product.
 */
import { PetEngine } from './pet-engine.js';
import { PetRenderer } from './pet-renderer.js';
import { HoverSense, RINGS, EXIT_MARGIN } from './hover-sense.js';

const ORDER = ['cat', 'ghost', 'jelly', 'cloud', 'dog', 'mochi', 'owl'];
const SCALE = 2;

const PERSONA = {
  cat: {
    name: '短耳猫', notice: 'tail_flick', noticeLabel: '甩尾', dwell: 'knead', dwellLabel: '踩奶',
    noticeDelayMs: 650, dwellDelayMs: 3200, cooldownMs: 9000,
  },
  ghost: {
    name: '小幽灵', notice: 'peek', noticeLabel: '探头', dwell: 'boo', dwellLabel: '吓一下',
    noticeDelayMs: 300, dwellDelayMs: 2400, cooldownMs: 7500,
  },
  jelly: {
    name: '小水母', notice: 'squish', noticeLabel: '缩一下', dwell: 'bubble', dwellLabel: '吐泡',
    noticeDelayMs: 240, dwellDelayMs: 2200, cooldownMs: 7000,
  },
  cloud: {
    name: '小云', notice: 'puff', noticeLabel: '呼气', dwell: 'drizzle', dwellLabel: '飘雨',
    noticeDelayMs: 450, dwellDelayMs: 2900, cooldownMs: 8500,
  },
  dog: {
    name: '小狗', notice: 'wag', noticeLabel: '摇尾', dwell: 'sniff', dwellLabel: '嗅一嗅',
    noticeDelayMs: 200, dwellDelayMs: 2100, cooldownMs: 6500,
  },
  mochi: {
    name: '麻糬', notice: 'bounce', noticeLabel: '弹一下', dwell: 'wobble', dwellLabel: '晃一晃',
    noticeDelayMs: 260, dwellDelayMs: 2300, cooldownMs: 7000,
  },
  owl: {
    name: '猫头鹰', notice: 'hoot', noticeLabel: '咕咕', dwell: 'preen', dwellLabel: '理羽',
    noticeDelayMs: 500, dwellDelayMs: 2900, cooldownMs: 8500,
  },
};

const layers = { gaze: true, notice: true, dwell: true, habit: true };
const pointer = { x: 0, y: 0, active: false };
const pets = [];

/**
 * @param {Record<string, object>} manifest Assembled pets, keyed by id.
 * @param {Record<string, string>} assets Data URLs keyed `<id>/<path>`.
 */
export async function mount(manifest, assets) {
  const grid = document.getElementById('grid');
  const slots = new Map();
  for (const id of ORDER) {
    const persona = PERSONA[id];
    const pet = manifest[id];
    if (!pet || !persona) continue;
    const slot = document.createElement('article');
    slot.className = 'pet-slot';
    slot.dataset.pet = id;
    slot.innerHTML = `
      <div class="field">
        <span class="ring gaze" aria-hidden="true"></span>
        <span class="ring near" aria-hidden="true"></span>
        <canvas class="pet-canvas" aria-label="${persona.name}"></canvas>
      </div>
      <div class="caption"><strong>${persona.name}</strong><span class="state">在发呆</span></div>`;
    grid.append(slot);
    slots.set(id, slot);
  }

  await Promise.all(ORDER.filter(id => manifest[id] && PERSONA[id]).map(async id => {
    const pet = manifest[id];
    const persona = PERSONA[id];
    const slot = slots.get(id);
    const canvas = slot.querySelector('canvas');
    const renderer = new PetRenderer(canvas, { ...pet, scale: SCALE });
    await renderer.load(path => assets[`${id}/${path}`]);
    const engine = new PetEngine(pet);
    engine.setAmbient(false);
    const sense = new HoverSense({
      noticeDelayMs: persona.noticeDelayMs,
      dwellDelayMs: persona.dwellDelayMs,
      cooldownMs: persona.cooldownMs,
    });
    const body = inkBox(renderer.images.get(pet.actions[pet.roles.idle].sheet));
    const eyes = inkBox(renderer.images.get(pet.expression));
    const entry = {
      id, pet, persona, slot, canvas, renderer, engine, sense,
      state: slot.querySelector('.state'),
      body: fitBox(body, pet.actions[pet.roles.idle].anchors[0].body),
      eyes: fitBox(eyes, pet.actions[pet.roles.idle].anchors[0].face),
      gaze: null,
      label: '',
    };
    pets.push(entry);
  }));

  bindSwitches();
  bindPointer();
  document.getElementById('reset').addEventListener('click', resetAll);
  if (new URLSearchParams(location.search).has('zones')) setZones(true);
  layoutZones();
  window.addEventListener('resize', layoutZones);

  if (new URLSearchParams(location.search).has('qa')) window.__hover = { pets, layers, pointer };

  let last = performance.now();
  const tick = now => {
    const delta = Math.max(Math.min(now - last, 100), 0);
    last = now;
    for (const p of pets) step(p, delta);
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

/** One pet's clock: proximity, the one-shot reactions, the eyes, and the study's status line. */
function step(p, deltaMs) {
  const rect = p.canvas.getBoundingClientRect();
  let distance = Infinity;
  if (pointer.active) {
    const scale = rect.width / p.pet.canvas;
    const at = { x: (pointer.x - rect.left) / scale, y: (pointer.y - rect.top) / scale };
    distance = distanceToBox(at, p.body);
  }
  const idle = p.engine.action === p.pet.roles.idle;
  const result = p.sense.update(deltaMs, distance, {
    idle,
    notice: layers.notice,
    dwell: layers.dwell,
    cooldown: layers.habit,
  });
  if (result.notice) p.engine.play(p.persona.notice);
  if (result.dwell) p.engine.play(p.persona.dwell);
  if (p.sense.greeted) document.body.classList.add('has-greeted');

  p.engine.update(deltaMs);
  p.gaze = gazeFor(p, rect, distance);
  p.renderer.drawPet(p.engine.state, [], {}, p.gaze);
  const next = stateLabel(p);
  if (next !== p.label) {
    p.label = next;
    p.state.textContent = next;
  }
  p.slot.classList.toggle('warm', p.sense.zone === 'gaze' && !p.sense.finished);
  p.slot.classList.toggle('hot', p.sense.zone === 'near' || p.sense.finished);
}

/** Eyes lead by one canvas pixel along the dominant axis; the body and face stay put. */
function gazeFor(p, rect, distance) {
  if (!layers.gaze || !p.sense.watching || distance === Infinity) return null;
  if (![p.pet.roles.idle, p.persona.notice, p.persona.dwell].includes(p.engine.action)) return null;
  const scale = rect.width / p.pet.canvas;
  const dx = (pointer.x - rect.left) / scale - p.eyes.x;
  const dy = (pointer.y - rect.top) / scale - p.eyes.y;
  if (Math.abs(dx) < 2 && Math.abs(dy) < 2) return null;
  const aim = [0, 0];
  if (Math.abs(dx) >= Math.abs(dy) * 1.15) aim[0] = Math.sign(dx);
  else if (Math.abs(dy) >= Math.abs(dx) * 1.15) aim[1] = Math.sign(dy);
  else return null;
  return aim;
}

function stateLabel(p) {
  const action = p.engine.action;
  if (action === p.persona.notice) return p.persona.noticeLabel;
  if (action === p.persona.dwell) return p.persona.dwellLabel;
  if (action !== p.pet.roles.idle) return '在忙';
  if (p.sense.quiet) return '刚刚打过招呼';
  if (p.sense.finished) return '心满意足';
  if (layers.notice && p.sense.greeted && p.sense.watching) return '注意到你';
  if (p.sense.zone === 'near') return '靠近了';
  if (p.sense.watching) return '看着你';
  return '在发呆';
}

function bindPointer() {
  window.addEventListener('pointermove', event => {
    pointer.x = event.clientX;
    pointer.y = event.clientY;
    pointer.active = true;
  }, { passive: true });
  document.addEventListener('pointerleave', () => { pointer.active = false; });
  window.addEventListener('blur', () => { pointer.active = false; });
}

function bindSwitches() {
  for (const button of document.querySelectorAll('[data-layer]')) {
    button.addEventListener('click', () => {
      const key = button.dataset.layer;
      layers[key] = !layers[key];
      button.setAttribute('aria-checked', String(layers[key]));
    });
  }
  const zones = document.getElementById('zones');
  zones.addEventListener('click', () => setZones(!document.body.classList.contains('zones')));
}

function setZones(on) {
  document.body.classList.toggle('zones', on);
  document.getElementById('zones').setAttribute('aria-checked', String(on));
}

function resetAll() {
  for (const p of pets) {
    p.engine.stop();
    p.sense.reset();
  }
}

/** Draws each pet's two rings from its ink bounds, in CSS pixels, so they survive a resize. */
function layoutZones() {
  for (const p of pets) {
    const rect = p.canvas.getBoundingClientRect();
    if (!rect.width) continue;
    const scale = rect.width / p.pet.canvas;
    const gaze = p.slot.querySelector('.ring.gaze');
    const near = p.slot.querySelector('.ring.near');
    placeRing(gaze, p.body, RINGS.gaze, scale);
    placeRing(near, p.body, RINGS.near, scale);
  }
}

function placeRing(element, box, radius, scale) {
  const pad = radius + EXIT_MARGIN;
  element.style.left = `${(box.left - pad) * scale}px`;
  element.style.top = `${(box.top - pad) * scale}px`;
  element.style.width = `${(box.right - box.left + pad * 2) * scale}px`;
  element.style.height = `${(box.bottom - box.top + pad * 2) * scale}px`;
}

/** Alpha bounds of one 88×88 frame, in canvas units. */
function inkBox(image) {
  const c = document.createElement('canvas');
  c.width = c.height = 88;
  const ctx = c.getContext('2d');
  ctx.drawImage(image, 0, 0, 88, 88, 0, 0, 88, 88);
  const data = ctx.getImageData(0, 0, 88, 88).data;
  let left = 88, top = 88, right = -1, bottom = -1;
  for (let y = 0; y < 88; y++) {
    for (let x = 0; x < 88; x++) {
      if (data[(y * 88 + x) * 4 + 3] === 0) continue;
      left = Math.min(left, x);
      top = Math.min(top, y);
      right = Math.max(right, x);
      bottom = Math.max(bottom, y);
    }
  }
  return { left, top, right: right + 1, bottom: bottom + 1, x: (left + right + 1) / 2, y: (top + bottom + 1) / 2 };
}

/** Moves a frame's ink bounds by the anchor the frame is drawn at. */
function fitBox(box, anchor = [0, 0]) {
  return {
    left: box.left + anchor[0], top: box.top + anchor[1],
    right: box.right + anchor[0], bottom: box.bottom + anchor[1],
    x: box.x + anchor[0], y: box.y + anchor[1],
  };
}

/** Shortest distance from a point to a box; zero inside it. */
function distanceToBox(point, box) {
  const dx = Math.max(box.left - point.x, 0, point.x - box.right);
  const dy = Math.max(box.top - point.y, 0, point.y - box.bottom);
  return Math.hypot(dx, dy);
}

/** The study's per-pet reaction table. */
const rows = ORDER.filter(id => PERSONA[id]).map(id => {
  const p = PERSONA[id];
  return `<div class="reaction">
    <div class="reaction-name"><strong>${p.name}</strong><span>${(p.noticeDelayMs / 1000).toFixed(2)}s / ${(p.dwellDelayMs / 1000).toFixed(1)}s</span></div>
    <div class="reaction-acts"><span>${p.noticeLabel}</span><i>→</i><span>${p.dwellLabel}</span></div>
  </div>`;
});
const table = document.getElementById('reactions');
if (table) table.innerHTML = rows.join('');

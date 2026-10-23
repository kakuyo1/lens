/**
 * Entrance and exit ceremony study: a pet walks in from a screen edge, looks around and settles; a swap walks the old
 * pet out and the new one in; turning the pet off walks it to the edge for a look back and a heart before it leaves.
 *
 * Demo scaffolding only. The pet, its actions and its movement style come from data/pet, served by Vite; the page
 * chooses the path and the beats. There is no new art: the farewell reuses look_around and encourage.
 */

import { assemblePet } from './pet-data.js';
import { PetRenderer } from './pet-renderer.js';

const SCALE = 3;
const PET_W = 88 * SCALE;
const PET_H = 88 * SCALE;
const PET_IDS = ['dog', 'mochi', 'owl', 'cat', 'ghost', 'jelly', 'cloud'];
const NAMES = {
  dog: '小狗',
  mochi: '麻糬',
  owl: '猫头鹰',
  cat: '短耳猫',
  ghost: '小幽灵',
  jelly: '小水母',
  cloud: '小云'
};
const GAITS = {
  trot: { bob: t => -Math.sin(Math.PI * t) * 2, ease: easeInOut, label: '小跑' },
  hop: { bob: t => -Math.sin(Math.PI * t) * 3, ease: easeOut, label: '蹦跳' },
  pounce: { bob: t => -Math.sin(Math.PI * t) * 4, ease: easeOut, label: '扑跳' },
  float: { bob: t => Math.sin(Math.PI * 2 * t) * 5, ease: easeInOut, label: '漂浮' },
  glide: { bob: () => 0, ease: easeInOut, label: '滑行' }
};

const params = new URLSearchParams(location.search);
const reducedMotion = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ?? false;
const speeds = [0.5, 1, 1.5];

const state = {
  loaded: false,
  selected: PET_IDS.includes(params.get('pet')) ? params.get('pet') : 'dog',
  on: false,
  busy: false,
  pendingOn: false,
  phase: 'off',
  step: -1,
  script: null,
  speed: speeds.includes(Number(params.get('speed'))) ? Number(params.get('speed')) : 1,
  side: ['auto', 'left', 'right'].includes(params.get('side')) ? params.get('side') : 'auto',
  instant: params.get('instant') === '1' || reducedMotion,
  switchLook: params.has('switchlook') ? params.get('switchlook') === '1' : true,
  activeId: null
};

const actors = new Map();
let active = null;
let timers = [];

const dom = {
  screen: document.querySelector('#screen'),
  petLayer: document.querySelector('#pet-layer'),
  live: document.querySelector('#live'),
  liveText: document.querySelector('#live-text'),
  homeMarker: document.querySelector('#home-marker'),
  emptyNote: document.querySelector('#empty-note'),
  loading: document.querySelector('#loading'),
  error: document.querySelector('#error'),
  workSize: document.querySelector('#work-size'),
  clockTime: document.querySelector('#clock-time'),
  clockDate: document.querySelector('#clock-date'),
  petGrid: document.querySelector('#pet-grid'),
  enabled: document.querySelector('#enabled'),
  instant: document.querySelector('#instant'),
  switchLook: document.querySelector('#switch-look'),
  showHome: document.querySelector('#show-home'),
  sideSeg: document.querySelector('#side-seg'),
  speedSeg: document.querySelector('#speed-seg'),
  replay: document.querySelector('#replay'),
  skip: document.querySelector('#skip'),
  status: document.querySelector('#status'),
  statusText: document.querySelector('#status-text'),
  scriptTitle: document.querySelector('#script-title'),
  scriptNote: document.querySelector('#script-note'),
  scriptCaption: document.querySelector('#script-caption'),
  timeline: document.querySelector('#timeline'),
  footStatus: document.querySelector('#foot-status')
};

const taskbar = document.querySelector('.taskbar');

function easeInOut(t) {
  return t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2;
}

function easeOut(t) {
  return 1 - (1 - t) ** 3;
}

function edgeName(side) {
  return side === 'left' ? '左边缘' : '右边缘';
}

function gaitOf(pet) {
  return GAITS[pet.movement?.style] ?? GAITS.glide;
}

function gaitLabel(pet) {
  return gaitOf(pet).label;
}

function actionFor(pet, role, fallback) {
  const name = pet.roles?.[role] ?? fallback;
  return name && pet.actions[name] ? name : null;
}

function geometry() {
  const w = dom.screen.clientWidth;
  const h = dom.screen.clientHeight;
  const workH = h - (taskbar?.offsetHeight ?? 46);
  const homeX = Math.round(Math.min(Math.max(w * 0.69 - PET_W / 2, 24), Math.max(0, w - PET_W - 24)));
  const homeY = Math.round(Math.min(Math.max(workH - PET_H - 16, 8), Math.max(8, workH - PET_H)));
  return { w, h, workH, homeX, homeY };
}

function resolveSide() {
  if (state.side === 'left' || state.side === 'right') return state.side;
  const g = geometry();
  return g.homeX + PET_W / 2 < g.w / 2 ? 'left' : 'right';
}

function offscreenX(side, width = PET_W) {
  return side === 'left' ? -width : geometry().w;
}

function edgeStopX(side) {
  return side === 'left' ? 10 : Math.max(10, geometry().w - PET_W - 10);
}

function after(ms, fn) {
  timers.push({ left: ms, fn });
}

function clearTimers() {
  timers = [];
}

function advanceTimers(dt) {
  const due = [];
  for (const timer of timers) {
    timer.left -= dt;
    if (timer.left <= 0) due.push(timer);
  }
  if (due.length === 0) return;
  timers = timers.filter(timer => !due.includes(timer));
  for (const timer of due) timer.fn();
}

class Actor {
  constructor(id, pet, renderer, canvas) {
    this.id = id;
    this.pet = pet;
    this.renderer = renderer;
    this.canvas = canvas;
    this.w = pet.canvas * SCALE;
    this.h = pet.height * SCALE;
    this.x = 0;
    this.y = 0;
    this.action = this.idleName;
    this.frame = 0;
    this.frameT = 0;
    this.walk = null;
    this.once = null;
    this.holding = false;
    this.blinkT = null;
    this.blinkClock = 0;
    this.blinkWait = 2800 + Math.random() * 3200;
  }

  get idleName() {
    return this.pet.roles.idle ?? 'idle';
  }

  get def() {
    return this.pet.actions[this.action];
  }

  show() {
    this.canvas.hidden = false;
  }

  hide() {
    this.canvas.hidden = true;
  }

  setAt(x, y) {
    this.x = x;
    this.y = y;
    this.canvas.style.transform = `translate3d(${Math.round(x)}px, ${Math.round(y)}px, 0)`;
  }

  setAction(name) {
    this.action = this.pet.actions[name] ? name : this.idleName;
    this.frame = 0;
    this.frameT = 0;
    this.holding = false;
    this.blinkT = null;
  }

  idle() {
    this.walk = null;
    this.once = null;
    this.setAction(this.idleName);
  }

  stop() {
    this.idle();
    this.blinkClock = 0;
  }

  play(name, done = null) {
    this.walk = null;
    this.setAction(name);
    this.once = { done };
  }

  startWalk(toX, toY = this.y, done = null) {
    const fromX = this.x;
    const fromY = this.y;
    const distance = Math.hypot(toX - fromX, toY - fromY);
    const duration = Math.min(2600, Math.max(850, (distance / 300) * 1000));
    const walkName = this.pet.movement?.walk && this.pet.actions[this.pet.movement.walk] ? this.pet.movement.walk : 'walk';
    const def = this.pet.actions[walkName];
    const cycleMs = def ? (def.frames * 1000) / def.fps : 750;
    const steps = Math.max(1, Math.round(duration / cycleMs));
    this.setAction(walkName);
    this.once = null;
    this.walk = {
      name: walkName,
      fromX,
      fromY,
      toX,
      toY,
      duration,
      elapsed: 0,
      steps,
      stepMs: duration / steps,
      gait: gaitOf(this.pet),
      done
    };
  }

  update(dt) {
    if (this.walk) {
      this.updateWalk(dt);
      return;
    }
    // A finished one-shot or walk is held on its last frame until the controller's next beat starts.
    if (this.holding) return;
    const def = this.def;
    if (!def) return;
    const frameMs = 1000 / def.fps;
    this.frameT += dt;
    if (this.once) {
      const total = def.frames * frameMs;
      this.frame = Math.min(def.frames - 1, Math.floor(this.frameT / frameMs));
      if (this.frameT >= total) {
        const done = this.once.done;
        this.once = null;
        this.holding = true;
        if (done) done();
        else this.idle();
        return;
      }
    } else {
      const total = def.frames * frameMs;
      if (this.frameT >= total) this.frameT %= total;
      this.frame = Math.min(def.frames - 1, Math.floor(this.frameT / frameMs));
    }
    this.updateBlink(dt, def);
  }

  updateWalk(dt) {
    const walk = this.walk;
    walk.elapsed = Math.min(walk.duration, walk.elapsed + dt);
    const stepIndex = Math.min(walk.steps - 1, Math.floor(walk.elapsed / walk.stepMs));
    const local = Math.min(1, (walk.elapsed - stepIndex * walk.stepMs) / walk.stepMs);
    const eased = walk.gait.ease(local);
    const x = walk.fromX + (walk.toX - walk.fromX) * ((stepIndex + eased) / walk.steps);
    const y = walk.fromY + (walk.toY - walk.fromY) * (walk.elapsed / walk.duration) + walk.gait.bob(local);
    this.setAt(x, y);
    const def = this.pet.actions[walk.name];
    if (def) this.frame = Math.min(def.frames - 1, Math.floor(local * def.frames));
    if (walk.elapsed >= walk.duration) {
      const done = walk.done;
      this.walk = null;
      this.holding = true;
      if (done) done();
      else this.idle();
    }
  }

  updateBlink(dt, def) {
    const idle = this.action === this.idleName;
    if (!idle || !def.blink || !this.pet.blink?.length) {
      if (!idle) this.blinkT = null;
      return;
    }
    if (this.blinkT === null) {
      this.blinkClock += dt;
      if (this.blinkClock >= this.blinkWait) {
        this.blinkClock = 0;
        this.blinkT = 0;
        this.blinkWait = 2800 + Math.random() * 3200;
      }
    } else {
      this.blinkT += dt;
      if (this.blinkT >= this.pet.blink.length * 80) this.blinkT = null;
    }
  }

  draw() {
    const def = this.def;
    if (!def) return;
    const frame = Math.min(this.frame, def.frames - 1);
    let expression = def.expressions?.[Math.min(frame, (def.expressions?.length ?? 1) - 1)] ?? 0;
    if (this.action === this.idleName && this.blinkT !== null && this.pet.blink?.length) {
      expression = this.pet.blink[Math.min(this.pet.blink.length - 1, Math.floor(this.blinkT / 80))];
    }
    const anchors = def.anchors?.[Math.min(frame, (def.anchors?.length ?? 1) - 1)] ?? { head: [0, 0], face: [0, 0], body: [0, 0] };
    this.renderer.clear();
    this.renderer.drawPet({ action: this.action, frame, expression, anchors }, []);
  }
}

/* ---------------------------------------------------------------- data */

async function fetchJson(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return res.json();
}

async function loadPet(id) {
  const files = Object.fromEntries(await Promise.all(
    ['pet', 'animations', 'anchors', 'accessories'].map(async name => [name, await fetchJson(`/${id}/${name}.json`)])
  ));
  return assemblePet(files);
}

function thumbFor(actor) {
  const idle = actor.pet.actions[actor.idleName];
  if (!idle) return null;
  const source = document.createElement('canvas');
  const renderer = new PetRenderer(source, { ...actor.pet, scale: 1 });
  renderer.images = actor.renderer.images;
  renderer.drawPet({
    action: actor.idleName,
    frame: 0,
    expression: idle.expressions?.[0] ?? 0,
    anchors: idle.anchors[0]
  }, []);

  const data = source.getContext('2d', { willReadFrequently: true })
    .getImageData(0, 0, source.width, source.height).data;
  let left = source.width;
  let top = source.height;
  let right = -1;
  let bottom = -1;
  for (let y = 0; y < source.height; y += 1) {
    for (let x = 0; x < source.width; x += 1) {
      if (data[(y * source.width + x) * 4 + 3] < 40) continue;
      if (x < left) left = x;
      if (x > right) right = x;
      if (y < top) top = y;
      if (y > bottom) bottom = y;
    }
  }
  if (right < left) return null;

  left = Math.max(0, left - 2);
  top = Math.max(0, top - 2);
  right = Math.min(source.width - 1, right + 2);
  bottom = Math.min(source.height - 1, bottom + 2);
  const cropW = right - left + 1;
  const cropH = bottom - top + 1;
  const size = 48;
  const fit = Math.min(size / cropW, size / cropH);
  const drawW = Math.max(1, Math.round(cropW * fit));
  const drawH = Math.max(1, Math.round(cropH * fit));
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  ctx.drawImage(
    source,
    left, top, cropW, cropH,
    Math.round((size - drawW) / 2), Math.round((size - drawH) / 2), drawW, drawH
  );
  return canvas;
}

function makePetButton(actor) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'pet-btn';
  button.dataset.pet = actor.id;
  button.title = `${NAMES[actor.id] ?? actor.id} · ${gaitLabel(actor.pet)}`;
  button.setAttribute('aria-pressed', String(actor.id === state.selected));

  const portrait = thumbFor(actor);
  if (portrait) {
    portrait.setAttribute('aria-hidden', 'true');
    button.append(portrait);
  }
  const name = document.createElement('span');
  name.className = 'name';
  name.textContent = NAMES[actor.id] ?? actor.id;
  const gait = document.createElement('span');
  gait.className = 'gait';
  gait.textContent = gaitLabel(actor.pet);
  button.append(name, gait);
  return button;
}

async function build() {
  const results = await Promise.allSettled(PET_IDS.map(async id => {
    const pet = await loadPet(id);
    const canvas = document.createElement('canvas');
    canvas.className = 'pet-canvas';
    canvas.hidden = true;
    canvas.setAttribute('aria-hidden', 'true');
    dom.petLayer.append(canvas);
    const renderer = new PetRenderer(canvas, { ...pet, scale: SCALE });
    try {
      await renderer.load(path => `/${id}/${path}`);
    } catch (error) {
      canvas.remove();
      throw error;
    }
    return new Actor(id, pet, renderer, canvas);
  }));

  for (const result of results) {
    if (result.status === 'fulfilled') actors.set(result.value.id, result.value);
    else console.warn('a pet did not load:', result.reason);
  }
  if (actors.size === 0) throw new Error('no pets loaded');
  if (!actors.has(state.selected)) state.selected = actors.keys().next().value;

  for (const id of PET_IDS) {
    const actor = actors.get(id);
    if (actor) dom.petGrid.append(makePetButton(actor));
  }
  state.loaded = true;
}

/* ---------------------------------------------------------------- script strip */

const SCRIPTS = {
  enter: {
    title: '开启 · 入场',
    steps: ['从边缘走入', '看看四周', '站定'],
    note: side => `从${edgeName(side)}走到落点，张望一轮，然后站定。`
  },
  switch: {
    title: '换宠 · 一进一出',
    steps: look => (look ? ['旧宠离场', '新宠走入', '看看四周', '站定'] : ['旧宠离场', '新宠走入', '站定']),
    note: (side, look) => `旧宠从${edgeName(side)}离开，新宠从同一边走进；${look ? '新宠也张望一轮。' : '新宠直接站定，不再张望。'}`
  },
  leave: {
    title: '关闭 · 告辞',
    steps: ['走到边上', '回头告辞', '走出屏幕'],
    note: side => `走到${edgeName(side)}，回头看看，送一颗心，再走出屏幕。`
  },
  instantOn: {
    title: '瞬时显隐 · 现状',
    steps: ['直接出现'],
    note: '没有走入与张望，直接出现在落点。'
  },
  instantOff: {
    title: '瞬时显隐 · 现状',
    steps: ['直接消失'],
    note: '没有告辞，直接从落点消失。'
  },
  instantSwitch: {
    title: '瞬时显隐 · 现状',
    steps: ['直接更换'],
    note: '没有一进一出，新宠直接出现在落点。'
  }
};

function setScript(kind, side = resolveSide(), { look = state.switchLook } = {}) {
  const spec = SCRIPTS[kind] ?? SCRIPTS.enter;
  const lookOn = kind === 'switch' ? look : undefined;
  const steps = typeof spec.steps === 'function' ? spec.steps(lookOn) : spec.steps;
  state.script = { kind, steps: [...steps], look: lookOn };
  state.step = -1;
  dom.scriptTitle.textContent = spec.title;
  dom.scriptNote.textContent = typeof spec.note === 'function' ? spec.note(side, lookOn) : spec.note;
  dom.timeline.style.gridTemplateColumns = `repeat(${steps.length}, 1fr)`;
  dom.timeline.replaceChildren(...steps.map((text, index) => {
    const li = document.createElement('li');
    const idx = document.createElement('span');
    idx.className = 'idx';
    idx.textContent = String(index + 1).padStart(2, '0');
    const label = document.createElement('span');
    label.className = 'txt';
    label.textContent = text;
    li.append(idx, label);
    return li;
  }));
  renderTimeline();
}

function renderTimeline() {
  [...dom.timeline.children].forEach((li, index) => {
    const done = state.step >= 0 && index <= state.step && (!state.busy || index < state.step);
    li.classList.toggle('active', state.busy && index === state.step);
    li.classList.toggle('done', done);
  });
}

function setLive(text, busy, caption = undefined) {
  if (text) {
    dom.live.hidden = false;
    dom.live.dataset.busy = String(busy);
    dom.liveText.textContent = text;
    dom.scriptCaption.textContent = caption ?? text;
  } else {
    dom.live.hidden = true;
    if (caption !== undefined) dom.scriptCaption.textContent = caption;
  }
}

function setPhase(phase, { busy, step, live, caption } = {}) {
  state.phase = phase;
  if (busy !== undefined) state.busy = busy;
  if (step !== undefined) state.step = step;
  dom.screen.dataset.phase = phase;
  if (live !== undefined) setLive(live, state.busy, caption);
  else if (caption !== undefined) dom.scriptCaption.textContent = caption;
  syncControls();
}

/* ---------------------------------------------------------------- controls */

function statusFor() {
  if (!state.loaded) return { text: '载入中', kind: 'off' };
  if (state.busy) {
    if (state.script?.kind === 'leave') return { text: '告辞中', kind: 'busy' };
    if (state.script?.kind === 'switch') return { text: '换宠中', kind: 'busy' };
    return { text: '入场中', kind: 'busy' };
  }
  if (state.instant) return { text: state.on ? '瞬时 · 开' : '瞬时 · 关', kind: 'instant' };
  return { text: state.on ? '已开启' : '已关闭', kind: state.on ? 'on' : 'off' };
}

function syncControls() {
  const status = statusFor();
  dom.status.dataset.state = status.kind;
  dom.statusText.textContent = status.text;
  dom.enabled.checked = state.on;
  dom.instant.checked = state.instant;
  dom.switchLook.checked = state.switchLook;
  dom.skip.hidden = !state.busy;

  for (const button of dom.sideSeg.querySelectorAll('button')) {
    button.setAttribute('aria-pressed', String(button.dataset.side === state.side));
  }
  for (const button of dom.speedSeg.querySelectorAll('button')) {
    button.setAttribute('aria-pressed', String(Number(button.dataset.speed) === state.speed));
  }
  for (const button of dom.petGrid.querySelectorAll('.pet-btn')) {
    button.setAttribute('aria-pressed', String(button.dataset.pet === state.selected));
  }

  const actor = actors.get(state.selected);
  let name = NAMES[state.selected] ?? state.selected;
  let gait = actor ? ` · ${gaitLabel(actor.pet)}` : '';
  if (state.busy && state.script?.kind === 'switch' && state.activeId && state.activeId !== state.selected) {
    const leaving = actors.get(state.activeId);
    name = `${NAMES[state.activeId] ?? state.activeId} → ${name}`;
    gait = leaving ? ` · ${gaitLabel(leaving.pet)}` : '';
  }
  dom.footStatus.textContent = `${name}${gait} · ${status.text} · ${state.speed}×`;
  dom.emptyNote.hidden = !state.loaded || state.on || state.busy;
  renderTimeline();
}

function updateMarkers() {
  if (!state.loaded) return;
  const g = geometry();
  dom.workSize.textContent = `工作区 ${g.w} × ${g.workH}`;
  dom.homeMarker.style.left = `${g.homeX + PET_W / 2}px`;
  dom.homeMarker.style.top = `${g.homeY + PET_H}px`;
  dom.emptyNote.style.left = `${g.homeX + PET_W / 2}px`;
  dom.homeMarker.hidden = !dom.showHome.checked;
}

function hideAll() {
  for (const actor of actors.values()) actor.hide();
}

function placeAtHome(actor) {
  hideAll();
  const g = geometry();
  actor.stop();
  actor.setAt(g.homeX, g.homeY);
  actor.show();
  actor.draw();
  active = actor;
  state.activeId = actor.id;
}

function playIf(actor, name, done) {
  if (actor.pet.actions[name]) actor.play(name, done);
  else after(0, done);
}

function finishIdle(actor, caption = '站定') {
  if (state.activeId !== actor.id) return;
  const g = geometry();
  if (Math.abs(actor.x - g.homeX) > 2 || Math.abs(actor.y - g.homeY) > 2) actor.setAt(g.homeX, g.homeY);
  actor.idle();
  setPhase('here', { busy: false, step: Math.max(0, (state.script?.steps.length ?? 3) - 1), live: caption });
}

/* ---------------------------------------------------------------- ceremonies */

function interrupt() {
  const wasBusy = state.busy;
  clearTimers();
  if (!wasBusy) return;
  for (const actor of actors.values()) actor.stop();
  const keep = state.pendingOn;
  state.busy = false;
  const last = Math.max(0, (state.script?.steps.length ?? 1) - 1);
  if (keep && actors.has(state.selected)) {
    const actor = actors.get(state.selected);
    placeAtHome(actor);
    state.on = true;
    state.phase = 'here';
    dom.screen.dataset.phase = 'here';
    state.step = last;
    setLive('站定', false);
  } else {
    hideAll();
    active = null;
    state.activeId = null;
    state.on = false;
    state.pendingOn = false;
    state.phase = 'off';
    dom.screen.dataset.phase = 'off';
    state.step = last;
    setLive(null, false, '已关闭');
  }
  syncControls();
}

function turnOn({ instant = state.instant, look = true } = {}) {
  if (!state.loaded) return;
  interrupt();
  clearTimers();
  const actor = actors.get(state.selected);
  if (!actor) return;

  state.on = true;
  state.pendingOn = true;
  state.busy = false;

  if (instant) {
    setScript('instantOn');
    placeAtHome(actor);
    setPhase('here', { busy: false, step: 0, live: '站定', caption: '瞬时出现 · 没有走入与张望' });
    return;
  }

  if (active) active.hide();
  active = null;
  state.activeId = null;
  setScript('enter', resolveSide());
  const side = resolveSide();
  const g = geometry();
  hideAll();
  actor.stop();
  actor.setAt(offscreenX(side), g.homeY);
  actor.show();
  active = actor;
  state.activeId = actor.id;
  setPhase('enter', { busy: true, step: 0, live: `从${edgeName(side)}走入` });

  actor.startWalk(g.homeX, g.homeY, () => {
    if (state.activeId !== actor.id) return;
    const lookName = actionFor(actor.pet, 'look_around', 'look_around');
    if (look && lookName) {
      setPhase('look', { busy: true, step: 1, live: '看看四周' });
      actor.play(lookName, () => {
        if (state.activeId !== actor.id) return;
        setPhase('settle', { busy: true, step: 2, live: '站定' });
        after(180, () => finishIdle(actor));
      });
    } else {
      setPhase('settle', { busy: true, step: 2, live: '站定' });
      after(140, () => finishIdle(actor));
    }
  });
}

function turnOff({ instant = state.instant } = {}) {
  if (!state.loaded) return;
  // Closing catches the pet where it is, so turning off during an entrance or a swap does not snap it home first.
  clearTimers();
  if (state.busy) {
    for (const actor of actors.values()) actor.stop();
    state.busy = false;
  }
  if (active && state.activeId && state.activeId !== state.selected) state.selected = state.activeId;
  const actor = active;
  state.on = false;
  state.pendingOn = false;

  if (!actor) {
    state.activeId = null;
    state.phase = 'off';
    dom.screen.dataset.phase = 'off';
    hideAll();
    setLive(null, false, '宠物已关闭');
    syncControls();
    return;
  }

  if (instant) {
    setScript('instantOff');
    hideAll();
    active = null;
    state.activeId = null;
    setPhase('off', { busy: false, step: 0, live: null, caption: '瞬时消失 · 没有告辞' });
    return;
  }

  const side = resolveSide();
  const g = geometry();
  setScript('leave', side);
  setPhase('leave', { busy: true, step: 0, live: `走到${edgeName(side)}` });

  actor.startWalk(edgeStopX(side), g.homeY, () => {
    if (state.activeId !== actor.id) return;
    setPhase('bye', { busy: true, step: 1, live: '回头看看' });
    playIf(actor, actionFor(actor.pet, 'look_around', 'look_around'), () => {
      if (state.activeId !== actor.id) return;
      after(120, () => {
        if (state.activeId !== actor.id) return;
        setLive('送一颗心', true);
        playIf(actor, actionFor(actor.pet, 'encourage', 'encourage'), () => {
          if (state.activeId !== actor.id) return;
          after(240, () => {
            if (state.activeId !== actor.id) return;
            setPhase('exit', { busy: true, step: 2, live: '走出屏幕' });
            actor.startWalk(offscreenX(side), g.homeY, () => {
              hideAll();
              active = null;
              state.activeId = null;
              setPhase('off', { busy: false, step: 2, live: null, caption: '已关闭 · 下次开启会重新走入' });
            });
          });
        });
      });
    });
  });
}

function switchTo(id) {
  if (!state.loaded) return;
  const next = actors.get(id);
  if (!next) return;
  if (id === state.selected && !state.busy) return;
  interrupt();
  clearTimers();
  if (id === state.selected && !state.busy) {
    syncControls();
    return;
  }
  const wasOn = state.on;
  state.selected = id;

  if (!wasOn) {
    setScript('enter', resolveSide());
    setPhase('off', { busy: false, step: -1, live: null, caption: `已选 ${NAMES[id] ?? id} · 打开开关后从边缘走入` });
    return;
  }

  if (state.instant) {
    setScript('instantSwitch');
    placeAtHome(next);
    setPhase('here', { busy: false, step: 0, live: '站定', caption: '瞬时更换 · 没有一进一出' });
    return;
  }

  if (!active) {
    turnOn({ instant: false, look: true });
    return;
  }

  const old = active;
  const side = resolveSide();
  const g = geometry();
  const lookName = actionFor(next.pet, 'look_around', 'look_around');
  const lookOnArrival = state.switchLook && Boolean(lookName);
  setScript('switch', side, { look: lookOnArrival });
  setPhase('switch-out', { busy: true, step: 0, live: '旧宠走到边上' });

  old.startWalk(offscreenX(side), g.homeY, () => {
    if (state.activeId !== old.id) return;
    old.hide();
    if (active === old) active = null;
    next.stop();
    next.setAt(offscreenX(side), g.homeY);
    next.show();
    active = next;
    state.activeId = next.id;
    setPhase('switch-in', { busy: true, step: 1, live: '新宠走入' });

    next.startWalk(g.homeX, g.homeY, () => {
      if (state.activeId !== next.id) return;
      if (lookOnArrival) {
        setPhase('switch-look', { busy: true, step: 2, live: '新宠看看四周' });
        next.play(lookName, () => after(140, () => finishIdle(next)));
      } else {
        setPhase('settle', { busy: true, step: Math.max(0, state.script.steps.length - 1), live: '站定' });
        after(140, () => finishIdle(next));
      }
    });
  });
}

function replay() {
  if (!state.loaded) return;
  interrupt();
  clearTimers();
  if (active) {
    active.hide();
    active = null;
    state.activeId = null;
  }
  state.on = true;
  state.pendingOn = true;
  turnOn({ instant: state.instant, look: true });
}

function setCaption(text) {
  dom.scriptCaption.textContent = text;
}

/* ---------------------------------------------------------------- wiring */

function bind() {
  dom.enabled.addEventListener('change', () => {
    if (dom.enabled.checked) turnOn();
    else turnOff();
  });

  dom.petGrid.addEventListener('click', event => {
    const button = event.target.closest('.pet-btn');
    if (button) switchTo(button.dataset.pet);
  });

  dom.sideSeg.addEventListener('click', event => {
    const button = event.target.closest('button');
    if (!button) return;
    state.side = button.dataset.side;
    if (!state.busy) {
      if (!state.on || state.script?.kind === 'enter') {
        setScript('enter', resolveSide());
        state.step = state.on ? state.script.steps.length - 1 : -1;
        renderTimeline();
      }
    }
    syncControls();
  });

  dom.speedSeg.addEventListener('click', event => {
    const button = event.target.closest('button');
    if (!button) return;
    state.speed = Number(button.dataset.speed);
    syncControls();
  });

  dom.instant.addEventListener('change', () => {
    state.instant = dom.instant.checked;
    if (state.instant && state.busy) interrupt();
    syncControls();
  });

  dom.switchLook.addEventListener('change', () => {
    state.switchLook = dom.switchLook.checked;
    if (!state.busy && state.script?.kind === 'switch') {
      setScript('switch', resolveSide(), { look: state.switchLook });
      state.step = state.on ? state.script.steps.length - 1 : -1;
      renderTimeline();
    }
    syncControls();
  });

  dom.showHome.addEventListener('change', updateMarkers);

  dom.replay.addEventListener('click', replay);
  dom.skip.addEventListener('click', () => {
    interrupt();
    setCaption(state.pendingOn ? '已跳过仪式 · 站定' : '已跳过仪式 · 已关闭');
  });

  // A resize while a ceremony runs must not abort it: the scrollbar that a growing panel adds is a resize too.
  // The ceremony finishes on its old path; the next one uses the new stage.
  let lastStage = { width: 0, height: 0 };
  const observer = new ResizeObserver(() => {
    if (!state.loaded) return;
    const g = geometry();
    if (g.w === lastStage.width && g.h === lastStage.height) return;
    lastStage = { width: g.w, height: g.h };
    updateMarkers();
    if (!state.busy && active) {
      active.stop();
      active.setAt(g.homeX, g.homeY);
      active.draw();
    }
  });
  observer.observe(dom.screen);
}

function updateClock() {
  const now = new Date();
  dom.clockTime.textContent = `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`;
  dom.clockDate.textContent = `${String(now.getMonth() + 1).padStart(2, '0')}/${String(now.getDate()).padStart(2, '0')}`;
}

function showError(error) {
  console.error(error);
  dom.loading.hidden = true;
  dom.error.hidden = false;
  dom.status.dataset.state = 'off';
  dom.statusText.textContent = '载入失败';
  dom.footStatus.textContent = '载入失败';
  if (window.__ceremony) window.__ceremony.error = String(error);
}

/* ---------------------------------------------------------------- loop */

let last = performance.now();

function tick(now) {
  const raw = Math.max(0, Math.min(now - last, 100));
  last = now;
  const dt = raw * state.speed;
  advanceTimers(dt);
  if (active) active.update(dt);
  active?.draw();
  requestAnimationFrame(tick);
}

async function init() {
  bind();
  updateClock();
  setInterval(updateClock, 1000);

  if (location.protocol === 'file:') {
    showError(new Error('file:// cannot fetch data/pet'));
    return;
  }

  try {
    await build();
  } catch (error) {
    showError(error);
    return;
  }

  dom.loading.hidden = true;
  updateMarkers();
  setScript('enter', resolveSide());
  setPhase('off', { busy: false, step: -1, live: null, caption: '宠物已关闭 · 打开开关，它会从边缘走进来' });
  syncControls();
  window.__ceremony.ready = true;

  if (params.get('auto') !== '0') {
    setTimeout(() => {
      if (state.loaded && !state.on) turnOn({ look: true });
    }, 450);
  }
}

window.__ceremony = {
  state,
  actors,
  ready: false,
  error: null,
  get active() {
    return active;
  },
  api: {
    turnOn,
    turnOff,
    switchTo,
    replay,
    interrupt,
    setSide(value) {
      if (!['auto', 'left', 'right'].includes(value)) return;
      state.side = value;
      syncControls();
    },
    setSpeed(value) {
      if (!speeds.includes(Number(value))) return;
      state.speed = Number(value);
      syncControls();
    },
    setInstant(value) {
      state.instant = Boolean(value);
      if (state.instant && state.busy) interrupt();
      syncControls();
    }
  }
};

requestAnimationFrame(tick);
init().catch(error => showError(error));

/**
 * The walk-cycle study: seven pets, each with its own new walk and edge-peek actions, moving around a small home area.
 *
 * The page is demo scaffolding. It reads each pet's normal data from data/pet (served by Vite): the walk and
 * edge-peek actions and the movement block are part of animations.json, the same files the app loads.
 *
 * The movement model is a compressed version of the planned idle wander: a home near the ground, an ellipse around
 * it, one walk cycle per step, a pause, and a long wait between strolls. The real app target is 120–300 s between
 * strolls; here the waits are shortened so the gaits can be compared by eye.
 *
 * At the left or right stage edge a stroll has a 70% chance to become a hide: the pet slides mostly out of the
 * stage and plays edge_peek. Nothing ends that state on its own; the pointer has to drag the pet back inside.
 */

import { assemblePet } from './pet-data.js';
import { PetRenderer } from './pet-renderer.js';

const SCALE = 3;
const HIDE_CHANCE = 0.7;
const HIDE_VISIBLE = 0.34;
const PEEK_TRAVEL = 30;
const PEEK_CYCLE_MS = 3600;
const PET_IDS = ['dog', 'mochi', 'owl', 'cat', 'ghost', 'jelly', 'cloud'];
const NAMES = {
  dog: '小狗',
  mochi: '麻薯',
  owl: '猫头鹰',
  cat: '猫',
  ghost: '幽灵',
  jelly: '水母',
  cloud: '云',
};
const GAIT_LABEL = {
  trot: 'trot 小跑',
  hop: 'hop 蹦跳',
  pounce: 'pounce 扑',
  float: 'float 漂浮',
  glide: 'glide 滑行',
};
const VERTICAL_RATIO = { float: 0.32, hop: 0.12, trot: 0.10, pounce: 0.06, glide: 0.18 };

const demo = { playing: true, speed: 1, inPlace: false };
const actors = [];

const grid = document.querySelector('#grid');
const status = document.querySelector('#status');
const toggle = document.querySelector('#toggle');
const speed = document.querySelector('#speed');
const inPlace = document.querySelector('#in-place');
const rangeToggle = document.querySelector('#range');
const peekButton = document.querySelector('#peek');
const reviewPetSelect = document.querySelector('#review-pet');
const reviewLeftButton = document.querySelector('#review-left');
const reviewRightButton = document.querySelector('#review-right');
let reviewDemo = null;

async function fetchJson(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return res.json();
}

async function loadPet(id) {
  const names = ['pet', 'animations', 'anchors', 'accessories'];
  const files = Object.fromEntries(await Promise.all(names.map(async name => [name, await fetchJson(`/${id}/${name}.json`)])));
  return assemblePet(files);
}

/// The movement block turned into the actions this page plays.
function walkOf(pet) {
  const movement = pet.movement;
  if (!movement) throw new Error('this pet has no movement block');
  return {
    gait: movement.style,
    range: movement.range,
    action: pet.actions[movement.walk],
    peek: movement.peek ? pet.actions[movement.peek] : undefined,
  };
}

class Walker {
  constructor({ id, pet, walk, renderer, canvas, stage, homeDot, ring, note }) {
    this.id = id;
    this.pet = pet;
    this.walk = walk;
    this.renderer = renderer;
    this.canvas = canvas;
    this.stage = stage;
    this.homeDot = homeDot;
    this.ring = ring;
    this.note = note;
    this.walkNote = note?.textContent ?? '';
    this.gait = walk.gait;
    this.petW = pet.canvas * SCALE;
    this.petH = pet.height * SCALE;
    const ink = this.measureInk();
    this.inkLeft = ink.left;
    this.inkRight = ink.right;
    this.inkWidth = Math.max(1, ink.right - ink.left);
    this.walkCycleMs = (walk.action.frames * 1000) / walk.action.fps;
    this.action = 'idle';
    this.frame = 0;
    this.frameElapsed = 0;
    this.mode = 'wander';
    this.hidingSide = null;
    this.hideFrom = { x: 0, y: 0 };
    this.hideBaseX = 0;
    this.hideY = 0;
    this.hideSlide = 0;
    this.hideSlideMs = 260;
    this.peekClock = 0;
    this.dragging = false;
    this.dragPointer = null;
    this.dragStart = null;
    this.layout();
    this.pos = { ...this.home };
    this.phase = 'wait';
    this.phaseLeft = 1800 + Math.random() * 2500;
    this.stepsLeft = 0;
    this.from = { ...this.home };
    this.to = { ...this.home };
    this.stepMs = this.walkCycleMs;
    this.stepElapsed = 0;
    this.updateTransform();
    this.bindPointer();
    new ResizeObserver(() => this.layout()).observe(this.stage);
  }

  layout() {
    const w = this.stage.clientWidth;
    const h = this.stage.clientHeight;
    this.home = {
      x: Math.max(0, Math.round((w - this.petW) / 2)),
      y: Math.max(0, Math.round(h - this.petH - 28)),
    };
    this.radius = (this.petW * this.walk.range) / 100;
    this.vertical = this.radius * (VERTICAL_RATIO[this.gait] ?? 0.2);
    if (this.mode === 'hiding') {
      this.hideBaseX = this.hiddenBaseX();
      this.hideY = Math.min(Math.max(0, this.hideY || this.home.y), this.maxY());
      if (!this.dragging) {
        const peek = this.peekOffset();
        this.pos = {
          x: this.hideBaseX + (this.hidingSide === 'left' ? peek : -peek),
          y: this.hideY,
        };
        this.updateTransform();
      }
    } else if (this.pos) {
      this.pos = this.clamped(this.pos);
      this.updateTransform();
    }
    this.placeHome();
  }

  maxX() {
    return Math.max(0, this.stage.clientWidth - this.petW);
  }

  maxY() {
    return Math.max(0, this.stage.clientHeight - this.petH);
  }

  /** @return A position clamped to the stage and to the home ellipse. */
  clamped(at) {
    const maxX = this.maxX();
    const maxY = this.maxY();
    let x = Math.min(Math.max(0, at.x), maxX);
    let y = Math.min(Math.max(0, at.y), maxY);
    const dx = (x - this.home.x) / Math.max(1, this.radius);
    const dy = (y - this.home.y) / Math.max(1, this.vertical);
    const length = Math.hypot(dx, dy);
    if (length > 1) {
      x = this.home.x + (x - this.home.x) / length;
      y = this.home.y + (y - this.home.y) / length;
    }
    return { x: Math.min(Math.max(0, x), maxX), y: Math.min(Math.max(0, y), maxY) };
  }

  /** The drag may take the pet outside the stage; it is allowed a window's width either way. */
  clampForDrag(at) {
    return {
      x: Math.min(Math.max(-this.petW, at.x), this.stage.clientWidth),
      y: Math.min(Math.max(0, at.y), this.maxY()),
    };
  }

  /**
   * The hidden window's left edge. The fraction is measured on the figure's ink, not the square canvas, so the
   * dog's wide transparent margin does not make it hide much deeper than the ghost.
   */
  hiddenBaseX() {
    const visible = this.inkWidth * HIDE_VISIBLE;
    return this.hidingSide === 'left'
      ? visible - this.inkRight
      : this.maxX() + (this.petW - this.inkLeft - visible);
  }

  /** @return The first edge-peek frame's drawn pixels in display px; falls back to the whole canvas. */
  measureInk() {
    const def = this.pet.actions.edge_peek;
    const image = def ? this.renderer.images.get(def.sheet) : null;
    if (!image || !this.renderer.ctx) return { left: 0, right: this.petW };
    const probe = document.createElement('canvas');
    probe.width = this.pet.canvas;
    probe.height = this.pet.canvas;
    const ctx = probe.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(image, 0, 0, this.pet.canvas, this.pet.canvas, 0, 0, this.pet.canvas, this.pet.canvas);
    const data = ctx.getImageData(0, 0, probe.width, probe.height).data;
    let left = probe.width;
    let right = -1;
    for (let y = 0; y < probe.height; y += 1) {
      for (let x = 0; x < probe.width; x += 1) {
        if (data[(y * probe.width + x) * 4 + 3] < 40) continue;
        if (x < left) left = x;
        if (x > right) right = x;
      }
    }
    if (right < left) return { left: 0, right: this.petW };
    return { left: left * SCALE, right: (right + 1) * SCALE };
  }

  peekOffset() {
    if (this.hideSlide < this.hideSlideMs) return 0;
    const phase = (this.peekClock / PEEK_CYCLE_MS) * Math.PI * 2;
    return (0.5 - 0.5 * Math.cos(phase)) * PEEK_TRAVEL;
  }

  updateTransform() {
    this.canvas.style.transform = `translate(${this.pos.x}px, ${this.pos.y}px)`;
  }

  placeHome() {
    const cx = this.home.x + this.petW / 2;
    const cy = this.home.y + this.petH;
    this.homeDot.style.left = `${cx}px`;
    this.homeDot.style.top = `${cy}px`;
    const rx = this.radius + this.petW / 2;
    const ry = this.vertical + this.petH / 2;
    this.ring.style.left = `${cx - rx}px`;
    this.ring.style.top = `${cy - ry}px`;
    this.ring.style.width = `${rx * 2}px`;
    this.ring.style.height = `${ry * 2}px`;
  }

  setAction(name) {
    if (this.action === name) return;
    this.action = name;
    this.frame = 0;
    this.frameElapsed = 0;
  }

  advanceAnimation(dt) {
    const def = this.pet.actions[this.action];
    if (!def) return;
    this.frameElapsed += dt;
    const frameMs = 1000 / def.fps;
    while (this.frameElapsed >= frameMs) {
      this.frameElapsed -= frameMs;
      this.frame = (this.frame + 1) % def.frames;
    }
  }

  nextStep() {
    for (let attempt = 0; attempt < 10; attempt += 1) {
      const angle = Math.random() * Math.PI * 2;
      const distance = this.radius * (0.35 + Math.random() * 0.4);
      const target = this.clamped({
        x: this.pos.x + Math.cos(angle) * distance,
        y: this.pos.y + Math.sin(angle) * distance * 0.55,
      });
      const travelled = Math.hypot(target.x - this.pos.x, target.y - this.pos.y);
      if (travelled < 10) continue;
      this.from = { ...this.pos };
      this.to = target;
      this.stepMs = this.walkCycleMs;
      this.stepElapsed = 0;
      this.phase = 'step';
      this.setAction('walk');
      return true;
    }
    return false;
  }

  finishStroll() {
    this.phase = 'wait';
    // Demo-compressed cadence. The app target is 120–300 s between strolls; this keeps the page watchable.
    const waits = { trot: [8, 16], hop: [10, 20], pounce: [14, 28], float: [12, 26], glide: [10, 22] };
    const [low, high] = waits[this.gait] ?? waits.glide;
    this.phaseLeft = (low + Math.random() * (high - low)) * 1000;
    this.setAction('idle');
  }

  edgeAt() {
    if (this.pos.x <= 1) return 'left';
    if (this.pos.x >= this.maxX() - 1) return 'right';
    return null;
  }

  enterHide(side) {
    if (!this.pet.actions.edge_peek) return false;
    this.mode = 'hiding';
    this.hidingSide = side;
    this.phase = 'hidden';
    this.stepsLeft = 0;
    this.hideFrom = { ...this.pos };
    this.hideSlide = 0;
    this.hideSlideMs = 260;
    this.peekClock = 0;
    this.hideBaseX = this.hiddenBaseX();
    this.hideY = Math.min(Math.max(0, this.pos.y), this.maxY());
    this.setAction('edge_peek');
    this.canvas.style.cursor = 'grab';
    this.stage.classList.add('hiding');
    if (this.note) {
      this.note.classList.add('hiding');
      this.note.textContent = `在${side === 'left' ? '左' : '右'}边缘偷看 · 拖出来才结束`;
    }
    return true;
  }

  updateHiding(dt) {
    this.peekClock += dt;
    if (this.dragging) return;
    if (this.hideSlide < this.hideSlideMs) {
      this.hideSlide = Math.min(this.hideSlideMs, this.hideSlide + dt);
      const t = this.hideSlide / this.hideSlideMs;
      const eased = 1 - (1 - t) ** 3;
      this.pos = {
        x: this.hideFrom.x + (this.hideBaseX - this.hideFrom.x) * eased,
        y: this.hideFrom.y + (this.hideY - this.hideFrom.y) * eased,
      };
    } else {
      const peek = this.peekOffset();
      this.pos = {
        x: this.hideBaseX + (this.hidingSide === 'left' ? peek : -peek),
        y: this.hideY,
      };
    }
    this.updateTransform();
  }

  exitHide() {
    const dropped = {
      x: Math.min(Math.max(0, this.pos.x), this.maxX()),
      y: Math.min(Math.max(0, this.pos.y), this.maxY()),
    };
    this.mode = 'wander';
    this.hidingSide = null;
    this.dragging = false;
    this.stage.classList.remove('hiding');
    this.canvas.style.cursor = 'default';
    if (this.note) {
      this.note.classList.remove('hiding');
      this.note.textContent = this.walkNote;
    }
    this.home = dropped;
    this.pos = { ...dropped };
    this.phase = 'wait';
    this.phaseLeft = 2500 + Math.random() * 3000;
    this.setAction('idle');
    this.placeHome();
    this.updateTransform();
  }

  snapBackToHide() {
    this.pos = { x: this.hideBaseX, y: this.hideY };
    this.hideSlide = this.hideSlideMs;
    this.peekClock = 0;
    this.updateTransform();
  }

  hideAtNearestEdge() {
    if (this.mode === 'hiding') return false;
    const side = this.pos.x + this.petW / 2 < this.stage.clientWidth / 2 ? 'left' : 'right';
    this.pos.x = side === 'left' ? 0 : this.maxX();
    this.pos.y = Math.min(Math.max(0, this.pos.y), this.maxY());
    this.updateTransform();
    return this.enterHide(side);
  }

  bindPointer() {
    this.canvas.addEventListener('pointerdown', event => {
      if (this.mode !== 'hiding') return;
      event.preventDefault();
      this.dragging = true;
      this.dragMoved = false;
      this.dragPointer = event.pointerId;
      this.dragStart = { x: event.clientX, y: event.clientY, pos: { ...this.pos } };
      this.canvas.setPointerCapture(event.pointerId);
      this.canvas.style.cursor = 'grabbing';
    });
    this.canvas.addEventListener('pointermove', event => {
      if (!this.dragging || event.pointerId !== this.dragPointer) return;
      const dx = event.clientX - this.dragStart.x;
      const dy = event.clientY - this.dragStart.y;
      if (Math.hypot(dx, dy) > 4) this.dragMoved = true;
      this.pos = this.clampForDrag({
        x: this.dragStart.pos.x + dx,
        y: this.dragStart.pos.y + dy,
      });
      this.updateTransform();
    });
    const release = event => {
      if (!this.dragging || event.pointerId !== this.dragPointer) return;
      this.dragging = false;
      if (this.canvas.hasPointerCapture?.(event.pointerId)) this.canvas.releasePointerCapture(event.pointerId);
      this.canvas.style.cursor = 'grab';
      // A click is not a drag out. This also covers a click during the slide-in, when the window is still at the edge.
      if (!this.dragMoved) {
        this.snapBackToHide();
        return;
      }
      const out = this.hidingSide === 'left'
        ? this.pos.x >= -this.petW * 0.2
        : this.pos.x <= this.maxX() + this.petW * 0.2;
      if (out) this.exitHide();
      else this.snapBackToHide();
    };
    this.canvas.addEventListener('pointerup', release);
    this.canvas.addEventListener('pointercancel', release);
  }

  update(dt) {
    this.advanceAnimation(dt);
    if (this.mode === 'hiding') {
      this.updateHiding(dt);
      return;
    }

    if (demo.inPlace) {
      this.phase = 'wait';
      this.phaseLeft = 400;
      this.pos = this.clamped(this.home);
      this.updateTransform();
      this.setAction('walk');
      return;
    }

    if (this.phase === 'wait') {
      this.phaseLeft -= dt;
      if (this.phaseLeft <= 0) {
        this.stepsLeft = this.gait === 'pounce' ? 1 : 1 + Math.floor(Math.random() * 2);
        if (!this.nextStep()) this.finishStroll();
      }
      return;
    }

    if (this.phase === 'pause') {
      this.phaseLeft -= dt;
      if (this.phaseLeft <= 0) {
        this.stepsLeft -= 1;
        if (this.stepsLeft > 0 && this.nextStep()) return;
        this.finishStroll();
      }
      return;
    }

    // step
    this.stepElapsed = Math.min(this.stepMs, this.stepElapsed + dt);
    const t = this.stepElapsed / this.stepMs;
    const eased = this.ease(t);
    let x = this.from.x + (this.to.x - this.from.x) * eased;
    let y = this.from.y + (this.to.y - this.from.y) * eased;
    const arc = Math.sin(t * Math.PI);
    if (this.gait === 'float') y += Math.sin(t * Math.PI * 2) * 5;
    else if (this.gait === 'hop') y -= arc * 3;
    else if (this.gait === 'trot') y -= arc * 2;
    else if (this.gait === 'pounce') y -= arc * 4;
    this.pos = this.clamped({ x, y });
    this.updateTransform();
    if (t >= 1) {
      const edge = this.edgeAt();
      if (edge && Math.random() < HIDE_CHANCE) {
        this.enterHide(edge);
        return;
      }
      this.phase = 'pause';
      this.phaseLeft = this.gait === 'pounce' ? 1000 + Math.random() * 900 : 220 + Math.random() * 600;
      this.setAction('idle');
    }
  }

  ease(t) {
    if (this.gait === 'pounce' || this.gait === 'hop') return 1 - (1 - t) ** 3;
    return t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2;
  }

  draw() {
    const def = this.pet.actions[this.action];
    if (!def) return;
    const frame = Math.min(this.frame, def.frames - 1);
    let expression = def.expressions?.[frame] ?? 0;
    if (this.action === 'edge_peek' && this.hidingSide) {
      // One action cannot look both ways: face the stage centre while peeking.
      const look = this.hidingSide === 'left' ? 4 : 3;
      expression = [look, 2, look, 0, look, 2][frame % 6];
    }
    this.renderer.clear();
    this.renderer.drawPet(
      {
        action: this.action,
        frame,
        expression,
        anchors: def.anchors[frame],
      },
      [],
    );
  }
}

/** @return The ink bounds of every frame in a horizontal sheet, in canvas px, or null for an empty frame. */
function frameInk(image, canvas, frames) {
  if (!image) return Array.from({ length: frames }, () => null);
  const probe = document.createElement('canvas');
  probe.width = canvas * frames;
  probe.height = canvas;
  const ctx = probe.getContext('2d', { willReadFrequently: true });
  ctx.drawImage(image, 0, 0);
  const data = ctx.getImageData(0, 0, probe.width, probe.height).data;
  return Array.from({ length: frames }, (_, frame) => {
    let left = canvas;
    let top = canvas;
    let right = -1;
    let bottom = -1;
    for (let y = 0; y < canvas; y += 1) {
      for (let x = 0; x < canvas; x += 1) {
        if (data[(y * probe.width + frame * canvas + x) * 4 + 3] < 40) continue;
        if (x < left) left = x;
        if (x > right) right = x;
        if (y < top) top = y;
        if (y > bottom) bottom = y;
      }
    }
    return right < left ? null : { left, top, right, bottom };
  });
}

class ReviewDemo {
  constructor({ pets, images, canvas, stage, sign, board, note }) {
    this.pets = pets;
    this.images = images;
    this.canvas = canvas;
    this.stage = stage;
    this.sign = sign;
    this.board = board;
    this.note = note;
    this.pet = null;
    this.id = null;
    this.side = 'left';
    this.frame = 0;
    this.frameElapsed = 0;
    this.show('dog');
  }

  show(id) {
    const pet = this.pets.get(id);
    if (!pet) return;
    this.id = id;
    this.pet = pet;
    this.idle = pet.actions.idle;
    this.ask = pet.actions.ask;
    this.renderer = new PetRenderer(this.canvas, { ...pet, scale: SCALE });
    this.renderer.images = this.images.get(id);
    this.ink = this.bodyInk();
    this.frame = 0;
    this.frameElapsed = 0;
    this.layout();
  }

  /// The idle body's ink box in CSS px: the sign stands outside its left/right edges and on its bottom edge.
  bodyInk() {
    const ink = frameInk(this.renderer.images.get(this.idle.sheet), this.pet.canvas, this.idle.frames)[0];
    if (!ink) return { left: 0, right: this.pet.canvas * SCALE, bottom: this.pet.canvas * SCALE };
    return { left: ink.left * SCALE, right: (ink.right + 1) * SCALE, bottom: (ink.bottom + 1) * SCALE };
  }

  setSide(side) {
    this.side = side;
    this.layout();
  }

  layout() {
    if (!this.pet) return;
    const stageW = this.stage.clientWidth;
    const stageH = this.stage.clientHeight;
    this.petW = this.pet.canvas * SCALE;
    this.petH = this.pet.height * SCALE;
    const inkWidth = Math.max(1, this.ink.right - this.ink.left);
    const visible = inkWidth * HIDE_VISIBLE;
    const maxX = Math.max(0, stageW - this.petW);
    this.petX = this.side === 'left'
      ? visible - this.ink.right
      : maxX + (this.petW - this.ink.left - visible);
    this.petY = Math.max(0, stageH - this.petH - 28);
    this.canvas.style.transform = `translate(${this.petX}px, ${this.petY}px)`;
    this.placeSign();
    if (this.note) {
      const edge = this.side === 'left' ? '左' : '右';
      this.note.textContent = `演示：${NAMES[this.id]} 躲在${edge}边缘；木牌立在露出的内侧，柱脚踩在宠物脚线`;
    }
  }

  /// The wooden sign on the inner side of the hidden pet, its post on the idle foot line. Only x is clamped to the stage.
  placeSign() {
    if (!this.pet) return;
    const stageW = this.stage.clientWidth;
    const K = SCALE / 3.01;
    const bw = this.board.offsetWidth * K;
    const inner = this.side === 'left' ? 'right' : 'left';
    const x = inner === 'right' ? this.petX + this.ink.right + 8 : this.petX + this.ink.left - 8 - bw;
    const base = this.petY + this.ink.bottom;
    this.sign.style.transform = `scale(${K})`;
    this.sign.style.left = `${Math.max(16, Math.min(x, stageW - bw - 16))}px`;
    this.sign.style.top = `${Math.max(16, base - (40 + 32 - 2) * K)}px`;
  }

  update(dt) {
    if (!this.pet) return;
    this.frameElapsed += dt;
    const frameMs = 1000 / this.ask.fps;
    while (this.frameElapsed >= frameMs) {
      this.frameElapsed -= frameMs;
      this.frame = (this.frame + 1) % this.ask.frames;
    }
    this.renderer.clear();
    this.renderer.drawPet({
      action: 'ask',
      frame: this.frame,
      expression: this.ask.expressions?.[this.frame] ?? 0,
      anchors: this.ask.anchors[this.frame],
    }, []);
  }
}

function makeCard(id) {
  const card = document.createElement('article');
  card.className = 'card';
  card.innerHTML = `
    <header><h2>${NAMES[id] ?? id}</h2><span class="gait"></span></header>
    <div class="stage">
      <div class="range-ring"></div>
      <div class="home"></div>
      <canvas aria-label="${NAMES[id] ?? id} 走路动画"></canvas>
    </div>
    <p class="note"></p>`;
  grid.append(card);
  return card;
}

async function build() {
  const pets = new Map();
  const imageMaps = new Map();
  const loaded = new Map();
  for (const id of PET_IDS) {
    const pet = await loadPet(id);
    loaded.set(id, pet);
  }
  // Put every card in the grid before the first actor measures its stage: a lone first card would take the full
  // page width, and its home would be wrong as soon as the rest of the grid arrived.
  const cards = new Map();
  for (const [id, pet] of loaded) {
    let walk;
    try {
      walk = walkOf(pet);
    } catch (error) {
      continue;
    }
    const card = makeCard(id);
    card.querySelector('.gait').textContent = GAIT_LABEL[walk.gait] ?? walk.gait;
    card.querySelector('.note').textContent =
      `walk · ${walk.action.frames} 帧 @ ${walk.action.fps} fps · 家范围 ${walk.range}% 身宽 · 边缘会偷看`;
    cards.set(id, card);
  }

  for (const [id, card] of cards) {
    try {
      const pet = loaded.get(id);
      const walk = walkOf(pet);
      const canvas = card.querySelector('canvas');
      const renderer = new PetRenderer(canvas, { ...pet, scale: SCALE });
      await renderer.load(path => `/${id}/${path}`);
      pets.set(id, pet);
      imageMaps.set(id, renderer.images);
      actors.push(new Walker({
        id,
        pet,
        walk,
        renderer,
        canvas,
        stage: card.querySelector('.stage'),
        homeDot: card.querySelector('.home'),
        ring: card.querySelector('.range-ring'),
        note: card.querySelector('.note'),
      }));
    } catch (error) {
      card.querySelector('.note').textContent = String(error);
    }
  }
  // A scrollbar or late font can change the card width while the sheets load; measure once more after all of it.
  actors.forEach(actor => actor.layout());
  reviewDemo = new ReviewDemo({
    pets,
    images: imageMaps,
    canvas: document.querySelector('#review-canvas'),
    stage: document.querySelector('.review-stage'),
    sign: document.querySelector('#review-sign'),
    board: document.querySelector('#review-board'),
    note: document.querySelector('#review-note'),
  });
  status.textContent = `${actors.length} 只宠物`;
}

toggle.addEventListener('click', () => {
  demo.playing = !demo.playing;
  toggle.textContent = demo.playing ? '暂停' : '继续';
  toggle.setAttribute('aria-pressed', String(demo.playing));
});
speed.addEventListener('change', () => { demo.speed = Number(speed.value); });
inPlace.addEventListener('change', () => { demo.inPlace = inPlace.checked; });
rangeToggle.addEventListener('change', () => { grid.classList.toggle('show-range', rangeToggle.checked); });
peekButton.addEventListener('click', () => {
  const visible = actors.filter(actor => actor.mode !== 'hiding');
  if (visible.length === 0) return;
  visible[Math.floor(Math.random() * visible.length)].hideAtNearestEdge();
});
for (const id of PET_IDS) reviewPetSelect.append(new Option(NAMES[id] ?? id, id, false, id === 'dog'));
const setReviewSide = side => {
  reviewLeftButton.setAttribute('aria-pressed', String(side === 'left'));
  reviewRightButton.setAttribute('aria-pressed', String(side === 'right'));
  reviewDemo?.setSide(side);
};
reviewPetSelect.addEventListener('change', () => reviewDemo?.show(reviewPetSelect.value));
reviewLeftButton.addEventListener('click', () => setReviewSide('left'));
reviewRightButton.addEventListener('click', () => setReviewSide('right'));
window.addEventListener('resize', () => {
  actors.forEach(actor => actor.layout());
  reviewDemo?.layout();
});

await build();

// The screenshot harness reads this; it is demo state, not a production API.
window.__walkDemo = { actors, demo, HIDE_CHANCE, review: () => reviewDemo };

let last = performance.now();
function tick(now) {
  const raw = Math.max(0, Math.min(now - last, 100));
  last = now;
  const dt = demo.playing ? raw * demo.speed : 0;
  for (const actor of actors) {
    actor.update(dt);
    actor.draw();
  }
  reviewDemo?.update(dt);
  const hiding = actors.filter(actor => actor.mode === 'hiding').length;
  status.textContent = demo.playing
    ? `${actors.length} 只宠物 · ${demo.speed}×${hiding ? ` · ${hiding} 只躲藏` : ''}`
    : '已暂停';
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);

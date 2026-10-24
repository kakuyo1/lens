/**
 * Scene-entry demo. The stage draws the real scene back/front sheets and the cat's own frames; the demo only
 * choreographs the first ~0.8 s, then lets the scene's 12 frames loop as the app does. The three modes are the
 * current hard cut, a per-scene entrance, and one generic crossfade for comparison.
 */

import catalogue from '../../../data/scenes/scenes.json';
import bathBack from '../../../data/scenes/bath/back.png';
import bathFront from '../../../data/scenes/bath/front.png';
import boxBack from '../../../data/scenes/cat_box/back.png';
import boxFront from '../../../data/scenes/cat_box/front.png';
import swingBack from '../../../data/scenes/swing/back.png';
import swingFront from '../../../data/scenes/swing/front.png';
import sunbeamBack from '../../../data/scenes/cat_sunbeam/back.png';
import sunbeamFront from '../../../data/scenes/cat_sunbeam/front.png';

const PET = 'cat';
const CANVAS = 88;
const SCENE_IDS = ['bath', 'cat_box', 'swing', 'cat_sunbeam'];
const SCENES = new Map(catalogue.scenes.map(scene => [scene.id, scene]));
const SHEETS = {
  bath: { back: bathBack, front: bathFront },
  cat_box: { back: boxBack, front: boxFront },
  swing: { back: swingBack, front: swingFront },
  cat_sunbeam: { back: sunbeamBack, front: sunbeamFront }
};

/**
 * The entrance choreography. Times are milliseconds at 1x. `sceneSwitch` is where the pet's own scene body takes
 * over; `loopStart` is the first frame of the 12-frame loop; `introEnd` closes the phase strip.
 */
const ENTRY = {
  bath: {
    subtitle: '跳进浴缸',
    note: '浴缸先淡入，宠物在前景蓄力，再用 idle 身体跳进去，落水时换成 bath 身体。',
    prep: 160,
    jump: { start: 160, end: 560, arc: 20, cross: 0.62 },
    sceneSwitch: 560,
    loopStart: 560,
    introEnd: 840,
    propIn: 180,
    propRise: 3,
    impact: { at: 560, x: 44, y: 66, kind: 'splash', duration: 340 },
    rules: [
      '<b>道具先行</b>：浴缸在 180 ms 内淡入，宠物仍画在 front 之上，所以先看见它站在缸前。',
      '<b>越过去</b>：idle 身体沿抛物线起跳，过了 62% 的弧线后改画在 front 之下，下半身被缸沿挡住。',
      '<b>落水</b>：第 560 ms 换成 <code>bath</code> 身体，水花盖住切换；随后场景帧从 0 开始循环。'
    ]
  },
  cat_box: {
    subtitle: '扑进纸箱',
    note: '纸箱先出现，宠物在箱前压一下，再一扑进箱，落定时换成 cat_box 身体。',
    prep: 140,
    jump: { start: 140, end: 520, arc: 13, cross: 0.58 },
    sceneSwitch: 520,
    loopStart: 520,
    introEnd: 780,
    propIn: 150,
    propRise: 2,
    impact: { at: 520, x: 45, y: 60, kind: 'dust', duration: 300 },
    rules: [
      '<b>道具先行</b>：纸箱在 150 ms 内升起、淡入，宠物先站在箱前。',
      '<b>一扑</b>：比泡澡更低的抛物线，越过箱沿后立刻藏到 front 之下。',
      '<b>落箱</b>：换成 <code>cat_box</code> 身体，箱口的尘土粒子遮住切换。'
    ]
  },
  swing: {
    subtitle: '跃上秋千',
    note: '绳索与木板先出现，宠物从地面跃起，落在板上后换成 swing 身体。',
    prep: 140,
    jump: { start: 140, end: 620, arc: 28, cross: 0.56 },
    sceneSwitch: 620,
    loopStart: 620,
    introEnd: 820,
    propIn: 200,
    propRise: 0,
    impact: null,
    rules: [
      '<b>道具先行</b>：绳索和木板淡入；秋千的后层在宠物之后，不需要遮挡切换。',
      '<b>一跃</b>：比泡澡更高的弧线，落点是第 0 帧的座板高度。',
      '<b>坐稳</b>：换成 <code>swing</code> 身体，场景帧继续带着座板上下的节奏。'
    ]
  },
  cat_sunbeam: {
    subtitle: '趴进阳光',
    note: '窗与光斑先淡入，宠物向光里挪半步、缩小趴下，身体用交叉淡化完成。',
    style: 'settle',
    prep: 120,
    jump: { start: 120, end: 600, arc: 0, cross: 0.66 },
    sceneSwitch: 690,
    crossfade: 180,
    loopStart: 690,
    introEnd: 850,
    propIn: 240,
    propRise: 0,
    impact: null,
    rules: [
      '<b>道具先行</b>：窗、墙和光斑淡入，宠物还在原来的站姿。',
      '<b>挪过去</b>：向光斑移 2 px、缩小到 0.85，最后一段同时淡出 idle、淡入 <code>cat_sunbeam</code> 身体。',
      '<b>趴下</b>：交叉淡化代替落地遮挡，因为这里没有 front 层可以藏住切换。'
    ]
  }
};

const FALLBACK_RULES = {
  enter: [],
  fade: [
    '<b>通用淡入</b>：道具和宠物在约 300 ms 内一起淡入，宠物从站姿交叉淡化到场景姿势。',
    '<b>没有位移</b>：不跳、不落，也不换层；适合所有场景，但没有 “跳进去” 的叙事。',
    '<b>切换点</b>：交叉淡化结束时场景帧从 0 开始循环。'
  ],
  cut: [
    '<b>现状</b>：道具、场景身体与位移在同一帧出现，没有入场。',
    '<b>用途</b>：作为对照，也对应减少动效时的最终行为。',
    '<b>循环</b>：场景立即从第 0 帧开始。'
  ]
};

const reduceMedia = window.matchMedia('(prefers-reduced-motion: reduce)');
const params = new URLSearchParams(window.location.search);
const state = {
  scene: SCENE_IDS.includes(params.get('scene')) ? params.get('scene') : 'bath',
  variant: ['enter', 'fade', 'cut'].includes(params.get('variant')) ? params.get('variant') : 'enter',
  speed: [0.5, 1, 1.5].includes(Number(params.get('speed'))) ? Number(params.get('speed')) : 1,
  reduced: params.has('reduced') ? params.get('reduced') === '1' : reduceMedia.matches,
  auto: params.get('auto') !== '0',
  showBox: params.get('box') === '1',
  startedAt: performance.now(),
  paused: false,
  holdAt: 0,
  assets: null
};

const dom = {
  screen: document.getElementById('screen'),
  stage: document.getElementById('stage'),
  caption: document.getElementById('stage-caption'),
  loading: document.getElementById('loading'),
  error: document.getElementById('error'),
  live: document.getElementById('live'),
  liveText: document.getElementById('live-text'),
  status: document.getElementById('status'),
  statusText: document.getElementById('status-text'),
  frameMeta: document.getElementById('frame-meta'),
  sceneGrid: document.getElementById('scene-grid'),
  variantSeg: document.getElementById('variant-seg'),
  variantHint: document.getElementById('variant-hint'),
  speedSeg: document.getElementById('speed-seg'),
  reduced: document.getElementById('reduced'),
  auto: document.getElementById('auto'),
  showBox: document.getElementById('show-box'),
  replay: document.getElementById('replay'),
  next: document.getElementById('next'),
  timeline: document.getElementById('timeline'),
  scriptTitle: document.getElementById('script-title'),
  scriptNote: document.getElementById('script-note'),
  scriptCaption: document.getElementById('script-caption'),
  rules: document.getElementById('rules-list'),
  foot: document.getElementById('foot-status')
};

const ctx = dom.stage.getContext('2d');
ctx.imageSmoothingEnabled = false;

// ---------------------------------------------------------------- helpers

const clamp = (v, lo = 0, hi = 1) => Math.min(hi, Math.max(lo, v));
const lerp = (a, b, t) => a + (b - a) * t;
const easeOutCubic = t => 1 - (1 - t) ** 3;
const easeInOutCubic = t => (t < 0.5 ? 4 * t ** 3 : 1 - (-2 * t + 2) ** 3 / 2);
const idleFrameAt = elapsed => {
  const idle = state.assets?.animations?.actions?.idle;
  const frames = idle?.frames ?? 5;
  const fps = idle?.fps ?? 5;
  return Math.floor((Math.max(0, elapsed) * fps) / 1000) % frames;
};

function loadImage(src) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error(`missing image: ${src}`));
    image.src = src;
  });
}

async function loadJson(path) {
  const response = await fetch(path);
  if (!response.ok) throw new Error(`${path}: ${response.status}`);
  return response.json();
}

function frameProgress(elapsed, from, to) {
  if (!Number.isFinite(to)) return elapsed >= from ? 1 : 0;
  return clamp((elapsed - from) / (to - from));
}

function activePhase(phases, elapsed) {
  return phases.find(phase => elapsed >= phase.from && elapsed < phase.to) ?? phases[phases.length - 1];
}

/** The idle figure's ink centre and bottom, measured the way PetAssets does: frame 0, alpha bounds. */
function measureInk(image) {
  const canvas = document.createElement('canvas');
  canvas.width = CANVAS;
  canvas.height = CANVAS;
  const g = canvas.getContext('2d');
  g.drawImage(image, 0, 0, CANVAS, CANVAS, 0, 0, CANVAS, CANVAS);
  const data = g.getImageData(0, 0, CANVAS, CANVAS).data;
  let x0 = CANVAS;
  let y0 = CANVAS;
  let x1 = -1;
  let y1 = -1;
  for (let y = 0; y < CANVAS; y += 1) {
    for (let x = 0; x < CANVAS; x += 1) {
      if (data[(y * CANVAS + x) * 4 + 3] === 0) continue;
      x0 = Math.min(x0, x);
      y0 = Math.min(y0, y);
      x1 = Math.max(x1, x);
      y1 = Math.max(y1, y);
    }
  }
  return { x: (x0 + x1) / 2, y: y1 };
}

function phaseList(mode) {
  if (mode === 'cut') {
    return [{ from: 0, to: Infinity, label: '硬切 / 循环', caption: '道具、身体与位移同时出现；第 0 帧开始循环' }];
  }
  if (mode === 'fade') {
    return [
      { from: 0, to: 300, label: '淡入', caption: '道具与身体一起淡入，没有位移' },
      { from: 300, to: Infinity, label: '场景循环', caption: '12 帧场景循环' }
    ];
  }
  const entry = ENTRY[state.scene];
  return [
    { from: 0, to: entry.prep, label: '预备', caption: '道具先就位，宠物仍在前景蓄力' },
    { from: entry.prep, to: entry.sceneSwitch, label: entry.style === 'settle' ? '挪过去' : '起跳', caption: entry.style === 'settle' ? '向落点移动并缩小，同时交叉淡化身体' : '按场景弧线移动；穿过道具前后层时切换遮挡' },
    { from: entry.sceneSwitch, to: entry.introEnd, label: entry.impact?.kind === 'splash' ? '落水' : entry.impact?.kind === 'dust' ? '落箱' : '落定', caption: entry.impact ? '换成场景身体，粒子遮住切换' : '换成场景身体，场景帧从 0 开始' },
    { from: entry.introEnd, to: Infinity, label: '场景循环', caption: '12 帧场景循环' }
  ];
}

// ---------------------------------------------------------------- drawing

function drawLayer(scene, layer, frame, alpha, offsetY = 0) {
  if (alpha <= 0) return;
  const sheet = state.assets.sceneSheets[scene.id][layer];
  ctx.save();
  ctx.globalAlpha = alpha;
  ctx.translate(state.assets.align[scene.id].x, state.assets.align[scene.id].y + offsetY);
  ctx.drawImage(sheet, frame * CANVAS, 0, CANVAS, CANVAS, 0, 0, CANVAS, CANVAS);
  ctx.restore();
}

function drawPet(action, frame, alpha, place = {}) {
  if (alpha <= 0) return;
  const { images, anchors } = state.assets;
  const body = images.pet[action];
  const sequence = anchors.actions[action];
  const anchor = sequence[Math.min(frame, sequence.length - 1)];
  const foot = state.assets.foot;
  const x = place.x ?? 0;
  const y = place.y ?? 0;
  const sx = place.sx ?? 1;
  const sy = place.sy ?? 1;
  const align = place.align ?? { x: 0, y: 0 };

  ctx.save();
  ctx.globalAlpha = alpha;
  ctx.translate(align.x, align.y);
  ctx.translate(x, y);
  ctx.translate(foot.x, foot.y);
  ctx.scale(sx, sy);
  ctx.translate(-foot.x, -foot.y);
  ctx.drawImage(body, frame * CANVAS, 0, CANVAS, CANVAS, anchor.body[0], anchor.body[1], CANVAS, CANVAS);
  ctx.drawImage(state.assets.images.eyes, 0, 0, CANVAS, CANVAS, anchor.face[0], anchor.face[1], CANVAS, CANVAS);
  ctx.restore();
}

const SPLASH = [
  [-18, 2, 1, '#d9ecec'], [-16, 0, 2, '#f7fbf8'], [-13, 1, 1, '#9cc8d4'],
  [-11, 2, 1, '#f7fbf8'], [-9, 0, 2, '#d9ecec'], [-7, 1, 1, '#9cc8d4'],
  [-5, 2, 1, '#f7fbf8'], [-3, 0, 2, '#d9ecec'], [0, 1, 1, '#f7fbf8'],
  [2, 2, 2, '#9cc8d4'], [4, 0, 1, '#d9ecec'], [6, 1, 1, '#f7fbf8'],
  [8, 2, 2, '#d9ecec'], [10, 0, 1, '#9cc8d4'], [12, 1, 1, '#f7fbf8'],
  [14, 2, 2, '#d9ecec'], [16, 0, 1, '#f7fbf8'], [18, 1, 1, '#9cc8d4']
];
const DUST = [
  [-16, 1, 1, '#d9cfc0'], [-13, 0, 2, '#b9a98f'], [-10, 1, 1, '#d9cfc0'],
  [-7, 0, 2, '#b9a98f'], [-4, 1, 1, '#d9cfc0'], [0, 0, 2, '#e7ddca'],
  [4, 1, 1, '#b9a98f'], [7, 0, 2, '#d9cfc0'], [10, 1, 1, '#b9a98f'],
  [13, 0, 2, '#d9cfc0'], [16, 1, 1, '#b9a98f']
];

function drawImpact(impact, elapsed) {
  if (!impact) return;
  const p = clamp((elapsed - impact.at) / impact.duration);
  if (p <= 0 || p >= 1) return;
  const particles = impact.kind === 'splash' ? SPLASH : DUST;
  ctx.save();
  ctx.globalAlpha = 1 - p;
  for (let i = 0; i < particles.length; i += 1) {
    const [dx, spread, size, colour] = particles[i];
    const lift = impact.kind === 'splash' ? 24 : 9;
    const fall = impact.kind === 'splash' ? 42 : 18;
    const x = impact.x + dx * (0.35 + 0.8 * p);
    const y = impact.y + spread * (1 - p) - lift * p + fall * p * p;
    ctx.fillStyle = colour;
    ctx.fillRect(Math.round(x), Math.round(y), size, size);
  }
  // The crown: a broken foam line on the rim, gone before the loop settles.
  ctx.fillStyle = impact.kind === 'splash' ? '#f7fbf8' : '#e7ddca';
  const width = Math.round((impact.kind === 'splash' ? 26 : 18) * (1 - p) + 4);
  for (let x = -width / 2; x <= width / 2; x += 2) {
    if ((Math.round(x) + Math.round(p * 10)) % 6 === 0) continue;
    ctx.fillRect(Math.round(impact.x + x), Math.round(impact.y - (Math.abs(x) < 6 ? 1 : 0)), 1, 1);
  }
  ctx.restore();
}

// ---------------------------------------------------------------- frame composition

function sceneFrameIndex(elapsed, entry, mode) {
  const fps = SCENES.get(state.scene).fps;
  const frames = SCENES.get(state.scene).frames;
  const start = mode === 'cut' ? 0 : mode === 'fade' ? 300 : entry.loopStart;
  const clock = Math.max(0, elapsed - start);
  return Math.floor((clock / 1000) * fps) % frames;
}

function frameOfScene(scene, index) {
  return scene.framesData[Math.min(index, scene.framesData.length - 1)];
}

function drawCut(elapsed) {
  const scene = SCENES.get(state.scene);
  const entry = ENTRY[state.scene];
  const index = sceneFrameIndex(elapsed, entry, 'cut');
  const frame = frameOfScene(scene, index);
  const align = state.assets.align[state.scene];
  drawLayer(scene, 'back', index, 1);
  drawPet(scene.id, index, 1, { x: frame.dx, y: frame.dy, sx: frame.scale, sy: frame.scale, align });
  drawLayer(scene, 'front', index, 1);
  return index;
}

function drawFade(elapsed) {
  const scene = SCENES.get(state.scene);
  const entry = ENTRY[state.scene];
  const index = sceneFrameIndex(elapsed, entry, 'fade');
  const show = elapsed < 300 ? 0 : index;
  const frame = frameOfScene(scene, show);
  const align = state.assets.align[state.scene];
  const alpha = easeOutCubic(clamp(elapsed / 300));
  const blend = clamp(elapsed / 300);
  drawLayer(scene, 'back', show, alpha);
  drawPet('idle', idleFrameAt(elapsed), 1 - blend, {});
  drawPet(scene.id, show, blend, { x: frame.dx, y: frame.dy, sx: frame.scale, sy: frame.scale, align });
  drawLayer(scene, 'front', show, alpha);
  return index;
}

function drawEnter(elapsed) {
  const scene = SCENES.get(state.scene);
  const entry = ENTRY[state.scene];
  const align = state.assets.align[state.scene];
  const index = sceneFrameIndex(elapsed, entry, 'enter');
  const propAlpha = easeOutCubic(clamp(elapsed / entry.propIn));
  const propRise = (1 - propAlpha) * entry.propRise;
  const beforeSwitch = elapsed < entry.sceneSwitch;
  const target = frameOfScene(scene, 0);

  if (!beforeSwitch) {
    const frame = frameOfScene(scene, index);
    const landing = clamp((elapsed - entry.sceneSwitch) / 140);
    // The last third of the landing tickle: a small squash, then neutral by the time the loop is readable.
    const squash = entry.impact ? 1 - 0.06 * (1 - landing) : 1;
    drawLayer(scene, 'back', index, 1);
    drawPet(scene.id, index, 1, {
      x: frame.dx,
      y: frame.dy,
      sx: frame.scale,
      sy: frame.scale * squash,
      align
    });
    drawLayer(scene, 'front', index, 1);
    drawImpact(entry.impact, elapsed);
    return index;
  }

  const jump = entry.jump;
  const p = clamp((elapsed - jump.start) / (jump.end - jump.start));
  const move = easeInOutCubic(p);
  let x = lerp(0, target.dx, move);
  let y = lerp(0, target.dy, move) - jump.arc * Math.sin(Math.PI * p);
  let sx = 1 + (target.scale - 1) * move;
  let sy = sx;

  if (elapsed < jump.start) {
    // A short crouch while the props come in; it reads as the spring before the leap.
    const crouch = easeOutCubic(clamp(elapsed / entry.prep));
    sy = 1 - 0.07 * crouch;
  } else {
    const stretch = 0.05 * Math.sin(Math.PI * p);
    sx *= 1 - stretch * 0.5;
    sy *= 1 + stretch;
  }

  const behindFront = p >= jump.cross;
  const idleFrame = idleFrameAt(elapsed);
  const crossBlend = entry.style === 'settle' ? clamp((elapsed - (entry.sceneSwitch - entry.crossfade / 2)) / entry.crossfade) : 0;

  drawLayer(scene, 'back', 0, propAlpha, propRise);
  if (!behindFront) drawLayer(scene, 'front', 0, propAlpha, propRise);
  if (entry.style === 'settle') {
    drawPet('idle', idleFrame, 1 - crossBlend, { x, y, sx, sy, align });
    drawPet(scene.id, 0, crossBlend, { x, y, sx, sy, align });
  } else {
    drawPet('idle', idleFrame, 1, { x, y, sx, sy, align });
  }
  if (behindFront) drawLayer(scene, 'front', 0, propAlpha, propRise);
  drawImpact(entry.impact, elapsed);
  return index;
}

function drawFrame(elapsed) {
  const mode = state.reduced ? 'cut' : state.variant;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.clearRect(0, 0, dom.stage.width, dom.stage.height);
  ctx.setTransform(3, 0, 0, 3, 0, 0);
  let index;
  if (mode === 'cut') index = drawCut(elapsed);
  else if (mode === 'fade') index = drawFade(elapsed);
  else index = drawEnter(elapsed);
  return index;
}

// ---------------------------------------------------------------- UI

function renderThumb(scene) {
  const canvas = document.createElement('canvas');
  canvas.width = CANVAS;
  canvas.height = CANVAS;
  canvas.className = 'scene-thumb';
  const g = canvas.getContext('2d');
  g.imageSmoothingEnabled = false;
  const bg = g.createLinearGradient(0, 0, 0, CANVAS);
  bg.addColorStop(0, '#2c3f46');
  bg.addColorStop(1, '#17252d');
  g.fillStyle = bg;
  g.fillRect(0, 0, CANVAS, CANVAS);
  const { align, anchors, foot, images, sceneSheets } = state.assets;
  const frame = frameOfScene(scene, 0);
  g.drawImage(sceneSheets[scene.id].back, 0, 0, CANVAS, CANVAS, align[scene.id].x, align[scene.id].y, CANVAS, CANVAS);
  const anchor = anchors.actions[scene.id][0];
  g.save();
  g.translate(align[scene.id].x + frame.dx, align[scene.id].y + frame.dy);
  g.translate(foot.x, foot.y);
  g.scale(frame.scale, frame.scale);
  g.translate(-foot.x, -foot.y);
  g.drawImage(images.pet[scene.id], 0, 0, CANVAS, CANVAS, anchor.body[0], anchor.body[1], CANVAS, CANVAS);
  g.drawImage(images.eyes, 0, 0, CANVAS, CANVAS, anchor.face[0], anchor.face[1], CANVAS, CANVAS);
  g.restore();
  g.drawImage(sceneSheets[scene.id].front, 0, 0, CANVAS, CANVAS, align[scene.id].x, align[scene.id].y, CANVAS, CANVAS);
  return canvas;
}

function buildSceneButtons() {
  dom.sceneGrid.innerHTML = '';
  for (const id of SCENE_IDS) {
    const scene = SCENES.get(id);
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'scene-btn';
    button.dataset.scene = id;
    button.setAttribute('aria-pressed', String(id === state.scene));
    const copy = document.createElement('span');
    copy.className = 'scene-copy';
    copy.innerHTML = `<b>${scene.name}</b><span>${ENTRY[id].subtitle}</span>`;
    button.append(renderThumb(scene), copy);
    button.addEventListener('click', () => selectScene(id));
    dom.sceneGrid.append(button);
  }
}

function syncSceneButtons() {
  for (const button of dom.sceneGrid.querySelectorAll('.scene-btn'))
    button.setAttribute('aria-pressed', String(button.dataset.scene === state.scene));
}

function buildTimeline() {
  const phases = phaseList(state.reduced ? 'cut' : state.variant);
  dom.timeline.innerHTML = '';
  phases.forEach((phase, index) => {
    const item = document.createElement('li');
    item.innerHTML = `<span class="idx">${String(index + 1).padStart(2, '0')}</span><span class="name">${phase.label}</span><span class="phase-bar"></span>`;
    dom.timeline.append(item);
  });
}

function updateTimeline(elapsed) {
  const phases = phaseList(state.reduced ? 'cut' : state.variant);
  const items = [...dom.timeline.children];
  items.forEach((item, index) => {
    const phase = phases[index];
    if (!phase) return;
    item.classList.remove('active', 'done');
    if (elapsed >= phase.to) item.classList.add('done');
    else if (elapsed >= phase.from) item.classList.add('active');
    const bar = item.querySelector('.phase-bar');
    if (bar) {
      const p = frameProgress(elapsed, phase.from, phase.to);
      bar.style.width = `${p * 100}%`;
    }
  });
  return activePhase(phases, elapsed);
}

function rulesFor(mode) {
  if (mode !== 'enter') return FALLBACK_RULES[mode];
  return ENTRY[state.scene].rules;
}

function syncPanels() {
  for (const button of dom.variantSeg.querySelectorAll('button'))
    button.setAttribute('aria-pressed', String(button.dataset.variant === state.variant));
  for (const button of dom.speedSeg.querySelectorAll('button'))
    button.setAttribute('aria-pressed', String(Number(button.dataset.speed) === state.speed));
  dom.variantHint.textContent = state.reduced
    ? '减少动效打开：所有入场退化为瞬时，场景直接从第 0 帧循环。'
    : {
        enter: '按场景编排：泡澡跳水、纸箱一扑、秋千一跃、窗台趴下。',
        fade: '所有场景共用一段 300 ms 淡入，没有位移，也最容易接进 QML。',
        cut: '现状：道具与姿势同一帧出现，作为对照保留。'
      }[state.variant];
  dom.rules.innerHTML = rulesFor(state.reduced ? 'cut' : state.variant).map(rule => `<li>${rule}</li>`).join('');
  dom.screen.classList.toggle('show-box', state.showBox);
}

function syncLive(elapsed, index) {
  const mode = state.reduced ? 'cut' : state.variant;
  const entry = ENTRY[state.scene];
  const phase = updateTimeline(elapsed);
  const phases = phaseList(mode);
  const busy = mode !== 'cut' && elapsed < (mode === 'fade' ? phases[phases.length - 1].from : entry.introEnd);
  const scene = SCENES.get(state.scene);
  const frameText = `${scene.name} · 场景帧 ${index + 1} / ${scene.frames}`;
  dom.live.dataset.busy = String(busy);
  dom.liveText.textContent = busy ? `${phase.label} · ${mode === 'fade' ? '通用淡入' : '场景入场'}` : frameText;
  dom.status.dataset.state = state.reduced ? 'instant' : busy ? 'busy' : 'on';
  dom.statusText.textContent = state.reduced ? '瞬时' : busy ? '入场中' : '循环';
  dom.frameMeta.textContent = `${scene.name} · 帧 ${index + 1} / ${scene.frames} · ${scene.fps} fps`;
  dom.caption.textContent = `${scene.name} · ${state.reduced ? '减少动效' : mode === 'cut' ? '现状硬切' : mode === 'fade' ? '通用淡入' : '场景入场'}`;
  dom.scriptTitle.textContent = `${scene.name} · ${state.reduced ? '减少动效' : mode === 'cut' ? '现状硬切' : mode === 'fade' ? '通用淡入' : '场景入场'}`;
  dom.scriptNote.textContent = mode === 'enter' ? entry.note : mode === 'fade' ? '道具与宠物一起淡入，宠物从站姿交叉淡化到场景姿势。' : '道具、身体与位移在同一帧出现。';
  dom.scriptCaption.textContent = state.reduced ? '减少动效：直接到场景循环' : elapsed >= phases[phases.length - 1].from ? `${phase.caption} · 帧 ${index + 1}` : phase.caption;
}

function play() {
  state.startedAt = performance.now();
  state.paused = false;
  syncPanels();
  buildTimeline();
  const mode = state.reduced ? 'cut' : state.variant;
  dom.stage.setAttribute('aria-label', `${SCENES.get(state.scene).name}，${mode === 'cut' ? '现状硬切' : mode === 'fade' ? '通用淡入' : '场景入场'}`);
}

/// Freezes the stage at one elapsed moment; `?t=` uses it for the QA shots, and the console can too.
function pauseAt(ms) {
  state.paused = true;
  state.holdAt = Math.max(0, ms);
  const index = drawFrame(state.holdAt);
  syncLive(state.holdAt, index);
}

function resume() {
  if (!state.paused) return;
  state.paused = false;
  state.startedAt = performance.now() - state.holdAt / state.speed;
}

function selectScene(id) {
  state.scene = id;
  syncSceneButtons();
  play();
}

function nextScene() {
  const index = SCENE_IDS.indexOf(state.scene);
  selectScene(SCENE_IDS[(index + 1) % SCENE_IDS.length]);
}

// ---------------------------------------------------------------- boot

async function loadAssets() {
  const [pet, animations, anchors] = await Promise.all([
    loadJson(`/${PET}/pet.json`),
    loadJson(`/${PET}/animations.json`),
    loadJson(`/${PET}/anchors.json`)
  ]);
  const actionNames = ['idle', ...SCENE_IDS];
  const [eyes, ...bodies] = await Promise.all([
    loadImage(`/${PET}/sprites/expressions/eyes.png`),
    ...actionNames.map(action => loadImage(`/${PET}/sprites/body/${action}.png`))
  ]);
  const petImages = Object.fromEntries(actionNames.map((action, index) => [action, bodies[index]]));
  const petSheets = Object.fromEntries(await Promise.all(SCENE_IDS.map(async id => {
    const [back, front] = await Promise.all([loadImage(SHEETS[id].back), loadImage(SHEETS[id].front)]);
    return [id, { back, front }];
  })));
  const foot = measureInk(petImages.idle);
  const align = Object.fromEntries(SCENE_IDS.map(id => {
    const scene = SCENES.get(id);
    return [id, { x: foot.x - scene.foot[0], y: foot.y - scene.foot[1] }];
  }));
  return {
    pet,
    animations,
    anchors,
    images: { pet: petImages, eyes },
    sceneSheets: petSheets,
    foot,
    align
  };
}

function bindControls() {
  dom.variantSeg.addEventListener('click', event => {
    const button = event.target.closest('button[data-variant]');
    if (!button) return;
    state.variant = button.dataset.variant;
    play();
  });
  dom.speedSeg.addEventListener('click', event => {
    const button = event.target.closest('button[data-speed]');
    if (!button) return;
    state.speed = Number(button.dataset.speed);
    play();
  });
  dom.reduced.addEventListener('change', () => {
    state.reduced = dom.reduced.checked;
    play();
  });
  dom.auto.addEventListener('change', () => {
    state.auto = dom.auto.checked;
  });
  dom.showBox.addEventListener('change', () => {
    state.showBox = dom.showBox.checked;
    dom.screen.classList.toggle('show-box', state.showBox);
  });
  dom.replay.addEventListener('click', play);
  dom.next.addEventListener('click', nextScene);
  reduceMedia.addEventListener('change', event => {
    if (params.has('reduced')) return;
    state.reduced = event.matches;
    dom.reduced.checked = state.reduced;
    play();
  });
}

async function boot() {
  try {
    state.assets = await loadAssets();
    dom.loading.hidden = true;
    buildSceneButtons();
    dom.reduced.checked = state.reduced;
    dom.auto.checked = state.auto;
    dom.showBox.checked = state.showBox;
    bindControls();
    play();
    const tick = now => {
      if (state.paused) {
        requestAnimationFrame(tick);
        return;
      }
      const elapsed = Math.max(0, now - state.startedAt) * state.speed;
      syncLive(elapsed, drawFrame(elapsed));
      const entry = ENTRY[state.scene];
      const loopStart = state.reduced || state.variant === 'cut' ? 0 : state.variant === 'fade' ? 300 : entry.loopStart;
      if (state.auto && elapsed > loopStart + 6500) nextScene();
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
    if (params.has('t')) pauseAt(Number(params.get('t')) || 0);
    window.sceneDemo = { pauseAt, resume, state, play, selectScene };
    dom.foot.textContent = '已载入：4 个场景 · 5 组身体帧 · 眼睛 1 组';
  } catch (error) {
    console.error(error);
    dom.loading.hidden = true;
    dom.error.hidden = false;
    dom.error.querySelector('p').textContent = error.message;
    dom.status.dataset.state = 'instant';
    dom.statusText.textContent = '载入失败';
    dom.foot.textContent = '载入失败';
  }
}

boot();

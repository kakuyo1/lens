/**
 * The six-scene board. Draws the fourteen proposed scenes on real pet frames, beside each pet's existing four
 * scenes and the weights, entries and accessory nudges a decision needs. Design only: the props are authored for
 * this page and are not read from data/scenes.
 */

import catalogue from '../../../data/scenes/scenes.json';
import { CANVAS, SCENES, pen } from './scenes-six-art.js';

const PETS = [
  {
    id: 'cat',
    label: '短耳猫',
    en: 'Stub cat',
    line: '先伸爪，再装作什么都没发生。',
    note: '现有四景里两景专属、两景共享。新增的一静一动：金鱼缸只用眼神，毛线球要伸爪。',
    existing: [
      ['cat_box', '专属'],
      ['cat_sunbeam', '专属'],
      ['swing', '共享'],
      ['bath', '共享']
    ],
    newScenes: ['cat_yarn', 'cat_fishbowl']
  },
  {
    id: 'cloud',
    label: '云',
    en: 'Cloud',
    line: '管一小片天气，也管把天气弄得很小。',
    note: '彩虹是它自己的天气，蒲公英是它吹给别处的天气。两景都把雨换成了别的东西。',
    existing: [
      ['cloud_watering', '专属'],
      ['cloud_fishing', '专属'],
      ['desk', '共享'],
      ['rain', '共享']
    ],
    newScenes: ['cloud_rainbow', 'cloud_dandelion']
  },
  {
    id: 'dog',
    label: '奶糖',
    en: 'Dog',
    line: '一根骨头能藏三次，一个雪人能闻半天。',
    note: '接飞盘和刨坑都在户外；雪人让它带一身凉气回来，狗窝让它把力气睡回去。',
    existing: [
      ['dog_frisbee', '专属'],
      ['dog_burying', '专属'],
      ['swing', '共享'],
      ['skate', '共享']
    ],
    newScenes: ['dog_snowman', 'dog_bed']
  },
  {
    id: 'ghost',
    label: '幽灵',
    en: 'Ghost',
    line: '怕黑，但更怕没人来喝茶。',
    note: '提灯与镜子都偏静。座钟给它一个会响的夜晚，茶会补上待客的一面。',
    existing: [
      ['ghost_lantern', '专属'],
      ['ghost_mirror', '专属'],
      ['desk', '共享'],
      ['bath', '共享']
    ],
    newScenes: ['ghost_clock', 'ghost_tea']
  },
  {
    id: 'jelly',
    label: '水母',
    en: 'Jellyfish',
    line: '一碰就晃，晃完继续发光。',
    note: '甜品杯和弹跳床都在玩。夜灯与贝壳把它放回海里：一景发光，一景睡进去。',
    existing: [
      ['jelly_sundae', '专属'],
      ['jelly_trampoline', '专属'],
      ['desk', '共享'],
      ['bath', '共享']
    ],
    newScenes: ['jelly_lantern', 'jelly_shell']
  },
  {
    id: 'mochi',
    label: '麻糬',
    en: 'Mochi',
    line: '冬天钻被炉，节日被木杵敲。',
    note: '温泉与烤炉都在被处理。被炉让它躲起来，捣年糕让它被敲——它好像都不介意。',
    existing: [
      ['mochi_onsen', '专属'],
      ['mochi_grill', '专属'],
      ['swing', '共享'],
      ['rain', '共享']
    ],
    newScenes: ['mochi_kotatsu', 'mochi_pound']
  },
  {
    id: 'owl',
    label: '猫头鹰',
    en: 'Owl',
    line: '白天值班，夜里读书。',
    note: '观星与书堆都在用脑。壁炉让它睡，送信让它动。',
    existing: [
      ['owl_stargazing', '专属'],
      ['owl_books', '专属'],
      ['swing', '共享'],
      ['desk', '共享']
    ],
    newScenes: ['owl_hearth', 'owl_letter']
  }
];

const HERO = ['cat_yarn', 'cloud_rainbow', 'ghost_tea', 'owl_hearth'];
const SCENE_CATALOGUE = new Map(catalogue.scenes.map(scene => [scene.id, scene]));
const SHEET_URLS = import.meta.glob('../../../data/scenes/*/*.png', { eager: true, query: '?url', import: 'default' });

const assetUrl = rel => SHEET_URLS[`../../../data/scenes/${rel}`];

const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
const params = new URLSearchParams(window.location.search);
const state = { clock: 0, playing: !reducedMotion, mounts: [], ready: false, dirty: true };

function loadImage(src) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error(`missing image: ${src}`));
    image.src = src;
  });
}

/** The idle figure's ink centre and bottom, measured as the app measures the pet's feet. */
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

async function loadPet(meta) {
  const base = `/${meta.id}/`;
  const files = await Promise.all(['pet', 'animations', 'anchors'].map(async name => {
    const response = await fetch(`${base}${name}.json`);
    if (!response.ok) throw new Error(`${name}.json: ${response.status}`);
    return response.json();
  }));
  const [pet, animations, anchors] = files;
  const names = new Set(['idle', 'sleep', ...meta.existing.map(([id]) => id)]);
  const wanted = [...names].filter(name => animations.actions[name]);

  const images = {};
  const effects = {};
  await Promise.all(wanted.map(async name => {
    const def = animations.actions[name];
    images[name] = await loadImage(base + def.sheet);
    if (def.effect) effects[name] = await loadImage(base + def.effect);
  }));
  const expression = await loadImage(base + pet.expression);
  return {
    ...pet,
    id: meta.id,
    actions: animations.actions,
    anchors: anchors.actions,
    expression,
    images,
    effects,
    foot: measureInk(images.idle)
  };
}

function drawPet(pet, ctx, ms, options = {}) {
  const action = options.action ?? 'idle';
  const def = pet.actions[action];
  if (!def) return;
  const frame = Math.floor((Math.max(0, ms) * def.fps) / 1000) % def.frames;
  let expression = def.expressions?.[frame] ?? 0;

  if (action === 'idle' && def.blink) {
    const cycle = ms % 5200;
    if (cycle < 150) {
      const blink = pet.actions.blink.expressions;
      expression = blink[Math.min(blink.length - 1, Math.floor((cycle / 150) * blink.length))];
    }
  }

  const anchor = pet.anchors[action]?.[frame] ?? { body: [0, 0], face: [0, 0] };
  const pivot = { x: Math.round(pet.foot.x), y: Math.round(pet.foot.y) };
  const gaze = options.gaze ?? [0, 0];

  ctx.save();
  ctx.translate(Math.round(options.x ?? 0), Math.round(options.y ?? 0));
  if (options.rotate || options.scaleY !== undefined) {
    ctx.translate(pivot.x, pivot.y);
    ctx.rotate((options.rotate ?? 0) * Math.PI / 180);
    ctx.scale(1, options.scaleY ?? 1);
    ctx.translate(-pivot.x, -pivot.y);
  }
  ctx.drawImage(pet.images[action], frame * CANVAS, 0, CANVAS, CANVAS, anchor.body[0], anchor.body[1], CANVAS, CANVAS);
  ctx.drawImage(pet.expression, expression * CANVAS, 0, CANVAS, CANVAS, anchor.face[0] + gaze[0], anchor.face[1] + gaze[1], CANVAS, CANVAS);
  if (options.effect !== false && pet.effects[action]) {
    ctx.drawImage(pet.effects[action], frame * CANVAS, 0, CANVAS, CANVAS, 0, 0, CANVAS, CANVAS);
  }
  ctx.restore();
}

/** Scene props are painted with a pen bound to the current context; one pen per frame is cheapest. */
function renderSceneWithPen(canvas, pet, scene, ms, scale, penFactory) {
  const ctx = canvas.getContext('2d');
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  ctx.imageSmoothingEnabled = false;
  ctx.clearRect(0, 0, CANVAS, CANVAS);
  scene.draw(ctx, ms, {
    p: penFactory(ctx),
    pet,
    foot: pet.foot,
    drawPet: options => drawPet(pet, ctx, ms, options)
  });
}

function renderExisting(canvas, pet, scene, sheets, scale = 2) {
  const ctx = canvas.getContext('2d');
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  ctx.imageSmoothingEnabled = false;
  ctx.clearRect(0, 0, CANVAS, CANVAS);

  const align = { x: pet.foot.x - scene.foot[0], y: pet.foot.y - scene.foot[1] };
  const frame = scene.framesData[0];
  const anchor = pet.anchors[scene.id]?.[0] ?? { body: [0, 0], face: [0, 0] };

  ctx.drawImage(sheets.back, 0, 0, CANVAS, CANVAS, align.x, align.y, CANVAS, CANVAS);
  ctx.save();
  ctx.translate(align.x + frame.dx, align.y + frame.dy);
  ctx.translate(pet.foot.x, pet.foot.y);
  ctx.scale(frame.scale, frame.scale);
  ctx.translate(-pet.foot.x, -pet.foot.y);
  ctx.drawImage(pet.images[scene.id], 0, 0, CANVAS, CANVAS, anchor.body[0], anchor.body[1], CANVAS, CANVAS);
  ctx.drawImage(pet.expression, 0, 0, CANVAS, CANVAS, anchor.face[0], anchor.face[1], CANVAS, CANVAS);
  if (pet.effects[scene.id]) {
    ctx.drawImage(pet.effects[scene.id], 0, 0, CANVAS, CANVAS, 0, 0, CANVAS, CANVAS);
  }
  ctx.restore();
  ctx.drawImage(sheets.front, 0, 0, CANVAS, CANVAS, align.x, align.y, CANVAS, CANVAS);
}

function el(tag, className, html) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (html !== undefined) node.innerHTML = html;
  return node;
}

function sceneCard(scene, pet, penFactory) {
  const card = el('article', 'scene-card');
  const stage = el('div', 'scene-stage');
  stage.style.background = scene.tint;

  const canvas = document.createElement('canvas');
  canvas.width = CANVAS * 3;
  canvas.height = CANVAS * 3;
  canvas.className = 'scene-canvas';
  canvas.setAttribute('role', 'img');
  canvas.setAttribute('aria-label', `${scene.name}：${scene.line}`);
  stage.append(canvas);

  const body = el('div', 'scene-body');
  body.append(el('div', 'scene-title', `<h3>${scene.name}</h3><span>${scene.en}</span>`));
  body.append(el('p', 'scene-line', scene.line));

  const facts = el('dl', 'facts');
  const fact = (label, value) => {
    const row = el('div', 'fact');
    row.append(el('dt', '', label), el('dd', '', value));
    facts.append(row);
  };
  fact('道具', scene.props);
  fact('入场', `${scene.entry} · ${scene.entryNote}`);
  fact('时段', scene.moods.map(([label, weight]) => `${label} ${weight}`).join(' · '));
  fact('牵引', scene.nudge);
  body.append(facts);
  card.append(stage, body);

  state.mounts.push({ canvas, pet, scene, kind: 'new', scale: 3, penFactory });
  return card;
}

function existingFigure(sceneId, kind, pet, sheets) {
  const figure = el('figure', 'existing-tile');
  const frame = el('div', 'existing-frame');
  const canvas = document.createElement('canvas');
  canvas.width = CANVAS * 2;
  canvas.height = CANVAS * 2;
  canvas.setAttribute('role', 'img');
  const def = SCENE_CATALOGUE.get(sceneId);
  canvas.setAttribute('aria-label', `${def?.name ?? sceneId} · ${kind}`);
  frame.append(canvas);
  const caption = el('figcaption');
  caption.append(el('span', '', def?.name ?? sceneId), el('small', '', kind));
  figure.append(frame, caption);
  state.mounts.push({ canvas, pet, scene: def, sheets, kind: 'existing' });
  return figure;
}

function petSection(meta, pet, penFactory) {
  const section = el('section', 'pet');
  section.id = meta.id;

  const head = el('header', 'pet-head');
  const title = el('div', 'pet-title', `<h2>${meta.label}</h2><span>${meta.en}</span>`);
  head.append(title, el('p', 'pet-line', meta.line), el('p', 'pet-meta', '现有 2 专属 + 2 共享 · 新增 2 专属'));

  const body = el('div', 'pet-body');
  const rail = el('aside', 'pet-rail');
  rail.append(el('h3', 'rail-head', '现有四景'));
  const grid = el('div', 'existing-grid');
  for (const [id, kind] of meta.existing) {
    grid.append(existingFigure(id, kind, pet, pet.sheets[id]));
  }
  rail.append(grid, el('p', 'rail-note', meta.note));

  const cards = el('div', 'scene-grid');
  for (const id of meta.newScenes) {
    cards.append(sceneCard(SCENES[id], pet, penFactory));
  }

  body.append(rail, cards);
  section.append(head, body);
  return section;
}

function render() {
  for (const mount of state.mounts) {
    if (mount.kind === 'existing') {
      if (!mount.done) {
        renderExisting(mount.canvas, mount.pet, mount.scene, mount.sheets);
        mount.done = true;
      }
    } else {
      renderSceneWithPen(mount.canvas, mount.pet, mount.scene, state.clock, mount.scale, mount.penFactory);
    }
  }
  state.dirty = false;
}

function animate(now) {
  const delta = Math.max(0, Math.min(now - animate.last, 80));
  animate.last = now;
  if (state.playing && state.ready) {
    state.clock += delta;
    state.dirty = true;
  }
  if (state.dirty) render();
  requestAnimationFrame(animate);
}
animate.last = performance.now();

async function main() {
  const pets = Object.fromEntries(await Promise.all(PETS.map(async meta => [meta.id, await loadPet(meta)])));

  // Scene sheets for the existing four-scene strips.
  const needed = new Set(PETS.flatMap(meta => meta.existing.map(([id]) => id)));
  const sheets = {};
  await Promise.all([...needed].map(async id => {
    const def = SCENE_CATALOGUE.get(id);
    sheets[id] = {
      back: await loadImage(assetUrl(def.sheets.back)),
      front: await loadImage(assetUrl(def.sheets.front))
    };
  }));
  for (const meta of PETS) {
    for (const [id] of meta.existing) pets[meta.id].sheets = { ...(pets[meta.id].sheets ?? {}), [id]: sheets[id] };
  }

  // Hero collage.
  const hero = document.querySelector('#hero-art');
  HERO.forEach(id => {
    const scene = SCENES[id];
    const meta = PETS.find(item => item.id === scene.pet);
    const tile = el('figure', 'hero-tile');
    const canvas = document.createElement('canvas');
    canvas.width = CANVAS * 2;
    canvas.height = CANVAS * 2;
    canvas.setAttribute('role', 'img');
    canvas.setAttribute('aria-label', `${scene.name}：${scene.line}`);
    canvas.style.background = scene.tint;
    tile.append(canvas, el('figcaption', '', `${scene.name} · ${meta.label}`));
    hero.append(tile);
    state.mounts.push({ canvas, pet: pets[scene.pet], scene, kind: 'new', scale: 2, penFactory: pen });
  });

  // Pet index.
  const index = document.querySelector('#pet-index');
  for (const meta of PETS) {
    index.append(el('a', 'pet-chip', `<span>${meta.label}</span><b>+2</b>`));
    index.lastElementChild.href = `#${meta.id}`;
  }

  // Sections.
  const mainNode = document.querySelector('#pets');
  for (const meta of PETS) {
    mainNode.append(petSection(meta, pets[meta.id], pen));
  }

  document.querySelector('#status').hidden = true;
  state.ready = true;
  state.dirty = true;

  const button = document.querySelector('#motion');
  const freeze = params.get('t');
  if (freeze !== null && Number.isFinite(Number(freeze))) {
    state.playing = false;
    state.clock = Math.max(0, Number(freeze));
  }
  button.textContent = state.playing ? '暂停动画' : '播放动画';
  button.addEventListener('click', () => {
    state.playing = !state.playing;
    button.textContent = state.playing ? '暂停动画' : '播放动画';
      state.dirty = true;
  });
  requestAnimationFrame(animate);
}

main().catch(error => {
  console.error(error);
  document.querySelector('#status').hidden = true;
  const failure = document.querySelector('#error');
  failure.hidden = false;
  document.querySelector('#error-file').textContent = error.message;
});

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { PetEngine } from '../src/pet-engine.js';

// Fixture has the shape assemblePet gives; the rules are what is under test.
// Only yawn declares a returnTo, as in the dog's file: everything else returns to the held state, or idle.
const still = n => Array.from({ length: n }, () => ({ head: [0, 0], face: [0, 0], body: [0, 0] }));
const pet = {
  roles: {
    idle: 'idle', study: 'study', thinking: 'thinking', celebrate: 'celebrate', encourage: 'encourage', sleep: 'sleep',
    click_react: 'click_react', pickup: 'pickup', look_around: 'look_around', yawn: 'yawn', stretch: 'stretch'
  },
  idlePool: ['look_around', 'stretch'],
  blink: [1, 2, 1],
  actions: {
    idle: { frames: 5, fps: 5, loop: true, blink: true, anchors: still(5) },
    study: { frames: 6, fps: 6, loop: true, blink: true, expressions: [3, 3, 0, 4, 4, 0], anchors: still(6) },
    thinking: { frames: 4, fps: 3, loop: true, blink: true, anchors: still(4) },
    celebrate: { frames: 6, fps: 10, loop: false, anchors: still(6) },
    click_react: { frames: 4, fps: 12, loop: false, anchors: still(4) },
    pickup: { frames: 4, fps: 6, loop: true, anchors: still(4) },
    look_around: { frames: 6, fps: 6, loop: false, anchors: still(6) },
    stretch: { frames: 6, fps: 6, loop: false, anchors: still(6) },
    yawn: { frames: 5, fps: 6, loop: false, returnTo: 'sleep', anchors: still(5) },
    sleep: { frames: 4, fps: 3, loop: true, expressions: [8, 8, 8, 8], anchors: still(4) }
  }
};

const make = (opts) => new PetEngine(pet, opts);

test('loops a looping action and wraps the frame index', () => {
  const e = make();
  e.handle('ExplanationShown');
  e.update(1000); // six frames at 6 fps (1000 ms), so the sixth frame wraps to 0
  assert.equal(e.state.frame, 0);
  assert.equal(e.state.action, 'study');
});

test('a single-play action returns to idle after its last frame when nothing is held', () => {
  const e = make();
  e.handle('KnownMarked');
  e.update(1000 * 6 / 10 + 1); // 6 frames at 10 fps, then one more ms
  assert.equal(e.state.action, 'idle');
  assert.equal(e.state.frame, 0);
});

test('a one-shot reaction returns to the reading that was held before it', () => {
  const e = make();
  e.handle('ExplanationShown');
  e.handle('Click');
  assert.equal(e.state.action, 'click_react');
  e.update(400); // 4 frames at 12 fps take 333 ms, rounded up to a clear margin
  assert.equal(e.state.action, 'study');
});

test('an event at the same tier waits for the one-shot before it, and is dropped if it arrives too late', () => {
  const e = make();
  e.handle('ExplanationShown');
  assert.equal(e.state.action, 'study');
  e.handle('KnownMarked'); // same tier as reading: dropped while reading plays
  assert.equal(e.state.action, 'study');
});

test('an event waits for a click to finish, and a selection does not interrupt a click', () => {
  const e = make();
  e.handle('Click');
  e.handle('SelectionShown');
  assert.equal(e.state.action, 'click_react');
});

test('a higher tier interrupts a lower one: a poke interrupts reading', () => {
  const e = make();
  e.handle('ExplanationShown');
  e.handle('Click');
  assert.equal(e.state.action, 'click_react');
});

test('thinking gives way to the explanation that completes it', () => {
  const e = make();
  e.handle('ExplanationRequested');
  assert.equal(e.state.action, 'thinking');
  e.handle('ExplanationShown');
  assert.equal(e.state.action, 'study');
});

test('pickup beats everything, and release returns to idle, not to the reading held before the grab', () => {
  const e = make();
  e.handle('ExplanationShown');
  e.handle('DragStart');
  e.handle('Click'); // ignored while carried
  assert.equal(e.state.action, 'pickup');
  e.handle('DragEnd');
  assert.equal(e.state.action, 'idle');
});

test('ending the explanation ends the reading and returns to idle', () => {
  const e = make();
  e.handle('ExplanationShown');
  e.handle('ExplanationHidden');
  assert.equal(e.state.action, 'idle');
});

test('a budget pause sleeps until resumed, and a poke wakes it and the sleep returns', () => {
  const e = make();
  e.handle('BudgetPaused');
  assert.equal(e.state.action, 'sleep');
  e.handle('Click');
  assert.equal(e.state.action, 'click_react');
  e.update(400);
  assert.equal(e.state.action, 'sleep');
  e.handle('BudgetResumed');
  assert.equal(e.state.action, 'idle');
});

test('an unknown event is an error, not a silent no-op', () => {
  assert.throws(() => make().handle('Unknown'), /unknown pet event/);
});

test('frame index and expression come from the same clock', () => {
  const e = make();
  e.handle('ExplanationShown');
  e.update(340); // two frames at 6 fps (333 ms)
  assert.equal(e.state.frame, 2);
  assert.equal(e.state.anchors.body[0], 0);
  assert.equal(e.state.expression, 0);
  e.update(200); // frame 3 uses the look-left eyes
  assert.equal(e.state.expression, 4);
});

test('blink runs on blink actions only and closes the eyes for three steps', () => {
  const e = make({ blinkEvery: 1000 });
  e.update(1000); // timer fires, blink starts
  assert.equal(e.state.expression, 1);
  e.update(80);
  assert.equal(e.state.expression, 2);
  e.update(80);
  assert.equal(e.state.expression, 1);
  e.update(80);
  assert.equal(e.state.expression, 0);
  e.handle('BudgetPaused');
  e.update(5000);
  assert.equal(e.state.expression, 8); // sleep draws its own eyes and never blinks
});

test('pause freezes time and step() advances one frame', () => {
  const e = make();
  e.handle('ExplanationShown');
  e.pause();
  e.update(10000);
  assert.equal(e.state.frame, 0);
  e.step();
  assert.equal(e.state.frame, 1);
  e.resume();
  assert.equal(e.state.paused, false);
});

test('setFps overrides the action fps and null restores it', () => {
  const e = make();
  e.handle('ExplanationShown');
  e.setFps(12);
  assert.equal(e.state.fps, 12);
  e.setFps(null);
  assert.equal(e.state.fps, 6);
});

test('a random idle action starts after the cooldown and never repeats back to back', () => {
  const e = make({ random: () => 0 }); // cooldown 20 s, always the first candidate
  e.update(19999);
  assert.equal(e.state.action, 'idle');
  e.update(1); // the pick happens before any frame advances, so the new action is visible
  assert.equal(e.state.action, 'look_around');
  e.update(1100); // look_around (1 s) ends and returns to idle
  assert.equal(e.state.action, 'idle');
  e.update(19999);
  e.update(1);
  assert.equal(e.state.action, 'stretch'); // look_around is excluded this time
});

test('three quiet minutes make the dog yawn, and the yawn leads to sleep', () => {
  const e = make({ random: () => 0.999 });
  let yawned = false;
  for (let t = 0; t < 1900 && !yawned; t += 1) {
    e.update(100);
    yawned = e.state.action === 'yawn';
  }
  assert.ok(yawned, 'no yawn within three minutes of quiet');
  e.update(1000);
  assert.equal(e.state.action, 'sleep');
});

test('a poke during the yawn cancels the return to sleep', () => {
  const e = make({ random: () => 0.999 });
  let yawned = false;
  for (let t = 0; t < 1900 && !yawned; t += 1) {
    e.update(100);
    yawned = e.state.action === 'yawn';
  }
  assert.ok(yawned, 'no yawn within three minutes of quiet');
  e.handle('Click');
  e.update(400);
  assert.equal(e.state.action, 'idle');
});

test('a request resets the quiet timer', () => {
  const e = make({ random: () => 0.999 });
  e.update(170000);
  e.handle('ExplanationShown');
  e.handle('ExplanationHidden');
  e.update(15000); // 185 s in total, but only 15 s since the request
  assert.notEqual(e.state.action, 'yawn');
});

test('an explanation after the yawn wakes the dog into reading (the return to sleep is situational)', () => {
  const e = make({ random: () => 0.999 });
  let yawned = false;
  for (let t = 0; t < 1900 && !yawned; t += 1) {
    e.update(100);
    yawned = e.state.action === 'yawn';
  }
  assert.ok(yawned, 'no yawn within three minutes of quiet');
  e.update(1000);
  assert.equal(e.state.action, 'sleep');
  e.handle('ExplanationShown');
  assert.equal(e.state.action, 'study');
});

test('thinking gives up after its timeout when no explanation comes', () => {
  const e = make({ random: () => 0.999 });
  e.handle('ExplanationRequested');
  e.update(29900);
  assert.equal(e.state.action, 'thinking');
  e.update(200);
  assert.equal(e.state.action, 'idle');
});

test('with only one random choice left it is never repeated back to back', () => {
  const m = structuredClone(pet);
  delete m.actions.stretch;
  const e = new PetEngine(m, { random: () => 0 });
  e.update(19999);
  e.update(1); // the pick happens before any frame advances, so the new action is visible
  assert.equal(e.state.action, 'look_around');
  e.update(1100); // look_around ends
  assert.equal(e.state.action, 'idle');
  e.update(19999);
  e.update(1);
  assert.equal(e.state.action, 'idle'); // the only candidate is the one just played, so the dog stays idle
});

test('blink runs only where the action declares it, and sleep never blinks', () => {
  const e = make({ blinkEvery: 1000 });
  assert.equal(e.def.blink, true);
  e.handle('BudgetPaused');
  assert.equal(e.def.blink, undefined);
  e.update(5000);
  assert.equal(e.state.expression, 8);
});

test('the roles decide which action plays, so a pet can name its own actions', () => {
  const m = structuredClone(pet);
  m.actions.bounce = m.actions.celebrate;
  delete m.actions.celebrate;
  m.roles.celebrate = 'bounce';
  const e = new PetEngine(m);
  e.handle('KnownMarked');
  assert.equal(e.state.action, 'bounce');
});

test('engine source imports no DOM, CSS or window and no graphics API', () => {
  const src = readFileSync(fileURLToPath(new URL('../src/pet-engine.js', import.meta.url)), 'utf8');
  const code = src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
  assert.doesNotMatch(code, /\bdocument\b|\bwindow\b|\bHTML\w*|\bCSS\b|\bcanvas\b|\bimport\b/i);
});

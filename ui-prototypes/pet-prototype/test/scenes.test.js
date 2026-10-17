import { test } from 'node:test';
import assert from 'node:assert/strict';
import { CELL, PALETTE, partBox } from '../src/pixel-art.js';
import { SCENES, sceneOffset, sceneParts, sceneMotion } from '../src/scenes.js';
import { angryAction, angryFace } from '../src/angry.js';

const PETS = ['cat', 'dog', 'mochi', 'owl'];
/** The pet's canvas, in units: a prop that lands outside it is drawn where nothing shows. */
const CANVAS = 88;
/** A prop that stands on the floor may run a little past the bottom edge: the bath's tub does, on purpose. */
const FLOOR_BLEED = 2 * CELL;

/** Every prop the scenes can draw, at a spread of clock times, for every pet's alignment. */
function everyPart() {
  const out = [];
  for (const scene of SCENES) {
    for (const pet of PETS) {
      for (const ms of [0, 350, 1200, 5000, 61000]) {
        for (const layer of ['back', 'front']) {
          for (const part of sceneParts(scene, ms, layer, sceneOffset(pet))) out.push({ scene, ms, part });
        }
      }
    }
  }
  return out;
}

test('scenes are distinct, named, and play an action the pets have', () => {
  assert.equal(new Set(SCENES.map(s => s.id)).size, SCENES.length);
  for (const scene of SCENES) {
    assert.match(scene.id, /^[a-z]+$/);
    assert.ok(scene.name.length > 0, `${scene.id} needs a name`);
    assert.equal(scene.petAction, 'idle');
    assert.ok(scene.parts(0).length > 0, `${scene.id} draws nothing`);
  }
});

test('props stay inside the pet canvas, on every pet', () => {
  for (const { scene, ms, part } of everyPart()) {
    const { left, top, right, bottom } = partBox(part);
    const where = `${scene.id} at ${ms}ms`;
    assert.ok(left >= 0 && top >= 0, `${where} starts outside the canvas: ${left},${top}`);
    assert.ok(right <= CANVAS && bottom <= CANVAS + FLOOR_BLEED, `${where} ends outside the canvas: ${right},${bottom}`);
  }
});

test('prop art is rectangular and only names palette colours', () => {
  for (const { scene, part } of everyPart()) {
    if (!part.art) {
      assert.ok(PALETTE[part.colour], `${scene.id} uses colour ${part.colour}`);
      continue;
    }
    const width = part.art[0].length;
    for (const row of part.art) assert.equal(row.length, width, `${scene.id} has a ragged art row: ${row}`);
    for (const row of part.art) {
      for (const ch of row) {
        if (ch === '.') continue;
        assert.ok(PALETTE[part.colours?.[ch]], `${scene.id} draws '${ch}' with no palette colour`);
      }
    }
  }
});

/** Where a box ends up after the motion: its four corners moved, and the box straightened again. */
function movedBox(box, motion) {
  const rad = (motion.angle * Math.PI) / 180;
  const cos = Math.cos(rad);
  const sin = Math.sin(rad);
  const corners = [[box.left, box.top], [box.right, box.top], [box.right, box.bottom], [box.left, box.bottom]]
    .map(([x, y]) => {
      const dx = (x - motion.at[0]) * motion.by;
      const dy = (y - motion.at[1]) * motion.by;
      return [
        motion.at[0] + dx * cos - dy * sin + motion.shift[0],
        motion.at[1] + dx * sin + dy * cos + motion.shift[1]
      ];
    });
  return {
    left: Math.min(...corners.map(p => p[0])),
    top: Math.min(...corners.map(p => p[1])),
    right: Math.max(...corners.map(p => p[0])),
    bottom: Math.max(...corners.map(p => p[1]))
  };
}

test('a scene moves the pet and its props by one transform, gently', () => {
  for (const scene of SCENES) {
    const sizes = [];
    for (let ms = 0; ms < 8000; ms += 80) {
      const motion = sceneMotion(scene, ms, sceneOffset('cat'));
      if (!motion) continue;
      assert.ok(motion.at.every(Number.isFinite));
      assert.ok(Math.abs(motion.angle) <= 10, `${scene.id} turns ${motion.angle} degrees`);
      assert.ok(motion.by > 0.9 && motion.by < 1.1, `${scene.id} scales by ${motion.by}`);
      assert.ok(motion.shift.every(Number.isFinite) && motion.shift.every(by => Math.abs(by) <= 12),
        `${scene.id} moves the rig by ${motion.shift}`);
      sizes.push(motion.by);
    }
    if (sizes.some(by => by !== 1)) {
      assert.ok(Math.min(...sizes) < 1 && Math.max(...sizes) > 1, `${scene.id} never comes towards the reader`);
    }
  }
});

test('what the scene moves with the pet stays inside the canvas all the way through the motion', () => {
  for (const scene of SCENES) {
    for (const pet of PETS) {
      const offset = sceneOffset(pet);
      for (let ms = 0; ms < 8000; ms += 80) {
        const motion = sceneMotion(scene, ms, offset);
        if (!motion) continue;
        for (const layer of ['back', 'front']) {
          for (const part of sceneParts(scene, ms, layer, offset)) {
            if (!part.rig) continue;
            const box = movedBox(partBox(part), motion);
            assert.ok(box.left >= 0 && box.top >= 0 && box.right <= CANVAS && box.bottom <= CANVAS,
              `${scene.id} carries a prop off the canvas: ${JSON.stringify(box)} at ${ms}ms`);
          }
        }
      }
    }
  }
});

test('the pet is placed by whole cells, so nothing lands off the pixel grid', () => {
  for (const scene of SCENES) {
    for (const frame of [0, 1, 2, 3]) {
      const [dx, dy] = scene.place({ frame });
      assert.ok(Number.isInteger(dx / CELL) && Number.isInteger(dy / CELL), `${scene.id} places the pet off the grid`);
    }
  }
});

test('the alignment table covers the four shipped pets and the dog sits a cell lower', () => {
  assert.deepEqual(PETS.map(sceneOffset), [[0, 0], [0, 1], [-1, 0], [-1, 0]]);
  assert.deepEqual(sceneOffset('axolotl'), [0, 0]);
});

test('angry shakes the pet and draws a face on every pet, inside the canvas', () => {
  const pet = { roles: { idle: 'idle' }, actions: { idle: { frames: 5, sheet: 'sprites/body/idle.png' } } };
  const angry = angryAction(pet);
  assert.equal(angry.loop, false);
  assert.equal(angry.sheet, 'sprites/body/idle.png');
  assert.equal(angry.anchors.length, angry.frames);
  assert.ok(angry.anchors.some(({ body }) => body[0] !== 0), 'the pet has to move');
  for (const petId of PETS) {
    const parts = angryFace(petId, 0);
    assert.ok(parts.length > 0, `${petId} has no eyes to draw a face from`);
    for (const part of parts) {
      const { left, top, right, bottom } = partBox(part);
      assert.ok(left >= 0 && top >= 0 && right <= CANVAS && bottom <= CANVAS,
        `${petId} draws its angry face outside the canvas: ${left},${top},${right},${bottom}`);
    }
  }
  assert.deepEqual(angryFace('axolotl', 0), []);
});

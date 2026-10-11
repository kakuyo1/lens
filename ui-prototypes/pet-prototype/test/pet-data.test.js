import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { assemblePet } from '../src/pet-data.js';

// The dog's data is read in place from the repository: the prototype keeps no copy of it.
const dog = new URL('../../../data/pet/dog/', import.meta.url);
const read = name => JSON.parse(readFileSync(new URL(name, dog), 'utf8'));
const sheetSize = path => {
  const buf = readFileSync(new URL(path, dog));
  return { width: buf.readUInt32BE(16), height: buf.readUInt32BE(20) }; // IHDR width and height
};
const pet = read('pet.json');
const animations = read('animations.json');
const anchors = read('anchors.json');
const accessories = read('accessories.json').accessories;

test('the roles the state machine needs all name actions that exist, and returnTo names a mapped role', () => {
  const required = ['idle', 'study', 'thinking', 'celebrate', 'encourage', 'sleep', 'click_react', 'pickup', 'look_around', 'yawn'];
  for (const role of required) assert.ok(animations.actions[animations.roles[role]], `role ${role} has no playback`);
  for (const [name, def] of Object.entries(animations.actions)) {
    if (def.returnTo) assert.ok(animations.roles[def.returnTo], `${name}.returnTo names an unmapped role`);
  }
});

test('the idle pool names one-shot actions only', () => {
  assert.ok(animations.idlePool.length > 0);
  for (const name of animations.idlePool) {
    assert.ok(animations.actions[name], `idlePool names missing action ${name}`);
    assert.equal(animations.actions[name].loop, false, `${name} in the idle pool must be a one-shot`);
  }
});

test('every sheet is frames x canvas wide and canvas tall, with one anchor per frame', () => {
  for (const [name, a] of Object.entries(animations.actions)) {
    if (name === 'blink') continue; // the blink is the expression sequence, not a playable action
    assert.deepEqual(sheetSize(a.sheet), { width: a.frames * pet.canvas, height: pet.height }, `${name}: sheet`);
    if (a.effect) assert.deepEqual(sheetSize(a.effect), { width: a.frames * pet.canvas, height: pet.height }, `${name}: effect`);
    assert.equal(anchors.actions[name].length, a.frames, `${name}: anchors`);
  }
});

test('the expression sheet and every accessory are canvas-sized cells, and the actions they name exist', () => {
  const { width, height } = sheetSize(pet.expression);
  assert.equal(height, pet.canvas);
  assert.equal(width % pet.canvas, 0);
  for (const a of Object.values(animations.actions)) {
    for (const e of a.expressions ?? []) assert.ok(e < width / pet.canvas, `expression ${e} out of range`);
  }
  for (const acc of accessories) {
    assert.deepEqual(sheetSize(acc.asset), { width: pet.canvas, height: pet.height }, `${acc.id}: asset`);
    for (const action of acc.supportedActions) {
      assert.ok(animations.actions[action], `${acc.id} supports missing action ${action}`);
      assert.notEqual(action, animations.roles.sleep, `${acc.id} must not show while the dog sleeps`);
    }
  }
});

test('assemblePet keeps blink out of the playable actions and hands it over as the expression sequence', () => {
  const assembled = assemblePet({ pet, animations, anchors, accessories: read('accessories.json') });
  assert.equal('blink' in assembled.actions, false);
  assert.deepEqual(assembled.blink, animations.actions.blink.expressions);
});

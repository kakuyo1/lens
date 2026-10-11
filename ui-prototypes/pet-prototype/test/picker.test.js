import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { actionOrder, keepWorn } from '../src/pet-picker.js';
import { listPets } from '../tools/build-pet-html.mjs';

test('the picker lists the pet folders that have a pet.json: the designed order, then unknown ids', t => {
  const root = mkdtempSync(path.join(tmpdir(), 'pet-list-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  for (const id of ['mochi', 'dog', 'owl', 'cat', 'axolotl', 'notes']) mkdirSync(path.join(root, id));
  for (const id of ['mochi', 'dog', 'cat', 'axolotl']) writeFileSync(path.join(root, id, 'pet.json'), '{}');
  writeFileSync(path.join(root, 'owl', 'animations.json'), '{}'); // still being authored: no pet.json yet
  assert.deepEqual(listPets(root), ['dog', 'mochi', 'cat', 'axolotl']);
});

test('switching pet drops a worn accessory the new catalogue lacks and keeps the one it shares', () => {
  const dog = [{ id: 'hat', slot: 'head' }, { id: 'glasses', slot: 'face' }, { id: 'bowtie', slot: 'body' }];
  const mochi = [{ id: 'sprout', slot: 'head' }, { id: 'glasses', slot: 'face' }, { id: 'scarf', slot: 'body' }];
  const worn = new Map([['head', 'hat'], ['face', 'glasses'], ['body', 'bowtie']]);
  assert.deepEqual([...keepWorn(worn, mochi)], [['face', 'glasses']]);
  assert.deepEqual([...keepWorn(new Map([['head', 'hat']]), mochi)], []);
  assert.deepEqual([...keepWorn(new Map(), dog)], []);
});

test('action pills: role actions in role order, then the idle pool; a role with no action gets no pill', () => {
  const pet = {
    roles: { idle: 'idle', study: 'study', stretch: 'stretch' },
    idlePool: ['look_around', 'stretch', 'wobble'],
    actions: { idle: {}, study: {}, look_around: {}, wobble: {} }
  };
  assert.deepEqual(actionOrder(pet), ['idle', 'study', 'look_around', 'wobble']);
});

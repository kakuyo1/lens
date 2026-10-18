import { test } from 'node:test';
import assert from 'node:assert/strict';
import { HoverSense } from '../src/hover-sense.js';

const quick = (extra = {}) => new HoverSense({ noticeDelayMs: 100, dwellDelayMs: 300, cooldownMs: 1000, rearmMs: 100, ...extra });

/** Walks into the near ring and returns once the greeting is due. */
function greet(sense, distance = 10) {
  sense.update(0, 40);
  sense.update(50, 30);
  sense.update(50, distance);
  return sense.update(100, distance);
}

test('a pointer crossing the rings only makes the eyes follow', () => {
  const sense = quick();
  assert.deepEqual(sense.update(0, 40), { zone: 'outside', notice: false, dwell: false });
  assert.deepEqual(sense.update(50, 20), { zone: 'gaze', notice: false, dwell: false });
  assert.deepEqual(sense.update(50, 10), { zone: 'near', notice: false, dwell: false });
  assert.deepEqual(sense.update(50, 40), { zone: 'outside', notice: false, dwell: false });
  assert.equal(sense.greeted, false);
});

test('holding in the near ring greets once, then earns one fuller reaction', () => {
  const sense = quick();
  const hello = greet(sense);
  assert.equal(hello.notice, true);
  assert.equal(hello.dwell, false);
  assert.equal(sense.greeted, true);

  assert.equal(sense.update(100, 10, { idle: false }).dwell, false, 'does not interrupt another action');
  assert.equal(sense.update(100, 10).dwell, true);
  assert.equal(sense.finished, true);
  assert.equal(sense.update(100, 10).dwell, false, 'the fuller reaction plays once per approach');
  assert.equal(sense.update(100, 10).notice, false);
});

test('the greeting waits for the pet to be idle', () => {
  const sense = quick();
  sense.update(0, 40);
  sense.update(50, 30);
  sense.update(50, 10);
  assert.equal(sense.update(200, 10, { idle: false }).notice, false);
  assert.equal(sense.update(50, 10).notice, true);
});

test('leaving and re-entering starts a new approach after the rearm delay', () => {
  const sense = quick({ cooldownMs: 0 });
  greet(sense);
  sense.update(100, 40);
  assert.equal(sense.update(100, 40).zone, 'outside');
  assert.equal(sense.greeted, false, 'the approach clears after the rearm delay');
  assert.equal(greet(sense).notice, true);
});

test('the rings keep their edge for a small retreat, then let go', () => {
  const sense = quick();
  sense.update(0, 40);
  sense.update(50, 10);
  assert.equal(sense.zone, 'near');
  assert.equal(sense.update(50, 18).zone, 'near', 'inside the exit margin');
  assert.equal(sense.update(50, 22).zone, 'gaze');
  assert.equal(sense.update(50, 31).zone, 'outside');
});

test('cooling down turns a second hello into a quiet one', () => {
  const sense = quick({ cooldownMs: 500 });
  greet(sense);
  sense.update(150, 40);
  assert.equal(sense.update(100, 40).zone, 'outside');
  assert.equal(greet(sense).notice, false);
  assert.equal(sense.quiet, true, 'the second hello is a quiet one');
  assert.equal(sense.finished, false, 'the quiet hello earns no fuller reaction either');

  sense.update(100, 40);
  sense.update(200, 40);
  assert.equal(greet(sense).notice, true, 'after the cooldown the pet is happy to greet again');
});

test('a disabled greeting still leaves room for the fuller reaction', () => {
  const sense = quick();
  sense.update(0, 40);
  sense.update(50, 10);
  assert.equal(sense.update(100, 10, { notice: false }).notice, false);
  assert.equal(sense.update(100, 10, { notice: false }).dwell, false);
  assert.equal(sense.update(100, 10, { notice: false }).dwell, true);
});

test('a disabled fuller reaction never plays', () => {
  const sense = quick();
  greet(sense);
  assert.equal(sense.update(100, 10, { dwell: false }).dwell, false);
  assert.equal(sense.update(100, 10, { dwell: false }).dwell, false);
});

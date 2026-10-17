/**
 * 生气 for the four shipped pets. Prototype only: the data files carry no angry action yet, so this builds one
 * from what is already there — the pet's idle sheet as the body, per-frame anchors for the shake, and a face
 * drawn over the expression layer because the shared expression sheet has no angry pattern to point at.
 *
 * The shipped version of this is two changes to the art pipeline, not code here: one more entry in
 * EYE_PATTERNS (render_pet.py, build-assets.py) and an `angry` action in each recipe, reusing the same
 * idle sheet, shake anchors and a puff effect.
 */

import { CELL, art, paintPart, rect } from './pixel-art.js';

/** Eye cells per pet: scripts/pet/recipes/<id>.json, and scripts/pet/dog/sprite.json for the dog. */
const EYES = {
  cat: [[6, 5], [10, 5]],
  dog: [[7, 3], [10, 3]],
  mochi: [[5, 6], [11, 6]],
  owl: [[5, 5], [11, 5]]
};

/** One shake of the whole pet, in canvas units, snapped to whole cells. The idle sheet has five frames. */
const SHAKE = [[0, 0], [-3, 0], [3, 0], [-3, 0], [0, -3], [0, 0]];

/**
 * The angry action for one pet: its own idle body sheet, shaken, with the angry face drawn instead of the
 * expression layer.
 * @param {object} pet Assembled by pet-data.js.
 * @returns {object} An action definition the engine and the renderer accept as they accept any other.
 */
export function angryAction(pet) {
  const idle = pet.actions[pet.roles.idle];
  const frames = Math.min(SHAKE.length, idle.frames);
  return {
    frames,
    fps: 10,
    loop: false,
    sheet: idle.sheet,
    overlay: 'angry',
    anchors: SHAKE.slice(0, frames).map(([dx, dy]) => ({ head: [dx, dy], face: [dx, dy], body: [dx, dy] }))
  };
}

/**
 * The angry face, in the pet's own grid: a narrowed eye under a brow, a frown below, a pulsing mark overhead and
 * two puffs of steam. Authored relative to the pet's eyes, so it lands on every pet. The eye and brow rows are
 * one canvas unit each — the size the pipeline draws the pets' own eye patterns at (render_pet.py face_frame),
 * not the three of a cell.
 * @param {string} petId
 * @param {number} ms Scene clock, for the pulse.
 * @returns {object[]} Parts to paint with the face anchor as the offset.
 */
export function angryFace(petId, ms) {
  const eyes = EYES[petId];
  if (!eyes) return [];
  const [[lx, ly], [rx, ry]] = eyes;
  const mouthX = lx + Math.floor((rx - lx - 2) / 2);
  const pulse = Math.floor(ms / 200) % 2;
  const ink = { '#': 'ink' };
  return [
    // The scowl is the pair: the eye's top pixel nearest the other eye is off, and the brow above it slopes in.
    art('front', false, [lx, ly], ['##.', '###', '###'], ink, 1),
    art('front', false, [rx, ry], ['.##', '###', '###'], ink, 1),
    art('front', false, [lx, ly - 1], ['#..', '.##'], ink, 1),
    art('front', false, [rx, ry - 1], ['..#', '##.'], ink, 1),
    rect('front', false, [mouthX, ly + 3], [1, 1], 'ink'), // a frown: the corners down, the middle up
    rect('front', false, [mouthX + 2, ly + 3], [1, 1], 'ink'),
    rect('front', false, [mouthX + 1, ly + 2], [1, 1], 'ink'),
    // The anger mark, over the head. Two puffs of steam sat on the ears looking like grey blocks, so the mark
    // carries it alone, and pulses a cell higher.
    art('front', false, [mouthX - 2, ly - 9 - pulse],
      ['...#...', '...#...', '...#...', '#######', '...#...', '...#...', '...#...'], { '#': 'rose' }, 1)
  ];
}

/**
 * Paints the angry face under the current layer transform.
 * @param {CanvasRenderingContext2D} ctx
 * @param {{anchors: {face: number[]}}} state From PetEngine.state.
 * @param {number} ms
 * @param {string} petId
 */
export function drawAngry(ctx, state, ms, petId) {
  const [fx, fy] = state.anchors.face;
  for (const part of angryFace(petId, ms)) paintPart(ctx, part, [fx / CELL, fy / CELL]);
}

/**
 * Scene catalogue: what the pet is doing when it is not just standing there — a swing, a desk, a skateboard,
 * a bath, an umbrella in the rain.
 *
 * A scene is one prop rig plus one motion. The pet keeps playing its own action (`petAction`), so its frames,
 * anchors, blinks and accessories all still come from the data files; the scene adds the world around it:
 *
 *   place(state)  Cells the pet itself is nudged by, so it lands on the seat or stands on the deck. The rest
 *                 of the scene does not follow: a desk stays put while the pet types.
 *   motion(ms)    How the pet and the props marked `rig` move together — a scale `by`, a rotation `angle`, or
 *                 both, about `at`, plus a `shift` of the whole rig, all in canvas units. A prop that has to stay
 *                 rigid — the swing's plank — instead places itself from ms and is not marked `rig`.
 *   parts(ms)     The props. `layer` picks behind-pet or in-front, `rig` whether the motion carries them.
 *
 * Coordinates are cells for the recipe pets (see pixel-art.js), authored against the cat's feet at (58, 81)
 * canvas units. `sceneOffset` shifts a scene for a pet whose art sits elsewhere on the canvas.
 */

import { CELL, art, rect } from './pixel-art.js';

/**
 * How far the swing's plank rides up and down, in whole cells, in step with the pet's size: it sits lower when the
 * swing is nearer the reader. Whole cells keep the plank and the rope ends on the art's own grid.
 * @param {number} ms Scene clock.
 * @returns {number} Cells, -2 to 2.
 */
const swingRide = ms => Math.round(2 * Math.sin(ms / 560));

/** Recipe pets put their feet there; the dog's are one cell lower and mochi and owl sit a cell left. */
const OFFSETS = { cat: [0, 0], dog: [0, 1], mochi: [-1, 0], owl: [-1, 0] };

/** @param {string} petId @returns {number[]} Cells to shift a scene by, for this pet's art. */
export const sceneOffset = petId => OFFSETS[petId] ?? [0, 0];

/** A bubble (a ring with a highlight), and the canopy of the umbrella. */
const BUBBLE = [
  '.###.',
  '#o..#',
  '#...#',
  '#...#',
  '.###.'
];
const CANOPY = [
  '....#####....',
  '..##ooooo##..',
  '.#ooooooooo#.',
  '#ooooooooooo#',
  '#.#.#.#.#.#.#'
];

/**
 * 荡秋千: two ropes and a plank, and the swing riding towards the reader and away again.
 *
 * Seen from the front, the arc has no sideways motion to draw. Two things move, and nothing is resized:
 *
 *   the pet     grows as the swing comes towards the reader and shrinks going back, scaled about its own feet so
 *               it never lifts off the plank. That is the whole of the perspective.
 *   the plank   rides up and down with it — lower when the swing is nearer, higher when it is further — and the
 *               ropes are redrawn to reach it from the bar each frame.
 *
 * The bar is where the ropes leave the top of the canvas, and it never moves. Scaling the ropes and the plank
 * along with the pet instead stretched the swing itself, which read as a picture being resized, not as a swing.
 */
const SWING = {
  id: 'swing',
  name: '荡秋千',
  petAction: 'idle',
  place: () => [-12, -9], // the pet's centre on the plank's centre, its feet at the plank's top
  motion: ms => ({ by: 1 + 0.07 * Math.sin(ms / 560), shift: [0, swingRide(ms) * CELL], at: [46.5, 72] }),
  parts: ms => {
    const ride = swingRide(ms);
    return [
      rect('back', false, [-4, -10], [1, 24 + ride], 'grey'), // the bar is the top of the canvas: ropes only grow down
      rect('back', false, [14, -10], [1, 24 + ride], 'grey'),
      rect('back', false, [-4, 14 + ride], [19, 1], 'sky'), // the plank, one cell lower for every cell of ride
      rect('back', false, [-4, 15 + ride], [19, 1], 'ink')
    ];
  }
};

/** 办公桌敲电脑: a desk in front, a laptop on it, the pet bobbing one cell as it types. */
const DESK = {
  id: 'desk',
  name: '敲电脑',
  petAction: 'idle',
  place: state => [0, state.frame % 2 ? -3 : 0],
  motion: () => null,
  parts: ms => {
    const line1 = 2 + (Math.floor(ms / 220) % 4); // the two lines of code in the display
    const line2 = 2 + (Math.floor(ms / 310) % 4);
    const puff = Math.floor(ms / 400) % 2; // the mug's steam
    return [
      rect('front', false, [1, 14], [17, 1], 'grey'), // desk top, then its front edge and legs
      rect('front', false, [1, 15], [17, 1], 'ink'),
      rect('front', false, [1, 16], [1, 2], 'ink'),
      rect('front', false, [17, 16], [1, 2], 'ink'),
      rect('front', false, [5, 9], [7, 4], 'grey'), // the laptop lid
      rect('front', false, [6, 10], [5, 2], 'sky'), // the display
      rect('front', false, [6, 10], [line1, 1], 'fur'),
      rect('front', false, [6, 11], [line2, 1], 'fur'),
      art('front', false, [4, 13], ['#.#.#.#.#'], { '#': 'ink' }), // the keyboard
      art('front', false, [15, 12], ['rrr', 'rrr'], { r: 'rose' }), // the mug
      art('front', false, [18, 12], ['r'], { r: 'rose' }),
      rect('front', false, [16, 10 - puff], [1, 1], 'grey'),
      rect('front', false, [17, 8 - puff], [1, 1], 'grey')
    ];
  }
};

/** 滑滑板: the pet rides a board, which drifts and tilts, wheels turning, dust and speed lines behind. */
const SKATE = {
  id: 'skate',
  name: '滑滑板',
  petAction: 'idle',
  place: () => [-6, -12], // centred on the deck, up four cells so the deck, trucks and wheels clear the ground
  motion: ms => ({ angle: 1.6 * Math.sin(ms / 430), at: [54, 81] }),
  parts: ms => {
    const corners = [[0, 0], [1, 0], [1, 1], [0, 1]];
    const spin = Math.floor(ms / 90) % 4;
    const [ax, ay] = corners[spin];
    const [bx, by] = corners[(spin + 2) % 4];
    const drift = (ms / 150) % 6;
    return [
      rect('back', false, [-9, 17], [28, 1], 'ink'), // the ground
      rect('back', false, [-2 - drift, 12], [4, 1], 'grey'), // speed lines, leaving to the left
      rect('back', false, [-1 - drift, 14], [4, 1], 'grey'),
      rect('back', false, [-2 - drift, 16], [4, 1], 'grey'),
      // The deck, its ends turned up. As three blocks the turned-up ends read as loose squares.
      art('back', true, [1, 12], ['.r.........r.', 'rrrrrrrrrrrrr'], { r: 'rose' }),
      rect('back', true, [3, 14], [2, 1], 'ink'), // the two trucks, over the wheels
      rect('back', true, [10, 14], [2, 1], 'ink'),
      rect('back', true, [3, 15], [2, 2], 'ink'), // wheels, one lit cell turning on each
      rect('back', true, [10, 15], [2, 2], 'ink'),
      rect('back', true, [3 + ax, 15 + ay], [1, 1], 'fur'),
      rect('back', true, [10 + bx, 15 + by], [1, 1], 'fur')
    ];
  }
};

/** A small bubble, for the ones that are not worth a full ring. */
const SMALL_BUBBLE = [
  '.##.',
  '#..#',
  '#..#',
  '.##.'
];

/**
 * The bath's bubbles: six of them, each with its own phase and rise time, so the tub never draws the same
 * columns in step. A bubble rises from the water to over the head within its rise share of the two-second
 * period and is then gone until its next turn; a shorter rise therefore reads as a faster bubble. The
 * period matches the scene sheet's eight frames at 4 fps, so the loop is seamless.
 */
const BATH_PERIOD = 2000;
const BATH_BUBBLES = [
  { x: -2, phase: 0.05, rise: 0.70, art: SMALL_BUBBLE },
  { x: 2, phase: 0.55, rise: 0.90, art: BUBBLE },
  { x: 6, phase: 0.22, rise: 0.60, art: SMALL_BUBBLE },
  { x: 10, phase: 0.78, rise: 0.85, art: BUBBLE },
  { x: 13, phase: 0.38, rise: 0.75, art: SMALL_BUBBLE }
];

/** 泡澡: a tub in front, the pet sunk to the chin in it, bubbles rising over its head. */
const BATH = {
  id: 'bath',
  name: '泡澡',
  petAction: 'idle',
  place: () => [0, 9], // down three cells: rim at the chin, the rest of the pet behind the tub
  motion: () => null,
  parts: ms => {
    const tub = [
      '###################', // rim, then the water, then the tub's front, tapering to the floor
      'sssssssssssssssssss',
      'ggggggggggggggggggg',
      'ggggggggggggggggggg',
      'ggggggggggggggggggg',
      '.ggggggggggggggggg.',
      '..ggggggggggggggg..',
      '..ggggggggggggggg..' // past the canvas edge: the pet is sunk to the chin and must not show below the tub
    ];
    const bubbles = BATH_BUBBLES.map(bubble => {
      const t = ((ms + bubble.phase * BATH_PERIOD) % BATH_PERIOD) / BATH_PERIOD;
      if (t >= bubble.rise) return null; // above the head and gone
      return art('front', false, [bubble.x, 10 - (16 * t) / bubble.rise], bubble.art, { '#': 'ink', o: 'fur' });
    }).filter(Boolean);
    return [art('front', false, [-2, 12], tub, { '#': 'ink', s: 'sky', g: 'grey' }), ...bubbles];
  }
};

/** 撑伞看雨: rain over everything, the pet holding an umbrella that sways a little. */
const RAIN = {
  id: 'rain',
  name: '撑伞看雨',
  petAction: 'idle',
  place: () => [0, 0],
  motion: ms => ({ angle: 2.5 * Math.sin(ms / 700), at: [58, 87] }),
  parts: ms => {
    const step = Math.floor(ms / 110);
    const drops = [];
    for (let c = 0; c < 14; c += 1) {
      for (let k = 0; k < 3; k += 1) {
        const x = -9 + c * 2;
        // One unit wide, three tall: at the width of a cell a drop reads as a blue brick, not rain.
        drops.push(art('back', false, [x, -10 + ((k * 9 + c * 4 + step) % 26)], ['#', '#', '#'], { '#': 'sky' }, 1));
      }
    }
    const ripple = Math.floor(ms / 320) % 2;
    return [
      ...drops,
      rect('back', false, [-9, 17], [28, 1], 'sky'), // the puddle
      rect('back', false, [-7 + ripple, 16], [3, 1], 'fur'),
      rect('back', false, [11 - ripple, 16], [3, 1], 'fur'),
      // Both behind the pet and well above its head: in front, the shaft came down over the face and the umbrella
      // read as mounted on the mouth. The canopy sits a little left of the head's centre, as asked.
      art('back', true, [1, -9], CANOPY, { '#': 'ink', o: 'rose' }),
      rect('back', true, [6, -4], [1, 13], 'ink')
    ];
  }
};

/** The five scenes, in the order the debug panel shows them. All four pets can play all of them. */
export const SCENES = [SWING, DESK, SKATE, BATH, RAIN];

/**
 * @param {object} scene
 * @param {number} ms Scene clock, which pauses with the pet.
 * @param {string} layer `back` or `front`.
 * @param {number[]} offset From sceneOffset().
 * @returns {object[]} The parts of that layer, moved to where this pet's art is.
 */
export function sceneParts(scene, ms, layer, offset) {
  return scene
    .parts(ms)
    .filter(part => part.layer === layer)
    .map(part => ({ ...part, at: [part.at[0] + offset[0], part.at[1] + offset[1]] }));
}

/**
 * @param {object} scene
 * @param {number} ms
 * @param {number[]} offset From sceneOffset().
 * @returns {{angle: number, by: number, shift: number[], at: number[]}|null} The transform the pet and the rigged
 *          props share, about a point in canvas units. Defaults are nothing: no turn, no move, own size.
 */
export function sceneMotion(scene, ms, offset) {
  const motion = scene.motion(ms);
  return motion && {
    angle: motion.angle ?? 0,
    by: motion.by ?? 1,
    shift: motion.shift ?? [0, 0],
    at: [motion.at[0] + offset[0] * 3, motion.at[1] + offset[1] * 3]
  };
}

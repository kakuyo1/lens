/**
 * The pet's own pixel grid, for everything drawn around it: the scene props and the angry face.
 * A cell is 3 canvas units and cell (0, 0) starts where the pet's does, so a prop sits on the same grid
 * as the pet and its pixels are the same size. The palette is render_pet.py's, by name: a scene may not
 * invent a colour, so props stay in the pets' own world.
 *
 * A drawn thing is a *part*: `{ at, layer, rig }` plus either `rect` (size in cells) or `art` (rows of
 * characters). `layer` is `back` (behind the pet) or `front`; `rig` says whether the scene's rig motion
 * moves it, which is how the ropes of a swing travel with the pet sitting on them.
 *
 * `at` is always a cell. One art character is one cell wide, unless `step` says otherwise: the pet's own eyes
 * and glyphs are single canvas units drawn inside a cell, and `step: 1` is how the angry face matches them.
 */

/** Canvas units per cell. */
export const CELL = 3;
/** Canvas units of cell (0, 0): the same ORIGIN render_pet.py uses. */
export const ORIGIN = 30;

/** The recipe palette, by name. */
export const PALETTE = {
  ink: '#1a1a1f',
  fur: '#fbfbfd',
  grey: '#4a4e56',
  rose: '#ec628c',
  sky: '#52a0ec',
  green: '#34aa60',
  lilac: '#a074e8'
};

const resolve = name => PALETTE[name] ?? null;

/**
 * @param {'back'|'front'} layer Behind the pet or in front of it.
 * @param {boolean} rig Whether the scene's rig motion carries it.
 * @param {number[]} at Cell of the top-left corner.
 * @param {string[]} rows Art rows; '.' is a hole, every other character keys `colours`.
 * @param {Record<string, string>} colours Character to palette name.
 * @param {number} [step] Canvas units per character; one cell by default, 1 for art as fine as the pet's eyes.
 * @returns {object} A part.
 */
export const art = (layer, rig, at, rows, colours, step) => ({ layer, rig, at, art: rows, colours, step });

/**
 * @param {'back'|'front'} layer
 * @param {boolean} rig
 * @param {number[]} at Cell of the top-left corner.
 * @param {number[]} size Cells across and down.
 * @param {string} colour Palette name.
 * @returns {object} A part.
 */
export const rect = (layer, rig, at, size, colour) => ({ layer, rig, at, rect: size, colour });

/**
 * Paints one part. Unknown colour names and '.' cells are skipped, so art may use '.' as a hole.
 * @param {CanvasRenderingContext2D} ctx Transform already set; this draws in canvas units.
 * @param {object} part From a scene's `parts()`.
 * @param {number[]} [offset] Cells nudged right and down, for a pet whose art sits elsewhere on the canvas.
 */
export function paintPart(ctx, part, offset = [0, 0]) {
  const [ox, oy] = offset;
  const left = ORIGIN + (part.at[0] + ox) * CELL;
  const top = ORIGIN + (part.at[1] + oy) * CELL;
  if (part.rect) {
    const colour = resolve(part.colour);
    if (colour) {
      ctx.fillStyle = colour;
      ctx.fillRect(left, top, part.rect[0] * CELL, part.rect[1] * CELL);
    }
    return;
  }
  const step = part.step ?? CELL;
  const rows = part.art;
  for (let r = 0; r < rows.length; r += 1) {
    for (let c = 0; c < rows[r].length; c += 1) {
      const colour = resolve(part.colours?.[rows[r][c]]);
      if (!colour) continue;
      ctx.fillStyle = colour;
      ctx.fillRect(left + c * step, top + r * step, step, step);
    }
  }
}

/**
 * The canvas units a part covers, as `{left, top, right, bottom}` bounds. Used by the tests to keep every prop
 * inside the canvas.
 * @param {object} part
 * @returns {{left: number, top: number, right: number, bottom: number}} Units, right and bottom exclusive.
 */
export function partBox(part) {
  const step = part.step ?? CELL;
  const [w, h] = part.rect
    ? [part.rect[0] * CELL, part.rect[1] * CELL]
    : [Math.max(...part.art.map(r => r.length)) * step, part.art.length * step];
  const left = ORIGIN + part.at[0] * CELL;
  const top = ORIGIN + part.at[1] * CELL;
  return { left, top, right: left + w, bottom: top + h };
}

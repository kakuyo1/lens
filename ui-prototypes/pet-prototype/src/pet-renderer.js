/**
 * Canvas drawing for the pet. Reads the engine state and the visible accessories; it never decides
 * what plays. The body follows the body anchor, the expression the face anchor, and each accessory the anchor of
 * its slot. Effects stay where they were drawn.
 *
 * A scene draws around the pet through the same transforms: `clear()`, then the scene's back layer, the pet, the
 * scene's front layer. `place()` is the one place a transform is set, so a rig rotation lands on the props and
 * the pet alike. An action whose definition carries `overlay` paints its face through `overlays` instead of the
 * expression sheet — the angry action's way in (angry.js).
 */

import { paintPart } from './pixel-art.js';
import { sceneParts } from './scenes.js';

export class PetRenderer {
  /**
   * @param {HTMLCanvasElement} canvas
   * @param {object} pet Assembled by pet-data.js, with `scale` set.
   */
  constructor(canvas, pet) {
    this.pet = pet;
    this.width = pet.canvas;
    this.height = pet.height;
    this.scale = pet.scale;
    canvas.width = this.width * this.scale;
    canvas.height = this.height * this.scale;
    this.ctx = canvas.getContext('2d');
    this.ctx.imageSmoothingEnabled = false;
    this.canvas = canvas;
    this.images = new Map();
    /// Which hand holds the sign: 'left' draws the action's effectLeft sheet, anything else the right-hand effect.
    this.handSide = 'right';
    /** @type {Record<string, (ctx: CanvasRenderingContext2D, state: object) => void>} Face painters by overlay name. */
    this.overlays = {};
  }

  /**
   * Loads every sheet and accessory PNG the pet names.
   * @param {(path: string) => string} urlOf Maps a data path (for example sprites/body/idle.png) to the URL it loads from.
   */
  async load(urlOf) {
    const { actions, expression, accessories } = this.pet;
    const paths = [
      ...Object.values(actions).flatMap(a => [a.sheet, a.effect, a.effectLeft].filter(Boolean)),
      expression,
      ...accessories.map(a => a.asset)
    ];
    await Promise.all(paths.map(path => new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => {
        this.images.set(path, img);
        resolve();
      };
      img.onerror = () => reject(new Error(`pet asset missing: ${path}`));
      img.src = urlOf(path);
    })));
  }

  /** Clears the canvas and returns to canvas units, 1 unit = `scale` device pixels. Starts every frame. */
  clear() {
    const { ctx, scale } = this;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
  }

  /**
   * Sets the transform for whatever is drawn next.
   * @param {{shift?: number[], motion?: {angle: number, by: number, shift: number[], at: number[]}|null}} [place]
   *        `shift` moves the pet on its own — where a scene puts it, on a seat or a deck. `motion` moves, scales and
   *        turns it about a point in canvas units, and is the same transform the scene's rigged props get, so the
   *        two stay together. Degrees.
   */
  place({ shift, motion } = {}) {
    const { ctx, scale } = this;
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    if (shift) ctx.translate(shift[0], shift[1]);
    if (motion) {
      const [mx, my] = motion.at;
      ctx.translate(motion.shift[0], motion.shift[1]);
      ctx.translate(mx, my);
      if (motion.by !== 1) ctx.scale(motion.by, motion.by);
      if (motion.angle) ctx.rotate((motion.angle * Math.PI) / 180);
      ctx.translate(-mx, -my);
    }
  }

  /**
   * @param {{action: string, frame: number, expression: number, anchors: {head: number[], face: number[], body: number[]}}} state
   *        From PetEngine.state.
   * @param {Array<{asset: string, slot: string, zIndex: number}>} accessories Worn accessories visible for this action.
   * @param {{shift?: number[], motion?: object|null}} [place] See place().
   */
  draw(state, accessories, place = {}) {
    this.clear();
    this.drawPet(state, accessories, place);
  }

  drawPet(state, accessories, place = {}) {
    const { ctx, width, height, pet } = this;
    const def = pet.actions[state.action];
    const { anchors } = state;
    const effect = this.handSide === 'left' && def.effectLeft ? def.effectLeft : def.effect;
    const layers = [
      { z: pet.layers.body, path: def.sheet, index: state.frame, at: anchors.body },
      // An overlay replaces the expression sheet: there is no cell in it to point at.
      ...(def.overlay ? [] : [{ z: pet.layers.expression, path: pet.expression, index: state.expression, at: anchors.face }]),
      ...(effect ? [{ z: pet.layers.effect, path: effect, index: state.frame, at: [0, 0] }] : []),
      ...accessories.map(a => ({ z: a.zIndex, path: a.asset, index: 0, at: anchors[a.slot] }))
    ].sort((p, q) => p.z - q.z);

    this.place(place);
    for (const { path, index, at } of layers) {
      ctx.drawImage(this.images.get(path), index * width, 0, width, height, at[0], at[1], width, height);
    }
    if (def.overlay) this.overlays[def.overlay]?.(ctx, state);
  }

  /**
   * Draws one layer of a scene's props.
   * @param {object} scene From SCENES.
   * @param {'back'|'front'} layer Behind the pet or in front of it.
   * @param {number} ms Scene clock.
   * @param {{motion?: object|null, offset?: number[]}} [options] `motion` is the transform rigged props share with
   *        the pet; `offset` is the scene's shift for this pet (sceneOffset).
   */
  drawScene(scene, layer, ms, { motion = null, offset = [0, 0] } = {}) {
    for (const part of sceneParts(scene, ms, layer, offset)) {
      this.place(part.rig ? { motion } : {});
      paintPart(this.ctx, part);
    }
  }
}

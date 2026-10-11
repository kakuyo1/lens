/**
 * Canvas drawing for the pet. Reads the engine state and the visible accessories; it never decides
 * what plays. The body follows the body anchor, the expression the face anchor, and each accessory the anchor of
 * its slot. Effects stay where they were drawn.
 */
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
  }

  /**
   * Loads every sheet and accessory PNG the pet names.
   * @param {(path: string) => string} urlOf Maps a data path (for example sprites/body/idle.png) to the URL it loads from.
   */
  async load(urlOf) {
    const { actions, expression, accessories } = this.pet;
    const paths = [
      ...Object.values(actions).flatMap(a => [a.sheet, a.effect].filter(Boolean)),
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

  /**
   * @param {{action: string, frame: number, expression: number, anchors: {head: number[], face: number[], body: number[]}}} state
   *        From PetEngine.state.
   * @param {Array<{asset: string, slot: string, zIndex: number}>} accessories Worn accessories visible for this action.
   */
  draw(state, accessories) {
    const { ctx, width, height, scale, pet } = this;
    const def = pet.actions[state.action];
    const { anchors } = state;
    const layers = [
      { z: pet.layers.body, path: def.sheet, index: state.frame, at: anchors.body },
      { z: pet.layers.expression, path: pet.expression, index: state.expression, at: anchors.face },
      ...(def.effect ? [{ z: pet.layers.effect, path: def.effect, index: state.frame, at: [0, 0] }] : []),
      ...accessories.map(a => ({ z: a.zIndex, path: a.asset, index: 0, at: anchors[a.slot] }))
    ].sort((p, q) => p.z - q.z);

    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.setTransform(scale, 0, 0, scale, 0, 0); // draw in canvas units, 1 unit = `scale` device pixels
    for (const { path, index, at } of layers) {
      ctx.drawImage(this.images.get(path), index * width, 0, width, height, at[0], at[1], width, height);
    }
  }
}

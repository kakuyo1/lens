/**
 * Settings-page entry: the prototype's own engine, renderer, pointer, accessory manager and picker rules, bundled as one
 * script for ui-prototypes/pet.html (tools/build-pet-html.mjs). The page supplies the pets' data and their images as
 * data URLs, because a file:// page can neither fetch nor load modules.
 */
import { PetEngine } from './pet-engine.js';
import { PetRenderer } from './pet-renderer.js';
import { AccessoryManager } from './accessory-manager.js';
import { attachPointer } from './pet-pointer.js';

export { keepWorn, actionOrder } from './pet-picker.js';

/**
 * @param {HTMLCanvasElement} canvas
 * @param {object} pet Assembled by pet-data.js.
 * @param {{scale: number, resolve: (path: string) => string, live?: boolean, bounds?: HTMLElement}} options
 *        `live: false` draws the idle frame once with no pointer and no clock, for thumbnails.
 * @returns {Promise<{engine: PetEngine, accessories: AccessoryManager, setRunning: (on: boolean) => void}>}
 *          `setRunning` stops the frame clock while the stage is hidden.
 */
export async function mountStage(canvas, pet, { scale, resolve, live = true, bounds }) {
  const renderer = new PetRenderer(canvas, { ...pet, scale });
  await renderer.load(resolve);
  const engine = new PetEngine(pet);
  const accessories = new AccessoryManager(pet.accessories);

  if (!live) {
    renderer.draw(engine.state, []);
    return { engine, accessories, renderer, setRunning() {} };
  }

  attachPointer(canvas, { engine, renderer, bounds });
  let running = false;
  let last = 0;
  let frame = 0;
  const tick = now => {
    engine.update(Math.max(Math.min(now - last, 100), 0)); // never backwards: see main.js
    last = now;
    renderer.draw(engine.state, accessories.visibleFor(engine.action));
    frame = requestAnimationFrame(tick);
  };
  const setRunning = on => {
    if (on === running) return;
    running = on;
    if (on) {
      last = performance.now();
      frame = requestAnimationFrame(tick);
    } else {
      cancelAnimationFrame(frame);
    }
  };
  return { engine, accessories, renderer, setRunning };
}

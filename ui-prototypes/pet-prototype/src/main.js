import { PetEngine } from './pet-engine.js';
import { PetRenderer } from './pet-renderer.js';
import { AccessoryManager } from './accessory-manager.js';
import { attachPointer } from './pet-pointer.js';
import { mountDebugPanel } from './debug-panel.js';
import { assemblePet } from './pet-data.js';

// publicDir is data/pet (vite.config.js), so data/pet/<id>/ is served at /<id>/. ?pet=<id> picks the pet; the dog is the default.
// __PET_IDS__ is the list of those folders, injected by vite.config.js.
const petId = new URLSearchParams(location.search).get('pet') ?? 'dog';
const BASE = `/${petId}/`;

// Filled before any fetch, so a bad ?pet= still leaves the picker to go back with. Changing it reloads the page.
const picker = document.querySelector('#pet-select');
for (const id of __PET_IDS__) picker.append(new Option(id, id, false, id === petId));
picker.addEventListener('change', () => { location.search = `?pet=${picker.value}`; });

const files = Object.fromEntries(await Promise.all(['pet', 'animations', 'anchors', 'accessories'].map(async name => {
  const res = await fetch(`${BASE}${name}.json`);
  return [name, await res.json()];
})));
const pet = assemblePet(files);
const canvas = document.querySelector('#pet');
const renderer = new PetRenderer(canvas, pet);
await renderer.load(path => BASE + path);

const engine = new PetEngine(pet);
const accessories = new AccessoryManager(pet.accessories);
attachPointer(canvas, { engine, renderer });
const panel = mountDebugPanel(document.querySelector('#panel'), { pet, engine, accessories });

// One clock for the whole pet: the engine advances the frame, every layer reads it.
let last = performance.now();
function tick(now) {
  engine.update(Math.min(now - last, 100));
  last = now;
  renderer.draw(engine.state, accessories.visibleFor(engine.action));
  panel.refresh();
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);

import { PetEngine } from './pet-engine.js';
import { PetRenderer } from './pet-renderer.js';
import { AccessoryManager } from './accessory-manager.js';
import { attachPointer } from './pet-pointer.js';
import { mountDebugPanel } from './debug-panel.js';
import { assemblePet } from './pet-data.js';
import { SCENES, sceneOffset, sceneMotion } from './scenes.js';

// publicDir is data/pet (vite.config.js), so data/pet/<id>/ is served at /<id>/. ?pet=<id> picks the pet; the dog is the default.
// __PET_IDS__ is the list of those folders, injected by vite.config.js. ?scene=<id> and ?action=<name> start on that.
const params = new URLSearchParams(location.search);
const petId = params.get('pet') ?? 'dog';
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

const offset = sceneOffset(petId);
let scene = null;
let sceneMs = 0;

/** Enters a scene, or leaves to no scene at all. In a scene the pet stops deciding for itself. */
function setScene(next) {
  scene = next;
  sceneMs = 0;
  engine.setAmbient(!scene);
  if (scene) engine.play(scene.petAction);
  else engine.stop();
  history.replaceState(null, '', `?pet=${petId}${scene ? `&scene=${scene.id}` : ''}`);
}

const panel = mountDebugPanel(document.querySelector('#panel'), {
  pet,
  engine,
  accessories,
  scenes: SCENES,
  current: () => scene?.id ?? null,
  onScene: id => setScene(SCENES.find(s => s.id === id) ?? null)
});

setScene(SCENES.find(s => s.id === params.get('scene')) ?? null);
const wanted = params.get('action');
if (wanted && pet.actions[wanted]) engine.play(wanted);

// One clock for the whole pet: the engine advances the frame, every layer reads it.
let last = performance.now();
function tick(now) {
  // Clamped at zero: the first frame's stamp can predate `last`, and a clock that runs backwards makes the
  // engine's timers and the scene clock count down.
  const delta = Math.max(Math.min(now - last, 100), 0);
  last = now;
  engine.update(delta);
  if (!engine.paused) sceneMs += delta;
  const motion = scene ? sceneMotion(scene, sceneMs, offset) : null;
  renderer.clear();
  if (scene) renderer.drawScene(scene, 'back', sceneMs, { motion, offset });
  renderer.drawPet(engine.state, accessories.visibleFor(engine.action), { shift: scene?.place(engine.state) ?? null, motion });
  if (scene) renderer.drawScene(scene, 'front', sceneMs, { motion, offset });
  panel.refresh();
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);

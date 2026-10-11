// Builds ui-prototypes/pet.html: the settings page template, with the prototype's bundled engine and every pet's
// data and PNGs inlined, so the page works when opened from file://.
// Run from anywhere: node tools/build-pet-html.mjs   (npm run embed)
// A path argument writes the page there instead, which leaves ui-prototypes/pet.html alone.
import { execFileSync } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { assemblePet } from '../src/pet-data.js';

const proto = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const petsRoot = path.resolve(proto, '..', '..', 'data', 'pet');

// The picker's order: the dog first, since it is the default, then the others as the design lists them. Any other pet follows, by id.
const PET_ORDER = ['dog', 'mochi', 'owl', 'cat'];

/**
 * The pets the page offers: every folder under `root` that has a pet.json. A folder still being authored, without one, is left out.
 * @param {string} root data/pet
 * @returns {string[]} Pet ids, in PET_ORDER.
 */
export function listPets(root) {
  const rank = id => (PET_ORDER.includes(id) ? PET_ORDER.indexOf(id) : PET_ORDER.length);
  return readdirSync(root, { withFileTypes: true })
    .filter(entry => entry.isDirectory() && existsSync(path.join(root, entry.name, 'pet.json')))
    .map(entry => entry.name)
    .sort((p, q) => rank(p) - rank(q) || (p < q ? -1 : p > q ? 1 : 0));
}

/**
 * One pet, assembled the way pet-data.js assembles it, with every PNG it names as a data URL keyed `<id>/<path>`.
 * @param {string} root data/pet
 * @param {string} id
 * @returns {{pet: object, assets: Record<string, string>}}
 */
export function readPet(root, id) {
  const dir = path.join(root, id);
  const read = name => JSON.parse(readFileSync(path.join(dir, name), 'utf8'));
  const pet = assemblePet({
    pet: read('pet.json'),
    animations: read('animations.json'),
    anchors: read('anchors.json'),
    accessories: read('accessories.json'),
  });
  const paths = [
    ...Object.values(pet.actions).flatMap(action => [action.sheet, action.effect].filter(Boolean)),
    pet.expression,
    ...pet.accessories.map(accessory => accessory.asset),
  ];
  const assets = Object.fromEntries(paths.map(p => [
    `${id}/${p}`,
    `data:image/png;base64,${readFileSync(path.join(dir, p)).toString('base64')}`,
  ]));
  return { pet, assets };
}

function main() {
  const output = process.argv[2] ? path.resolve(process.argv[2]) : path.resolve(proto, '..', 'pet.html');
  const bundleDir = path.join(proto, 'dist-embed');
  const template = path.join(proto, 'embed', 'settings.template.html');
  const vite = path.join(proto, 'node_modules', 'vite', 'bin', 'vite.js');

  execFileSync(process.execPath, [vite, 'build', '--config', 'vite.embed.config.js', '--outDir', bundleDir, '--emptyOutDir'], {
    cwd: proto,
    stdio: 'inherit',
  });

  const bundle = readFileSync(path.join(bundleDir, 'pet-embed.js'), 'utf8');
  rmSync(bundleDir, { recursive: true, force: true });
  if (bundle.includes('</script')) throw new Error('bundle contains </script, would break the inline script');

  const ids = listPets(petsRoot);
  if (ids.length === 0) throw new Error(`no pet.json under ${petsRoot}`);
  const manifest = {};
  const assets = {};
  for (const id of ids) {
    const built = readPet(petsRoot, id);
    manifest[id] = built.pet;
    Object.assign(assets, built.assets);
  }

  // Serialised JSON must not close the script element early. The template reads the pets from PET_MANIFEST, keyed by id.
  const json = value => JSON.stringify(value).replaceAll('</', '<\\/');
  const data = `const PET_MANIFEST = ${json(manifest)};\nconst PET_ASSETS = ${json(assets)};`;

  // Replacer functions, not strings: the bundle contains `$` sequences that String.replace would expand.
  const html = readFileSync(template, 'utf8')
    .replace('/*@PET_DATA@*/', () => data)
    .replace('/*@PET_BUNDLE@*/', () => bundle);
  writeFileSync(output, html);
  console.log(`wrote ${output}: ${Buffer.byteLength(html)} bytes, pets ${ids.join(', ')}`);
}

// Import from the tests reads the helpers only; the build runs when this file is the entry point.
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();

// Builds ui-prototypes/pet-hover.html: the hover interaction study, with the prototype's bundled
// engine and every pet's data and PNGs inlined, so the page works when opened from file://.
// Run from anywhere: node tools/build-pet-hover-html.mjs   (npm run embed:hover)
import { execFileSync } from 'node:child_process';
import { readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { listPets, readPet } from './build-pet-html.mjs';

const proto = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const petsRoot = path.resolve(proto, '..', '..', 'data', 'pet');

function main() {
  const output = path.resolve(proto, '..', 'pet-hover.html');
  const bundleDir = path.join(proto, 'dist-hover-embed');
  const template = path.join(proto, 'embed', 'hover.template.html');
  const vite = path.join(proto, 'node_modules', 'vite', 'bin', 'vite.js');

  execFileSync(process.execPath, [vite, 'build', '--config', 'vite.hover.config.js', '--outDir', bundleDir, '--emptyOutDir'], {
    cwd: proto,
    stdio: 'inherit',
  });

  const bundle = readFileSync(path.join(bundleDir, 'pet-hover-embed.js'), 'utf8');
  rmSync(bundleDir, { recursive: true, force: true });
  if (bundle.includes('</script')) throw new Error('bundle contains </script, would break the inline script');

  const ids = listPets(petsRoot);
  const manifest = {};
  const assets = {};
  for (const id of ids) {
    const built = readPet(petsRoot, id);
    manifest[id] = built.pet;
    Object.assign(assets, built.assets);
  }

  const json = value => JSON.stringify(value).replaceAll('</', '<\\/');
  const data = `const PET_MANIFEST = ${json(manifest)};\nconst PET_ASSETS = ${json(assets)};`;

  const html = readFileSync(template, 'utf8')
    .replace('/*@PET_DATA@*/', () => data)
    .replace('/*@PET_BUNDLE@*/', () => bundle);
  writeFileSync(output, html);
  console.log(`wrote ${output}: ${Buffer.byteLength(html)} bytes, pets ${ids.join(', ')}`);
}

main();

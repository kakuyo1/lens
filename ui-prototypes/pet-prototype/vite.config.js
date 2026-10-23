import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';
import { listPets } from './tools/build-pet-html.mjs';

const petsRoot = fileURLToPath(new URL('../../data/pet', import.meta.url));

export default defineConfig({
  // data/pet is the pets' source of truth: data/pet/<id>/ is served at /<id>/, so the page reads the JSON and PNGs in place.
  publicDir: '../../data/pet',
  // The dev page's picker lists the same pets the embed does, in the same order.
  define: { __PET_IDS__: JSON.stringify(listPets(petsRoot)) },
  // The review sign is drawn with the app's bundled pixel face, which lives in data/fonts at the repository root rather
  // than under the prototype's public directory.
  server: { fs: { allow: [fileURLToPath(new URL('../..', import.meta.url))] } },
});

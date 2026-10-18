import { defineConfig } from 'vite';

// One classic script for the hover study: tools/build-pet-hover-html.mjs inlines it into ui-prototypes/pet-hover.html.
export default defineConfig({
  publicDir: false,
  build: {
    lib: {
      entry: 'src/hover-embed.js',
      name: 'PetHover',
      formats: ['iife'],
      fileName: () => 'pet-hover-embed.js',
    },
    minify: false,
  },
});

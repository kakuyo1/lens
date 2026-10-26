// Renders the seven review signs to data/pet/<id>/sign/: one sprite sheet per width step, frames left to right, and
// sign.json for the loader (UI-SPEC 4.13, ADR 0125, 0126). A step is the sign drawn at that text width by the same
// designs the prototype shows, so the sign at a step is exact; the sign takes the first step at or above its word.
// Run from the repository root: node scripts/pet/signs/export.mjs
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { deflateSync, crc32 } from "node:zlib";
import { P, PETS, paint } from "./designs.mjs";

const PET_DIR = fileURLToPath(new URL("../../../data/pet/", import.meta.url));
/// Thinnest letter is 4.4 px and widest 17.3 px in Pixelify Sans at 20 px, so 14 letters fit in 8..244 px.
const FIRST = 8;
const LAST = 244;
const STEP = 4;
/// One beat of the shared clock, from the art data (the prototype's 120 ms was a placeholder until now).
const BEAT_MS = 120;

function chunk(type, body) {
  const head = Buffer.alloc(8);
  head.writeUInt32BE(body.length, 0);
  head.write(type, 4, "latin1");
  const tail = Buffer.alloc(4);
  tail.writeUInt32BE(crc32(Buffer.concat([Buffer.from(type, "latin1"), body])) >>> 0, 0);
  return Buffer.concat([head, body, tail]);
}

/** A plain RGBA PNG; every row uses filter 0. */
function png(width, height, rgba) {
  const row = width * 4;
  const raw = Buffer.alloc((row + 1) * height);
  for (let y = 0; y < height; y++) rgba.copy(raw, y * (row + 1) + 1, y * row, (y + 1) * row);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw, { level: 9 })), chunk("IEND", Buffer.alloc(0)),
  ]);
}

/** One sheet: the frames side by side, every frame in the same cell. */
function sheet(frames) {
  const cw = frames[0].width, ch = frames[0].height, row = cw * 4, width = cw * frames.length;
  const rgba = Buffer.alloc(width * 4 * ch);
  frames.forEach((img, f) => {
    for (let y = 0; y < ch; y++) Buffer.from(img.data.buffer, img.data.byteOffset + y * row, row).copy(rgba, y * width * 4 + f * row);
  });
  return png(width, ch, rgba);
}

for (const pet of PETS) {
  const cand = pet.cand;
  const dir = PET_DIR + pet.id + "/sign/";
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });

  const bins = [];
  let foot = null, word = null;
  for (let width = FIRST; width <= LAST; width += STEP) {
    const frames = [], feet = [];
    let first = null;
    for (let f = 0; f < cand.anim.n; f++) {
      const d = cand.build(width, f);
      const { img, foot: ft } = paint(d);
      frames.push(img);
      feet.push(ft);
      first ??= d;
    }
    // A frame that moves its base would walk on the pet's feet line: refuse it.
    if (new Set(feet).size !== 1) throw new Error(`${cand.id} at ${width} px: foot differs between frames (${feet})`);
    if (foot !== null && foot !== feet[0]) throw new Error(`${cand.id} at ${width} px: foot changed with width`);
    foot = feet[0];
    const text = { top: first.ty, color: first.tc };
    if (word && JSON.stringify(word) !== JSON.stringify(text)) throw new Error(`${cand.id}: word placement changed with width`);
    word = text;
    bins.push({ width, W: first.W, cellWidth: frames[0].width, cellHeight: frames[0].height, sheet: `sign/${width}.png` });
    writeFileSync(dir + width + ".png", sheet(frames));
  }

  const json = { design: cand.id, frames: cand.anim.n, every: cand.anim.every, beatMs: BEAT_MS, margin: P, foot, word, bins };
  writeFileSync(PET_DIR + pet.id + "/sign.json", JSON.stringify(json, null, 2) + "\n");
  console.log(`${pet.id.padEnd(6)} ${cand.id.padEnd(15)} ${bins.length} steps, ${cand.anim.n} frames, foot ${foot}`);
}

// The review signs' engine and the seven designs (UI-SPEC 4.13, ADR 0125, 0126). Pure JS with no DOM: the prototype's page
// imports it, and export.mjs renders it to data/pet/<id>/sign. Every pixel is decided here, nowhere else.
/** Margin around each sign for its outline and shadow. */
export const P = 2;

/* ---------- shape tests on pixel centres (x + .5, y + .5) ---------- */
/** Rounded rectangle over [0,w) x [0,h). */
const rr = (x, y, w, h, r) => {
  if (x < 0 || y < 0 || x >= w || y >= h) return false;
  const left = x < r, right = x >= w - r, top = y < r, bottom = y >= h - r;
  if (!((left || right) && (top || bottom))) return true;
  const cx = left ? r : w - r, cy = top ? r : h - r;
  return (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2 <= r * r;
};
const el = (x, y, cx, cy, rx, ry) => ((x + .5 - cx) / rx) ** 2 + ((y + .5 - cy) / ry) ** 2 <= 1;
/** Bevel: light on the top and left edges, shade on the bottom and right edges. */
const lit = (base, e, hi, lo) => (e.t || e.l) ? hi : (e.b || e.r) ? lo : base;
/** Heart, s is its half-width in pixels. */
const heart = (x, y, cx, cy, s) => { const u = (x + .5 - cx) / s, v = (cy - (y + .5)) / s; return (u * u + v * v - 1) ** 3 - u * u * v * v * v <= 0; };
/** Five-point star with radius R. */
const star = (x, y, cx, cy, R) => { const dx = x + .5 - cx, dy = y + .5 - cy; return Math.hypot(dx, dy) <= R * (0.42 + 0.58 * ((1 + Math.cos(5 * Math.atan2(dy, dx))) / 2) ** 3); };
/** A plus-shaped sparkle centred on pixel (sx, sy) with arms n pixels long. */
const spark = (x, y, sx, sy, n = 1) => (x === sx && Math.abs(y - sy) <= n) || (y === sy && Math.abs(x - sx) <= n);
/** Polka dot on a grid of step s, offset (ox, oy). */
const dot = (x, y, s, ox, oy) => ((x - ox) % s + s) % s === 0 && ((y - oy) % s + s) % s === 0;
/** Bow: two loops at cx -/+ s, knot in the middle. */
const bow = (x, y, cx, cy, s) => el(x, y, cx - s, cy, s, s * 0.75) || el(x, y, cx + s, cy, s, s * 0.75);
const knot = (x, y, cx, cy, s) => el(x, y, cx, cy, s * 0.45, s * 0.45);
/** True where pixel (x, y) of shape f has a neighbour outside it: the shape's own edge. */
const edge = (f, x, y) => !f(x - 1, y) || !f(x + 1, y) || !f(x, y - 1) || !f(x, y + 1);
/** Defaults every sign shares: dark ink outline, soft drop shadow. */
const box = o => ({ ink: "#1a1a1f", off: 2, shadow: "#14141a2a", ...o });

/* ---------- engine: one pixel grid becomes outline, shadow and bevel ---------- */
const rgbaOf = c => { const n = parseInt(c.slice(1).padEnd(8, "f"), 16); return [n >>> 24, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]; };

/** Returns the canvas and foot: the lowest mask row + 1, so the sign stands exactly on the pet's foot line. */
export function paint(d) {
  const { W, H, inside, face, ink = null, off = 2, shadow = null } = d;
  const cw = W + off + 2 * P, ch = H + off + 2 * P;
  const M = new Uint8Array(W * H);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) M[y * W + x] = inside(x, y, W, H) ? 1 : 0;
  let foot = 0;
  for (let i = 0; i < M.length; i++) if (M[i]) foot = Math.max(foot, Math.floor(i / W) + 1);
  const at = (x, y) => x >= 0 && y >= 0 && x < W && y < H && M[y * W + x] === 1;
  const near = (x, y) => {
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) if ((dx || dy) && at(x + dx, y + dy)) return true;
    return false;
  };
  const img = { width: cw, height: ch, data: new Uint8ClampedArray(cw * ch * 4) };
  for (let y = -P; y < H + off + P; y++) for (let x = -P; x < W + off + P; x++) {
    let c = null;
    if (at(x, y)) c = face(x, y, W, H, { t: !at(x, y - 1), l: !at(x - 1, y), b: !at(x, y + 1), r: !at(x + 1, y) });
    else if (ink && near(x, y)) c = ink;
    else if (shadow && at(x - off, y - off)) c = shadow;
    if (c) img.data.set(rgbaOf(c), 4 * ((y + P) * cw + x + P));
  }
  return { img, foot };
}

/* ---------- default: ReviewSignBoard.qml, pixel for pixel (UI-SPEC 4.13); not animated ---------- */
export const WOOD = {
  id: "wood", name: "浅橡木牌",
  note: "逐像素复刻 ReviewSignBoard.qml：浅橡木板 40 px，3 px 深边，四枚钉子，两道木纹，短柱 10 × 32。外部拓展宠物默认用它。",
  anim: { n: 1, every: 1 },
  build(tw) {
    const W = Math.round(tw) + 34, H = 70, px = Math.round((W - 10) / 2);
    const nails = [[7, 7], [W - 9, 7], [7, 31], [W - 9, 31]];
    return { W, H, ty: 10, tc: "#2b1a0e", ink: null, off: 4, shadow: "#14141a21",
      inside: (x, y) => (y < 40 && x >= 0 && x < W) || (y >= 40 && y < 70 && x >= px && x < px + 10),
      face: (x, y) => {
        if (y >= 40) {
          const lx = x - px;
          if (y >= 68 || lx < 2 || lx >= 8) return "#3a2412";
          if (lx === 2) return "#d2a76a";
          if (lx === 7) return "#8a5a2f";
          return "#b8864f";
        }
        if (y < 3 || y >= 37 || x < 3 || x >= W - 3) return "#3a2412";
        if (y === 3 || x === 3) return "#e6c88f";
        if (y === 36 || x === W - 4) return "#a8783f";
        for (const [nx, ny] of nails) if (x >= nx && x < nx + 2 && y >= ny && y < ny + 2) return x === nx && y === ny ? "#e6c88f" : "#5b3b20";
        if ((y === 8 || y === 31) && x >= 8 && x < W - 8) return "#bd8d4f";
        return "#d2a76a";
      } };
  }
};

/* ---------- the seven chosen signs, each with one small motion inside the sign ---------- */
/* ink: the idle frame's ink box in canvas units, [left, right, bottom], measured from sprites/body/idle.png */
export const PETS = [
  {id: "cat", zh: "短耳猫", ink: [18, 87, 81], cand: {id: "cat-cardboard", name: "纸箱蝴蝶结", anim: {n: 5, every: 2},
    note: "奶油色的小纸箱，顶上一圈玫红胶带点着白点，蝴蝶结坐在胶带正中，白色标签上写字，右下角一枚猫爪印。动效：胶带上的白点缓缓向一侧滑动。",
    build(tw, f) {
      const W = Math.round(tw) + 44, H = 60, cx = W / 2;
      const body = (x, y) => rr(x, y - 6, W, H - 6, 9);
      const bw = (x, y) => bow(x, y, cx, 4, 6) || knot(x, y, cx, 4, 6);
      const label = (x, y) => rr(x - 9, y - 22, W - 18, 22, 6);
      const paw = (x, y) => el(x, y, W - 11, H - 8, 3.2, 2.6) || el(x, y, W - 15, H - 11, 1.3, 1.3)
        || el(x, y, W - 12, H - 13, 1.3, 1.3) || el(x, y, W - 9, H - 13, 1.3, 1.3) || el(x, y, W - 6, H - 11, 1.3, 1.3);
      return box({ W, H, ty: 22, tc: "#5a3b2e",
        inside: (x, y) => body(x, y) || bw(x, y),
        face: (x, y, W, H, e) => {
          if (knot(x, y, cx, 4, 6)) return "#c97f92";
          if (bw(x, y)) return "#f0b8c6";
          if (y >= 6 && y < 13) return dot(x, y, 5, 2 + f, 9) ? "#ffffff" : "#e7a9b8";
          if (paw(x, y)) return "#e7a9b8";
          if (label(x, y)) return edge(label, x, y) ? "#e7a9b8" : "#fff8f1";
          if (dot(x, y, 6, 1, 3)) return "#e9cca6";
          return lit("#f3e2c7", e, "#fff6e6", "#dcc39c");
        } });
    } } },
  {id: "dog", zh: "奶糖", ink: [18, 84, 84], cand: {id: "dog-tag", name: "骨头牌", anim: {n: 6, every: 2},
    note: "奶油色的小骨头，两端四个圆球，玫红的圆点点缀在球上，字写在骨头中间。动效：一道白光从左到右扫过骨头。",
    build(tw, f) {
      const W = Math.round(tw) + 64, H = 40, r = 7, kl = 8, kr = W - 9, cyT = 9, cyB = H - 9;
      const bar = (x, y) => rr(x - 8, y - 12, W - 16, 19, 9);
      const knob = (x, y) => el(x, y, kl, cyT, r, r) || el(x, y, kl, cyB, r, r) || el(x, y, kr, cyT, r, r) || el(x, y, kr, cyB, r, r);
      const sx = 12 + f * ((W - 24) / 6);
      return box({ W, H, ty: 11, tc: "#6b3d4d", ink: "#8a4a5a",
        inside: (x, y) => bar(x, y) || knob(x, y),
        face: (x, y, W, H, e) => {
          if (knob(x, y)) return dot(x, y, 4, 1, 1) ? "#e7a9b8" : lit("#f9e4cf", e, "#ffffff", "#e7a9b8");
          if (Math.abs(x - sx + 0.4 * (y - 21)) < 1.3) return "#ffffff";
          return lit("#f9e4cf", e, "#fff6ee", "#f2c4cf");
        } });
    } } },
  {id: "ghost", zh: "小幽灵", ink: [27, 87, 81], cand: {id: "ghost-candle", name: "小蜡烛", anim: {n: 4, every: 2},
    note: "圆角的淡紫小蜡烛，身上斜纹白条，顶上滴下一圈白色的蜡，烛心一朵粉紫的火苗，火苗旁两颗星星，字写在白色的小标签上。动效：火苗摇曳，两颗星星轮流闪。",
    build(tw, f) {
      const W = Math.round(tw) + 42, H = 66, cx = W / 2;
      const ry = [6, 6.8, 5.6, 6.4][f], dx = [0, 0.5, -0.5, 0.3][f];
      const flame = (x, y) => el(x, y, cx + dx, 10, 5.5, ry) || (y >= 1 && y < 10 && Math.abs(x + .5 - cx - dx) <= (y - .5) * 0.42);
      const wick = (x, y) => x === Math.floor(cx) && y >= 16 && y < 19;
      const body = (x, y) => rr(x, y - 18, W, H - 18, 10);
      const lw = Math.round(tw) + 8, lx = (W - lw) / 2;
      const label = (x, y) => rr(x - lx, y - 30, lw, 24, 7);
      return box({ W, H, ty: 32, tc: "#2b2550", ink: "#2b2550",
        inside: (x, y) => flame(x, y) || wick(x, y) || body(x, y),
        face: (x, y, W, H, e) => {
          if (flame(x, y)) return el(x, y, cx + dx * 0.5, 11, 2.2, 2.6) ? "#ffffff" : "#f0b8e0";
          if (wick(x, y)) return "#2b2550";
          if (label(x, y)) return edge(label, x, y) ? "#cdb8f2" : "#ffffff";
          if (y < 22 && ((x - 6) % 12 + 12) % 12 < 5) return "#ffffff";
          if (f % 2 === 0 ? spark(x, y, 4, 14, 1) : spark(x, y, W - 5, 6, 1)) return "#ffffff";
          return ((x + y) % 10) < 5 ? "#cdb8f2" : "#f1eaff";
        } });
    } } },
  {id: "jelly", zh: "小水母", ink: [33, 81, 81], cand: {id: "jelly-tank", name: "小鱼缸", anim: {n: 6, every: 2},
    note: "圆圆的玻璃鱼缸，底下一座粉色的小底座，水里一条粉色小鱼来回游，几颗白色泡泡往上冒，字浮在水里。动效：小鱼游动，泡泡上升，水面起伏。",
    build(tw, f) {
      const W = Math.round(tw) + 60, H = 72, cx = W / 2, cy = 34, rx = W / 2 - 2, ry = 32;
      const globe = (x, y) => el(x, y, cx, cy, rx, ry);
      const stand = (x, y) => rr(x - (Math.round(cx) - 15), y - 63, 30, 9, 4);
      const fx = Math.round(cx) + [-18, -9, 0, 9, 16, 9][f], tk = f % 2 ? 0.9 : 0.55;
      const fish = (x, y) => el(x, y, fx, 54, 5, 2.6) || (x >= fx + 4 && x < fx + 9 && Math.abs(y + .5 - 54) <= (fx + 9 - x) * tk);
      const bubble = (x, y) => [[20, 0], [W - 20, 11], [22, 21]].some(([bx, o]) => el(x, y, bx, 56 - ((f * 5 + o) % 30), 1.4, 1.4));
      const surf = x => 24 + Math.round(Math.sin(x * 0.5 + f * 1.6));
      return box({ W, H, ty: 32, tc: "#1e3a39", ink: "#2e5552",
        inside: (x, y) => globe(x, y) || stand(x, y),
        face: (x, y, W, H, e) => {
          if (stand(x, y)) return lit("#e7a9b8", e, "#f3c3cf", "#c97f92");
          if (fish(x, y)) return el(x, y, fx + 2, 53, 1, 1) ? "#ffffff" : "#f5b8c8";
          if (bubble(x, y)) return "#ffffff";
          if (!el(x, y, cx, cy, rx - 4, ry - 4)) return lit("#dff3f1", e, "#ffffff", "#9fcac5");
          if (y < surf(x)) return "#f4fbfa";
          if (y === surf(x)) return "#ffffff";
          if (y > cy + ry - 9) return dot(x, y, 3, 1, 1) ? "#ffffff" : "#eef7f6";
          return lit("#bfe3df", e, "#d5efec", "#9fcac5");
        } });
    } } },
  {id: "cloud", zh: "小云", ink: [30, 84, 81], cand: {id: "cloud-stone", name: "小云朵", anim: {n: 4, every: 3},
    note: "奶白的小云，表面一圈淡蓝圆点，云边一颗黄色的小星星，字写在云朵中间。动效：星星一大一小地闪，圆点慢慢流动。",
    build(tw, f) {
      const W = Math.round(tw) + 54, H = 50;
      const puff = (x, y, cx, cy, r) => (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2 <= r * r;
      const cloud = (x, y) => puff(x, y, W * 0.22, H - 17, 14) || puff(x, y, W * 0.45, H - 29, 19)
        || puff(x, y, W * 0.7, H - 24, 15) || rr(x - 4, y - (H - 18), W - 8, 18, 9);
      return box({ W, H, ty: H - 35, tc: "#2e4256", ink: "#4f6b82",
        inside: cloud,
        face: (x, y, W, H, e) => {
          if (star(x, y, W - 15, 39, f % 2 ? 2.5 : 3.8)) return "#ffe6a8";
          if (dot(x, y, 6, 1 + f, 3) && y > H * 0.55) return "#c9dcea";
          return lit("#f7fbff", e, "#ffffff", "#d6e4ef");
        } });
    } } },
  {id: "mochi", zh: "麻糬", ink: [30, 81, 81], cand: {id: "mochi-cup", name: "纸杯蛋糕", anim: {n: 4, every: 3},
    note: "玫红和白色条纹的纸杯，奶油顶上撒着彩色的小糖粒，顶上一颗红樱桃，字写在纸杯正中的白色小标签上。动效：糖粒左右跳动，樱桃闪一下光。",
    build(tw, f) {
      const W = Math.round(tw) + 46, H = 64, cx = W / 2;
      const cherry = (x, y) => el(x, y, cx, 5, 4, 4);
      const frost = (x, y) => el(x, y, cx, 20, W / 2 - 1, 11) || el(x, y, cx - W * 0.25, 21, W * 0.2, 8) || el(x, y, cx + W * 0.25, 21, W * 0.2, 8);
      const cup = (x, y) => y >= 28 && x + .5 >= 4 + ((y - 28) / (H - 28)) * 0.1 * W && x + .5 <= W - 4 - ((y - 28) / (H - 28)) * 0.1 * W;
      const lw = Math.round(tw) + 8, lx = (W - lw) / 2;
      const label = (x, y) => rr(x - lx, y - 36, lw, 20, 6);
      return box({ W, H, ty: 36, tc: "#6b3a4a", ink: "#6b3a4a",
        inside: (x, y) => cherry(x, y) || frost(x, y) || cup(x, y),
        face: (x, y, W, H, e) => {
          if (cherry(x, y)) return el(x, y, cx - 1.5, 3.5, 1, 1) && f % 2 === 0 ? "#ffffff" : "#e0576f";
          if (frost(x, y)) {
            if (dot(x, y, 5, 2 + f, 14)) return "#a6d8f0";
            if (dot(x, y, 7, 4 - f, 17)) return "#f7e27a";
            return lit("#fffdfb", e, "#ffffff", "#f1dfe6");
          }
          if (label(x, y)) return edge(label, x, y) ? "#e7a9b8" : "#ffffff";
          return (x % 6) < 3 ? "#e7a9b8" : "#fff7ef";
        } });
    } } },
  {id: "owl", zh: "猫头鹰", ink: [33, 78, 81], cand: {id: "owl-card", name: "书袋卡", anim: {n: 4, every: 3},
    note: "奶白的借书卡，右上角一颗淡紫的星星贴纸，插在银灰色的小书袋里，书袋上一排白色的针脚，字写在卡片上。动效：星星贴纸闪烁，书袋里的小心一跳一跳。",
    build(tw, f) {
      const W = Math.round(tw) + 44, H = 54, pk = 36;
      const card = (x, y) => rr(x - 4, y, W - 8, pk + 4, 8);
      const pocket = (x, y) => rr(x, y - pk, W, H - pk, 8);
      const sr = [4, 3.2, 2.6, 3.2][f], hr = [3, 3.6, 3, 2.6][f];
      return box({ W, H, ty: 6, tc: "#2a2a33", ink: "#1f2126",
        inside: (x, y) => card(x, y) || pocket(x, y),
        face: (x, y, W, H, e) => {
          if (pocket(x, y)) {
            if (y === pk + 2 && x % 3 === 0) return "#ffffff";
            if (heart(x, y, W / 2, pk + 9, hr)) return "#e7a9b8";
            return lit("#b7bfcc", e, "#d3d8e2", "#8f97a6");
          }
          if (star(x, y, W - 12, 7, sr)) return "#cdb8f2";
          if (y === 28 || y === 31) return "#e4e7ee";
          return lit("#fff8ee", e, "#ffffff", "#e3d9c6");
        } });
    } } }
];
/** Candidate lookup by id, for the clock to read each image's motion. */
export const PETS_CANDS = Object.fromEntries(PETS.map(p => [p.cand.id, p.cand]));

/**
 * Scene proposals for the six-scene lineup: two new exclusive scenes per shipped pet, drawn on the same
 * 88 px canvas and in the same twelve-colour palette as data/scenes. Each entry is a poster, not final
 * data: the pet stands in its real idle/sleep frames, and the props are authored here so the shapes,
 * mood weights and entries can be reviewed before they become scene recipes.
 */

export const CANVAS = 88;

export const COLORS = {
  ink: '#1a1a1f',
  fur: '#fbfbfd',
  grey: '#4a4e56',
  rose: '#ec628c',
  sky: '#52a0ec',
  green: '#34aa60',
  lilac: '#a074e8',
  amber: '#d8a454',
  brown: '#965e36',
  cream: '#f7e8c8',
  gold: '#f2ba40',
  pumpkin: '#f08838'
};

/** Pixel helpers. All coordinates are canvas units (88 × 88), not cells. */
export function pen(ctx) {
  const paint = name => COLORS[name] ?? null;

  const rect = (x, y, w, h, colour) => {
    const fill = paint(colour);
    if (!fill) return;
    ctx.fillStyle = fill;
    ctx.fillRect(Math.round(x), Math.round(y), Math.round(w), Math.round(h));
  };

  const px = (x, y, colour) => rect(x, y, 1, 1, colour);

  /** Rows of characters: one character is one cell (3 px) unless `step` says otherwise. `.` is a hole. */
  const rows = (x, y, art, map, step = 3) => {
    for (let r = 0; r < art.length; r += 1) {
      for (let c = 0; c < art[r].length; c += 1) {
        const colour = map[art[r][c]];
        if (colour) rect(x + c * step, y + r * step, step, step, colour);
      }
    }
  };

  const line = (x0, y0, x1, y1, colour) => {
    let x = Math.round(x0);
    let y = Math.round(y0);
    const ex = Math.round(x1);
    const ey = Math.round(y1);
    const dx = Math.abs(ex - x);
    const dy = -Math.abs(ey - y);
    const sx = x < ex ? 1 : -1;
    const sy = y < ey ? 1 : -1;
    let err = dx + dy;
    for (;;) {
      px(x, y, colour);
      if (x === ex && y === ey) break;
      const e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y += sy;
      }
    }
  };

  const ellipse = (cx, cy, rx, ry, colour) => {
    for (let y = -ry; y <= ry; y += 1) {
      const span = Math.round(rx * Math.sqrt(Math.max(0, 1 - (y * y) / (ry * ry))));
      rect(cx - span, cy + y, span * 2 + 1, 1, colour);
    }
  };

  const disc = (cx, cy, r, colour) => ellipse(cx, cy, r, r, colour);

  const ring = (cx, cy, r, colour) => {
    let x = r;
    let y = 0;
    let err = 0;
    while (x >= y) {
      px(cx + x, cy + y, colour);
      px(cx + y, cy + x, colour);
      px(cx - y, cy + x, colour);
      px(cx - x, cy + y, colour);
      px(cx - x, cy - y, colour);
      px(cx - y, cy - x, colour);
      px(cx + y, cy - x, colour);
      px(cx + x, cy - y, colour);
      y += 1;
      if (err <= 0) err += 2 * y + 1;
      if (err > 0) {
        x -= 1;
        err -= 2 * x + 1;
      }
    }
  };

  /** A one-pixel arc; angles in radians, 0 pointing right, positive clockwise on screen. */
  const arc = (cx, cy, r, from, to, colour, step = 0.09) => {
    for (let a = from; a <= to; a += step) px(cx + Math.round(Math.cos(a) * r), cy + Math.round(Math.sin(a) * r), colour);
  };

  const star = (cx, cy, colour) => {
    px(cx, cy - 1, colour);
    px(cx - 1, cy, colour);
    px(cx, cy, colour);
    px(cx + 1, cy, colour);
    px(cx, cy + 1, colour);
  };

  const sparkle = (cx, cy, colour) => {
    px(cx, cy, colour);
    px(cx - 2, cy, colour);
    px(cx + 2, cy, colour);
    px(cx, cy - 2, colour);
    px(cx, cy + 2, colour);
  };

  /** A rising steam or smoke wisp: one pixel per step with a sideways sway. */
  const wisp = (x, y, ms, phase, height, colour, speed = 520) => {
    for (let i = 0; i < height; i += 2) {
      const sway = Math.round(Math.sin(ms / speed + phase + i / 5) * 2);
      px(x + sway, y - i, colour);
    }
  };

  return { rect, px, rows, line, ellipse, disc, ring, arc, star, sparkle, wisp };
}

/** Dandelion seeds drifting right, authored once so every seed shares the same fluff. */
function seed(p, x, y) {
  p.px(x, y, 'fur');
  p.px(x - 1, y - 1, 'fur');
  p.px(x + 1, y - 1, 'fur');
  p.px(x, y + 1, 'grey');
}

/** A small orange fish, tail left; `dir` flips it. */
function goldfish(p, x, y, dir = 1) {
  p.rect(dir > 0 ? x : x - 4, y, 4, 3, 'pumpkin');
  p.rect(dir > 0 ? x + 4 : x - 6, y + 1, 2, 1, 'gold');
  p.px(dir > 0 ? x + 1 : x - 2, y + 1, 'ink');
  p.px(dir > 0 ? x - 1 : x + 4, y + 2, 'gold');
}

/** A coin-sized mikan with its leaf. */
function mikan(p, x, y) {
  p.disc(x, y, 4, 'pumpkin');
  p.px(x + 1, y - 2, 'gold');
  p.px(x - 2, y + 2, 'gold');
  p.rect(x, y - 6, 1, 2, 'brown');
  p.rect(x + 1, y - 7, 3, 2, 'green');
}

/** The slipper: rounded toe, cream lining, brown sole and a heel loop. `flip` points the toe right. */
function slipper(p, x, y, flip = false) {
  const rows = [
    '..######..',
    '.#cccccc#.',
    '#rrrrrrrr#',
    '#rrrrrrrr#',
    '.########.'
  ];
  const art = rows.map(row => (flip ? [...row].reverse().join('') : row));
  p.rows(x, y, art, { '#': 'ink', c: 'cream', r: 'rose' });
  const heel = flip ? x + 1 : x + 23;
  p.rect(heel, y + 3, 3, 5, 'cream');
  p.px(heel, y + 3, 'ink');
  p.px(heel + 2, y + 7, 'ink');
  p.rect(flip ? x + 24 : x + 3, y + 6, 3, 3, 'cream');
  p.rect(x + 2, y + 13, 23, 2, 'brown');
  p.px(flip ? x + 8 : x + 18, y + 10, 'gold');
  // Plush trim along the opening.
  p.px(x + 7, y + 2, 'fur');
  p.px(x + 11, y + 2, 'fur');
  p.px(x + 15, y + 2, 'fur');
}

/** A cream envelope with a rose stamp and an ink flap. */
function envelope(p, x, y, stamp = 'rose') {
  p.rect(x, y, 13, 8, 'cream');
  p.px(x + 7, y + 2, stamp);
  p.px(x + 7, y + 3, stamp);
  p.line(x + 1, y + 7, x + 6, y + 3, 'ink');
  p.line(x + 6, y + 3, x + 11, y + 7, 'ink');
}

export const SCENES = {
  cat_yarn: {
    id: 'cat_yarn',
    pet: 'cat',
    name: '毛线球',
    en: 'Yarn tangle',
    line: '一团玫红毛线滚到脚边。猫按住它，线绕过前爪；每隔几秒，线团又被拽回一点。',
    props: '线团、线头、地毯',
    entry: '滑就位',
    entryNote: '猫先蹲下，线团滚到脚边',
    moods: [['午后', 2.5], ['上午', 1.25], ['傍晚', 1]],
    nudge: '心形腮红 ×2',
    tint: '#f0e7ea',
    draw(ctx, ms, stage) {
      const { p } = stage;
      const tug = Math.round(Math.sin(ms / 720) * 2);
      const bob = Math.round(Math.sin(ms / 380));

      // The ball sits at the cat's left paw: a round rose ball with cream strands winding across it.
      const bx = 20 + tug;
      const by = 70 + bob;
      p.disc(bx, by, 10, 'rose');
      p.ring(bx, by, 10, 'ink');
      // One continuous strand wound around the ball, then out to the cat.
      for (let t = 0; t < 2.9 * Math.PI; t += 0.22) {
        const r = 1.5 + t * 0.85;
        p.px(Math.round(bx + Math.cos(t) * r), Math.round(by + Math.sin(t) * r), 'cream');
      }
      p.px(bx - 4, by - 6, 'fur');
      p.px(bx - 8, by + 2, 'cream');
      p.line(bx + 9, by + 3, 36, 80, 'rose');
      p.line(36, 80, 42, 78, 'rose');
      // Two loose strands on the floor.
      p.line(bx - 11, by + 8, bx - 16, by + 10, 'rose');
      p.px(bx - 17, by + 9, 'rose');
      p.px(bx - 13, by + 11, 'rose');

      stage.drawPet({ gaze: [1, 0] });

      // The strand in front of the paws, tightening as the ball is tugged.
      const lift = Math.round(Math.sin(ms / 620) * 2);
      p.line(42, 78, 47, 73 + lift, 'rose');
      p.line(47, 73 + lift, 53, 78 - lift, 'rose');
      p.line(53, 78 - lift, 61, 76, 'rose');
      p.line(61, 76, 65, 79, 'rose');
      p.px(65, 80, 'rose');
      p.px(45, 69 + lift, 'cream');
    }
  },

  cat_fishbowl: {
    id: 'cat_fishbowl',
    pet: 'cat',
    name: '金鱼缸',
    en: 'Goldfish bowl',
    line: '小凳上的鱼缸里有一条金鱼。猫的视线跟着鱼转，气泡一颗颗升到水面。',
    props: '小凳、鱼缸、金鱼、气泡',
    entry: '滑就位',
    entryNote: '猫踱到凳边，抬头',
    moods: [['傍晚', 2], ['深夜', 1.5], ['刚回来', 1.5]],
    nudge: '鱼骨项圈 ×2.5',
    tint: '#e5edf2',
    draw(ctx, ms, stage) {
      const { p } = stage;
      stage.drawPet({ gaze: [-2, 0] });

      // Stool first, then the bowl sits on it.
      p.rect(8, 71, 26, 3, 'brown');
      p.rect(8, 71, 26, 1, 'amber');
      p.rect(10, 74, 3, 7, 'brown');
      p.rect(29, 74, 3, 7, 'brown');
      p.rect(12, 78, 18, 1, 'amber');

      // Water, glass and base.
      p.disc(21, 55, 16, 'sky');
      p.disc(21, 55, 15, 'sky');
      p.ellipse(21, 67, 10, 3, 'grey');
      p.ellipse(21, 66, 10, 2, 'amber');
      p.rect(12, 47, 18, 1, 'fur');

      // The goldfish, with a darting beat every six seconds.
      const beat = ms % 6000;
      const speed = beat > 4800 ? 2.2 : 0.8;
      const angle = (ms / 1000) * speed;
      const fx = Math.round(21 + Math.cos(angle) * 8);
      const fy = Math.round(55 + Math.sin(angle * 1.4) * 5);
      goldfish(p, fx, fy, Math.cos(angle) >= 0 ? 1 : -1);

      // Bubbles.
      for (let i = 0; i < 4; i += 1) {
        const t = ((ms / 1600 + i / 4) % 1);
        const by = Math.round(58 - t * 12);
        const bx = Math.round(fx + (i % 2 ? 2 : -1) + Math.sin(t * 6 + i) * 1.5);
        p.px(bx, by, 'fur');
        if (t < 0.7) p.px(bx + 1, by, 'sky');
      }

      // Glass shine and a ripple at the waterline.
      p.arc(21, 55, 14, Math.PI * 1.05, Math.PI * 1.45, 'fur');
      if (beat > 4800) {
        p.line(12, 48, 19, 48, 'fur');
        p.line(24, 48, 30, 48, 'fur');
      }
      p.px(13, 56, 'fur');
      p.px(29, 62, 'fur');
    }
  },

  cloud_rainbow: {
    id: 'cloud_rainbow',
    pet: 'cloud',
    name: '彩虹滑梯',
    en: 'Rainbow slide',
    line: '云把彩虹当滑梯，滑到坡脚再慢慢飘回坡顶，重新滑一次。',
    props: '彩虹滑道、云尾、火花',
    entry: '滑就位',
    entryNote: '云先滑到坡顶就位',
    moods: [['清晨', 2], ['午后', 1.25]],
    nudge: '雨链 ×2.5',
    tint: '#eef0e7',
    draw(ctx, ms, stage) {
      const { p } = stage;

      // A diagonal chute from the upper left to the lower right; the pet rides its top edge.
      const rampY = x => 38 + 44 * Math.sin((Math.min(1, Math.max(0, (x - 18) / 52)) * Math.PI) / 2);
      const bands = ['rose', 'amber', 'green', 'sky'];
      for (let x = 18; x <= 70; x += 1) {
        const y = Math.round(rampY(x));
        p.px(x, y - 1, 'ink');
        bands.forEach((colour, i) => p.rect(x, y + i * 5, 1, 5, colour));
        p.px(x, y + 20, 'ink');
      }

      // The flat run-out at the bottom, where the cloud lands.
      p.ellipse(74, 82, 9, 4, 'fur');
      p.ellipse(74, 84, 9, 2, 'grey');

      const cycle = 5700;
      const t = ms % cycle;
      let x = 28;
      let y = rampY(28) - 1;
      let progress = 0;
      let mode = 'top';
      if (t < 2200) {
        const u = t / 2200;
        progress = u * u * (3 - 2 * u);
        x = 28 + 32 * progress;
        y = rampY(x) - 1;
        mode = 'down';
      } else if (t < 2500) {
        progress = 1;
        x = 60;
        y = rampY(x) - 1 + Math.round(Math.sin((t - 2200) / 110));
        mode = 'rest';
      } else if (t < 5300) {
        const u = (t - 2500) / 2800;
        progress = 1 - u * u * (3 - 2 * u);
        x = 28 + 32 * progress;
        y = rampY(x) - 1 - Math.round(Math.sin(progress * Math.PI) * 12);
        mode = 'up';
      }
      const shift = [Math.round(x - stage.foot.x), Math.round(y - stage.foot.y)];

      // Trail puffs: behind on the chute, below the cloud on the float back up.
      for (let i = 1; i <= 3; i += 1) {
        let back = progress;
        if (mode === 'down') back = progress - i * 0.075;
        else if (mode === 'up') back = progress + i * 0.065;
        back = Math.max(0, Math.min(1, back));
        const tx = 28 + 32 * back;
        const ty = mode === 'up'
          ? rampY(tx) - 1 - Math.sin(back * Math.PI) * 12 + 4
          : rampY(tx) - 5;
        p.ellipse(Math.round(tx), Math.round(ty), 4 - i, 2, i === 1 ? 'fur' : 'cream');
      }

      // Sparks trail the cloud; the way down leaves a longer one.
      const count = mode === 'down' ? 4 : 2;
      for (let i = 0; i < count; i += 1) {
        const a = ms / 620 + i * 1.7;
        p.star(Math.round(x + 18 + Math.cos(a) * 12), Math.round(y - 12 + Math.sin(a) * 10), i === 0 ? 'gold' : 'cream');
      }
      if (mode === 'down') {
        p.line(Math.round(x - 18), Math.round(rampY(x - 18) - 8), Math.round(x - 10), Math.round(rampY(x - 10) - 9), 'cream');
        p.px(Math.round(x - 22), Math.round(rampY(x - 22) - 5), 'gold');
      }

      stage.drawPet({ x: shift[0], y: shift[1] });

      // The lip puff when a cloud gathers at the top; the landing puff at the foot.
      if (mode === 'top' || t < 220) {
        p.disc(28, 52, 3, 'fur');
        p.disc(23, 54, 2, 'cream');
        p.disc(33, 54, 2, 'cream');
      }
      if (mode === 'rest' || (mode === 'down' && progress > 0.82)) {
        p.disc(64, 84, 6, 'fur');
        p.disc(60, 85, 3, 'cream');
        p.disc(68, 85, 3, 'cream');
      }
    }
  },

  cloud_dandelion: {
    id: 'cloud_dandelion',
    pet: 'cloud',
    name: '吹蒲公英',
    en: 'Dandelion',
    line: '一株蒲公英长在右边。云鼓起腮帮，种子一粒粒被吹向屏幕外。',
    props: '蒲公英、草、飞散的种子',
    entry: '随道具淡入',
    entryNote: '风把草与种子一起带来',
    moods: [['午后', 2], ['傍晚', 1]],
    nudge: '风暴帽 ×2',
    tint: '#eaf0ea',
    draw(ctx, ms, stage) {
      const { p } = stage;

      // Grass and two dandelions: one full head, one already bare.
      p.line(66, 81, 70, 76, 'green');
      p.line(74, 81, 72, 76, 'green');
      p.line(84, 81, 81, 76, 'green');
      p.rect(77, 59, 2, 22, 'green');
      p.line(78, 75, 84, 71, 'green');
      p.line(78, 77, 70, 73, 'green');
      p.rect(65, 65, 2, 16, 'green');
      p.disc(66, 63, 2, 'cream');
      p.px(66, 63, 'green');
      p.disc(76, 50, 9, 'cream');
      p.disc(76, 50, 5, 'fur');
      p.px(76, 50, 'green');

      // Seed stalks still on the head.
      for (let i = 0; i < 12; i += 1) {
        const a = (i / 12) * Math.PI * 2;
        p.px(Math.round(76 + Math.cos(a) * 10), Math.round(50 + Math.sin(a) * 10), 'fur');
        p.px(Math.round(76 + Math.cos(a) * 9), Math.round(50 + Math.sin(a) * 9), 'grey');
      }

      stage.drawPet({ x: -18, gaze: [2, 0] });

      // The puff leaves the cloud's mouth in three puffs.
      for (let i = 0; i < 3; i += 1) {
        const t = ((ms / 1500 + i / 3) % 1);
        p.disc(Math.round(46 + t * 18), Math.round(65 - t * 4 + Math.sin(t * 9 + i) * 2), 2 + (t > 0.5 ? 1 : 0), 'fur');
      }
      p.arc(44, 66, 5 + ((ms % 2800) / 2800) * 9, -0.5, 0.5, 'cream');

      // Wind lines.
      for (let i = 0; i < 4; i += 1) {
        const y = 60 + i * 4 + Math.round(Math.sin(ms / 500 + i) * 1);
        const x = 52 + ((ms / 90 + i * 9) % 16);
        p.line(x, y, x + 7, y, i === 1 ? 'grey' : 'fur');
      }

      // Seeds drifting right, some still close to the head.
      for (let i = 0; i < 7; i += 1) {
        const t = ((ms / 6800 + i / 7) % 1);
        const x = Math.round(46 + t * 38 + Math.sin(t * 8 + i) * 2);
        const y = Math.round(45 - t * 8 + Math.sin(t * 10 + i * 2) * 4);
        seed(p, x, y);
      }
    }
  },

  dog_snowman: {
    id: 'dog_snowman',
    pet: 'dog',
    name: '雪人',
    en: 'Snowman',
    line: '雪人站在院子里，奶糖把鼻子凑过去闻了又闻；每呼一口气，雪人就掉下一点雪。',
    props: '雪人、雪地、雪花',
    entry: '跳进来',
    entryNote: '落在雪里，扬起一团雪',
    moods: [['上午', 1.5], ['午后', 2]],
    nudge: '针织围巾 ×2.5',
    tint: '#e6ebee',
    draw(ctx, ms, stage) {
      const { p } = stage;

      // Snow bank and the paw prints leading to the snowman.
      p.ellipse(52, 82, 36, 6, 'fur');
      p.ellipse(52, 84, 36, 4, 'cream');
      p.ellipse(15, 84, 8, 3, 'grey');
      p.px(62, 80, 'grey');
      p.px(66, 79, 'grey');
      p.px(70, 81, 'grey');

      stage.drawPet({ gaze: [-2, 0] });

      // The snowman, in front of the dog's left side.
      p.disc(23, 72, 11, 'ink');
      p.disc(23, 72, 10, 'fur');
      p.disc(23, 57, 9, 'ink');
      p.disc(23, 57, 8, 'fur');
      p.disc(23, 44, 7, 'ink');
      p.disc(23, 44, 6, 'fur');
      p.arc(23, 72, 10, 1.1, 2.5, 'grey');
      p.arc(23, 57, 8, 1.1, 2.5, 'grey');
      p.arc(23, 44, 6, 1.1, 2.5, 'grey');

      // Coal face, carrot nose, buttons.
      p.px(20, 42, 'ink');
      p.px(26, 42, 'ink');
      p.rect(29, 43, 5, 2, 'pumpkin');
      p.px(34, 43, 'pumpkin');
      p.px(21, 47, 'ink');
      p.px(23, 48, 'ink');
      p.px(25, 47, 'ink');
      p.px(23, 54, 'ink');
      p.px(23, 58, 'ink');
      p.px(23, 62, 'ink');

      // Twig arms, scarf and hat.
      p.line(14, 55, 7, 50, 'brown');
      p.line(7, 50, 4, 52, 'brown');
      p.line(32, 55, 39, 50, 'brown');
      p.line(39, 50, 42, 52, 'brown');
      p.rect(15, 49, 16, 3, 'rose');
      p.rect(28, 52, 3, 7, 'rose');
      p.px(29, 59, 'cream');
      p.rect(16, 35, 14, 2, 'ink');
      p.rect(18, 29, 10, 7, 'brown');
      p.rect(18, 31, 10, 2, 'amber');
      p.px(23, 27, 'ink');

      // Snow falls; the dog's breath crosses the gap.
      for (let i = 0; i < 5; i += 1) {
        const t = (ms / 2400 + i / 5) % 1;
        p.px(8 + i * 7 + Math.round(Math.sin(t * 6 + i) * 2), Math.round(20 + t * 55), i % 2 ? 'fur' : 'cream');
      }
      for (let i = 0; i < 3; i += 1) {
        const t = (ms / 1500 + i / 3) % 1;
        p.disc(Math.round(48 - t * 12), Math.round(51 - Math.sin(t * Math.PI) * 3), 2, i % 2 ? 'fur' : 'cream');
      }
      const drop = (ms % 3200) / 3200;
      if (drop < 0.45) p.px(40, 52 + Math.round(drop * 34), 'fur');
    }
  },

  dog_bed: {
    id: 'dog_bed',
    pet: 'dog',
    name: '窝里打盹',
    en: 'Dog bed',
    line: '奶糖蜷进小窝，半个脑袋露在窝沿；骨头就在旁边，梦里还在找。',
    props: '狗窝、骨头、呼噜',
    entry: '滑就位',
    entryNote: '蜷进窝里，只露一个脑袋',
    moods: [['傍晚', 1.5], ['深夜', 2.5], ['刚回来', 2]],
    nudge: '针织冷帽 ×2.5',
    tint: '#e9e7ea',
    draw(ctx, ms, stage) {
      const { p } = stage;

      // Bone on the floor at the left.
      p.rect(2, 78, 13, 3, 'cream');
      p.disc(2, 78, 2, 'cream');
      p.disc(2, 81, 2, 'cream');
      p.disc(15, 78, 2, 'cream');
      p.disc(15, 81, 2, 'cream');
      p.rect(4, 78, 9, 2, 'amber');

      // The bed's back rim and cushion.
      p.ellipse(51, 74, 33, 10, 'ink');
      p.ellipse(51, 74, 32, 9, 'rose');
      p.ellipse(51, 73, 28, 7, 'cream');

      // The dog dozes in the bed.
      stage.drawPet({ action: 'sleep', effect: false, y: -2, gaze: [0, 1] });

      // The front rim, over the lower body.
      p.ellipse(51, 81, 33, 8, 'ink');
      p.ellipse(51, 80, 32, 7, 'rose');
      p.ellipse(51, 78, 27, 5, 'cream');
      p.ellipse(51, 84, 33, 3, 'rose');

      // A tail tip and the Zzz.
      const wag = Math.round(Math.sin(ms / 520));
      p.rect(81, 71 + wag, 4, 3, 'grey');
      p.px(85, 72 + wag, 'cream');
      const zz = Math.round(Math.sin(ms / 850));
      p.rows(65, 25 - zz, ['.###', '..#.', '.#..', '###'], { '#': 'grey' }, 1);
      p.rows(71, 18 - zz, ['.##', '.#.', '##.'], { '#': 'fur' }, 1);
      p.rows(76, 12 - zz, ['.#', '#.'], { '#': 'cream' }, 1);
    }
  },

  ghost_clock: {
    id: 'ghost_clock',
    pet: 'ghost',
    name: '午夜座钟',
    en: 'Midnight clock',
    line: '座钟立在左边，钟摆来回。每到整点，钟门开一条缝，幽灵探头听钟声。',
    props: '座钟、钟摆、钟声',
    entry: '随道具淡入',
    entryNote: '钟声先响，幽灵随后显形',
    moods: [['傍晚', 1.5], ['深夜', 4]],
    nudge: '蜡烛 ×2.5',
    tint: '#e8e9ef',
    draw(ctx, ms, stage) {
      const { p } = stage;

      // Feet, case, crown.
      p.rect(3, 77, 32, 4, 'brown');
      p.rect(3, 77, 32, 1, 'amber');
      p.rect(5, 20, 28, 57, 'brown');
      p.rect(5, 20, 1, 57, 'ink');
      p.rect(32, 20, 1, 57, 'ink');
      p.rect(7, 22, 2, 54, 'amber');
      p.rect(8, 14, 22, 7, 'brown');
      p.rect(8, 14, 22, 1, 'ink');
      p.rect(30, 14, 1, 7, 'ink');
      p.rect(17, 9, 5, 5, 'gold');
      p.rect(18, 14, 3, 1, 'gold');
      p.px(19, 9, 'cream');
      p.px(18, 8, 'gold');
      p.px(21, 8, 'gold');

      // The chime leaves the bell, well left of the ghost.
      const chime = ms % 8000;
      if (chime < 1400) {
        const grow = 2 + (chime / 1400) * 7;
        p.arc(19, 9, grow, Math.PI, Math.PI * 2, 'gold');
        p.arc(19, 9, grow + 3, Math.PI, Math.PI * 2, 'cream');
      }

      // The face and hands.
      p.disc(19, 31, 9, 'cream');
      p.ring(19, 31, 9, 'ink');
      const minute = ms / 9000;
      const hour = minute / 12;
      p.line(19, 31, 19 + Math.round(Math.cos(minute) * 7), 31 + Math.round(Math.sin(minute) * 7), 'ink');
      p.line(19, 31, 19 + Math.round(Math.cos(hour) * 4), 31 + Math.round(Math.sin(hour) * 4), 'ink');
      p.px(19, 31, 'gold');

      // Pendulum window.
      p.rect(10, 46, 18, 22, 'ink');
      p.rect(11, 47, 16, 20, 'grey');
      p.rect(11, 47, 1, 20, 'amber');
      const swing = Math.sin(ms / 760) * 5;
      p.line(19, 47, 19 + Math.round(swing), 61, 'gold');
      p.disc(19 + Math.round(swing), 63, 3, 'gold');
      p.px(18 + Math.round(swing), 62, 'cream');

      stage.drawPet({ gaze: [-2, 0] });
      p.px(76, 70, 'lilac');
      p.px(79, 73, 'lilac');
      p.px(30, 62, 'lilac');
      p.px(33, 60, 'lilac');
      p.px(26, 65, 'lilac');
    }
  },

  ghost_tea: {
    id: 'ghost_tea',
    pet: 'ghost',
    name: '幽灵茶会',
    en: 'Ghost tea',
    line: '茶壶自己飘起来，往悬空的杯子里倒茶；幽灵坐在对面，像在招待看不见的客人。',
    props: '茶壶、茶杯、托盘、热气',
    entry: '随道具淡入',
    entryNote: '茶具先浮起来',
    moods: [['傍晚', 2], ['深夜', 1.5], ['刚回来', 1.5]],
    nudge: '小胡子 ×2.5',
    tint: '#eae6e1',
    draw(ctx, ms, stage) {
      const { p } = stage;

      // Table legs and the back edge of the top.
      p.rect(54, 69, 3, 12, 'brown');
      p.rect(84, 69, 3, 12, 'brown');
      p.rect(50, 62, 38, 7, 'brown');
      p.rect(50, 62, 38, 2, 'amber');
      p.rect(50, 68, 38, 1, 'ink');

      stage.drawPet({ x: -24, gaze: [2, 0] });

      // The tabletop and its cream runner.
      p.rect(50, 60, 38, 5, 'brown');
      p.rect(50, 60, 38, 2, 'amber');
      p.ellipse(69, 60, 17, 2, 'cream');

      // Saucer and the floating cup, under the spout.
      const bob = Math.round(Math.sin(ms / 620));
      p.ellipse(57, 59, 8, 2, 'cream');
      p.px(57, 59, 'gold');
      p.rows(50, 48 + bob, [
        '.#####.',
        '#ccccc#',
        '#ccccc#',
        '.#####.'
      ], { '#': 'ink', c: 'cream' }, 2);
      p.rect(51, 49 + bob, 11, 1, 'rose');
      p.rect(52, 52 + bob, 9, 1, 'amber');
      p.rect(64, 50 + bob, 2, 4, 'cream');
      p.px(65, 51 + bob, 'ink');
      p.px(55, 47 + bob, 'cream');

      // The pot: a lidded body, a foot, a spout and a handle.
      ctx.save();
      ctx.translate(69, 35);
      ctx.rotate(-0.03);
      ctx.translate(-69, -35);
      p.ellipse(69, 36, 12, 8, 'ink');
      p.ellipse(69, 36, 11, 7, 'cream');
      p.rect(59, 38, 20, 2, 'rose');
      p.rect(63, 43, 12, 1, 'amber');
      p.rect(64, 44, 10, 3, 'brown');
      p.rect(65, 47, 8, 2, 'brown');
      p.ellipse(69, 27, 7, 3, 'ink');
      p.ellipse(69, 26, 6, 2, 'cream');
      p.rect(66, 23, 6, 3, 'rose');
      p.px(68, 22, 'gold');
      p.rect(60, 30, 5, 3, 'cream');
      p.rect(57, 33, 4, 3, 'cream');
      p.rect(55, 35, 3, 2, 'brown');
      p.px(54, 36, 'gold');
      p.arc(81, 35, 6, -1.35, 1.35, 'brown');
      p.arc(81, 35, 5, -1.35, 1.35, 'cream');
      p.px(65, 33, 'cream');
      ctx.restore();

      // A short vertical stream from spout to cup.
      for (let i = 0; i < 4; i += 1) {
        const y = Math.round(38 + ((ms / 260 + i / 4) % 1) * 9);
        p.px(54 + (i % 2), y, i % 2 ? 'amber' : 'gold');
      }

      // Steam over the cup and pot.
      p.wisp(56, 44, ms, 0, 12, 'fur');
      p.wisp(52, 42, ms, 1.8, 9, 'grey');
      p.wisp(68, 21, ms, 0.7, 10, 'cream');

      // A biscuit on the saucer.
      p.rect(67, 57, 4, 2, 'amber');
      p.px(68, 58, 'brown');
      p.px(72, 55, 'cream');
    }
  },

  jelly_lantern: {
    id: 'jelly_lantern',
    pet: 'jelly',
    name: '深海夜灯',
    en: 'Deep-sea lamp',
    line: '礁石上有一盏小灯，有条鱼绕着它转。水母在灯旁一明一暗，像另一盏灯。',
    props: '礁石、海草、小灯、鱼、气泡',
    entry: '随道具淡入',
    entryNote: '灯先亮，水母再游进来',
    moods: [['傍晚', 2], ['深夜', 4]],
    nudge: '小鱼挂坠 ×2.5',
    tint: '#e2e8ea',
    draw(ctx, ms, stage) {
      const { p } = stage;
      const sway = phase => Math.round(Math.sin(ms / 900 + phase) * 2);

      // Rock, algae, kelp.
      p.ellipse(17, 77, 20, 8, 'ink');
      p.ellipse(17, 72, 18, 9, 'grey');
      p.ellipse(12, 76, 9, 6, 'ink');
      p.rect(2, 78, 36, 3, 'green');
      for (let i = 0; i < 3; i += 1) {
        const x = 8 + i * 11;
        for (let y = 72; y > 30; y -= 4) {
          p.rect(x + sway(i) + Math.round(Math.sin(y / 8 + i) * 2), y, 2, 4, 'green');
        }
        p.px(x + 2 + sway(i), 56, 'green');
        p.px(x - 1 + sway(i), 48, 'green');
      }

      // The little lamp on the rock.
      p.rect(17, 56, 3, 18, 'brown');
      p.rect(12, 42, 13, 3, 'brown');
      p.rect(12, 45, 2, 10, 'brown');
      p.rect(23, 45, 2, 10, 'brown');
      p.rect(14, 47, 9, 8, 'gold');
      p.rect(16, 49, 5, 4, 'cream');
      p.px(18, 51, 'gold');
      const glow = 9 + Math.round(Math.sin(ms / 500) * 2);
      p.ring(18, 51, glow, 'gold');
      p.px(18, 36, 'gold');
      p.px(10, 44, 'cream');
      p.px(26, 40, 'cream');

      // A small fish circling the lamp.
      const a = ms / 1100;
      const fx = Math.round(22 + Math.cos(a) * 16);
      const fy = Math.round(35 + Math.sin(a) * 8);
      p.ellipse(fx, fy, 5, 3, 'sky');
      p.rect(fx - 7, fy - 1, 3, 3, 'gold');
      p.px(fx + 3, fy - 1, 'ink');
      p.px(fx - 1, fy - 3, 'cream');
      p.px(fx - 2, fy + 3, 'gold');

      stage.drawPet({ x: 6 });

      // Glow at the tentacle tips, bubbles rising.
      p.px(50, 72, 'gold');
      p.px(57, 76, 'cream');
      p.px(64, 70, 'gold');
      p.px(54, 58, 'gold');
      p.px(61, 61, 'cream');
      p.px(47, 65, 'gold');
      for (let i = 0; i < 5; i += 1) {
        const t = ((ms / 2600 + i / 5) % 1);
        const x = 30 + i * 8 + Math.round(Math.sin(t * 8 + i) * 2);
        const y = Math.round(74 - t * 46);
        p.px(x, y, i % 2 ? 'cream' : 'gold');
      }
    }
  },

  jelly_shell: {
    id: 'jelly_shell',
    pet: 'jelly',
    name: '贝壳床',
    en: 'Shell bed',
    line: '贝壳半开着，水母窝在里面的软垫上；潮水一动，壳盖跟着晃，像在呼吸。',
    props: '大贝壳、珍珠、沙地',
    entry: '滑就位',
    entryNote: '飘进贝壳，只露出伞盖',
    moods: [['午后', 1], ['深夜', 2.5], ['刚回来', 2]],
    nudge: '贝壳项链 ×2.5',
    tint: '#e4eaec',
    draw(ctx, ms, stage) {
      const { p } = stage;
      const sway = Math.sin(ms / 1200);

      // Sand and a few pebbles.
      p.ellipse(50, 82, 38, 6, 'cream');
      p.ellipse(50, 84, 38, 4, 'amber');
      p.px(20, 78, 'grey');
      p.px(76, 79, 'grey');
      p.px(30, 83, 'cream');

      // The upper valve, behind the pet: a ribbed fan hinged low.
      ctx.save();
      ctx.translate(50, 68);
      ctx.rotate(sway * 0.03);
      ctx.translate(-50, -68);
      p.ellipse(50, 52, 28, 20, 'ink');
      p.ellipse(50, 51, 27, 19, 'rose');
      for (let i = 0; i < 7; i += 1) {
        const a = Math.PI + (i / 6) * Math.PI;
        p.line(50, 68, Math.round(50 + Math.cos(a) * 26), Math.round(68 + Math.sin(a) * 18), 'cream');
      }
      p.arc(50, 51, 20, Math.PI * 1.1, Math.PI * 1.9, 'cream');
      ctx.restore();

      // The lower valve and its soft cushion.
      p.ellipse(50, 73, 28, 10, 'ink');
      p.ellipse(50, 72, 27, 9, 'rose');
      p.ellipse(50, 71, 23, 7, 'cream');

      // The jelly nestled in the shell.
      stage.drawPet({ x: -3, y: 3, gaze: [0, -1] });

      // The near rim, over the tentacles.
      p.ellipse(50, 80, 28, 7, 'ink');
      p.ellipse(50, 79, 27, 6, 'rose');
      p.ellipse(50, 77, 22, 4, 'cream');

      // Pearls and bubbles.
      p.disc(31, 70, 2, 'cream');
      p.px(31, 70, 'gold');
      p.disc(68, 72, 2, 'cream');
      p.px(68, 72, 'gold');
      p.disc(80, 78, 2, 'rose');
      p.px(80, 78, 'cream');
      for (let i = 0; i < 4; i += 1) {
        const t = (ms / 2400 + i / 4) % 1;
        p.px(38 + i * 9 + Math.round(Math.sin(t * 6 + i) * 2), Math.round(68 - t * 42), i % 2 ? 'fur' : 'cream');
      }
    }
  },

  mochi_kotatsu: {
    id: 'mochi_kotatsu',
    pet: 'mochi',
    name: '暖桌',
    en: 'Kotatsu',
    line: '麻糬钻进被炉，只露出半个脑袋；桌上橘子慢慢滚，被角随呼吸起伏。',
    props: '被炉、桌板、橘子',
    entry: '滑就位',
    entryNote: '麻糬钻进去，只露头',
    moods: [['傍晚', 2.5], ['深夜', 2], ['刚回来', 2]],
    nudge: '风吕敷 ×2.5',
    tint: '#f1e8e6',
    draw(ctx, ms, stage) {
      const { p } = stage;
      const breath = Math.round(Math.sin(ms / 1100));

      stage.drawPet({ gaze: [1, 0] });

      // Tabletop, then the blanket in front.
      p.rect(14, 58, 74, 5, 'brown');
      p.rect(14, 58, 74, 2, 'amber');
      p.rect(14, 62, 74, 1, 'ink');
      p.rect(8, 63, 80, 18, 'rose');
      p.rect(8, 63 + breath, 80, 3, 'cream');
      for (let x = 22; x < 88; x += 14) p.rect(x, 67, 1, 14, 'cream');
      p.rect(8, 73, 80, 1, 'cream');
      p.rect(8, 78, 80, 1, 'cream');
      p.rect(8, 79, 80, 2, 'rose');
      p.rect(30, 66, 1, 15, 'cream');
      p.rect(58, 66, 1, 15, 'cream');

      // Mikans sitting on the tabletop.
      const roll = Math.round(Math.sin(ms / 950) * 2);
      mikan(p, 68 + roll, 54);
      mikan(p, 36, 54);
    }
  },

  mochi_pound: {
    id: 'mochi_pound',
    pet: 'mochi',
    name: '捣年糕',
    en: 'Mochi pounding',
    line: '木杵落下，麻糬被敲得一跳，米粉扬起；下一杵落下前，它又弹回原样。',
    props: '木臼、木杵、米粉',
    entry: '跳进来',
    entryNote: '麻糬跳进木臼',
    moods: [['清晨', 1.5], ['午后', 2]],
    nudge: '钵卷 ×2.5',
    tint: '#efeae2',
    draw(ctx, ms, stage) {
      const { p } = stage;

      // Straw mat, a plate of finished mochi, and the mortar.
      p.rect(28, 79, 44, 3, 'cream');
      p.rect(28, 79, 44, 1, 'amber');
      p.px(26, 80, 'cream');
      p.px(74, 80, 'cream');
      p.ellipse(17, 80, 9, 2, 'cream');
      p.ellipse(17, 81, 8, 1, 'amber');
      p.disc(13, 76, 4, 'fur');
      p.disc(21, 76, 4, 'fur');
      p.arc(13, 76, 4, 0.4, 2.7, 'grey');
      p.arc(21, 76, 4, 0.4, 2.7, 'grey');
      p.px(13, 74, 'fur');
      p.px(21, 74, 'fur');

      // The mallet swings from a bracket at the top right; the pet is drawn under it.
      const phase = (ms % 1800) / 1800;
      const lift = 0.5 + 0.5 * Math.cos(phase * Math.PI * 2);
      const headX = 52;
      const headY = 10 + 26 * (1 - lift);

      stage.drawPet({ scaleY: headY > 31 ? 0.94 : 1, gaze: [0, 1] });

      // Mortar in front of the lower body: a rim and a tapering body.
      p.rect(34, 66, 44, 14, 'brown');
      p.rect(32, 62, 48, 5, 'amber');
      p.rect(32, 62, 48, 1, 'ink');
      p.rect(35, 65, 42, 2, 'brown');
      p.rect(36, 78, 40, 2, 'ink');
      p.rect(40, 70, 32, 1, 'amber');
      p.rect(30, 70, 4, 4, 'brown');
      p.rect(78, 70, 4, 4, 'brown');
      p.px(34, 80, 'ink');
      p.px(79, 80, 'ink');

      // The mallet: a bracket, a handle, a banded head.
      p.rect(80, 0, 8, 10, 'brown');
      p.rect(80, 0, 8, 2, 'ink');
      p.disc(84, 7, 2, 'amber');
      p.px(84, 7, 'ink');
      p.line(84, 6, headX + 10, headY + 3, 'brown');
      p.line(85, 7, headX + 10, headY + 4, 'cream');
      p.rect(headX - 10, headY - 6, 20, 12, 'ink');
      p.rect(headX - 9, headY - 5, 18, 10, 'brown');
      p.rect(headX - 9, headY - 1, 18, 3, 'amber');
      p.px(headX - 7, headY - 5, 'cream');
      p.px(headX + 7, headY - 4, 'amber');

      // Speed lines on the way down.
      if (phase > 0.28 && phase < 0.52) {
        p.line(headX + 14, headY - 2, headX + 20, headY - 6, 'cream');
        p.line(headX + 14, headY + 4, headX + 20, headY + 2, 'cream');
        p.line(headX - 14, headY - 2, headX - 20, headY - 6, 'cream');
      }

      // Flour puffs at the moment of impact.
      const hitPhase = (ms % 1800) - 850;
      if (hitPhase > 0 && hitPhase < 360) {
        const t = hitPhase / 360;
        for (let i = 0; i < 8; i += 1) {
          const a = i * 0.9;
          p.px(Math.round(headX + Math.cos(a) * (6 + t * 11)), Math.round(headY + 8 + Math.sin(a) * (3 + t * 7)), 'cream');
        }
      }
      p.px(44, 60, 'cream');
      p.px(62, 59, 'cream');
    }
  },

  owl_hearth: {
    id: 'owl_hearth',
    pet: 'owl',
    name: '壁炉边',
    en: 'By the fire',
    line: '壁炉里的火忽明忽暗。猫头鹰缩在炉前打盹，羽毛被火光染暖。',
    props: '壁炉、柴火、火星、地毯',
    entry: '滑就位',
    entryNote: '猫头鹰挪到炉前',
    moods: [['傍晚', 2], ['深夜', 2.5], ['读完长文', 2]],
    nudge: '眼罩 ×2.5',
    tint: '#f0e7e3',
    draw(ctx, ms, stage) {
      const { p } = stage;

      // Rug in front of the hearth.
      p.ellipse(61, 82, 26, 5, 'cream');
      p.ellipse(61, 84, 26, 3, 'rose');

      // Chimney breast, bricks, mantel.
      p.rect(6, 8, 42, 24, 'brown');
      for (let y = 8; y < 32; y += 6) p.rect(6, y, 42, 1, 'grey');
      for (let y = 14; y < 32; y += 6) {
        for (let x = (y % 12 === 2 ? 14 : 8); x < 46; x += 12) p.rect(x, y - 6, 1, 6, 'grey');
      }
      p.rect(2, 33, 50, 5, 'cream');
      p.rect(2, 37, 50, 1, 'ink');
      p.rect(4, 38, 12, 43, 'brown');
      p.rect(38, 38, 12, 43, 'brown');
      for (let y = 40; y < 80; y += 6) {
        p.rect(4, y, 12, 1, 'grey');
        p.rect(38, y, 12, 1, 'grey');
      }
      p.rect(16, 46, 22, 32, 'ink');
      p.rect(18, 70, 18, 5, 'brown');
      p.rect(16, 78, 22, 2, 'grey');

      // Fire: three tongues, flickering.
      const flick = Math.sin(ms / 160);
      const tongues = [
        [21, 64 - Math.round(4 + flick * 2)],
        [27, 60 - Math.round(6 + Math.sin(ms / 140) * 3)],
        [33, 65 - Math.round(3 + Math.sin(ms / 180) * 2)]
      ];
      p.rect(19, 64, 16, 6, 'pumpkin');
      tongues.forEach(([x, top], i) => {
        p.rect(x, top, 3, 70 - top, i === 1 ? 'gold' : 'pumpkin');
        p.px(x + 1, top + 2, 'cream');
      });
      p.rect(19, 69, 16, 2, 'gold');
      const glow = 0.5 + 0.5 * Math.sin(ms / 260);
      p.px(14, 56 + Math.round(glow * 4), 'gold');
      p.px(40, 60 - Math.round(glow * 3), 'gold');

      // A little jug on the mantel.
      p.rect(8, 25, 8, 8, 'cream');
      p.rect(10, 23, 4, 2, 'cream');
      p.rect(16, 27, 2, 3, 'cream');

      // The owl sits at the right, dozing; the sleep effect draws its Zzz.
      stage.drawPet({ action: 'sleep', x: 9, gaze: [-2, 0], effect: false });

      // Zzz over the owl's head, drifting up.
      const zz = Math.round(Math.sin(ms / 900) * 2);
      p.rows(66, 24 - zz, ['.###', '..#.', '.#..', '###'], { '#': 'grey' }, 1);
      p.rows(72, 18 - zz, ['.##', '.#.', '##.'], { '#': 'fur' }, 1);
      p.rows(77, 12 - zz, ['.#', '#.'], { '#': 'cream' }, 1);

      // Embers rising in front, warmth on the rug.
      for (let i = 0; i < 4; i += 1) {
        const t = ((ms / 2200 + i / 4) % 1);
        p.px(24 + i * 4 + Math.round(Math.sin(t * 7 + i) * 3), Math.round(72 - t * 22), i % 2 ? 'gold' : 'cream');
      }
      p.line(46, 79, 62, 79, 'gold');
      p.px(70, 62 + Math.round(Math.sin(ms / 600) * 2), 'gold');
    }
  },

  owl_letter: {
    id: 'owl_letter',
    pet: 'owl',
    name: '送信',
    en: 'Mail owl',
    line: '信箱里露出一封信，猫头鹰叼着另一封，站在旁边等出发。',
    props: '信箱、两封信',
    entry: '跳进来',
    entryNote: '落在信箱旁',
    moods: [['清晨', 2], ['午后', 1.5]],
    nudge: '书袋 ×2.5',
    tint: '#e8eef0',
    draw(ctx, ms, stage) {
      const { p } = stage;
      const bob = Math.round(Math.sin(ms / 650));

      // A letter dropped on the ground, left of the owl.
      envelope(p, 2, 72);
      p.px(14, 75, 'gold');

      // Mailbox: post, box, slot, flag.
      p.rect(76, 56, 4, 25, 'brown');
      p.rect(76, 56, 1, 25, 'ink');
      p.rect(66, 42, 22, 16, 'rose');
      p.rect(66, 42, 22, 1, 'ink');
      p.rect(66, 57, 22, 1, 'ink');
      p.rect(71, 47, 12, 2, 'ink');
      p.rect(70, 37, 12, 6, 'cream');
      p.px(78, 38, 'sky');
      p.line(71, 42, 75, 39, 'ink');
      p.line(75, 39, 80, 42, 'ink');
      p.rect(84, 42, 2, 9, 'gold');
      const raise = Math.sin(ms / 900) > 0.6 ? 2 : 0;
      p.rect(84, 42 - raise, 3, 2, 'gold');

      stage.drawPet({ x: -9, gaze: [2, 0] });

      // The envelope in the beak, below the eyes and tilted with the flutter.
      ctx.save();
      ctx.translate(43, 56);
      ctx.rotate(-0.12 + Math.sin(ms / 800) * 0.03);
      ctx.translate(-43, -56);
      envelope(p, 37, 52 + bob, 'sky');
      p.px(43, 53 + bob, 'rose');
      ctx.restore();
      p.px(46, 51, 'gold');
      p.px(47, 51, 'amber');

    }
  }
};

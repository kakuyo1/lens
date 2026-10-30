/// Liquid glass for the README scenes. Every element marked data-glass gets the same refraction the
/// bubble card has in ui-prototypes/bubble.html: blur first, then the displacement filter, with a map
/// drawn from the element's own rounded-rect mask and rebuilt whenever its size changes. The bubble
/// keeps its own copy of this; the two stay in step by hand.
(() => {
  const NS = 'http://www.w3.org/2000/svg';
  const make = (tag, attrs) => {
    const node = document.createElementNS(NS, tag);
    for (const [key, value] of Object.entries(attrs)) node.setAttribute(key, value);
    return node;
  };
  const probe = document.createElement('div');
  probe.style.backdropFilter = 'url(#probe)';
  if (probe.style.backdropFilter === '') return; // no url() backdrop filters: the CSS blur stays

  const host = make('svg', { width: 0, height: 0, 'aria-hidden': 'true', style: 'position:absolute' });
  const defs = make('defs', {});
  host.append(defs);
  document.body.prepend(host);
  // The card is see-through: the page shows through blurred and bent, with no milky fill over it.
  const style = document.createElement('style');
  style.textContent = '[data-glass]{background:transparent!important;background-image:none!important}';
  document.head.append(style);

  /// Displacement map: R and G encode the inward gradient of the blurred mask, 128 is no shift.
  /// S is the blur radius of the mask; a larger S spreads the bend wider.
  function liquidMap(W, H, R, S) {
    const mask = document.createElement('canvas');
    mask.width = W; mask.height = H;
    const mctx = mask.getContext('2d');
    mctx.filter = `blur(${S}px)`;
    mctx.fillStyle = '#fff';
    mctx.beginPath(); mctx.roundRect(0, 0, W, H, R); mctx.fill();
    const m = mctx.getImageData(0, 0, W, H).data;
    const at = (x, y) => m[(Math.min(Math.max(y, 0), H - 1) * W + Math.min(Math.max(x, 0), W - 1)) * 4 + 3] / 255;
    const gx = new Float32Array(W * H), gy = new Float32Array(W * H);
    let max = 0;
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const i = y * W + x;
      gx[i] = (at(x + 1, y) - at(x - 1, y)) / 2;
      gy[i] = (at(x, y + 1) - at(x, y - 1)) / 2;
      max = Math.max(max, Math.hypot(gx[i], gy[i]));
    }
    const canvas = document.createElement('canvas');
    canvas.width = W; canvas.height = H;
    const ctx = canvas.getContext('2d'), img = ctx.createImageData(W, H);
    for (let i = 0; i < W * H; i++) img.data.set([128 + 127 * gx[i] / max, 128 + 127 * gy[i] / max, 128, 255], i * 4);
    ctx.putImageData(img, 0, 0);
    return canvas.toDataURL();
  }

  let count = 0;
  /// Gives every data-glass element under root its filter once; a scene that builds markup late calls it again.
  window.liquidGlass = (root = document) => {
    for (const el of root.querySelectorAll('[data-glass]:not([data-glass-id])')) glassify(el);
  };
  function glassify(el) {
    const id = `lg-glass-${++count}`;
    el.dataset.glassId = id;
    const map = make('feImage', { preserveAspectRatio: 'none', result: 'map' });
    const filter = make('filter', { id, filterUnits: 'userSpaceOnUse', 'color-interpolation-filters': 'sRGB' });
    filter.append(map, make('feDisplacementMap', { in: 'SourceGraphic', in2: 'map', scale: 20, xChannelSelector: 'R', yChannelSelector: 'G' }));
    defs.append(filter);
    new ResizeObserver(() => {
      const W = el.offsetWidth, H = el.offsetHeight;
      const R = Math.min(parseFloat(getComputedStyle(el).borderTopLeftRadius), W / 2, H / 2);
      const S = Math.min(13, Math.min(W, H) / 4);
      for (const node of [filter, map]) { node.setAttribute('width', W); node.setAttribute('height', H); }
      map.setAttribute('href', liquidMap(W, H, R, S));
      el.style.backdropFilter = `blur(6px) url(#${id})`;
    }).observe(el);
  }
  window.liquidGlass();
})();

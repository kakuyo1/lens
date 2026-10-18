/**
 * Proximity awareness for one pet. Pure: it reads a distance, a delta and whether the pet may act, and
 * reports the two moments worth turning into an action. Every threshold is in canvas pixels, so a pet
 * mounted at another scale keeps the same personal space.
 *
 * The outer ring only makes the eyes follow. The near ring, held for a moment, is a greeting; held for
 * longer, it earns the fuller reaction. Both happen at most once per approach, and a cooldown keeps a
 * pointer resting nearby from replaying them.
 */

/** Canvas pixels from the pet's ink to each ring. */
export const RINGS = { gaze: 26, near: 16 };
/** Extra pixels before a ring lets go, so a pointer on a boundary does not flicker between zones. */
export const EXIT_MARGIN = 4;

const DEFAULTS = {
  ...RINGS,
  exitMargin: EXIT_MARGIN,
  noticeDelayMs: 350,
  dwellDelayMs: 2600,
  cooldownMs: 8000,
  rearmMs: 800,
};

/**
 * @typedef {object} HoverUpdate
 * @property {'outside'|'gaze'|'near'} zone
 * @property {boolean} notice True on the one update that starts a greeting.
 * @property {boolean} dwell True on the one update that starts the fuller reaction.
 */

export class HoverSense {
  /** @param {{gaze?: number, near?: number, exitMargin?: number, noticeDelayMs?: number, dwellDelayMs?: number,
   *          cooldownMs?: number, rearmMs?: number}} [options] */
  constructor(options = {}) {
    this.options = { ...DEFAULTS, ...options };
    this.reset();
  }

  reset() {
    this.nowMs = 0;
    this.zone = 'outside';
    this.outsideSince = 0;
    this.nearSince = null;
    this.approachNoticed = false;
    this.approachDwelled = false;
    this.approachQuiet = false;
    this.lastNoticeAt = -Infinity;
  }

  /** True while the last greeting is still cooling down. The demo shows it; the state machine uses it. */
  get cooling() {
    return this.nowMs - this.lastNoticeAt < this.options.cooldownMs;
  }

  /** When the pointer is close enough for the eyes to follow. */
  get watching() {
    return this.zone !== 'outside';
  }

  /** Whether this approach has already been greeted. */
  get greeted() {
    return this.approachNoticed;
  }

  /** Whether this approach has already earned the fuller reaction. */
  get finished() {
    return this.approachDwelled;
  }

  /** Whether the cooldown turned this approach into a quiet hello. */
  get quiet() {
    return this.approachQuiet;
  }

  /**
   * @param {number} deltaMs Time since the last update.
   * @param {number} distance Canvas pixels from the pointer to the pet's ink; zero inside it.
   * @param {{idle?: boolean, notice?: boolean, dwell?: boolean, cooldown?: boolean}} [allows]
   *        `idle` gates events, not eyes: a pet in the middle of another action still looks at the pointer.
   * @returns {HoverUpdate}
   */
  update(deltaMs, distance, { idle = true, notice = true, dwell = true, cooldown = true } = {}) {
    const o = this.options;
    this.nowMs += Math.max(0, deltaMs);
    const previous = this.zone;
    const next = this.zoneAt(distance, previous);

    if (next === 'outside') {
      if (previous !== 'outside') this.outsideSince = this.nowMs;
      this.nearSince = null;
      this.zone = next;
      if (this.nowMs - this.outsideSince >= o.rearmMs) this.clearApproach();
      return { zone: next, notice: false, dwell: false };
    }

    this.zone = next;
    if (next === 'near') {
      if (this.nearSince === null) this.nearSince = this.nowMs;
    } else {
      this.nearSince = null;
    }

    if (this.nearSince !== null) {
      const greetedAt = this.nearSince + o.noticeDelayMs;
      const dwellAt = this.nearSince + o.dwellDelayMs;

      if (idle && !this.approachNoticed && this.nowMs >= greetedAt) {
        if (!cooldown || !this.cooling) {
          this.lastNoticeAt = this.nowMs;
          this.approachNoticed = true;
          if (notice) return { zone: next, notice: true, dwell: false };
        } else {
          // A quiet hello: the pet has noticed, but it will not perform or stay for the fuller reaction.
          this.approachNoticed = true;
          this.approachQuiet = true;
        }
      }

      if (idle && dwell && this.approachNoticed && !this.approachDwelled && !this.approachQuiet &&
          this.nowMs >= dwellAt) {
        this.approachDwelled = true;
        return { zone: next, notice: false, dwell: true };
      }
    }

    return { zone: next, notice: false, dwell: false };
  }

  /** A pointer that has left every ring for the rearm delay starts a fresh approach. */
  clearApproach() {
    this.approachNoticed = false;
    this.approachDwelled = false;
    this.approachQuiet = false;
  }

  /**
   * @param {number} distance Canvas pixels to the ink.
   * @param {'outside'|'gaze'|'near'} previous The zone already held, for the exit margin.
   * @returns {'outside'|'gaze'|'near'}
   */
  zoneAt(distance, previous) {
    const o = this.options;
    const held = (threshold, inside) => distance <= threshold + (inside ? o.exitMargin : 0);
    if (held(o.near, previous === 'near')) return 'near';
    if (held(o.gaze, previous !== 'outside')) return 'gaze';
    return 'outside';
  }
}

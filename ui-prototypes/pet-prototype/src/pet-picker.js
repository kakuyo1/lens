/**
 * The settings page's picker rules, kept pure so the node tests read them: which worn accessories survive a switch
 * of pet, and the order of a pet's action pills. No DOM, no window.
 */

/**
 * Keeps the worn accessories that the new pet's catalogue still has. Each one takes its slot from that catalogue.
 * @param {Map<string, string>} worn Slot to accessory id, as the page holds it.
 * @param {Array<{id: string, slot: string}>} accessories The new pet's accessories.
 * @returns {Map<string, string>}
 */
export function keepWorn(worn, accessories) {
  const kept = new Map();
  for (const id of worn.values()) {
    const accessory = accessories.find(a => a.id === id);
    if (accessory) kept.set(accessory.slot, id);
  }
  return kept;
}

/**
 * One pill per action except blink: the role actions in role order, then the idle-pool extras, then anything else
 * the animations name. A role whose action is absent (stretch may be) gets no pill.
 * @param {{roles: Record<string, string>, idlePool: string[], actions: Record<string, object>}} pet Assembled by pet-data.js.
 * @returns {string[]}
 */
export function actionOrder(pet) {
  const names = new Set([...Object.values(pet.roles), ...pet.idlePool, ...Object.keys(pet.actions)]);
  return [...names].filter(name => pet.actions[name]);
}

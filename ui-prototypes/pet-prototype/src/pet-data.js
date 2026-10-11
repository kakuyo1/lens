/**
 * Joins one pet's four data files, data/pet/<id>/pet.json, animations.json, anchors.json and accessories.json, into the
 * document the engine, renderer and accessory manager read. Pure: the caller reads the files (fetch on the page,
 * fs in the embed build).
 */

/**
 * @param {{pet: object, animations: object, anchors: object, accessories: object}} files The parsed files.
 * @returns {object} `actions` holds only the playable actions, each with its per-frame anchors. The `blink` entry is
 *          not an action: it becomes `blink`, the expression sequence of one blink. `roles`, `idlePool` and
 *          `accessories` are taken as the files give them.
 */
export function assemblePet({ pet, animations, anchors, accessories }) {
  const { blink, ...playable } = animations.actions;
  const actions = Object.fromEntries(
    Object.entries(playable).map(([name, def]) => [name, { ...def, anchors: anchors.actions[name] }])
  );
  return {
    ...pet,
    blink: blink.expressions,
    roles: animations.roles,
    idlePool: animations.idlePool,
    actions,
    accessories: accessories.accessories
  };
}

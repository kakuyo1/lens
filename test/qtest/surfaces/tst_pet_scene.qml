import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The desktop pet's scene stage (ADR 0049, docs/QML.md section 10): the props go behind and in front of the
 * pet, the pet is moved onto the seat by the scene's own shift, and the whole stage is aligned to the pet's
 * measured feet. The machine's own scene roll is random, so the case previews a scene by name; the frame
 * clock is not run, which keeps the frame and its shift fixed.
 */
Item {
    id: root
    width: 400
    height: 400

    /// The pet as the window shows it: the scene scales itself, so a snapshot has to grab a parent that
    /// carries the scaled bounds rather than the item's own 88 px.
    Item {
        id: frame
        width: Math.round(Pet.pose.canvas * Pet.pose.scale)
        height: width
        clip: true

        PetScene {
            id: scene
        }
    }

    TestCase {
        id: testCase
        name: "PetScene"
        when: windowShown

        function cleanup() {
            Pet.setPreviewing(false);
            for (const id of ["flat_cap", "spectacles", "bandana"])
                if (Pet.wornIn.head === id || Pet.wornIn.face === id || Pet.wornIn.body === id)
                    Pet.toggleAccessory(id);
        }

        /// A worn piece is a sprite layer above the body, with the slot's z: body 3, face 4, head 5.
        function test_wornAccessoriesDrawInSlotOrder() {
            for (const id of ["flat_cap", "spectacles", "bandana"]) Pet.toggleAccessory(id);
            tryVerify(() => Pet.pose.worn.length === 3, 2000, "three worn pieces did not reach the scene");

            const accessoryLayers = () => Util.findAll(scene, function (o) {
                return o.sheet && o.sheet.indexOf("/sprites/accessories/") >= 0;
            });
            tryVerify(() => accessoryLayers().length === 3, 2000, "the scene did not draw three accessory layers");
            const layers = accessoryLayers();
            const zOf = suffix => layers.find(o => o.sheet.endsWith(suffix)).z;
            compare(zOf("/flat_cap.png"), 5);
            compare(zOf("/spectacles.png"), 4);
            compare(zOf("/bandana.png"), 3);
        }

        function test_aSceneDrawsItsPropsOnBothSidesOfThePet() {
            Pet.preview("swing");
            tryCompare(Pet.pose, "scene", "swing");

            const layers = Util.findAll(scene, function (o) { return o.objectName === "sceneBack"
                                                                        || o.objectName === "sceneFront"; });
            compare(layers.length, 2, "PetScene did not draw both scene layers");
            const back = layers.find(function (o) { return o.objectName === "sceneBack"; });
            const front = layers.find(function (o) { return o.objectName === "sceneFront"; });
            verify(back.sheet.indexOf("/scenes/swing/back.png") >= 0, "the back layer is not the swing's");
            verify(front.sheet.indexOf("/scenes/swing/front.png") >= 0, "the front layer is not the swing's");
            compare(back.frame, Pet.pose.sceneFrame);
            compare(front.frame, Pet.pose.sceneFrame);
            verify(back.z < front.z, "the scene layers do not stack back, pet, front");

            const rig = Util.findAll(scene, function (o) { return o.objectName === "petRig"; })[0];
            verify(rig, "PetScene has no pet rig");
            verify(rig.z > back.z && rig.z < front.z, "the pet is not between the scene's layers");
            compare(rig.x, Pet.pose.sceneShift.x);
            compare(rig.y, Pet.pose.sceneShift.y);
            compare(rig.transform[0].xScale, Pet.pose.sceneScale);
            compare(rig.transform[0].origin.x, Pet.pose.foot.x);
            compare(rig.transform[0].origin.y, Pet.pose.foot.y);
        }

        function test_theStageShiftsToThePetsMeasuredFeet() {
            // The fixture's pet is the dog: its ink centre is 7.5 px left of the catalogue's foot, its feet a cell lower.
            compare(Pet.pose.foot.x, 51);
            compare(Pet.pose.foot.y, 84);

            Pet.preview("swing");
            tryCompare(Pet.pose, "scene", "swing");
            const stage = Util.findAll(scene, function (o) { return o.objectName === "sceneStage"; })[0];
            verify(stage, "PetScene has no stage");
            compare(stage.x, -7.5);
            compare(stage.y, 3);
        }

        /// The QML stage as it renders, one shot per scene (TEST.md section 5). Offscreen does not rasterise
        /// MultiEffect, which the pet does not use.
        function test_captureEveryScene() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            for (const id of ["swing", "skate", "dog_frisbee", "dog_burying"]) {
                Pet.preview(id);
                tryCompare(Pet.pose, "scene", id);
                verify(Util.saveSnapshot(testCase, frame, lensQaSnapshotDir, "pet-scene-" + id));
            }

            // Anger is not a scene: the props go away and the face is the expression sheet's last cell.
            Pet.preview("angry");
            tryCompare(Pet.pose, "scene", "");
            compare(Pet.pose.expression, 9);
            verify(Util.saveSnapshot(testCase, frame, lensQaSnapshotDir, "pet-action-angry"));
        }

        function test_leavingTheSceneTakesThePropsAndTheShiftAway() {
            Pet.preview("swing");
            tryCompare(Pet.pose, "scene", "swing");
            Pet.setPreviewing(false);
            tryCompare(Pet.pose, "scene", "");

            const layers = Util.findAll(scene, function (o) { return o.objectName === "sceneBack"
                                                                        || o.objectName === "sceneFront"; });
            for (const layer of layers)
                compare(layer.visible, false, "a scene layer stayed on after the scene ended");
            const rig = Util.findAll(scene, function (o) { return o.objectName === "petRig"; })[0];
            compare(rig.x, 0);
            compare(rig.y, 0);
        }

        /// The two readable moments of a hop, for the hand-checked snapshot set: the idle body in front of the
        /// props, and the scene body just after the landing.
        function test_captureSceneEntry() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the scene entry frames");

            Pet.host.setMotionReduced(false);
            Pet.picker.setPet("cat");
            tryCompare(Pet, "pet", "cat");
            Pet.setScene("bath");
            tryCompare(Pet.pose, "scene", "bath");
            Pet.setPreviewing(true);
            wait(300);
            waitForRendering(frame);
            verify(Util.saveSnapshot(testCase, frame, lensQaSnapshotDir, "pet-entry-bath-air"));
            wait(400);
            waitForRendering(frame);
            verify(Util.saveSnapshot(testCase, frame, lensQaSnapshotDir, "pet-entry-bath-land"));

            // The scene belongs to the machine; the switch back to the default pet ends it and restores the fixture.
            Pet.picker.setPet("dog");
            tryCompare(Pet, "pet", "dog");
            Pet.setPreviewing(false);
        }

        /// ADR 0101: a hop starts with the props transparent and the idle body in front of the front layer, then
        /// lands on the scene pose. A real scene is the machine's, so the case clicks it out at the end.
        function test_aHopEnterHoldsTheIdlePoseAndFlipsLayers() {
            Pet.host.setMotionReduced(false);
            if (Pet.picker.pet !== "dog") {
                Pet.picker.setPet("dog");
                tryCompare(Pet, "pet", "dog");
            }
            Pet.setScene("swing");
            tryCompare(Pet.pose, "scene", "swing");
            tryVerify(() => Pet.pose.entry.active === true, 1000, "the scene did not start an entry");

            const back = Util.findAll(scene, function (o) { return o.objectName === "sceneBack"; })[0];
            const front = Util.findAll(scene, function (o) { return o.objectName === "sceneFront"; })[0];
            const rig = Util.findAll(scene, function (o) { return o.objectName === "petRig"; })[0];
            const entryRig = Util.findAll(scene, function (o) { return o.objectName === "entryPoseRig"; })[0];
            compare(back.opacity, Pet.pose.entry.propAlpha);
            compare(back.opacity, 0);
            verify(rig.z > front.z, "the entering pet is not in front of the props");
            compare(rig.opacity, 1);
            verify(Pet.pose.sheets.body.indexOf("/sprites/body/idle.png") >= 0, "a hop did not hold the idle body");
            verify(!entryRig.visible, "a hop should not draw the settle overlay");

            Pet.setPreviewing(true);
            tryVerify(() => Pet.pose.entry.active === false, 3000, "the entry never landed");
            compare(back.opacity, 1);
            compare(rig.z, 10);
            verify(Pet.pose.sheets.body.indexOf("/sprites/body/swing.png") >= 0, "the landing did not switch to the scene body");
            verify(Pet.pose.entry.inFront === false);

            // A second scene with a landing effect: the effect item follows the controller's impact map.
            Pet.setScene("dog_burying");
            tryVerify(() => Pet.pose.entry.active === true, 1000, "the second scene did not start an entry");
            tryVerify(() => Pet.pose.entry.impact && Pet.pose.entry.impact.kind === "dust", 3000,
                       "the dust landing never reached the scene");
            const effect = Util.findAll(scene, function (o) { return o.objectName === "sceneImpact"; })[0];
            verify(effect, "PetScene has no landing effect item");
            verify(effect.on, "the landing effect item is not on");

            // A real scene belongs to the machine and would outlive this case; a click is the product's own way out.
            Pet.click();
            tryVerify(() => Pet.pose.scene === "", 2000, "the cleanup click did not end the test scene");
            tryVerify(() => Pet.pose.action === "idle", 4000, "the cleanup click did not settle");
            Pet.setPreviewing(false);
        }
    }
}

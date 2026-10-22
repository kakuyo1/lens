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
        width: Math.round(Pet.canvas * Pet.scale)
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
        }

        function test_aSceneDrawsItsPropsOnBothSidesOfThePet() {
            Pet.preview("swing");
            tryCompare(Pet, "scene", "swing");

            const layers = Util.findAll(scene, function (o) { return o.objectName === "sceneBack"
                                                                        || o.objectName === "sceneFront"; });
            compare(layers.length, 2, "PetScene did not draw both scene layers");
            const back = layers.find(function (o) { return o.objectName === "sceneBack"; });
            const front = layers.find(function (o) { return o.objectName === "sceneFront"; });
            verify(back.sheet.indexOf("/scenes/swing/back.png") >= 0, "the back layer is not the swing's");
            verify(front.sheet.indexOf("/scenes/swing/front.png") >= 0, "the front layer is not the swing's");
            compare(back.frame, Pet.sceneFrame);
            compare(front.frame, Pet.sceneFrame);
            verify(back.z < front.z, "the scene layers do not stack back, pet, front");

            const rig = Util.findAll(scene, function (o) { return o.objectName === "petRig"; })[0];
            verify(rig, "PetScene has no pet rig");
            verify(rig.z > back.z && rig.z < front.z, "the pet is not between the scene's layers");
            compare(rig.x, Pet.sceneShift.x);
            compare(rig.y, Pet.sceneShift.y);
            compare(rig.transform[0].xScale, Pet.sceneScale);
            compare(rig.transform[0].origin.x, Pet.foot.x);
            compare(rig.transform[0].origin.y, Pet.foot.y);
        }

        function test_theStageShiftsToThePetsMeasuredFeet() {
            // The fixture's pet is the dog: its ink centre is 7.5 px left of the catalogue's foot, its feet a cell lower.
            compare(Pet.foot.x, 51);
            compare(Pet.foot.y, 84);

            Pet.preview("swing");
            tryCompare(Pet, "scene", "swing");
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
                tryCompare(Pet, "scene", id);
                verify(Util.saveSnapshot(testCase, frame, lensQaSnapshotDir, "pet-scene-" + id));
            }

            // Anger is not a scene: the props go away and the face is the expression sheet's last cell.
            Pet.preview("angry");
            tryCompare(Pet, "scene", "");
            compare(Pet.expression, 9);
            verify(Util.saveSnapshot(testCase, frame, lensQaSnapshotDir, "pet-action-angry"));
        }

        function test_leavingTheSceneTakesThePropsAndTheShiftAway() {
            Pet.preview("swing");
            tryCompare(Pet, "scene", "swing");
            Pet.setPreviewing(false);
            tryCompare(Pet, "scene", "");

            const layers = Util.findAll(scene, function (o) { return o.objectName === "sceneBack"
                                                                        || o.objectName === "sceneFront"; });
            for (const layer of layers)
                compare(layer.visible, false, "a scene layer stayed on after the scene ended");
            const rig = Util.findAll(scene, function (o) { return o.objectName === "petRig"; })[0];
            compare(rig.x, 0);
            compare(rig.y, 0);
        }
    }
}

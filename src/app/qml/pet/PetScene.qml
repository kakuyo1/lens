import QtQuick

/**
 * The pet as layers, at the frame the controller chose. Body, expression and effect take their z from pet.json;
 * accessories take theirs from accessories.json. The scene scales by Pet.scale, a real magnification the size slider
 * sets: the sprite is drawn nearest-neighbour, so a fractional scale leaves its pixel widths uneven.
 *
 * While the pet is in a scene (ADR 0049) the frame also carries the props: a static back sheet, the pet moved onto
 * the seat or the deck, and a static front sheet that hides the pet's lower body. The stage moves as a whole by the
 * difference between this pet's feet and the catalogue's, so all four pets share one set of props.
 */
Item {
    id: root
    width: Pet.canvas
    height: Pet.canvas
    scale: Pet.scale
    transformOrigin: Item.TopLeft

    readonly property bool inScene: Pet.scene !== ""
    readonly property var sceneLayers: Pet.sceneLayers
    /// The hand that holds the sign, "left" or "right": the desktop pet's own, unless a stage chooses another.
    property string handSide: Pet.handSide

    Item {
        id: stage
        objectName: "sceneStage"
        x: root.inScene ? Pet.sceneAlign.x : 0
        y: root.inScene ? Pet.sceneAlign.y : 0
        width: root.width
        height: root.height

        SpriteLayer {
            objectName: "sceneBack"
            z: root.sceneLayers.back !== undefined ? root.sceneLayers.back : 0
            visible: root.inScene
            sheet: root.inScene ? Pet.sceneSheets.back : ""
            frame: Pet.sceneFrame
            at: Qt.point(0, 0)
        }

        // The pet and its accessories, placed by the scene and scaled about its own feet. The frame never turns it.
        Item {
            id: petRig
            objectName: "petRig"
            z: 10
            x: root.inScene ? Pet.sceneShift.x : 0
            y: root.inScene ? Pet.sceneShift.y : 0
            width: root.width
            height: root.height
            transform: Scale {
                origin.x: Pet.foot.x
                origin.y: Pet.foot.y
                xScale: root.inScene ? Pet.sceneScale : 1
                yScale: root.inScene ? Pet.sceneScale : 1
            }

            SpriteLayer {
                z: Pet.layers.body
                sheet: Pet.sheets.body
                frame: Pet.frameIndex
                at: Pet.anchors.body
            }

            SpriteLayer {
                z: Pet.layers.expression
                sheet: Pet.sheets.expression
                frame: Pet.expression
                // The eyes follow a nearby pointer. The offset is one canvas pixel on the dominant axis, and zero
                // whenever the pet is not idle, so no other action's face is moved.
                at: Qt.point(Pet.anchors.face.x + Pet.gaze.x, Pet.anchors.face.y + Pet.gaze.y)
            }

            SpriteLayer {
                z: Pet.layers.effect
                sheet: root.handSide === "left" && Pet.sheets.effectLeft ? Pet.sheets.effectLeft : Pet.sheets.effect
                frame: Pet.frameIndex
                at: Qt.point(0, 0)
            }

            Repeater {
                model: Pet.wornAccessories

                SpriteLayer {
                    required property var modelData

                    z: modelData.zIndex
                    sheet: modelData.asset
                    frame: 0
                    at: Qt.point(modelData.x, modelData.y)
                }
            }
        }

        SpriteLayer {
            objectName: "sceneFront"
            z: root.sceneLayers.front !== undefined ? root.sceneLayers.front : 0
            visible: root.inScene
            sheet: root.inScene ? Pet.sceneSheets.front : ""
            frame: Pet.sceneFrame
            at: Qt.point(0, 0)
        }
    }
}

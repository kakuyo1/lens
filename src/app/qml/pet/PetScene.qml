import QtQuick

/**
 * The pet as layers, at the frame the controller chose. Body, expression and effect take their z from pet.json;
 * accessories take theirs from accessories.json. The scene scales by Pet.scale, a real magnification the size slider
 * sets: the sprite is drawn nearest-neighbour, so a fractional scale leaves its pixel widths uneven.
 *
 * While the pet is in a scene (ADR 0049, 0080) the frame also carries the props: a static back sheet, the pet moved
 * onto the seat or the deck, and a static front sheet that hides the pet's lower body. The stage moves as a whole by
 * the difference between this pet's feet and the catalogue's, so a shared prop rig fits every pet that names it.
 */
Item {
    id: root
    width: Pet.canvas
    height: Pet.canvas
    scale: Pet.scale
    transformOrigin: Item.TopLeft

    readonly property bool inScene: Pet.scene !== ""
    readonly property var sceneLayers: Pet.sceneLayers
    /// The entry in progress (ADR 0101). It holds the pose and transform separately from the scene loop, and a
    /// landing impact may outlive the entry itself.
    readonly property var entry: Pet.sceneEntry
    readonly property bool entryActive: entry !== undefined && entry.active === true
    readonly property var entrySceneSheets: entryActive && entry.sceneSheets ? entry.sceneSheets : ({})
    readonly property var entrySceneAnchors: entryActive && entry.sceneAnchors ? entry.sceneAnchors : ({})
    readonly property real propAlpha: entryActive ? entry.propAlpha : 1
    readonly property real propOffset: entryActive ? entry.propOffset : 0
    readonly property bool entryInFront: entryActive && entry.inFront === true
    readonly property var impact: entry !== undefined && entry.impact !== undefined ? entry.impact : ({})

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
            opacity: root.propAlpha
            sheet: root.inScene ? Pet.sceneSheets.back : ""
            frame: Pet.sceneFrame
            at: Qt.point(0, root.inScene ? root.propOffset : 0)
        }

        // The pet and its accessories, placed by the scene and scaled about its own feet. The frame never turns it.
        Item {
            id: petRig
            objectName: "petRig"
            z: root.entryInFront ? (root.sceneLayers.front !== undefined ? root.sceneLayers.front + 1 : 21) : 10
            opacity: root.entryActive ? root.entry.idleAlpha : 1
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
                sheet: Pet.sheets.effect
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

        // A settle crossfades the idle rig out while the scene's first frame folds in; both ride the same transform.
        Item {
            id: entryPoseRig
            objectName: "entryPoseRig"
            z: petRig.z
            visible: root.inScene && root.entryActive && root.entry.sceneAlpha > 0
            opacity: visible ? root.entry.sceneAlpha : 0
            x: petRig.x
            y: petRig.y
            width: root.width
            height: root.height
            transform: Scale {
                origin.x: Pet.foot.x
                origin.y: Pet.foot.y
                xScale: root.inScene ? Pet.sceneScale : 1
                yScale: root.inScene ? Pet.sceneScale : 1
            }

            readonly property point bodyAt: root.entrySceneAnchors.body !== undefined ? root.entrySceneAnchors.body : Qt.point(0, 0)
            readonly property point faceAt: root.entrySceneAnchors.face !== undefined ? root.entrySceneAnchors.face : Qt.point(0, 0)

            SpriteLayer {
                z: Pet.layers.body
                sheet: root.entrySceneSheets.body !== undefined ? root.entrySceneSheets.body : ""
                frame: 0
                at: entryPoseRig.bodyAt
            }

            SpriteLayer {
                z: Pet.layers.expression
                sheet: root.entrySceneSheets.expression !== undefined ? root.entrySceneSheets.expression : ""
                frame: root.entryActive ? root.entry.sceneExpression : 0
                at: entryPoseRig.faceAt
            }

            SpriteLayer {
                z: Pet.layers.effect
                sheet: root.entrySceneSheets.effect !== undefined ? root.entrySceneSheets.effect : ""
                frame: 0
                at: Qt.point(0, 0)
            }
        }

        SpriteLayer {
            objectName: "sceneFront"
            z: root.sceneLayers.front !== undefined ? root.sceneLayers.front : 0
            visible: root.inScene
            opacity: root.propAlpha
            sheet: root.inScene ? Pet.sceneSheets.front : ""
            frame: Pet.sceneFrame
            at: Qt.point(0, root.inScene ? root.propOffset : 0)
        }

        SceneEntryEffect {
            objectName: "sceneImpact"
            anchors.fill: parent
            z: root.sceneLayers.front !== undefined ? Math.max(root.sceneLayers.front + 2, 12) : 22
            impact: root.impact
        }
    }
}

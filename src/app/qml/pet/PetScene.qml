pragma ComponentBehavior: Bound

import QtQuick

/**
 * The pet as layers, at the frame the controller chose. Body, expression and effect take their z from pet.json;
 * accessories take theirs from accessories.json. The scene scales by the pose's scale, a real magnification the size
 * slider sets: the sprite is drawn nearest-neighbour, so a fractional scale leaves its pixel widths uneven.
 *
 * Everything the stage draws comes from Pet.pose, one value: the figure (canvas, scale, layer order, feet), the frame
 * (sheets, anchors, expression, worn pieces) and the scene it belongs to.
 *
 * While the pet is in a scene (ADR 0049, 0080) the frame also carries the props: a static back sheet, the pet moved
 * onto the seat or the deck, and a static front sheet that hides the pet's lower body. The stage moves as a whole by
 * the difference between this pet's feet and the catalogue's, so a shared prop rig fits every pet that names it.
 */
Item {
    id: root
    readonly property var pose: Pet.pose
    width: root.pose.canvas
    height: root.pose.canvas
    scale: root.pose.scale
    transformOrigin: Item.TopLeft

    readonly property bool inScene: root.pose.scene !== ""
    readonly property var sceneLayers: root.pose.sceneLayers
    /// The entry in progress (ADR 0101). It holds the pose and transform separately from the scene loop, and a
    /// landing impact may outlive the entry itself.
    readonly property var entry: root.pose.entry
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
        x: root.inScene ? root.pose.sceneAlign.x : 0
        y: root.inScene ? root.pose.sceneAlign.y : 0
        width: root.width
        height: root.height

        SpriteLayer {
            objectName: "sceneBack"
            z: root.sceneLayers.back !== undefined ? root.sceneLayers.back : 0
            visible: root.inScene
            opacity: root.propAlpha
            canvas: root.pose.canvas
            sheet: root.inScene ? root.pose.sceneSheets.back : ""
            frame: root.pose.sceneFrame
            at: Qt.point(0, root.inScene ? root.propOffset : 0)
        }

        // The pet and its accessories, placed by the scene and scaled about its own feet. The frame never turns it.
        Item {
            id: petRig
            objectName: "petRig"
            z: root.entryInFront ? (root.sceneLayers.front !== undefined ? root.sceneLayers.front + 1 : 21) : 10
            opacity: root.entryActive ? root.entry.idleAlpha : 1
            x: root.inScene ? root.pose.sceneShift.x : 0
            y: root.inScene ? root.pose.sceneShift.y : 0
            width: root.width
            height: root.height
            transform: Scale {
                origin.x: root.pose.foot.x
                origin.y: root.pose.foot.y
                xScale: root.inScene ? root.pose.sceneScale : 1
                yScale: root.inScene ? root.pose.sceneScale : 1
            }

            SpriteLayer {
                z: root.pose.layers.body
                canvas: root.pose.canvas
                sheet: root.pose.sheets.body
                frame: root.pose.frame
                at: root.pose.anchors.body
            }

            SpriteLayer {
                z: root.pose.layers.expression
                canvas: root.pose.canvas
                sheet: root.pose.sheets.expression
                frame: root.pose.expression
                // The eyes follow a nearby pointer. The offset is one canvas pixel on the dominant axis, and zero
                // whenever the pet is not idle, so no other action's face is moved.
                at: Qt.point(root.pose.anchors.face.x + root.pose.gaze.x, root.pose.anchors.face.y + root.pose.gaze.y)
            }

            SpriteLayer {
                z: root.pose.layers.effect
                canvas: root.pose.canvas
                sheet: root.pose.sheets.effect
                frame: root.pose.frame
                at: Qt.point(0, 0)
            }

            Repeater {
                model: root.pose.worn

                SpriteLayer {
                    required property var modelData

                    z: modelData.zIndex
                    canvas: root.pose.canvas
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
                origin.x: root.pose.foot.x
                origin.y: root.pose.foot.y
                xScale: root.inScene ? root.pose.sceneScale : 1
                yScale: root.inScene ? root.pose.sceneScale : 1
            }

            readonly property point bodyAt: root.entrySceneAnchors.body !== undefined ? root.entrySceneAnchors.body : Qt.point(0, 0)
            readonly property point faceAt: root.entrySceneAnchors.face !== undefined ? root.entrySceneAnchors.face : Qt.point(0, 0)

            SpriteLayer {
                z: root.pose.layers.body
                canvas: root.pose.canvas
                sheet: root.entrySceneSheets.body !== undefined ? root.entrySceneSheets.body : ""
                frame: 0
                at: entryPoseRig.bodyAt
            }

            SpriteLayer {
                z: root.pose.layers.expression
                canvas: root.pose.canvas
                sheet: root.entrySceneSheets.expression !== undefined ? root.entrySceneSheets.expression : ""
                frame: root.entryActive ? root.entry.sceneExpression : 0
                at: entryPoseRig.faceAt
            }

            SpriteLayer {
                z: root.pose.layers.effect
                canvas: root.pose.canvas
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
            canvas: root.pose.canvas
            sheet: root.inScene ? root.pose.sceneSheets.front : ""
            frame: root.pose.sceneFrame
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

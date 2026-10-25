pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * One sense's pictures, as the card draws them.
 *
 * Three states: a skeleton while the lookup runs, the thumbnails with their caption and attribution once it lands, and
 * one line offering the image search when nothing could be shown. A strip never shows an empty slot or a broken
 * picture -- what arrived is what is drawn.
 *
 * The strip is not glass and adds no colour of its own: the skeleton is drawn in the card's line token and the two text
 * lines in its text tokens, so it reads as part of the card. Only the corners of a photograph are rounded; the picture
 * itself stays rectangular, because a blurred photograph is unreadable.
 *
 * The state lives on the duty, keyed by the query: a strip whose own query is not the one the duty is answering for has
 * been collapsed or replaced, and draws nothing.
 */
Item {
    id: root

    /// This sense's `imageQuery`, which is also how this strip tells its own answer from another's.
    required property string query
    /// Whether the reader has this sense's strip open. Closed, nothing is drawn even when the duty
    /// still holds an answer for the query.
    property bool open: true
    /// The multiple the answer is drawn at, from the reader's font-size setting (UI-SPEC.md 4.4).
    property real scale: 1

    /// The table the strip takes its colours from: the one of the window it sits in.
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    readonly property var answer: Controller.pictureDuty.pictures
    /// `idle`, `loading`, `ready` or `failed` -- and `idle` for any query but this strip's own.
    readonly property string phase: root.query !== "" && root.answer.query === root.query ? root.answer.state : "idle"
    readonly property var items: root.phase === "ready" ? root.answer.items : []
    /// Which thumbnail the pointer or the keyboard is on; the caption follows it, first by default.
    property int focusedThumbnail: -1

    visible: root.open && root.phase !== "idle"
    implicitHeight: root.visible ? column.implicitHeight : 0
    width: parent ? parent.width : 0

    /// @return The fixed sentence for the duty's failure reason; a transport's own words never
    ///         reach the card.
    readonly property string failureText: {
        if (root.phase !== "failed")
            return "";
        if (root.answer.failure === "no-results")
            return qsTr("No pictures matched this sense.");
        if (root.answer.failure === "download")
            return qsTr("Pictures could not be downloaded. Try again.");
        return qsTr("Pictures unavailable. Check your connection.");
    }

    /// @return The item whose caption and attribution are drawn: the focused one, or the first.
    function currentItem() {
        if (root.items.length === 0)
            return null;
        return root.items[root.focusedThumbnail >= 0 && root.focusedThumbnail < root.items.length ? root.focusedThumbnail : 0];
    }

    Column {
        id: column
        width: parent.width
        spacing: 0

        // The skeleton: three 4:3 slots in the card's hairline token, static. Reduced motion has
        // nothing to take away here -- there is no spinner to stop.
        Row {
            id: skeleton
            visible: root.phase === "loading"
            width: parent.width
            topPadding: 6
            spacing: 4

            Repeater {
                model: 3
                delegate: Rectangle {
                    width: (skeleton.width - 2 * skeleton.spacing) / 3
                    height: Math.round(width * 0.75)
                    radius: 8
                    color: root.tokens.line2
                }
            }
        }

        Text {
            visible: root.phase === "loading"
            topPadding: 5
            bottomPadding: 6
            width: parent.width
            text: qsTr("Loading pictures…")
            color: root.tokens.muted
            font.pixelSize: Math.round(10.5 * root.scale)
            elide: Text.ElideRight
        }

        // One to three pictures, never a filler slot: two are two columns wide, one fills the strip.
        Row {
            id: strip
            visible: root.phase === "ready"
            width: parent.width
            topPadding: 6
            spacing: 4

            readonly property int  count: Math.max(1, root.items.length)
            readonly property real cell: strip.count === 1 ? strip.width : (strip.width - (strip.count - 1) * strip.spacing) / strip.count

            Repeater {
                model: root.items

                delegate: Item {
                    id: cell
                    required property int index
                    required property var modelData

                    width: strip.cell
                    height: Math.round(width * 0.75)
                    activeFocusOnTab: true
                    // The caption is the picture's accessible name: a screen reader gets the same
                    // words the card shows beside it.
                    Accessible.role: Accessible.Button
                    Accessible.name: cell.modelData.caption
                    Accessible.onPressAction: root.openPage(cell.modelData.page)

                    /// @brief Whether this is the thumbnail the caption is showing.
                    readonly property bool current: root.focusedThumbnail === cell.index

                    Image {
                        id: art
                        anchors.fill: parent
                        source: cell.modelData.image
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: false
                        // Rasterised at the size it is drawn at and at the screen's own scale, the
                        // way Icon.qml does it: the 480 px the source served is not a size to hand
                        // the renderer.
                        sourceSize: Qt.size(Math.max(1, Math.round(cell.width * Screen.devicePixelRatio)),
                                            Math.max(1, Math.round(cell.height * Screen.devicePixelRatio)))
                    }

                    // The corners, and only the corners. The shape is drawn apart from the picture
                    // and the two are combined by the effect, for the reason ShadowCard.qml gives:
                    // MultiEffect renders its source through an offscreen texture.
                    Rectangle {
                        id: mask
                        anchors.fill: parent
                        radius: 8
                        color: "white"
                        visible: false
                        layer.enabled: true
                    }

                    MultiEffect {
                        anchors.fill: parent
                        source: art
                        maskEnabled: true
                        maskSource: mask
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: "transparent"
                        border.width: 1
                        border.color: root.tokens.line2
                        // A hovered, current or keyboard-focused thumbnail is outlined: a clickable
                        // cell needs a focus state as much as a card's buttons do (DESIGN.md's accessibility rules).
                        visible: cell.current || thumbnailHover.hovered || cell.activeFocus
                    }

                    HoverHandler {
                        id: thumbnailHover
                        cursorShape: Qt.PointingHandCursor
                        onHoveredChanged: {
                            if (hovered)
                                root.focusedThumbnail = cell.index;
                            else if (root.focusedThumbnail === cell.index)
                                root.focusedThumbnail = -1;
                        }
                    }
                    TapHandler {
                        onTapped: root.openPage(cell.modelData.page)
                    }
                    Keys.onReturnPressed: root.openPage(cell.modelData.page)
                    Keys.onSpacePressed: root.openPage(cell.modelData.page)
                }
            }
        }

        Text {
            id: caption
            visible: root.phase === "ready"
            topPadding: 5
            width: parent.width
            text: {
                const item = root.currentItem();
                return item === null ? "" : item.caption;
            }
            color: root.tokens.muted
            font.pixelSize: Math.round(10.5 * root.scale)
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
        }

        // The attribution is permanent, not a tooltip: a licence that only appears on hover is a
        // licence the reader never saw. Tapping it opens the same source page.
        Text {
            id: attribution
            visible: root.phase === "ready"
            topPadding: 2
            bottomPadding: 6
            width: parent.width
            text: {
                const item = root.currentItem();
                if (item === null)
                    return "";
                return qsTr("Photo: %1 · %2 · %3").arg(item.author).arg(item.license).arg(item.source);
            }
            color: root.tokens.faint
            font.pixelSize: Math.round(9.5 * root.scale)
            elide: Text.ElideRight

            HoverHandler {
                id: attributionHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: {
                    const item = root.currentItem();
                    if (item === null)
                        return;
                    root.openPage(item.page);
                }
            }
        }

        Text {
            id: failure
            visible: root.phase === "failed"
            topPadding: 6
            width: parent.width
            text: root.failureText
            color: root.tokens.muted
            font.pixelSize: Math.round(10.5 * root.scale)
            wrapMode: Text.WordWrap
        }

        // The one thing a reader can do about it: the image search, opened in the browser. Not a
        // browser inside Lens, and no result is read back. It is the same pill
        // as the entry that opened this strip -- same height, same shape, same left baseline -- but
        // filled at rest: in the failure state it is the action, not a second line of explanation.
        Item {
            id: fallback
            visible: root.phase === "failed"
            width: parent.width
            height: 22

            Item {
                id: fallbackPill
                objectName: "picturesFallback"
                // As the entry's pill: its own padding hangs off the card's content edge, so the
                // label stands where the sense text above stands.
                x: -fallbackPill.sidePadding
                y: 0
                width: fallbackPill.sidePadding * 2 + fallbackLabel.implicitWidth
                height: 22
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: fallbackLabel.text
                Accessible.onPressAction: Controller.pictureDuty.openFallback()

                /// The pill's own padding, the entry's.
                readonly property int sidePadding: 7

                Rectangle {
                    id: fallbackPillShape
                    objectName: "picturesFallbackPill"
                    anchors.fill: parent
                    radius: root.tokens.radiusPill
                    // Filled at rest, unlike the entry: when nothing could be drawn, this pill is
                    // the only action left and has to read as one without waiting for hover.
                    color: fallbackHover.hovered || fallbackPill.activeFocus ? root.tokens.line
                                                                             : root.tokens.panel2
                    scale: fallbackPress.pressed ? 0.97 : 1.0
                    Behavior on scale {
                        NumberAnimation {
                            duration: root.tokens.motion.press
                            easing.type: root.tokens.motion.easing
                        }
                    }
                }

                Text {
                    id: fallbackLabel
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -0.5
                    x: fallbackPill.sidePadding
                    height: fallbackPill.height
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("Open image search")
                    color: fallbackHover.hovered || fallbackPill.activeFocus ? root.tokens.text : root.tokens.muted
                    font.pixelSize: 11
                }

                HoverHandler {
                    id: fallbackHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    id: fallbackPress
                    onTapped: Controller.pictureDuty.openFallback()
                }
                Keys.onSpacePressed: Controller.pictureDuty.openFallback()
                Keys.onReturnPressed: Controller.pictureDuty.openFallback()
            }
        }
    }

    /// @brief Open one picture's source page in the reader's browser.
    function openPage(page) {
        if (page !== "")
            Controller.pictureDuty.openPage(page);
    }
}
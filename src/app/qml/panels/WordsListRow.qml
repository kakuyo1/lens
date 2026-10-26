import QtQuick

/**
 * One row of the words list (UI-SPEC.md 4.8): the word, when it was last shown, its tally, the two
 * verdict pills and the cross that asks to remove it. A component rather than part of
 * WordsPopup because that file sits at the QML size cap.
 *
 * The model supplies the row's fields as roles; the view's remove transition closes the row when
 * the model lets it go, so nothing here folds, hides or rebuilds (docs/adr/0122).
 *
 * @p host is the WordsPopup window: the row reads its tokens and card width from it, and calls
 * back for the width handshake and the removal request.
 */
Item {
    id: entry
    required property string word
    required property string day
    required property string date
    required property string time
    required property string verdict
    required property int    pops
    required property var    host

    width: entry.host.cardWidth - 40

    /// What this row needs to draw itself whole: the card's padding, the word at its unelided width,
    /// the gap, and the right-hand cluster -- which keeps 8 px off the row's right edge, the room the
    /// cross's focus ring needs inside the list's clip. The card is sized to the widest of these, so
    /// the row hands it over rather than having its text measured from outside.
    readonly property real naturalWidth: 48 + wordText.implicitWidth + 9 + side.width
    onNaturalWidthChanged: entry.host.noteRowWidth(naturalWidth)
    Component.onCompleted: entry.host.noteRowWidth(naturalWidth)

    // The remove transition animates this from the row's height to zero; a binding would only
    // be re-read if the row height token changed, which it does not while a row is leaving.
    height: entry.host.rowHeight
    clip: true

    Timer {
        id: reviewDwell
        interval: 400
        onTriggered: {
            // The word's own rectangle, not a point above it: the review card is pinned by its
            // bottom edge to the word and flips below when the screen has no room above. The
            // pixels go out in the physical units the other surface anchors use, converted from
            // this screen's own origin rather than from zero -- on a screen away from zero the
            // two differ by origin * (1 - ratio) (docs/QML.md section 4). The card's own height
            // is read when it is placed, so a long answer moves its top, never its gap.
            const ratio = Screen.devicePixelRatio || 1;
            const at = wordText.mapToGlobal(0, 0);
            const nativeAt = SurfaceGeometry.nativeFromDipPoint(Qt.point(at.x, at.y),
                                                                Qt.point(Screen.virtualX, Screen.virtualY),
                                                                ratio);
            Controller.reviewWord(entry.word, nativeAt.x, nativeAt.y,
                                  SurfaceGeometry.nativeFromDipLength(wordText.width, ratio),
                                  SurfaceGeometry.nativeFromDipLength(wordText.height, ratio));
        }
    }

    /// The dismissal a pointer leaving the word asks for, held back the moment it takes to move
    /// from the word onto the card above it. The card reports the arrival itself
    /// (holdReviewCard), and a dismissal that lands while the pointer is on the card waits for it
    /// to leave instead -- the entry at the card's foot is there to be pressed, and it cannot be
    /// pressed by a pointer the word's exit has already chased away.
    Timer {
        id: reviewGrace
        interval: 260
        onTriggered: Controller.explanationDuty.dismissReview()
    }

    /// The row is going away -- removed from the model, or the list reset -- and a bubble it raised
    /// must not outlive it. This is the pointer leaving in every sense but the event: a destroyed
    /// delegate reports no un-hover, so the grace has nothing left to wait for.
    Component.onDestruction: Controller.explanationDuty.dismissReviewNow()

    Text {
        id: wordText
        anchors.left: parent.left
        // The word takes what the right-hand side leaves. The card is sized to the widest row, so this
        // bites only past maxCardWidth: a longer word elides rather than pushing the pills off the card.
        anchors.right: side.left
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: entry.word
        color: entry.host.tokens.text
        font.family: entry.host.englishFont
        font.pixelSize: 13
        elide: Text.ElideRight

        // Pointing at the word, not the row: the rest is metadata and verdict pills, which a pointer
        // crossing them is not after a reading of. The word hands over the text it is already drawing
        // rather than a case looking it up in the model -- that walk measured ~170 ms a row
        // (docs/QML.md section 6).
        HoverHandler {
            onHoveredChanged: {
                if (hovered) {
                    // The dwell: a pointer crossing the list on its way
                    // somewhere else is not asking about every word it passes.
                    // The grace goes with it: a pointer coming back to the word
                    // has not left the reading, and the dismissal it started by
                    // leaving is not one it still wants.
                    reviewGrace.stop();
                    reviewDwell.restart();
                    return;
                }
                reviewDwell.stop();
                reviewGrace.restart();
            }
        }
    }

    Row {
        id: side
        anchors.right: parent.right
        // The cross is the row's last tab stop, and its ring stands 4 px clear of the glyph: without
        // this much room the list's clip cuts the ring's right stroke off at the scrollbar.
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        // When the word was last shown, and how many times: the same faint metadata, so one cluster
        // rather than two more columns.
        Row {
            id: meta
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            MixedText {
                anchors.verticalCenter: parent.verticalCenter
                value: entry.host.whenLabel(entry.day, entry.date, entry.time)
                color: entry.host.tokens.faint
                pixelSize: 11
            }

            MixedText {
                anchors.verticalCenter: parent.verticalCenter
                // The history is what the count is read from, and it is
                // capped, so this is a floor once a word outlives it.
                value: qsTr("%1×").arg(entry.pops)
                color: entry.host.tokens.faint
                pixelSize: 11
            }
        }

        // The bubble's two verdict pills, in the same two shapes, with this word's verdict filled
        // instead of both being actions; a word never marked leaves both plain.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Rectangle {
                id: knownPill
                readonly property bool current: entry.verdict === "known"
                anchors.verticalCenter: parent.verticalCenter
                width: knownLabel.width + 13
                height: 20
                radius: entry.host.tokens.radiusPill
                color: current ? entry.host.tokens.ink : entry.host.tokens.panel2
                border.width: 1
                border.color: current ? entry.host.tokens.ink : entry.host.tokens.line
                scale: knownTap.pressed ? 0.97 : 1.0
                Behavior on scale {
                    NumberAnimation {
                        duration: entry.host.tokens.motion.press
                        easing.type: entry.host.tokens.motion.easing
                    }
                }

                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: knownLabel.text
                Accessible.onPressAction: knownTap.tapped()
                Keys.onPressed: (event) => {
                    if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return
                         || event.key === Qt.Key_Space) && !event.isAutoRepeat) {
                        event.accepted = true;
                        knownTap.tapped();
                    }
                }
                FocusRing { radius: entry.host.tokens.radiusPill }

                Text {
                    id: knownLabel
                    anchors.centerIn: parent
                    text: qsTr("Known")
                    color: knownPill.current ? entry.host.tokens.on : entry.host.tokens.muted
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }

                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: knownTap
                    onTapped: Controller.explanationDuty.mark(entry.word, true)
                }
            }

            Rectangle {
                id: newPill
                readonly property bool current: entry.verdict === "new"
                anchors.verticalCenter: parent.verticalCenter
                width: newLabel.width + 13
                height: 20
                radius: entry.host.tokens.radiusPill
                color: current ? entry.host.tokens.ink : entry.host.tokens.panel2
                border.width: 1
                border.color: current ? entry.host.tokens.ink : entry.host.tokens.line
                scale: newTap.pressed ? 0.97 : 1.0
                Behavior on scale {
                    NumberAnimation {
                        duration: entry.host.tokens.motion.press
                        easing.type: entry.host.tokens.motion.easing
                    }
                }

                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: newLabel.text
                Accessible.onPressAction: newTap.tapped()
                Keys.onPressed: (event) => {
                    if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return
                         || event.key === Qt.Key_Space) && !event.isAutoRepeat) {
                        event.accepted = true;
                        newTap.tapped();
                    }
                }
                FocusRing { radius: entry.host.tokens.radiusPill }

                Text {
                    id: newLabel
                    anchors.centerIn: parent
                    text: qsTr("New")
                    color: newPill.current ? entry.host.tokens.on : entry.host.tokens.muted
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }

                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: newTap
                    onTapped: Controller.explanationDuty.mark(entry.word, false)
                }
            }
        }

        // Centred on the row's own line like the two clusters beside it: left
        // to the Row it would sit on the row's first pixel and read high.
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            source: "qrc:/icons/ui-close.svg"
            color: entry.host.tokens.faint
            accessibleName: qsTr("Remove %1").arg(entry.word)
            onTapped: entry.host.requestRemoval(entry.word)
        }
    }
}

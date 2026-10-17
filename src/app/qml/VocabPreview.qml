pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The vocabulary-level dropdown's preview (UI.md 4.4): six words the hovered level should know,
 * laid out as a word cloud and raised beside the open list by DropdownField.
 *
 * The words are content rather than interface copy, so they live in data/preview.json with the
 * other preview sample. The card is a window of its own -- the list it belongs to is one -- and
 * the two glass themes draw the real desktop through a sheet of their own (docs/adr/0041).
 */
Window {
    id: root

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: "transparent"
    visible: false

    /// The dropdown option under the pointer: {value, label, group, note}.
    property var option: null
    /// The level's sample words, in the order the cloud draws them, from data/preview.json.
    readonly property var words: {
        if (!root.option)
            return [];
        const vocabulary = Controller.settingPreview.vocabulary || {};
        return vocabulary[String(root.option.value)] || [];
    }
    /// Whether there is a card to raise: a machine whose preview.json could not be read has none.
    readonly property bool hasContent: root.words.length > 0
    readonly property string levelLabel: root.option ? String(root.option.label) : ""

    /// @brief Point this window at @p value, the option whose level it is to draw.
    function showOption(value) {
        root.option = value;
    }

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 302
    readonly property int cardHeight: 240

    width: root.cardWidth + 2 * root.shadowMargin
    height: root.cardHeight + 2 * root.shadowMargin

    /// The desktop under this window, for the glass themes only.
    readonly property var glass: Controller.glassBackdrop.surface("vocabPreview")
    /// The stored theme's table: this card follows the reader's theme rather than previewing one.
    readonly property TokenTable tokens: TokenTable {
        theme: Tokens.theme
        glassEnabled: true
        liquidTextColor: Tokens.liquidTextColor
        glassLightText: root.glass ? root.glass.lightText : false
        motion: Tokens.motion
    }

    GlassHost {
        window: root
        sheet: root.glass
    }

    /// One entry per drawn word: the data word with the size and tone its place carries. The
    /// first two are the biggest and read as the level's most common words.
    readonly property var wordChips: {
        const sizes = [21, 12, 18, 11, 16, 14];
        const colors = [root.tokens.text, root.tokens.faint, root.tokens.text,
                        root.tokens.faint, root.tokens.muted, root.tokens.muted];
        const chips = [];
        for (let i = 0; i < Math.min(root.words.length, sizes.length); ++i) {
            chips.push({word: String(root.words[i]), size: sizes[i], color: colors[i],
                        bold: i === 0 || i === 2});
        }
        return chips;
    }

    ShadowCard {
        id: surface
        anchors.fill: parent
        radius: root.tokens.radiusCard
        color: root.tokens.glassMode === "" ? root.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: root.glass
            mode: root.tokens.glassMode
            radius: root.tokens.radiusCard
        }

        Column {
            x: 14
            y: 14
            width: parent.width - 28
            spacing: 10

            // The level and the demo mark. No title: the chip and the words are the card.
            Item {
                width: parent.width
                height: 19

                Rectangle {
                    width: levelText.width + 16
                    height: parent.height
                    radius: root.tokens.radiusPill
                    color: root.tokens.panel2
                    border.width: 1
                    border.color: root.tokens.line2

                    Text {
                        id: levelText
                        anchors.centerIn: parent
                        text: root.levelLabel
                        color: root.tokens.muted
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("SettingsPopup", "Preview")
                    color: root.tokens.faint
                    font.pixelSize: 11
                }
            }

            // The cloud: words are wrapped into centred rows from their measured widths, so a
            // long academic word and a short one share the card without a fixed grid.
            Item {
                id: cloudArea
                width: parent.width
                height: 146

                function relayout() {
                    const items = [];
                    for (let i = 0; i < wordRepeater.count; ++i) {
                        const item = wordRepeater.itemAt(i);
                        if (item)
                            items.push(item);
                    }

                    const gapX = 12;
                    const gapY = 8;
                    const rows = [];
                    let row = [];
                    let rowWidth = 0;
                    for (let i = 0; i < items.length; ++i) {
                        const item = items[i];
                        if (row.length > 0 && rowWidth + gapX + item.implicitWidth > cloudArea.width) {
                            rows.push({items: row, width: rowWidth});
                            row = [];
                            rowWidth = 0;
                        }
                        if (row.length > 0)
                            rowWidth += gapX;
                        row.push(item);
                        rowWidth += item.implicitWidth;
                    }
                    if (row.length > 0)
                        rows.push({items: row, width: rowWidth});

                    let total = gapY * Math.max(0, rows.length - 1);
                    for (let i = 0; i < rows.length; ++i) {
                        let rowHeight = 0;
                        for (let j = 0; j < rows[i].items.length; ++j)
                            rowHeight = Math.max(rowHeight, rows[i].items[j].implicitHeight);
                        rows[i].height = rowHeight;
                        total += rowHeight;
                    }

                    let y = Math.max(0, (cloudArea.height - total) / 2);
                    for (let i = 0; i < rows.length; ++i) {
                        const placed = rows[i];
                        let x = (cloudArea.width - placed.width) / 2;
                        for (let j = 0; j < placed.items.length; ++j) {
                            const item = placed.items[j];
                            item.x = Math.round(x);
                            item.y = Math.round(y + (placed.height - item.implicitHeight) / 2);
                            x += item.implicitWidth + gapX;
                        }
                        y += placed.height + gapY;
                    }
                }

                Repeater {
                    id: wordRepeater
                    model: root.wordChips

                    delegate: Text {
                        required property var modelData
                        text: modelData.word
                        color: modelData.color
                        font.pixelSize: modelData.size
                        font.weight: modelData.bold ? Font.Bold : Font.Normal

                        onImplicitWidthChanged: Qt.callLater(cloudArea.relayout)
                        onTextChanged: Qt.callLater(cloudArea.relayout)
                    }

                    onCountChanged: {
                        cloudArea.relayout();
                        Qt.callLater(cloudArea.relayout);
                    }
                    onModelChanged: {
                        cloudArea.relayout();
                        Qt.callLater(cloudArea.relayout);
                    }
                }

                // Laid out before the first paint where the delegates are already there, and once
                // more after layout for the frame where they were not.
                Component.onCompleted: {
                    cloudArea.relayout();
                    Qt.callLater(cloudArea.relayout);
                }
                onWidthChanged: cloudArea.relayout()
            }

            Rectangle {
                width: parent.width
                height: 1
                color: root.tokens.line2
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTranslate("SettingsPopup", "Size = commonness")
                color: root.tokens.faint
                font.pixelSize: 11
            }
        }
    }

    // The same demo mark every preview card carries, outside the card rather than on its edge.
    PreviewFrame {
        card: surface.card
        cardRadius: root.tokens.radiusCard
        strokeColor: root.tokens.muted
        opacity: surface.opacity
        transform: Translate { y: (1 - surface.opacity) * 8 }
    }
}

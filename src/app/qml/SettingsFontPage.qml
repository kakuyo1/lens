import QtQuick

/**
 * The face English is read in (UI.md 4.4).
 *
 * One setting: the family the explanation's English runs are drawn in -- the word, its
 * pronunciation, the definition and the example sentence, in the bubble and in the word list.
 * What the rest of the card draws is the reader's own language, which keeps the face the
 * application installed, so this is one choice and not a set of them.
 *
 * The list is the machine's own Latin families (Controller.latinFontFamilies), which is why the
 * field is typeable: a machine with a few hundred families is browsed by typing rather than by
 * scrolling. The face itself is shown on the card the label raises (UI.md 4.4) -- the explanation
 * bubble, drawn with the reader's settings, this one included.
 */
Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property var settings: Controller.settings

    readonly property var openChildRect: fontField.openChildRect
    implicitHeight: content.implicitHeight

    /// The preview the pointer has called up, or "" for none (UI.md 4.4).
    signal previewRequested(string key)

    function closeChild() {
        fontField.closeList();
    }

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            // The label raises the preview itself: this row's control is a dropdown, and what the
            // preview shows is the card, not the list the dropdown opens.
            Text {
                text: qsTranslate("SettingsPopup", "Font")
                color: page.tokens.muted
                font.pixelSize: 12
                HoverHandler {
                    onHoveredChanged: page.previewRequested(hovered ? "font" : "")
                }
            }
            DropdownField {
                id: fontField
                objectName: "fontField"
                width: parent.width
                editable: true
                options: Controller.latinFontFamilies()
                currentValue: page.settings.font
                onPicked: (value) => Controller.setFont(value)
                // A name the machine does not have is refused in C++, and the field goes back to
                // the family in force by the same binding the model field is drawn by.
                onEdited: (text) => Controller.setFont(text)
            }
        }

        // The one line the page says: nothing here explains the family, because the label beside it
        // already draws one. What it names is the only thing a reader cannot guess -- that the same
        // label answers the question the list leaves open, which is what the family will look like.
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTranslate("SettingsPopup", "Hover the title to preview.")
            color: page.tokens.faint
            font.pixelSize: 11
        }
    }
}

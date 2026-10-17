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
 * scrolling. The face itself is shown on the card a row of that list raises (UI.md 4.4) -- the
 * explanation bubble, drawn with the reader's settings, this one forced to the row's family.
 */
Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property var settings: Controller.settings

    readonly property var openChildRect: fontField.openChildRect
    implicitHeight: content.implicitHeight

    /// The preview the pointer has called up, or "" for none (UI.md 4.4). @p font is the family
    /// to draw it in, and only ever accompanies "font".
    signal previewRequested(string key, string font)

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
            Text {
                text: qsTranslate("SettingsPopup", "Font")
                color: page.tokens.muted
                font.pixelSize: 12
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
                // The preview is the settings panel's own bubble, not a window of this field's:
                // the row hands it the family, and the panel draws it beside itself as it does
                // for every other setting preview.
                onOptionHovered: (option) => page.previewRequested("font", String(option.value))
                onOptionUnhovered: page.previewRequested("", "")
            }
        }

        // The one line the page says: nothing here explains the family, because the card beside
        // the panel draws one. What it names is the only thing a reader cannot guess -- where the
        // hover that shows that card lives.
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTranslate("SettingsPopup", "Hover a font in the list to preview.")
            color: page.tokens.faint
            font.pixelSize: 11
        }
    }
}

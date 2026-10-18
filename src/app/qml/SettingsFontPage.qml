import QtQuick

/**
 * The face English is read in, and the size it is read at (UI.md 4.4).
 *
 * Two settings on one page because they answer one question -- how the explanation looks -- and
 * neither is worth a subpage of its own. The family is the one the explanation's English runs are
 * drawn in: the word, its pronunciation, the definition and the example sentence, in the bubble and
 * in the word list. What the rest of the card draws is the reader's own language, which keeps the
 * face the application installed, so the family is one choice and not a set of them. The size is
 * the body size the answer is read at, and the card scales its other runs from it.
 *
 * The list is the machine's own Latin families (Controller.latinFontFamilies), which is why the
 * field is typeable: a machine with a few hundred families is browsed by typing rather than by
 * scrolling. The face itself is shown on the card a row of that list raises (UI.md 4.4) -- the
 * explanation bubble, drawn with the reader's settings, this one forced to the row's family. The
 * size list raises the same card at the row's size, so both rows answer "what does this look like"
 * with the card rather than with a number.
 */
Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property var settings: Controller.settings

    readonly property var openChildRect: fontField.openChildRect || sizeField.openChildRect
    implicitHeight: content.implicitHeight

    /// The preview the pointer has called up, or "" for none (UI.md 4.4). @p value is the family
    /// to draw it in when @p key is "font", and the size to draw it at when it is "fontSize"; it
    /// only ever accompanies one of those two.
    signal previewRequested(string key, var value)

    function closeChild() {
        fontField.closeList();
        sizeField.closeList();
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

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Font size")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: sizeField
                objectName: "sizeField"
                width: parent.width
                // A whole number of pixels, so the field's value is compared as one: the options
                // carry ints, and the setting hands out an int.
                options: Controller.bubbleFontSizeOptions()
                currentValue: page.settings.bubbleFontSize
                onPicked: (value) => Controller.setBubbleFontSize(value)
                // The same card as the family's rows, at the row's size: a number in a field says
                // nothing about how a sentence reads, and this is the one page where that is the
                // whole question.
                onOptionHovered: (option) => page.previewRequested("fontSize", Number(option.value))
                onOptionUnhovered: page.previewRequested("", "")
            }
        }

        // The one line the page says: nothing here explains the family or the size, because the
        // card beside the panel draws both. What it names is the only thing a reader cannot guess
        // -- where the hover that shows that card lives.
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTranslate("SettingsPopup", "Hover a font or a size in the list to preview.")
            color: page.tokens.faint
            font.pixelSize: 11
        }
    }
}

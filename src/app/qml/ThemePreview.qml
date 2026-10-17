pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The theme dropdown's preview (UI.md 4.4): a miniature settings card drawn in the theme under
 * the pointer, put beside the open list by DropdownField.
 *
 * It is a window of its own because the list it belongs to is one: a card drawn inside the list
 * would be clipped at its edge. Its colours come from a table forced to the hovered option rather
 * than the stored theme, and the two glass themes draw the real desktop through a sheet of their
 * own -- one sheet holds one backdrop (docs/adr/0041).
 */
Window {
    id: root

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: "transparent"
    visible: false

    /// The dropdown option under the pointer: {value, label, previewTheme}.
    property var option: null

    /// The theme to draw: "light", "dark", "frosted", "liquid", or a custom: JSON value.
    readonly property string themeValue: root.option && root.option.previewTheme
        ? String(root.option.previewTheme) : "light"
    /// The label the mock field shows: the row's own, so the box and the row say the same thing.
    readonly property string optionLabel: root.option ? String(root.option.label) : ""

    /// @brief Point this window at @p value, the option whose theme it is to draw.
    function showOption(value) {
        root.option = value;
    }

    /// The margin the card's shadow needs. DropdownField subtracts it to put the card clear of
    /// the list, and keeps the window's own margin free to hang over the screen's edge.
    readonly property int shadowMargin: 26
    readonly property int cardWidth: 228
    /// A fixed height: the same window whichever row is hovered, so the card does not jump as the
    /// pointer crosses the list, and placement can read it before the first layout.
    readonly property int cardHeight: 190

    width: root.cardWidth + 2 * root.shadowMargin
    height: root.cardHeight + 2 * root.shadowMargin

    /// The desktop under this window, for the glass themes only.
    readonly property var glass: Controller.glassBackdrop.surface("themePreview")
    /// The hovered theme instead of the stored one: this window is the preview.
    readonly property TokenTable tokens: TokenTable {
        theme: root.themeValue
        glassEnabled: true
        liquidTextColor: Tokens.liquidTextColor
        glassLightText: root.glass ? root.glass.lightText : false
        motion: Tokens.motion
    }

    GlassHost {
        id: glassHost
        window: root
        sheet: root.glass
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
            id: content
            x: 14
            y: 14
            width: parent.width - 28
            spacing: 9

            // The header: the accent dot, the surface's own name, and the faint dismiss glyph
            // the real panel carries. Enough to read as the settings card at a glance.
            Item {
                width: parent.width
                height: 16

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6
                    height: 6
                    radius: 3
                    color: root.tokens.ok
                }

                Text {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("SettingsPopup", "Settings")
                    color: root.tokens.text
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                Icon {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10
                    height: 10
                    source: "qrc:/icons/ui-close.svg"
                    color: root.tokens.faint
                }
            }

            // The control the reader is choosing from, showing the hovered option's own label.
            Column {
                width: parent.width
                spacing: 5

                Text {
                    text: qsTranslate("SettingsPopup", "Theme")
                    color: root.tokens.muted
                    font.pixelSize: 11
                }

                Rectangle {
                    width: parent.width
                    height: 30
                    radius: root.tokens.radiusField
                    color: root.tokens.panel2
                    border.width: 1
                    border.color: root.tokens.line

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: chevron.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.optionLabel
                        color: root.tokens.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Icon {
                        id: chevron
                        anchors.right: parent.right
                        anchors.rightMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10
                        height: 10
                        source: "qrc:/icons/ui-chevron-down.svg"
                        color: root.tokens.faint
                    }
                }
            }

            // A switch and a slider: the two control shapes whose fills carry the secondary
            // surface and the primary action colour.
            Item {
                width: parent.width
                height: 18

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("SettingsPopup", "Animations")
                    color: root.tokens.text
                    font.pixelSize: 11
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    height: 18
                    radius: 9
                    color: root.tokens.ink

                    Rectangle {
                        x: parent.width - width - 2
                        y: 2
                        width: 14
                        height: 14
                        radius: 7
                        color: root.tokens.on
                    }
                }
            }

            Item {
                width: parent.width
                height: 18

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("SettingsPopup", "Typing speed")
                    color: root.tokens.text
                    font.pixelSize: 11
                }

                Item {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 74
                    height: 12

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 3
                        radius: 1.5
                        color: root.tokens.line
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * 0.62
                        height: 3
                        radius: 1.5
                        color: root.tokens.ink
                    }

                    Rectangle {
                        x: parent.width * 0.62 - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10
                        height: 10
                        radius: 5
                        color: root.tokens.panel
                        border.width: 1
                        border.color: root.tokens.line
                    }
                }
            }

            Item {
                width: parent.width
                height: 24

                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 60
                    height: 22
                    radius: root.tokens.radiusPill
                    color: root.tokens.ink

                    Text {
                        anchors.centerIn: parent
                        text: qsTranslate("SettingsPopup", "Save")
                        color: root.tokens.on
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
                }
            }
        }
    }

    // The card is a demo, and wears the mark the settings preview wears: a dashed frame outside
    // it rather than an edge of its own (UI.md 4.4).
    PreviewFrame {
        card: surface.card
        cardRadius: root.tokens.radiusCard
        strokeColor: root.tokens.muted
        opacity: surface.opacity
        transform: Translate { y: (1 - surface.opacity) * 8 }
    }

    // A move to a glass row while the window is already up has to take a sheet for the new theme;
    // the first show is caught by GlassHost's own visibility watch.
    Connections {
        target: root
        function onThemeValueChanged() {
            glassHost.capture();
        }
    }
}

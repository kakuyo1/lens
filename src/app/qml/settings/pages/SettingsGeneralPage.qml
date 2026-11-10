pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    readonly property var glass: Window.window.glass
    // qmllint enable missing-property

    /// The backdrop refresh tiers, in the order GlassQuality numbers them (docs/adr/0073, docs/adr/0074).
    readonly property var qualityOptions: [
        {value: 0, label: qsTranslate("SettingsPopup", "Automatic"), group: "",
         note: qsTranslate("SettingsPopup", "Follow this screen up to about 60 times a second when it can be read live; otherwise read about 15 times a second.")},
        {value: 1, label: qsTranslate("SettingsPopup", "Battery saver"), group: "",
         note: qsTranslate("SettingsPopup", "Keep the picture still; retake it only when the card appears or moves.")},
        {value: 2, label: qsTranslate("SettingsPopup", "Standard"), group: "",
         note: qsTranslate("SettingsPopup", "About 15 reads a second on the CPU.")},
        {value: 3, label: qsTranslate("SettingsPopup", "Smooth"), group: "",
         note: qsTranslate("SettingsPopup", "Follow this screen's refresh rate when it can be read live.")}
    ]

    /// What the chosen tier is doing on this screen: the option's own note, or why Smooth could not be reached.
    function qualityNote() {
        let note = "";
        for (let i = 0; i < page.qualityOptions.length; ++i)
            if (page.qualityOptions[i].value === Controller.settings.glassQuality)
                note = page.qualityOptions[i].note;
        if ((Controller.settings.glassQuality === 0 || Controller.settings.glassQuality === 3)
                && page.glass && page.glass.duplicationRefused)
            note = qsTranslate("SettingsPopup", "This screen cannot be read live, so the backdrop refreshes about 15 times a second.");
        return note;
    }

    property int themeChoice: page.tokens.themeIndex(Controller.settings.theme)
    readonly property var openChildRect: themeField.openChildRect || qualityField.openChildRect
    implicitHeight: content.implicitHeight

    signal childRequested(string value)

    /// The preview the pointer has called up, or "" for none: a row's title raises one on hover
    /// (UI-SPEC.md 4.4). The popup turns it into the window Main puts beside the card.
    signal previewRequested(string key)

    function closeChild() {
        themeField.closeList();
        qualityField.closeList();
    }

    function chooseTheme(index) {
        page.themeChoice = index;
        Controller.setSetting("theme", page.tokens.themeName(index));
    }

    Connections {
        target: Controller
        function onSettingsChanged() {
            page.themeChoice = page.tokens.themeIndex(Controller.settings.theme);
        }
    }

    /// The card a hovered theme row raises beside the list. Every option carries the theme it
    /// stands for, so the preview needs nothing from this page but the option itself.
    Component {
        id: themePreviewComponent
        ThemePreview {}
    }

    /// The card a hovered refresh row raises: the row's own note, which the rows have no room for.
    Component {
        id: notePreviewComponent
        NotePreview {}
    }

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Theme")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: themeField
                objectName: "themeField"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Theme")
                options: [
                    {value: 0, label: qsTranslate("SettingsPopup", "Light"), group: "", note: "",
                     previewTheme: "light"},
                    {value: 1, label: qsTranslate("SettingsPopup", "Dark"), group: "", note: "",
                     previewTheme: "dark"},
                    {value: 2, label: qsTranslate("SettingsPopup", "Frosted glass"), group: "", note: "",
                     previewTheme: "frosted"},
                    {value: 3, label: qsTranslate("SettingsPopup", "Liquid glass"), group: "", note: "",
                     previewTheme: "liquid"}
                ]
                currentValue: page.themeChoice
                optionPreview: themePreviewComponent
                onPicked: (value) => page.chooseTheme(value)
            }
        }

        Column {
            id: refreshSection
            width: parent.width
            spacing: 10
            visible: page.themeChoice === 2 || page.themeChoice === 3
            height: visible ? implicitHeight : 0
            // The list is a window of its own, so hiding the section does not put it down by itself.
            onVisibleChanged: {
                if (!visible)
                    qualityField.closeList();
            }

            // A field under Theme, not a section of its own: it takes the other field labels'
            // 12 px muted weight rather than the bold section heading (UI-SPEC 4.4).
            Text {
                text: qsTranslate("SettingsPopup", "Backdrop refresh")
                color: page.tokens.muted
                font.pixelSize: 12
            }

            DropdownField {
                id: qualityField
                objectName: "glassQualityField"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Backdrop refresh")
                options: page.qualityOptions
                currentValue: Controller.settings.glassQuality
                // Each tier's note is a sentence about what it costs on this screen, which no row
                // has room for: it goes into the card the pointer raises beside the list.
                notesInRows: false
                optionPreview: notePreviewComponent
                onPicked: (value) => Controller.setSetting("glassQuality", value)
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: page.tokens.muted
                font.pixelSize: 12
                text: page.qualityNote()
            }
        }

        Column {
            width: parent.width
            spacing: 10
            visible: page.themeChoice === 2
            height: visible ? implicitHeight : 0

            Text {
                text: qsTranslate("SettingsPopup", "Frosted glass parameters")
                color: page.tokens.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            // The six glass numbers read the live projection rather than the settings map: a drag then repaints the
            // glass and leaves the rest of the application alone (docs/adr/0173).
            SliderRow {
                label: qsTranslate("SettingsPopup", "Blur")
                suffix: "%"
                to: Controller.settingRange("frostedBlur").high
                value: Controller.glassParameters.frostedBlur
                onMoved: (stop) => Controller.setSetting("frostedBlur", stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Saturation")
                suffix: "%"
                to: Controller.settingRange("frostedSaturation").high
                value: Controller.glassParameters.frostedSaturation
                onMoved: (stop) => Controller.setSetting("frostedSaturation", stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Rim highlight")
                suffix: "%"
                to: Controller.settingRange("frostedRim").high
                value: Controller.glassParameters.frostedRim
                onMoved: (stop) => Controller.setSetting("frostedRim", stop)
            }
        }

        Column {
            width: parent.width
            spacing: 10
            visible: page.themeChoice === 3
            height: visible ? implicitHeight : 0

            Text {
                text: qsTranslate("SettingsPopup", "Liquid glass parameters")
                color: page.tokens.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Refraction")
                suffix: " px"
                to: Controller.settingRange("liquidRefraction").high
                value: Controller.glassParameters.liquidRefraction
                onMoved: (stop) => Controller.setSetting("liquidRefraction", stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Blur")
                suffix: "%"
                to: Controller.settingRange("liquidBlur").high
                value: Controller.glassParameters.liquidBlur
                onMoved: (stop) => Controller.setSetting("liquidBlur", stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Rim highlight")
                suffix: "%"
                to: Controller.settingRange("liquidRim").high
                value: Controller.glassParameters.liquidRim
                onMoved: (stop) => Controller.setSetting("liquidRim", stop)
            }
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Launch at startup")
            checked: Controller.settings.autostart
            onToggled: (on) => Controller.setAutostart(on)
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Animations")
            checked: Controller.settings.animationsEnabled
            onToggled: (on) => Controller.setSetting("animationsEnabled", on)
        }

        // The typing pace the answer is written out at. It only means anything while animations are
        // on -- a reduced-motion card arrives whole and has no pace to set -- so it is shown only
        // while the switch above it is on, and hidden with it rather than left as a control that
        // does nothing. The blur follows this level by a ratio, so a slow speed never widens the
        // tail of shaders (RevealText.qml).
        SliderRow {
            visible: Controller.settings.animationsEnabled
            height: visible ? implicitHeight : 0
            label: qsTranslate("SettingsPopup", "Typing speed")
            suffix: " / " + Controller.settingRange("revealSpeed").high
            from: Controller.settingRange("revealSpeed").low
            to: Controller.settingRange("revealSpeed").high
            value: Controller.settings.revealSpeed
            previewKey: "speed"
            onMoved: (stop) => Controller.setSetting("revealSpeed", stop)
            onPreviewChanged: (key) => page.previewRequested(key)
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Check for updates at startup")
            checked: Controller.updateDuty.autoCheck
            onToggled: (on) => Controller.updateDuty.setAutoCheck(on)
        }

        // The page's one door: the typeface this machine's copy reads in.
        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "Font")
            onPicked: page.childRequested("font")
        }

        Text {
            width: parent.width
            visible: SystemMotion.reduced
            text: qsTranslate("SettingsPopup", "Animations are disabled by Windows accessibility settings.")
            color: page.tokens.muted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}

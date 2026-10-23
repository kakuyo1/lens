pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    readonly property var openChildRect: petTypeField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        petTypeField.closeList();
    }

    /// Whether the page is asking the reader a question of its own. Escape answers it before the panel backs out.
    readonly property bool questionOpen: packageDialog.opened

    function closeQuestion() {
        packageDialog.close();
    }

    function actionLabel(name) {
        switch (name) {
        case "idle": return qsTranslate("SettingsPopup", "Idle");
        case "study": return qsTranslate("SettingsPopup", "Reading");
        case "thinking": return qsTranslate("SettingsPopup", "Thinking");
        case "celebrate": return qsTranslate("SettingsPopup", "Celebrate");
        case "encourage": return qsTranslate("SettingsPopup", "Encourage");
        case "sleep": return qsTranslate("SettingsPopup", "Sleep");
        case "click_react": return qsTranslate("SettingsPopup", "Poked");
        case "pickup": return qsTranslate("SettingsPopup", "Picked up");
        case "look_around": return qsTranslate("SettingsPopup", "Look around");
        case "yawn": return qsTranslate("SettingsPopup", "Yawn");
        case "stretch": return qsTranslate("SettingsPopup", "Stretch");
        case "angry": return qsTranslate("SettingsPopup", "Angry");
        case "ask": return qsTranslate("SettingsPopup", "Sign");
        case "walk": return qsTranslate("SettingsPopup", "Walk");
        case "edge_peek": return qsTranslate("SettingsPopup", "Edge peek");
        case "wobble": return qsTranslate("SettingsPopup", "Wobble");
        case "bounce": return qsTranslate("SettingsPopup", "Bounce");
        case "hiccup": return qsTranslate("SettingsPopup", "Hiccup");
        case "shiver": return qsTranslate("SettingsPopup", "Shiver");
        case "spin": return qsTranslate("SettingsPopup", "Spin");
        case "preen": return qsTranslate("SettingsPopup", "Preen");
        case "hop": return qsTranslate("SettingsPopup", "Hop");
        case "flap": return qsTranslate("SettingsPopup", "Flap");
        case "hoot": return qsTranslate("SettingsPopup", "Hoot");
        case "nod": return qsTranslate("SettingsPopup", "Nod");
        case "groom": return qsTranslate("SettingsPopup", "Groom");
        case "tail_flick": return qsTranslate("SettingsPopup", "Tail flick");
        case "knead": return qsTranslate("SettingsPopup", "Knead");
        case "pounce": return qsTranslate("SettingsPopup", "Pounce");
        case "scratch": return qsTranslate("SettingsPopup", "Scratch");
        case "float": return qsTranslate("SettingsPopup", "Float");
        case "peek": return qsTranslate("SettingsPopup", "Peek");
        case "boo": return qsTranslate("SettingsPopup", "Boo");
        case "bob": return qsTranslate("SettingsPopup", "Bob");
        case "squish": return qsTranslate("SettingsPopup", "Squish");
        case "bubble": return qsTranslate("SettingsPopup", "Bubble");
        case "drift": return qsTranslate("SettingsPopup", "Drift");
        case "drizzle": return qsTranslate("SettingsPopup", "Drizzle");
        case "puff": return qsTranslate("SettingsPopup", "Puff");
        default: return name;
        }
    }

    function sceneLabel(id) {
        switch (id) {
        case "swing": return qsTranslate("SettingsPopup", "Swing");
        case "desk": return qsTranslate("SettingsPopup", "Desk");
        case "skate": return qsTranslate("SettingsPopup", "Skateboard");
        case "bath": return qsTranslate("SettingsPopup", "Bubble bath");
        case "rain": return qsTranslate("SettingsPopup", "Rain");
        case "cat_box": return qsTranslate("SettingsPopup", "Cardboard box");
        case "cat_sunbeam": return qsTranslate("SettingsPopup", "Sunny sill");
        case "cloud_watering": return qsTranslate("SettingsPopup", "Watering");
        case "cloud_fishing": return qsTranslate("SettingsPopup", "Star fishing");
        case "dog_frisbee": return qsTranslate("SettingsPopup", "Frisbee");
        case "dog_burying": return qsTranslate("SettingsPopup", "Burying a bone");
        case "ghost_lantern": return qsTranslate("SettingsPopup", "Jack-o'-lantern");
        case "ghost_mirror": return qsTranslate("SettingsPopup", "Mirror");
        case "jelly_sundae": return qsTranslate("SettingsPopup", "Sundae");
        case "jelly_trampoline": return qsTranslate("SettingsPopup", "Trampoline");
        case "mochi_onsen": return qsTranslate("SettingsPopup", "Hot spring");
        case "mochi_grill": return qsTranslate("SettingsPopup", "Roasted mochi");
        case "owl_stargazing": return qsTranslate("SettingsPopup", "Stargazing");
        case "owl_books": return qsTranslate("SettingsPopup", "Book nook");
        default: return id;
        }
    }

    function accessoryLabel(id) {
        switch (id) {
        case "hat": return qsTranslate("SettingsPopup", "Top hat");
        case "beanie": return qsTranslate("SettingsPopup", "Beanie");
        case "sprout": return qsTranslate("SettingsPopup", "Sprout");
        case "crown": return qsTranslate("SettingsPopup", "Crown");
        case "glasses": return qsTranslate("SettingsPopup", "Round glasses");
        case "sunglasses": return qsTranslate("SettingsPopup", "Sunglasses");
        case "bowtie": return qsTranslate("SettingsPopup", "Bow tie");
        case "flower": return qsTranslate("SettingsPopup", "Flower");
        case "party_hat": return qsTranslate("SettingsPopup", "Party hat");
        case "blush": return qsTranslate("SettingsPopup", "Blush");
        case "scarf": return qsTranslate("SettingsPopup", "Scarf");
        case "apron": return qsTranslate("SettingsPopup", "Apron");
        case "hair_bow": return qsTranslate("SettingsPopup", "Hair bow");
        case "reading_glasses": return qsTranslate("SettingsPopup", "Reading glasses");
        case "sleep_mask": return qsTranslate("SettingsPopup", "Sleep mask");
        case "vest": return qsTranslate("SettingsPopup", "Vest");
        case "bell_collar": return qsTranslate("SettingsPopup", "Bell collar");
        case "bandana": return qsTranslate("SettingsPopup", "Bandana");
        default: return id;
        }
    }

    function slotLabel(slot) {
        switch (slot) {
        case "head": return qsTranslate("SettingsPopup", "Head");
        case "face": return qsTranslate("SettingsPopup", "Face");
        default: return qsTranslate("SettingsPopup", "Body");
        }
    }

    function petLabel(id) {
        switch (id) {
        case "dog": return qsTranslate("SettingsPopup", "Dog");
        case "mochi": return qsTranslate("SettingsPopup", "Mochi");
        case "owl": return qsTranslate("SettingsPopup", "Owl");
        case "cat": return qsTranslate("SettingsPopup", "Stub cat");
        case "ghost": return qsTranslate("SettingsPopup", "Ghost");
        case "jelly": return qsTranslate("SettingsPopup", "Jellyfish");
        case "cloud": return qsTranslate("SettingsPopup", "Cloud");
        default: return id;
        }
    }

    /// Why a package was refused, in the words UI.md 4.12 gives. The package never supplies these strings.
    function refusalLabel(reason) {
        switch (reason) {
        case "Format": return qsTranslate("SettingsPopup", "The package format is not supported.");
        case "TooLarge": return qsTranslate("SettingsPopup", "The package exceeds the size limits.");
        case "BadName": return qsTranslate("SettingsPopup", "The package name is not valid.");
        case "MissingRole": return qsTranslate("SettingsPopup", "A required action is missing.");
        case "BadAnimation": return qsTranslate("SettingsPopup", "An action is not valid.");
        case "BadAnchors": return qsTranslate("SettingsPopup", "The anchor data is not valid.");
        case "BadAccessory": return qsTranslate("SettingsPopup", "An accessory is not valid.");
        case "BadAssetType": return qsTranslate("SettingsPopup", "An image is not a valid PNG.");
        case "SheetSize": return qsTranslate("SettingsPopup", "An image size does not match its frames.");
        case "ExpressionIndex": return qsTranslate("SettingsPopup", "An expression index is out of range.");
        case "PathEscapes": return qsTranslate("SettingsPopup", "An image path points outside the package.");
        case "IdTaken": return qsTranslate("SettingsPopup", "The package id is already in use.");
        default: return qsTranslate("SettingsPopup", "The package could not be read.");
        }
    }

    // Each time the section is opened the page is built again, so this is the rescan UI.md 4.4 asks for.
    Component.onCompleted: Pet.refresh()

    PetPackageDialog {
        id: packageDialog

        onEnableRequested: Pet.confirmPet(packageDialog.petId)
    }

    Connections {
        target: Pet

        function onConfirmationRequested(id, name) {
            packageDialog.petId = id;
            packageDialog.petName = name;
            packageDialog.open();
        }
    }

    Column {
        id: content
        width: parent.width
        spacing: 7

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Desktop companion")
            checked: Pet.enabled
            interactive: Pet.available
            onToggled: (on) => Pet.setEnabled(on)
        }

        SwitchRow {
            objectName: "passthroughSwitch"
            width: parent.width
            label: qsTranslate("SettingsPopup", "Mouse pass-through")
            checked: Pet.passthrough
            interactive: Pet.enabled
            onToggled: (on) => Pet.setPassthrough(on)
        }

        Row {
            id: sizeRow

            width: parent.width
            height: 33
            spacing: 8

            Text {
                id: sizeLabel
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("SettingsPopup", "Desktop pet size")
                color: Pet.enabled ? page.tokens.text : page.tokens.faint
                font.pixelSize: 13
            }

            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, sizeRow.width - sizeLabel.width - sizeSlider.width - sizeValue.width - 3 * sizeRow.spacing)
                height: 1
            }

            StepSlider {
                id: sizeSlider
                anchors.verticalCenter: parent.verticalCenter
                from: Pet.minSize
                to: Pet.maxSize
                value: Pet.size
                interactive: Pet.enabled
                onMoved: (stop) => Pet.setSize(stop)
            }

            Text {
                id: sizeValue
                anchors.verticalCenter: parent.verticalCenter
                text: Pet.size + "%"
                color: Pet.enabled ? page.tokens.muted : page.tokens.faint
                font.pixelSize: 13
            }
        }

        // Everything below exists only while the pet is on (PHASE3 3.5): the pet type, the preview, the actions and
        // the accessories. Off, the whole block is gone and no pet animation runs.
        Column {
            id: petSection

            width: parent.width
            spacing: 14
            visible: Pet.enabled

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: qsTranslate("SettingsPopup", "Pet type")
                    color: page.tokens.muted
                    font.pixelSize: 11
                }

                DropdownField {
                    id: petTypeField
                    width: parent.width
                    options: Pet.pets.map(entry => ({
                        value: entry.id,
                        label: entry.external ? entry.name : page.petLabel(entry.id),
                        group: "",
                        note: entry.external ? qsTranslate("SettingsPopup", "External") : ""
                    }))
                    currentValue: Pet.pet
                    onPicked: (value) => Pet.setPet(value)
                }
            }

            // The packages sit right under the picker that lists them: a refused package is an error the reader must not
            // have to scroll to find (UI.md 4.4).
            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: qsTranslate("SettingsPopup", "Pet packages")
                    color: page.tokens.muted
                    font.pixelSize: 11
                }

                Text {
                    width: parent.width
                    text: qsTranslate("SettingsPopup", "Copy a pet package folder into the packages folder. Lens reads it when you open this section.")
                    color: page.tokens.faint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Chip {
                    objectName: "openPacksButton"
                    label: qsTranslate("SettingsPopup", "Open packages folder")
                    onPicked: Pet.openPacksFolder()
                }

                Repeater {
                    model: Pet.refusals

                    Text {
                        id: refusalRow

                        required property var modelData

                        width: parent.width
                        text: qsTranslate("SettingsPopup", "%1: %2").arg(modelData.id).arg(page.refusalLabel(modelData.reason))
                        textFormat: Text.PlainText
                        color: page.tokens.muted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: qsTranslate("SettingsPopup", "Preview")
                    color: page.tokens.muted
                    font.pixelSize: 11
                }

                PetStage {
                    width: parent.width
                }
            }

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: qsTranslate("SettingsPopup", "Actions")
                    color: page.tokens.muted
                    font.pixelSize: 11
                }

                Flow {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: Pet.actions

                        Chip {
                            required property string modelData

                            label: page.actionLabel(modelData)
                            selected: Pet.action === modelData
                            onPicked: Pet.preview(modelData)
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 7
                // Scenes are the app's own art and only a shipped pet has them (ADR 0049 / 0080).
                visible: Pet.scenes.length > 0

                Text {
                    text: qsTranslate("SettingsPopup", "Scenes")
                    color: page.tokens.muted
                    font.pixelSize: 11
                }

                Flow {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: Pet.scenes

                        Chip {
                            required property string modelData

                            label: page.sceneLabel(modelData)
                            selected: Pet.scene === modelData
                            onPicked: Pet.setScene(modelData)
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: qsTranslate("SettingsPopup", "Accessories")
                    color: page.tokens.muted
                    font.pixelSize: 11
                }

                Repeater {
                    model: ["head", "face", "body"]

                    Row {
                        id: slotRow

                        required property string modelData

                        width: petSection.width
                        spacing: 10

                        Text {
                            width: 52
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.slotLabel(slotRow.modelData)
                            color: page.tokens.faint
                            font.pixelSize: 11
                        }

                        Flow {
                            width: slotRow.width - 62
                            spacing: 6

                            Repeater {
                                model: Pet.accessories.filter(item => item.slot === slotRow.modelData)

                                Chip {
                                    required property var modelData

                                    label: page.accessoryLabel(modelData.id)
                                    selected: Pet.wornIn[slotRow.modelData] === modelData.id
                                    onPicked: Pet.toggleAccessory(modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

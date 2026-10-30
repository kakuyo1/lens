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
        case "cat_yarn": return qsTranslate("SettingsPopup", "Yarn ball");
        case "cat_fishbowl": return qsTranslate("SettingsPopup", "Goldfish bowl");
        case "cloud_rainbow": return qsTranslate("SettingsPopup", "Rainbow slide");
        case "cloud_dandelion": return qsTranslate("SettingsPopup", "Dandelion");
        case "dog_snowman": return qsTranslate("SettingsPopup", "Snowman");
        case "dog_bed": return qsTranslate("SettingsPopup", "Dog bed");
        case "ghost_clock": return qsTranslate("SettingsPopup", "Midnight clock");
        case "ghost_tea": return qsTranslate("SettingsPopup", "Ghost tea");
        case "jelly_lantern": return qsTranslate("SettingsPopup", "Deep-sea lamp");
        case "jelly_shell": return qsTranslate("SettingsPopup", "Shell bed");
        case "mochi_kotatsu": return qsTranslate("SettingsPopup", "Kotatsu");
        case "mochi_pound": return qsTranslate("SettingsPopup", "Mochi pounding");
        case "owl_hearth": return qsTranslate("SettingsPopup", "By the fire");
        case "owl_letter": return qsTranslate("SettingsPopup", "Mail owl");
        case "dizzy": return qsTranslate("SettingsPopup", "Dizzy");
        default: return id;
        }
    }

    function accessoryLabel(id) {
        switch (id) {
        case "flat_cap": return qsTranslate("SettingsPopup", "Newsboy cap");
        case "beret": return qsTranslate("SettingsPopup", "Berry beret");
        case "knit_beanie": return qsTranslate("SettingsPopup", "Knit beanie");
        case "spectacles": return qsTranslate("SettingsPopup", "Round frames");
        case "round_shades": return qsTranslate("SettingsPopup", "Round shades");
        case "heart_frames": return qsTranslate("SettingsPopup", "Heart frames");
        case "bandana": return qsTranslate("SettingsPopup", "Bandana");
        case "knit_scarf": return qsTranslate("SettingsPopup", "Knit scarf");
        case "backpack": return qsTranslate("SettingsPopup", "Backpack");
        case "kanzashi": return qsTranslate("SettingsPopup", "Sakura pin");
        case "hachimaki": return qsTranslate("SettingsPopup", "Hachimaki");
        case "chasen": return qsTranslate("SettingsPopup", "Chasen");
        case "glasses": return qsTranslate("SettingsPopup", "Round glasses");
        case "candy_shades": return qsTranslate("SettingsPopup", "Candy shades");
        case "blush": return qsTranslate("SettingsPopup", "Blush");
        case "nori": return qsTranslate("SettingsPopup", "Nori wrap");
        case "furoshiki": return qsTranslate("SettingsPopup", "Furoshiki");
        case "bean_pouch": return qsTranslate("SettingsPopup", "Bean pouch");
        case "mortarboard": return qsTranslate("SettingsPopup", "Mortarboard");
        case "nightcap": return qsTranslate("SettingsPopup", "Nightcap");
        case "quill_clip": return qsTranslate("SettingsPopup", "Quill clip");
        case "readers": return qsTranslate("SettingsPopup", "Reading frames");
        case "sleep_mask": return qsTranslate("SettingsPopup", "Sleep mask");
        case "satchel": return qsTranslate("SettingsPopup", "Book satchel");
        case "scarf": return qsTranslate("SettingsPopup", "Scarf");
        case "bow_tie": return qsTranslate("SettingsPopup", "Bow tie");
        case "boat_hat": return qsTranslate("SettingsPopup", "Paper boat");
        case "fish_clip": return qsTranslate("SettingsPopup", "Sardine clip");
        case "flower_crown": return qsTranslate("SettingsPopup", "Flower crown");
        case "wire_glasses": return qsTranslate("SettingsPopup", "Cat-eye frames");
        case "cat_shades": return qsTranslate("SettingsPopup", "Cat-eye shades");
        case "heart_blush": return qsTranslate("SettingsPopup", "Heart blush");
        case "fish_collar": return qsTranslate("SettingsPopup", "Fishbone collar");
        case "sailor_collar": return qsTranslate("SettingsPopup", "Sailor collar");
        case "bell_collar": return qsTranslate("SettingsPopup", "Bell collar");
        case "witch_hat": return qsTranslate("SettingsPopup", "Witch hat");
        case "moon_clip": return qsTranslate("SettingsPopup", "Moon clip");
        case "halo": return qsTranslate("SettingsPopup", "Halo");
        case "monocle": return qsTranslate("SettingsPopup", "Monocle");
        case "star_frames": return qsTranslate("SettingsPopup", "Star frames");
        case "mustache": return qsTranslate("SettingsPopup", "Mustache");
        case "key_chain": return qsTranslate("SettingsPopup", "Key chain");
        case "candle": return qsTranslate("SettingsPopup", "Candle");
        case "mist_scarf": return qsTranslate("SettingsPopup", "Mist scarf");
        case "bubble_crown": return qsTranslate("SettingsPopup", "Bubble crown");
        case "starfish_clip": return qsTranslate("SettingsPopup", "Starfish clip");
        case "sailor_cap": return qsTranslate("SettingsPopup", "Sailor cap");
        case "bubble_glasses": return qsTranslate("SettingsPopup", "Bubble frames");
        case "dive_mask": return qsTranslate("SettingsPopup", "Dive mask");
        case "bubble_cheeks": return qsTranslate("SettingsPopup", "Bubble cheeks");
        case "pearls": return qsTranslate("SettingsPopup", "Pearl strand");
        case "shell_necklace": return qsTranslate("SettingsPopup", "Shell necklace");
        case "fish_charm": return qsTranslate("SettingsPopup", "Fish charm");
        case "storm_cap": return qsTranslate("SettingsPopup", "Storm cap");
        case "star_beanie": return qsTranslate("SettingsPopup", "Star beanie");
        case "rain_hat": return qsTranslate("SettingsPopup", "Rain hat");
        case "raindrop_glasses": return qsTranslate("SettingsPopup", "Raindrop frames");
        case "cloud_frames": return qsTranslate("SettingsPopup", "Cloud frames");
        case "snow_freckles": return qsTranslate("SettingsPopup", "Snow freckles");
        case "rain_chain": return qsTranslate("SettingsPopup", "Rain chain");
        case "bolt_charm": return qsTranslate("SettingsPopup", "Bolt charm");
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

    /// The scenes an accessory tilts toward, in the order the reader is most likely to see them.
    function sceneWeightNames(weights) {
        return Object.keys(weights)
            .sort((left, right) => weights[right] - weights[left])
            .map(id => page.sceneLabel(id))
            .join(" / ")
    }

    /// One line for the accessories settings: what this piece nudges the pet toward, or "" when it has no such pull.
    function accessoryTemperament(accessory) {
        if (!accessory.sceneWeights)
            return "";
        const names = page.sceneWeightNames(accessory.sceneWeights);
        if (names === "")
            return "";
        return qsTranslate("SettingsPopup", "%1: often heads to %2").arg(page.accessoryLabel(accessory.id)).arg(names);
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

    /// Why a package was refused, in the words UI-SPEC.md 4.12 gives. The package never supplies these strings.
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

    // Each time the section is opened the page is built again, so this is the rescan UI-SPEC.md 4.4 asks for.
    Component.onCompleted: Pet.picker.refresh()

    /// The postcard a hovered pet row raises beside the list. Every option carries whether it is a package, so the
    /// card needs nothing from this page but the option itself.
    Component {
        id: petPostcardComponent
        PetPostcardPreview {}
    }

    PetPackageDialog {
        id: packageDialog

        onEnableRequested: Pet.picker.confirmPet(packageDialog.petId)
    }

    Connections {
        target: Pet.picker

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
                    objectName: "petTypeField"
                    width: parent.width
                    accessibleName: qsTranslate("SettingsPopup", "Pet type")
                    options: Pet.picker.pets.map(entry => ({
                        value: entry.id,
                        label: entry.external ? entry.name : page.petLabel(entry.id),
                        group: "",
                        note: entry.external ? qsTranslate("SettingsPopup", "External") : "",
                        // A package has no postcard: the card is the app's picture of the pets it came with.
                        external: entry.external
                    }))
                    currentValue: Pet.picker.pet
                    optionPreview: petPostcardComponent
                    onPicked: (value) => Pet.picker.setPet(value)
                }
            }

            // The packages sit right under the picker that lists them: a refused package is an error the reader must not
            // have to scroll to find (UI-SPEC.md 4.4).
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
                    onPicked: Pet.picker.openPacksFolder()
                }

                Repeater {
                    model: Pet.picker.refusals

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
                            selected: Pet.pose.action === modelData
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
                            selected: Pet.pose.scene === modelData
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

                // Only a worn piece with sceneWeights speaks here; taking it off removes its line again (ADR 0102).
                Repeater {
                    model: {
                        const lines = [];
                        for (const item of Pet.accessories) {
                            if (Pet.wornIn[item.slot] !== item.id)
                                continue;
                            const line = page.accessoryTemperament(item);
                            if (line !== "")
                                lines.push(line);
                        }
                        return lines;
                    }

                    Text {
                        objectName: "accessoryTemperament"
                        required property string modelData

                        width: parent.width
                        text: modelData
                        wrapMode: Text.WordWrap
                        color: page.tokens.faint
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}

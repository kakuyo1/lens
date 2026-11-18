pragma ComponentBehavior: Bound

import QtQuick

/**
 * The career statistics page (UI-SPEC.md 4.4): lifetime counters over the whole behavior log,
 * one key-value row per counter, the way a game's statistics screen lists a career. Every counter
 * is listed even at zero; the numbers come from core/telemetry/career in one pass over the months
 * (docs/adr/0194). The page scrolls in the panel body like every other settings page.
 */
Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    readonly property var openChildRect: null

    /// Asking the panel before the irreversible clear; the panel owns the question window.
    signal confirmRequested(string heading, string message, string confirmLabel, var action)

    function closeChild() {}

    /// The counters as the duty last counted them; empty until the first read lands.
    property var counters: ({})

    function reload() {
        page.counters = Controller.telemetryDuty.career();
    }

    function count(key) {
        const value = page.counters[key];
        return value === undefined ? 0 : value;
    }

    /// A duration as the career reads it: whole minutes under an hour, hours and minutes over.
    function durationText(ms) {
        const minutes = Math.round(ms / 60000);
        if (minutes < 60)
            return qsTranslate("SettingsPopup", "%1 min").arg(minutes);
        return qsTranslate("SettingsPopup", "%1 h %2 min").arg(Math.floor(minutes / 60)).arg(minutes % 60);
    }

    function valueText(row) {
        const value = page.count(row.key);
        if (row.duration)
            return page.durationText(value);
        return Number(value).toLocaleString(Qt.locale(), "f", 0);
    }

    /// The table: one group per feature family, one row per counter id the career carries.
    readonly property var groups: [
        {title: qsTranslate("SettingsPopup", "General"), rows: [
            {key: "starts", label: qsTranslate("SettingsPopup", "App launches")},
            {key: "uptimeMs", label: qsTranslate("SettingsPopup", "Time in use"), duration: true},
            {key: "events", label: qsTranslate("SettingsPopup", "Recorded events")}]},
        {title: qsTranslate("SettingsPopup", "Lookups"), rows: [
            {key: "pops", label: qsTranslate("SettingsPopup", "Words looked up")},
            {key: "pop.selection", label: qsTranslate("SettingsPopup", "Selection lookups")},
            {key: "pop.hover", label: qsTranslate("SettingsPopup", "Hover lookups")},
            {key: "pop.scan", label: qsTranslate("SettingsPopup", "Scan lookups")},
            {key: "pop.words", label: qsTranslate("SettingsPopup", "Word-list lookups")},
            {key: "pop.new", label: qsTranslate("SettingsPopup", "New words popped")},
            {key: "pop.known", label: qsTranslate("SettingsPopup", "Known words popped")},
            {key: "peeks", label: qsTranslate("SettingsPopup", "Mother-tongue peeks")}]},
        {title: qsTranslate("SettingsPopup", "Verdicts"), rows: [
            {key: "verdict.known", label: qsTranslate("SettingsPopup", "Marked known")},
            {key: "verdict.mastered", label: qsTranslate("SettingsPopup", "Marked mastered")},
            {key: "verdict.new", label: qsTranslate("SettingsPopup", "Reset to new")}]},
        {title: qsTranslate("SettingsPopup", "Scan & OCR"), rows: [
            {key: "scan.sessions", label: qsTranslate("SettingsPopup", "Auto scan sessions")},
            {key: "scan.words", label: qsTranslate("SettingsPopup", "Words scanned")},
            {key: "scan.durationMs", label: qsTranslate("SettingsPopup", "Time scanning"), duration: true},
            {key: "ocr.captures", label: qsTranslate("SettingsPopup", "OCR captures")},
            {key: "ocr.ok", label: qsTranslate("SettingsPopup", "OCR captures succeeded")}]},
        {title: qsTranslate("SettingsPopup", "Review"), rows: [
            {key: "review.shown", label: qsTranslate("SettingsPopup", "Review questions")},
            {key: "review.answered", label: qsTranslate("SettingsPopup", "Review answers")},
            {key: "review.known", label: qsTranslate("SettingsPopup", "Answered remembered")},
            {key: "review.new", label: qsTranslate("SettingsPopup", "Answered forgotten")},
            {key: "review.dismissed", label: qsTranslate("SettingsPopup", "Reviews skipped")}]},
        {title: qsTranslate("SettingsPopup", "Model"), rows: [
            {key: "llm.calls", label: qsTranslate("SettingsPopup", "Model requests")},
            {key: "llm.ok", label: qsTranslate("SettingsPopup", "Requests succeeded")},
            {key: "llm.failed", label: qsTranslate("SettingsPopup", "Requests failed")},
            {key: "llm.promptTokens", label: qsTranslate("SettingsPopup", "Prompt tokens")},
            {key: "llm.completionTokens", label: qsTranslate("SettingsPopup", "Completion tokens")},
            {key: "llm.explain", label: qsTranslate("SettingsPopup", "Explanations")},
            {key: "llm.peek", label: qsTranslate("SettingsPopup", "Peek requests")},
            {key: "llm.sense", label: qsTranslate("SettingsPopup", "Sense requests")},
            {key: "llm.breakdown", label: qsTranslate("SettingsPopup", "Sentence breakdowns")},
            {key: "llm.correction", label: qsTranslate("SettingsPopup", "Corrections")},
            {key: "llm.portrait", label: qsTranslate("SettingsPopup", "Reading portraits")},
            {key: "llm.story", label: qsTranslate("SettingsPopup", "Stories")},
            {key: "llm.example", label: qsTranslate("SettingsPopup", "Example sentences")}]}
    ]

    implicitHeight: content.implicitHeight

    Component.onCompleted: page.reload()

    Connections {
        target: Controller.telemetryDuty
        function onCleared() {
            page.reload();
        }
    }

    Column {
        id: content
        width: parent.width
        spacing: 16

        Item {
            width: parent.width
            height: 27

            Chip {
                id: refreshChip
                objectName: "refreshCareerButton"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                label: qsTranslate("SettingsPopup", "Refresh")
                onPicked: page.reload()
            }

            Text {
                objectName: "careerTotalLine"
                anchors.left: parent.left
                anchors.right: refreshChip.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("SettingsPopup", "%1 events").arg(page.count("events"))
                      + (page.count("firstTs") > 0
                         ? " · " + qsTranslate("SettingsPopup", "since %1").arg(Qt.formatDate(new Date(page.count("firstTs")), "yyyy-MM-dd"))
                         : "")
                color: page.tokens.muted
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        Text {
            objectName: "careerEmptyHint"
            width: parent.width
            visible: page.count("events") === 0
            height: visible ? implicitHeight : 0
            wrapMode: Text.Wrap
            text: qsTranslate("SettingsPopup", "No behavior records yet. Events appear here as you look up words, scan pages and answer reviews.")
            color: page.tokens.faint
            font.pixelSize: 11
        }

        Repeater {
            model: page.groups

            Column {
                id: group

                required property var modelData

                width: parent.width
                spacing: 4

                Text {
                    text: group.modelData.title
                    color: page.tokens.muted
                    font.pixelSize: 12
                }

                Column {
                    width: parent.width

                    Repeater {
                        model: group.modelData.rows

                        Item {
                            id: row

                            required property var modelData
                            required property int index

                            width: parent.width
                            height: 30

                            Accessible.role: Accessible.StaticText
                            Accessible.name: row.modelData.label + ", " + page.valueText(row.modelData)

                            Rectangle {
                                visible: row.index > 0
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                height: 1
                                color: page.tokens.line2
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.right: valueText.left
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.modelData.label
                                color: page.tokens.text
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }

                            Text {
                                id: valueText
                                objectName: "careerValue." + row.modelData.key
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.valueText(row.modelData)
                                color: page.tokens.text
                                font.family: page.tokens.monoFamily
                                font.pixelSize: 13
                                font.weight: Font.Bold
                            }
                        }
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: Math.max(clearChip.height, privacyText.height)

            Chip {
                id: clearChip
                objectName: "clearEventDataButton"
                anchors.right: parent.right
                anchors.top: parent.top
                label: qsTranslate("SettingsPopup", "Clear")
                onPicked: page.confirmRequested(
                    qsTranslate("SettingsPopup", "Clear behavior data"),
                    qsTranslate("SettingsPopup", "The local months are removed, and their encrypted cloud copies are deleted at the next sync."),
                    qsTranslate("SettingsPopup", "Clear"),
                    () => Controller.telemetryDuty.clear())
            }

            Text {
                id: privacyText
                anchors.left: parent.left
                anchors.right: clearChip.left
                anchors.rightMargin: 10
                anchors.top: parent.top
                wrapMode: Text.Wrap
                text: qsTranslate("SettingsPopup", "Records stay on this device. Encrypted copies go to your own sync account.")
                color: page.tokens.faint
                font.pixelSize: 11
            }
        }
    }
}
import QtQuick.Dialogs

/**
 * The words popup's export (UI-SPEC.md section 4.8). It is apart from WordsPopup.qml because that
 * file is at the size cap (docs/QML.md section 1). The strings keep the WordsPopup context, so
 * their existing translations carry over.
 */
FileDialog {
    id: root

    /// The words popup's segment: "all", "known" or "new". The export and its file name follow it.
    required property string scope

    // QtQuick.Dialogs names a file but cannot write one -- Controller.saveWords() takes the answer and
    // logs a failed write. Native on purpose: choosing a file is the system's business, unlike
    // confirming a removal.
    title: qsTranslate("WordsPopup", "Export words")
    fileMode: FileDialog.SaveFile
    defaultSuffix: "txt"
    // The card formats carry each word's cached explanation; saveWords() reads the extension to pick one.
    nameFilters: [
        qsTranslate("WordsPopup", "Text files (*.txt)"),
        qsTranslate("WordsPopup", "JSON files (*.json)"),
        qsTranslate("WordsPopup", "CSV with explanations (*.csv)"),
        qsTranslate("WordsPopup", "Anki TSV with explanations (*.tsv)"),
        qsTranslate("WordsPopup", "All files (*)")
    ]
    // The scope is in the name because the scope is what the export is: the reader who
    // exports twice from two filters gets two files rather than one overwriting the other.
    currentFile: "words-" + root.scope + ".txt"
    onAccepted: Controller.saveWords(root.selectedFile, root.scope)
}

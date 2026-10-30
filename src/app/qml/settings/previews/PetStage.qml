import QtQuick
import QtQuick.Window

/**
 * The settings preview: the same pet as the desktop, on a baseline, at the size the reader chose. The clock
 * runs only while this stage is on a visible settings window, so a closed settings panel costs nothing.
 *
 * It shares Pet with the desktop window, so an action picked here plays on the desktop too while the
 * settings stay open. That is the one pet; a second clock would only fight the first for frames.
 *
 * While the ask action plays, the review sign stands beside the pet on the stage's foot line as it does on the desktop
 * (UI-SPEC.md 4.13), so the reader sees what the action is for. The pet stays where it is; only the sign moves, to stay on the stage.
 */
Rectangle {
    id: stage
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    readonly property bool onScreen: visible && Window.window !== null && Window.window.visible
    readonly property int petSize: Math.round(Pet.pose.canvas * Pet.pose.scale)
    readonly property bool signShown: Pet.pose.action === "ask" && sign.word !== ""
    /// Where the figure sits: centred on the art's ink, as it always was.
    readonly property real figureX: stage.width / 2 - Pet.pose.inkCentre * Pet.pose.scale
    readonly property real figureY: stage.height - stage.petSize - 12
    implicitHeight: petSize + 24
    radius: stage.tokens.radiusGroup
    color: stage.tokens.panel2
    border.width: 1
    border.color: stage.tokens.line2
    clip: true

    onOnScreenChanged: Pet.setPreviewing(onScreen)
    Component.onCompleted: Pet.setPreviewing(onScreen)
    // Leaving the page (another category) ends the preview too, as closing the window does.
    Component.onDestruction: Pet.setPreviewing(false)

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        height: 1
        color: stage.tokens.line
    }

    /// The sign's placement on this stage, with the stage as the screen and no card counted: the side it stands on beside
    /// the pet, and where its board sits on that side.
    ReviewSignLayout {
        id: layout

        scale: Pet.pose.scale
        figure: Qt.point(stage.figureX, stage.figureY)
        ink: Pet.pose.inkBox
        area: ({left: 0, top: 0, right: stage.width, bottom: stage.height})
        word: Controller.settingPreview.word || ""
        reviewSign: Pet.pose.reviewSign
        // The card pet-review.html draws beside the board: .btn 34 px tall, 8 px padding and a 1 px border on each side.
        // The preview draws no card, so the card is not counted.
        cardWidth: 210
        cardHeight: 52
        withCard: false
    }
    readonly property var choice: layout.choose()
    readonly property point signAt: choice ? layout.boardAt(choice.side, layout.nudgeFor(choice.side, choice.on)) : Qt.point(0, 0)

    PetScene {
        id: scene

        x: stage.figureX
        y: stage.figureY
    }

    ReviewSignBoard {
        id: sign

        visible: stage.signShown
        word: Controller.settingPreview.word || ""
        petScale: Pet.pose.scale
        designWidth: layout.designWidth
        reviewSign: Pet.pose.reviewSign
        bin: layout.bin
        x: Math.round(stage.signAt.x - layout.bleed)
        y: Math.round(stage.signAt.y - layout.bleed)
    }

}

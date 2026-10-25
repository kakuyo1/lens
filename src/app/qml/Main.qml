import QtQuick
import QtQuick.Window

/**
 * The root, and the only place the surfaces are tied to the controller.
 *
 * Not a main window: a 1x1 transparent window kept off every screen, owning the surfaces and being
 * the engine's root object. It still has to be shown -- a window declared inside another becomes its
 * transient child, and Windows keeps a transient child off screen while its parent is hidden. Every
 * surface is a window of its own (UI-SPEC.md's "each surface is independent, sharing no window").
 * Placement and panel routing live in SurfaceHost.qml; ScreenPoints.qml and dev/ProfileScenario.qml
 * hold the other two helpers, keeping this file under the QML size cap.
 */
Window {
    id: root

    width: 1
    height: 1
    x: 0
    y: 0
    opacity: 0
    visible: true
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput

    property int selectionBarGeneration: 0

    /// The colour table follows the stored theme, whichever surface is showing.
    Binding {
        target: Tokens
        property: "theme"
        value: Controller.settings.theme
    }

    /// The liquid theme's text colour, read from the same document as the theme.
    Binding {
        target: Tokens
        property: "liquidTextColor"
        value: Controller.settings.liquidTextColor
    }


    // Debug-only automation called by main.cpp when QT_QML_DEBUG and the profiling scenario
    // environment flag are both enabled. The steps are in ProfileScenario.qml.
    function profileScenario(step) {
        profile.run(step);
    }


    SelectionBar { id: bar }
    Bubble {
        id: bubble
        objectName: "bubble"
        onPlacementRequested: (payload) => surfaceHost.placeBubble(bubble, payload)
        onHeightChanged: surfaceHost.keepOnScreen(bubble)
    }
    /// The pet's right-click opens the panel on the page that carries the pet's block, beside the pet, on the side
    /// chosen from where the pet is at that click; the tray's entry opens it without choosing a page, at the tray icon.
    PetWindow {
        id: petWindow
        onSettingsRequested: {
            surfaceHost.showSettings(true);
            // After, not before: the panel clears its own category every time it comes up, so it opens on the list.
            settingsPopup.openCategory("extensions");
        }
    }

    /// The pet's review sign (UI-SPEC.md 4.13) and the card that answers it. Passive like the pet it hangs from.
    ReviewSignWindow {
        id: reviewSign
        petWindow: petWindow
        workAreaAt: point => surfaceHost.workArea(surfaceHost.screenFor(point))
    }

    /// The debug profiling steps, driven by main.cpp through profileScenario() (ProfileScenario.qml).
    ProfileScenario {
        id: profile

        host: surfaceHost
        trayMenu: trayMenu
        bubble: bubble
        bar: bar
    }

    /// The mouse hook's native-pixel points, mapped to the DIPs the surfaces are placed in (ScreenPoints.qml).
    ScreenPoints {
        id: points

        host: surfaceHost
    }

    Notice {
        id: notice
        // The word the notice is about, and the cost of asking for it, are both the controller's.
        onActionTriggered: Controller.explainLemma(notice.noticeLemma)
    }

    /// A new version's card. Passive: not in the panels' mutual exclusion above, and not on
    /// the explanation notice slot, so it neither takes a notice's place nor loses one to it.
    UpdateCard {
        id: updateCard
        onViewRequested: Controller.updateDuty.openReleasePage()
        onSkipRequested: Controller.updateDuty.skipUpdate()
        onDownloadRequested: Controller.updateDuty.downloadUpdate()
        onCancelRequested: Controller.updateDuty.cancelDownload()
        onInstallRequested: Controller.updateDuty.installUpdate()
    }

    /// The capture mask, raised by the global trigger key and by nothing else. What it gives back is
    /// a rectangle for the capture duty; that it is also a surface is why it is here and not in the
    /// settings panel that owns the key.
    Mask {
        id: mask
        // The one place the mask and the capture duty meet: the rectangle comes back in desktop
        // coordinates and goes straight to the only thing that can take a shot of it. Neither side
        // knows the other exists; this wire once went missing with no test to catch it.
        onRegionChosen: (region) => Controller.captureRegion(region)
    }

    SettingsPopup {
        id: settingsPopup
        onWidthChanged: surfaceHost.keepOnScreen(settingsPopup)
        onHeightChanged: surfaceHost.keepOnScreen(settingsPopup)
    }

    StatsPopup {
        id: statsPopup
        onCostRequested: surfaceHost.showCost(statsPopup)
        onWordsRequested: surfaceHost.showWords(statsPopup)
        onKnownRequested: surfaceHost.showWords(statsPopup, 1)
        onNewRequested: surfaceHost.showWords(statsPopup, 2)
        onWidthChanged: surfaceHost.holdCorner(statsPopup)
        onHeightChanged: surfaceHost.holdCorner(statsPopup)
    }

    CostPopup {
        id: costPopup
        onBackRequested: surfaceHost.showStats(costPopup)
        onWidthChanged: surfaceHost.holdCorner(costPopup)
        onHeightChanged: surfaceHost.holdCorner(costPopup)
    }

    WordsPopup {
        id: wordsPopup
        onYearRequested: surfaceHost.showYearWords(wordsPopup)
        onBackRequested: surfaceHost.showStats(wordsPopup)
        onWidthChanged: surfaceHost.holdCorner(wordsPopup)
        onHeightChanged: surfaceHost.holdCorner(wordsPopup)
    }

    YearWordsPopup {
        id: yearWordsPopup
        onBackRequested: surfaceHost.showWords(yearWordsPopup)
        onWidthChanged: surfaceHost.holdCorner(yearWordsPopup)
        onHeightChanged: surfaceHost.holdCorner(yearWordsPopup)
    }

    TrayMenu {
        id: trayMenu
        onStatsRequested: surfaceHost.showStats(trayMenu)
        onSettingsRequested: surfaceHost.showSettings()
        onWidthChanged: surfaceHost.keepOnScreen(trayMenu)
        onHeightChanged: surfaceHost.keepOnScreen(trayMenu)
    }

    /// The placement and panel rules live in SurfaceHost.qml, keeping this file under the QML size cap.
    SurfaceHost {
        id: surfaceHost
        objectName: "surfaceHost"

        screen: root.screen
        bar: bar
        petWindow: petWindow
        reviewSign: reviewSign
        trayMenu: trayMenu
        settingsPopup: settingsPopup
        statsPopup: statsPopup
        wordsPopup: wordsPopup
        yearWordsPopup: yearWordsPopup
        costPopup: costPopup
    }

    /// Polls the screen layout, so a panel follows a monitor change.
    Timer {
        interval: 250
        repeat: true
        running: true
        onTriggered: surfaceHost.checkScreenLayout()
    }

    Connections {
        target: Controller

        function onSelectionBarRequested(payload) {
            const generation = ++root.selectionBarGeneration;
            // The minimum is a letter count: an accidental drag over two letters of English must not
            // raise the bar. Chinese is counted in characters, one of which is a word on its own, so
            // no floor reaches it -- a two-character phrase, the common Chinese word size, never would.
            if (payload.kind !== "translation" && payload.letterCount < Controller.settings.minimumWordLength) {
                bar.dismiss();
                return;
            }

            const at = points.toDip(payload);
            bar.selectionText = payload.text;
            bar.selectionKind = payload.kind;
            // A bar that is away stays hidden until it has been placed, rather than showing at its
            // last spot -- the screen corner, the first time -- and moving a frame later. One already
            // up is moved in the same turn, so a second selection does not flash it out and in.
            // open() is two steps for the reason placePanel() gives: `visible` alone can leave a
            // topmost window under the taskbar.
            if (!bar.surfaceShown)
                bar.visible = false;
            Qt.callLater(() => {
                if (generation !== root.selectionBarGeneration)
                    return;
                surfaceHost.placeSelectionBar(bar, at);
                bar.open();
            });
        }

        function onCaptureMaskRequested() {
            mask.open();
        }

        function onPointerPressed(at) {
            ++root.selectionBarGeneration;
            surfaceHost.dismissOutside(points.toDip(at));
        }
    }

    Connections {
        target: Controller.explanationDuty

        function onBubbleChanged() {
            const payload = Controller.explanationDuty.bubble;
            if (payload && Object.keys(payload).length > 0) {
                const at = points.toDip(payload);
                bubble.show(at);
                // An answer arrives a field at a time, so this runs several times for one card; a card
                // being dragged must not be pulled back to its anchor (repositionVisible keeps the
                // same rule for a screen change).
                Qt.callLater(() => {
                    // A language switch replays the answer in the frame the card already has:
                    // asking for placement here would move it as the new text changes height.
                    if (!bubble.dragging && !bubble.languageReplay)
                        surfaceHost.placeBubble(bubble, at);
                });
            } else {
                bubble.dismiss();
            }
        }

        function onChainAnswered(entry) {
            bubble.chainSteps.receive(entry);
        }

        function onChainFailed(lemma) {
            bubble.chainSteps.fail(lemma);
        }

        function onNoticeChanged() {
            const payload = Controller.explanationDuty.notice;
            if (payload && Object.keys(payload).length > 0)
                notice.show(payload);
            else
                notice.dismiss();
        }
    }

    Connections {
        target: Controller.updateDuty

        /// Raised and taken down from the one flag the duty holds. `show()` centres the window, so
        /// it only runs when there is something new to show: the signal also carries the settings
        /// switch and state the card does not draw, and re-centring would move the card out from
        /// under a reader looking at it.
        function onUpdateChanged() {
            if (!Controller.updateDuty.cardVisible) {
                updateCard.dismiss();
                return;
            }
            const payload = Controller.updateDuty.update;
            if (updateCard.visible && updateCard.version === payload.latest)
                return;
            updateCard.show({version: payload.latest, pageUrl: payload.pageUrl, notes: payload.notes});
        }
    }

    Connections {
        target: Tray
        function onMenuRequested() {
            surfaceHost.showTrayMenu();
        }
    }
}

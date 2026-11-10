import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The action bar's dismissal by a press elsewhere: which parts of its window count as the bar.
 *
 * The window is larger than the bar -- a shadow margin all round, and the side a drawer may open to
 * (reach) -- and the tag's body carries a 16 px strip beside its string. A press in any of that blank
 * room is a press outside the bar and puts it down; a press on the card, or on the drawer while it is
 * out, is a press on the bar and leaves it up. SurfaceHost.dismissOutside() reads containsPoint() for
 * that, instead of a rectangle drawn around the window.
 *
 * Driven through Main because dismissOutside is the host's; the press itself is the mouse hook's, and
 * no offscreen case injects one, so the cases hand the host the point the hook would report.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "BarDismiss"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// The root is a Window rather than an Item, so a surface is reached by type.
        function surface(main, typeName) {
            const found = Util.ofType(main, typeName);
            verify(found, "Main did not build a " + typeName);
            return found;
        }

        /// The bar's window is larger than the bar: a shadow margin all round and the side a drawer may
        /// open to are blank. A press there used to count as a press on the bar and left it up; it is a
        /// press outside the bar, and the bar answers with its own shape -- the body, and the drawer
        /// while it is out -- rather than with a rectangle around the window.
        function test_aPressOnTheBlankRoomAroundTheBarClosesIt() {
            const main = make();
            const bar = surface(main, "SelectionBar");
            const host = Util.surfaceHost(main);
            Controller.selectionBarRequested({ x: 300, y: 300, kind: "sentence",
                                               text: "a sentence the breakdown can take",
                                               letterCount: 31, breakdownable: true });
            tryCompare(bar, "visible", true);
            // The fade ends with the card at its final place, which is the geometry these presses read.
            wait(Tokens.motion.pop + 80);

            const body = Qt.point(bar.x + bar.originX + bar.bodyItem.width / 2,
                                  bar.y + bar.originY + bar.bodyItem.height / 2);
            verify(bar.containsPoint(body), "a press on the bar read as outside it");
            host.dismissOutside(body);
            compare(bar.surfaceShown, true, "a press on the bar put it down");

            const drawer = bar.bodyItem.drawer;
            verify(drawer, "the sentence's bar drew no drawer");
            drawer.expand();
            wait(50);
            const at = drawer.mapToItem(bar.contentItem, drawer.width / 2, drawer.height / 2);
            verify(bar.containsPoint(Qt.point(bar.x + at.x, bar.y + at.y)),
                   "the open drawer read as outside the bar");
            drawer.setOpen(false);
            wait(50);

            // The room the window holds for the drawer is blank while the drawer is shut. The press sits
            // just above the body, inside that room -- inside the window's own bounds, which is what the
            // rectangle around the window read as the bar.
            verify(bar.reach.top > 2, "the window kept no room above the body for the drawer");
            const blank = Qt.point(bar.x + bar.originX + bar.bodyItem.width / 2,
                                   bar.y + bar.originY - 2);
            verify(!bar.containsPoint(blank), "the room the drawer needs read as the bar");
            host.dismissOutside(blank);
            tryCompare(bar, "surfaceShown", false, 1000, "a press on the blank room left the bar up");
            tryCompare(bar, "visible", false, 1000, "the bar never finished leaving");
        }

        /// The tag's body is its card plus the 16 px the string hangs in. Beside the string that strip is
        /// blank, and a press there is a press outside the bar -- with the tag above the selection it is
        /// the strip under the card, which reads as the blank below the tag. The body answers for its
        /// card, not its box.
        function test_aTagPressOnTheBlankStripBesideItsStringClosesIt() {
            const main = make();
            const bar = surface(main, "SelectionBar");
            Controller.setSetting("selectionBarStyle", "tag");
            Controller.selectionBarRequested({ x: 300, y: 300, kind: "sentence",
                                               text: "a sentence the breakdown can take",
                                               letterCount: 31, breakdownable: true });
            tryCompare(bar, "visible", true);
            wait(Tokens.motion.pop + 80);

            // The strip is on the side the string hangs from; the string itself is a hairline at the card's
            // left, so a point 100 px in is blank either way.
            const strip = bar.tagAbove
                        ? Qt.point(bar.x + bar.originX + 100, bar.y + bar.originY + bar.bodyItem.height - 8)
                        : Qt.point(bar.x + bar.originX + 100, bar.y + bar.originY + 8);
            verify(!bar.containsPoint(strip), "the strip beside the string read as the bar");
            Util.surfaceHost(main).dismissOutside(strip);
            tryCompare(bar, "surfaceShown", false, 1000, "a press beside the string left the bar up");
            tryCompare(bar, "visible", false, 1000, "the bar never finished leaving");

            Controller.setSetting("selectionBarStyle", "bar");
        }
    }
}
import QtQuick
import QtTest
import "../../../src/app/qml/util/Numbers.js" as Numbers
import "../../../src/app/qml/util/Format.js" as Format

/// The pure libraries under qml/util: no surface, no controller, no window.
Item {
    width: 100
    height: 100

    TestCase {
        name: "QmlUtil"

        function test_clampHoldsAValueInsideItsRange() {
            compare(Numbers.clamp(5, 0, 10), 5);
            compare(Numbers.clamp(-1, 0, 10), 0);
            compare(Numbers.clamp(11, 0, 10), 10);
            // An empty or reversed range collapses to its low end.
            compare(Numbers.clamp(5, 10, 0), 10);
        }

        function test_formatBytesUsesBinaryUnitsAndADecimalBelowTen() {
            compare(Format.formatBytes(0), "0 B");
            compare(Format.formatBytes(-1), "0 B");
            compare(Format.formatBytes(512), "512 B");
            compare(Format.formatBytes(1536), "1.5 KB");
            compare(Format.formatBytes(1024 * 1024), "1.0 MB");
            compare(Format.formatBytes(12.5 * 1024 * 1024), "13 MB");
        }

        function test_formatMoneyKeepsTwoDecimalsAndTheCurrencyAsGiven() {
            compare(Format.formatMoney("¥", 0), "¥0.00");
            compare(Format.formatMoney("$", 12.5), "$12.50");
            compare(Format.formatMoney("CNY ", 1.236), "CNY 1.24");
        }
    }
}

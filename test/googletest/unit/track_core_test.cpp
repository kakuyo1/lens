/** @file track_core_test.cpp
 * @brief The scan's acquisition policy, driven through TrackCore::step alone over a scripted page
 *        reader: acceptance, anchors, cadences, self-heal and the id contract, with no thread and
 *        no clock of its own -- every timing constant is a step count or a virtual timestamp.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "capture/track_core.h"
#include "capture/word_track.h"

namespace {

using namespace std::chrono_literals;

using lens::capture::Band;
using lens::capture::TrackCore;
using lens::capture::TrackOutput;
using lens::capture::WordRead;

/// The window's page area the scan draws in (the band the duty computes).
constexpr Band kBand{.x = 0, .y = 166, .width = 1921, .height = 854};

/// The clock's origin: far enough from the epoch that every since-epoch gate is due on the first
/// step, the way the track's first tick after a rebuild finds them.
constexpr auto kStart = std::chrono::steady_clock::time_point{} + 10s;

/// @brief One scripted word of the fake page: its spelling and its box, with the box of its first
///        trailing character when the word trails one off. The fake supplies boxes only -- the
///        trimming policy is TrackCore's (docs/adr/0164).
struct Cell {
    std::string         text;
    Band                box{};
    std::optional<Band> junkBox;
};

/// @brief The word at @p box, spelled @p text.
Cell cell(std::string text, Band box)
{
    return {.text = std::move(text), .box = box};
}

/// @brief A word unit that keeps a trailing comma: @p lettersWidth says where the letters end.
Cell commaCell(std::string text, Band box, int lettersWidth)
{
    return {.text    = std::move(text),
            .box     = box,
            .junkBox = Band{.x = box.x + lettersWidth, .y = box.y, .width = 0, .height = box.height}};
}

/// @brief Whether @p box holds the point, the way a page answers a point query.
bool holdsPoint(Band const& box, int const x, int const y)
{
    return x >= box.x and x < box.x + box.width and y >= box.y and y < box.y + box.height;
}

/// @brief A word of the fake page: its index into the script's flat reading order.
class FakeWord final : public lens::capture::Word {
public:
    explicit FakeWord(int const index)
        : index_{index}
    {}
    int index() const
    {
        return index_;
    }

private:
    int index_;
};

/// @brief A page the script holds: a word grid, a page box the script moves or collapses, a present
///        flag, and the text the names are read from.
class FakePage final : public lens::capture::Page {
public:
    std::vector<Cell> cells; ///< Flat reading order: step() walks it, wordAt() searches it.
    Band              pageBox{};
    bool              inTree = true;       ///< present(): a tab switch takes the page out of the tree.
    std::string       textContent;         ///< The document text the names are read from.
    bool              blankMiddle = false; ///< wordAt answers a word whose own box reads empty.
    /// The scroll a chunk read runs into: armed by the test, it shifts the whole grid the first time
    /// a word other than @p shiftExcludes is read, so the chunk's own stability check fails.
    bool armed = false;
    Band shiftBy{};
    int  shiftExcludes = -1;
    int  boxReads      = 0; ///< Every box read, anchor and walked words alike.

    bool present() const override
    {
        return inTree;
    }

    Band box() const override
    {
        return pageBox;
    }

    std::string text() const override
    {
        return textContent;
    }

    std::shared_ptr<lens::capture::Word> wordAt(int const x, int const y) override
    {
        if (blankMiddle) return std::make_shared<FakeWord>(static_cast<int>(cells.size())); // reads no box
        for (std::size_t i = 0; i < cells.size(); ++i)
            if (holdsPoint(cells[i].box, x, y)) return std::make_shared<FakeWord>(static_cast<int>(i));
        return nullptr;
    }

    std::optional<Band> box(lens::capture::Word const& word) override
    {
        int const index = static_cast<FakeWord const&>(word).index();
        if (index >= static_cast<int>(cells.size())) return std::nullopt;
        ++boxReads;
        if (armed and index != shiftExcludes) {
            // The scroll lands mid-chunk: the words read after it sit at the new scroll position.
            armed = false;
            for (auto& c : cells) {
                c.box.x += shiftBy.x;
                c.box.y += shiftBy.y;
            }
        }
        return cells[index].box;
    }

    std::optional<WordRead> read(lens::capture::Word const& word) override
    {
        int const index = static_cast<FakeWord const&>(word).index();
        if (index >= static_cast<int>(cells.size())) return std::nullopt;
        Cell const& c = cells[index];
        return WordRead{.text = c.text, .fullBox = c.box, .lettersEnd = c.junkBox};
    }

    std::shared_ptr<lens::capture::Word> step(lens::capture::Word const& word, int const delta) override
    {
        int const index = static_cast<FakeWord const&>(word).index() + delta;
        if (index < 0 or index >= static_cast<int>(cells.size())) return nullptr;
        return std::make_shared<FakeWord>(index);
    }
};

/// @brief The scripted reader: it answers with the script's page and counts what the track asked of it.
class FakePageReader final : public lens::capture::PageReader {
public:
    std::shared_ptr<FakePage> page;           ///< The page the script holds, when there is one.
    bool                      answers = true; ///< Whether the window answers with a page at all.
    std::vector<std::string>  anyTexts;       ///< The names' source, scripted; the last one repeats.
    int                       acquires = 0;
    int                       drops    = 0;
    std::optional<Band>       pageBoxAfterDrop; ///< The fresh page's box once the cached one is dropped.

    std::shared_ptr<lens::capture::Page> acquire(Band const&) override
    {
        ++acquires;
        if (not answers or not page) return nullptr;
        return page;
    }

    void drop() override
    {
        ++drops;
        if (page and pageBoxAfterDrop) page->pageBox = *pageBoxAfterDrop;
    }

    std::string anyText() override
    {
        if (anyTexts.empty()) return {};
        auto const text = anyTexts.front();
        if (anyTexts.size() > 1) anyTexts.erase(anyTexts.begin());
        return text;
    }
};

/// @brief One step on the 16 ms cadence the production shell keeps; the only way a test drives the core.
TrackOutput step(TrackCore& core, std::chrono::steady_clock::time_point& t)
{
    auto const out = core.step(kBand, t);
    t += 16ms;
    return out;
}

/// @brief The word grid the canonical page holds: an article around the band's middle, with rows
///        below it for the walks to read. The word at the band's middle is "the".
std::vector<Cell> articleGrid()
{
    std::vector<Cell> cells;
    cells.push_back(cell("Richard", Band{.x = 700, .y = 480, .width = 120, .height = 40}));
    cells.push_back(cell("met", Band{.x = 830, .y = 480, .width = 70, .height = 40}));
    cells.push_back(cell("the", Band{.x = 940, .y = 580, .width = 60, .height = 40}));
    cells.push_back(commaCell("cat,", Band{.x = 1020, .y = 580, .width = 70, .height = 40}, 60));
    cells.push_back(cell("and", Band{.x = 1110, .y = 580, .width = 70, .height = 40}));
    cells.push_back(cell("the", Band{.x = 1200, .y = 580, .width = 60, .height = 40}));
    for (int row = 0; row < 9; ++row)
        for (int column = 0; column < 4; ++column)
            cells.push_back(cell("w" + std::to_string(row * 4 + column),
                                 Band{.x = 700 + column * 120, .y = 660 + row * 40, .width = 100, .height = 40}));
    return cells;
}

/// @brief A reader whose page is the article and whose names are known on the first read.
FakePageReader articleReader()
{
    FakePageReader reader;
    reader.page              = std::make_shared<FakePage>();
    reader.pageBoxAfterDrop  = kBand;
    reader.page->cells       = articleGrid();
    reader.page->pageBox     = kBand;
    reader.page->textContent = "Richard met Jeff. The cat sat on the mat.";
    reader.anyTexts          = {"Richard met Jeff. The cat sat on the mat."};
    return reader;
}

/// @brief Drives the track to its first drawn frame. Only a set page can reach here.
void settleToFirstDraw(TrackCore& core, FakePageReader const& reader, std::chrono::steady_clock::time_point& t)
{
    for (int steps = 0; steps < 40; ++steps) {
        auto const out = step(core, t);
        if (out.frame and not out.frame->visible.empty()) return;
        ASSERT_TRUE(reader.page) << "the track lost its page before it drew";
    }
    ADD_FAILURE() << "the track never drew";
}

TEST(TrackCore, ABackgroundTabSliverIsRefusedUntilThePageFillsTheBand)
{
    FakePageReader reader = articleReader();
    // The iTab sliver a background tab answers with: full width, 77 px at the window's bottom edge
    // (docs/adr/0163). The healthy page is what a search after the drop finds.
    reader.page->pageBox    = Band{.x = 0, .y = 942, .width = 1921, .height = 77};
    reader.pageBoxAfterDrop = kBand;
    TrackCore   core{reader};
    auto        t = kStart;
    std::string refused;
    bool        anchored = false;

    for (int i = 0; i < 20 and not anchored; ++i) { // 320 ms: the sliver is refused, the next search waits on the slow clock
        auto const out = step(core, t);
        ASSERT_FALSE(out.frame and not out.frame->visible.empty()) << "the sliver drew";
        if (out.report and not out.report->note.empty()) refused = out.report->note;
        if (out.frame and not out.frame->added.empty()) {
            ASSERT_EQ(out.frame->added.front().text, "the");
            anchored = true;
        }
    }
    EXPECT_TRUE(anchored); // over the slow clock the healthy page lands, and the track anchors
    EXPECT_NE(refused.find("refused: it answers 1921x77 in a 1921x854 band"), std::string::npos);
    EXPECT_EQ(reader.drops, 1);    // refused once, forgotten once
    EXPECT_EQ(reader.acquires, 2); // the 250 ms clock held the search to one retry in the 320 ms

    // The article settles and draws.
    bool drew = false;
    for (int i = 0; i < 16 and not drew; ++i) {
        auto const out = step(core, t);
        drew           = static_cast<bool>(out.frame) and not out.frame->visible.empty();
    }
    EXPECT_TRUE(drew);
}

TEST(TrackCore, AStandingPageThatCollapsesToASliverIsDroppedByThePeriodicRecheck)
{
    FakePageReader reader = articleReader();
    TrackCore      core{reader};
    auto           t = kStart;
    settleToFirstDraw(core, reader, t);

    // The page's own reads keep answering: its words ride into the sliver, and only its box
    // collapses. The track keeps drawing in the strip's corner until the 500 ms recheck drops the
    // page -- the incident's own symptom (docs/adr/0163).
    reader.page->pageBox      = Band{.x = 0, .y = 942, .width = 1921, .height = 77};
    reader.page->cells[2].box = Band{.x = 940, .y = 950, .width = 60, .height = 40};
    reader.pageBoxAfterDrop.reset();

    std::optional<TrackOutput> dropped;
    std::string                droppedNote;
    int                        acquiresAtLoss = 0;
    for (int i = 0; i < 64; ++i) { // just past the 500 ms recheck
        auto const out = step(core, t);
        if (out.frame and out.frame->lost) {
            dropped        = out;
            droppedNote    = out.report ? out.report->note : std::string{};
            acquiresAtLoss = reader.acquires;
            break;
        }
    }
    ASSERT_TRUE(dropped);
    EXPECT_TRUE(dropped->frame->added.empty());
    EXPECT_TRUE(dropped->frame->visible.empty());
    EXPECT_NE(droppedNote.find("dropped: it answers 1921x77 in a 1921x854 band"), std::string::npos);
    EXPECT_EQ(reader.drops, 1);
    for (int i = 0; i < 32; ++i)
        step(core, t);                              // the slow clock paces the next search after the drop
    EXPECT_LE(reader.acquires - acquiresAtLoss, 3); // retries on the 250 ms clock: at most three land in 512 ms
}

TEST(TrackCore, ATabSwitchIsSeenByThePeriodicCheckAndLosesTheTrack)
{
    FakePageReader reader = articleReader();
    TrackCore      core{reader};
    auto           t = kStart;
    settleToFirstDraw(core, reader, t);

    reader.page->inTree = false; // the tab switch: the page is no longer a document of its window

    std::optional<TrackOutput> lost;
    for (int i = 0; i < 64; ++i) {
        auto const out = step(core, t);
        if (out.frame and out.frame->lost) {
            lost = out;
            break;
        }
    }
    ASSERT_TRUE(lost);
    EXPECT_TRUE(lost->frame->added.empty());
    EXPECT_TRUE(lost->frame->visible.empty());
    EXPECT_EQ(reader.drops, 1); // the cache is forgotten with the track
}

TEST(TrackCore, AChunkReadDuringAScrollIsDroppedWhole)
{
    FakePageReader reader = articleReader();
    TrackCore      core{reader};
    auto           t = kStart;
    settleToFirstDraw(core, reader, t);

    // Arm the scroll: the next chunk reads across it, and the anchor's own stability check catches
    // the mixed scroll positions.
    int const readsBefore      = reader.page->boxReads;
    reader.page->shiftBy       = Band{.x = 0, .y = 40, .width = 0, .height = 0};
    reader.page->shiftExcludes = 2; // the anchor's own word: its box moves with the grid, not by the arm
    reader.page->armed         = true;

    auto const chunk = step(core, t);
    // Words were read -- the chunk was in flight -- and the chunk is dropped whole: nothing is sent
    // on a step where the anchor moved during the read.
    EXPECT_GT(reader.page->boxReads - readsBefore, 1);
    EXPECT_FALSE(chunk.frame);
    EXPECT_FALSE(chunk.report and not chunk.report->note.empty());

    // The dropped words keep no line and are not re-added: they were marked seen on the way in.
    int reAdded = 0;
    for (int i = 0; i < 32; ++i) {
        auto const out = step(core, t);
        if (out.frame) reAdded += static_cast<int>(out.frame->added.size());
    }
    EXPECT_EQ(reAdded, 0);
}

TEST(TrackCore, AScrollMovesTheAnchorToTheMiddleAndRebasesTheSpans)
{
    FakePageReader reader = articleReader();
    TrackCore      core{reader};
    auto           t = kStart;
    settleToFirstDraw(core, reader, t);

    // A scroll of 400 px: the anchor lands a margin's width from the band's top edge, so it moves to
    // the word the band's middle now holds, and the spans re-base on it.
    for (auto& c : reader.page->cells)
        c.box.y -= 400;

    std::optional<TrackOutput> collapsed;
    for (int i = 0; i < 8 and not collapsed; ++i) {
        auto const out = step(core, t);
        if (out.frame and out.frame->visible.empty() and not out.frame->lost) collapsed = out;
    }
    ASSERT_TRUE(collapsed) << "the scroll never hid the lines";

    // The page holds still again: the lines come back, and every word sits where it is on screen.
    std::optional<TrackOutput> drawn;
    for (int i = 0; i < 16 and not drawn; ++i) {
        auto const out = step(core, t);
        if (out.frame and not out.frame->visible.empty()) drawn = out;
    }
    ASSERT_TRUE(drawn);
    bool anchorDrawn = false;
    for (auto const& placed : drawn->frame->visible)
        if (placed.id == 0) {
            anchorDrawn = true;
            EXPECT_EQ(placed.x, 940);
            EXPECT_EQ(placed.y, 180); // the anchor's own new place on screen
        }
    EXPECT_TRUE(anchorDrawn);

    // The move is counted at the next summary.
    bool sawMoves = false;
    for (int i = 0; i < 80 and not sawMoves; ++i) {
        auto const out = step(core, t);
        if (out.report and out.report->moves > 0) sawMoves = true;
    }
    EXPECT_TRUE(sawMoves);
}

TEST(TrackCore, NamesGateAcquisitionAndEveryWordIsAddedOnce)
{
    FakePageReader reader = articleReader();
    reader.anyTexts       = {"", "Richard met Jeff. The cat sat on the mat."}; // the page answers no text at first
    TrackCore core{reader};
    auto      t = kStart;

    for (int i = 0; i < 62; ++i) { // the first second: the names gate everything, no page is asked for
        auto const out = step(core, t);
        EXPECT_FALSE(out.frame);
        EXPECT_EQ(reader.acquires, 0);
    }

    int                      nameReads = 0;
    bool                     drew      = false;
    std::vector<std::string> added;
    for (int i = 0; i < 8 * 62; ++i) { // eight seconds: the retry lands the names, the page is read
        auto const out = step(core, t);
        if (out.report) nameReads += out.report->nameReads;
        if (out.frame) {
            drew = drew or not out.frame->visible.empty();
            for (auto const& word : out.frame->added)
                added.push_back(word.text);
        }
    }
    EXPECT_EQ(reader.acquires, 1); // the 250 ms clock searched once, and the page stayed cached
    EXPECT_TRUE(drew);
    // Every word once, in reading order past the anchor -- forward to the band's bottom edge, then
    // backward to the words above it: the second "the" is the anchor word again, and "Richard" is a
    // name (docs/adr/0161). The grid words below the band's bottom edge are never read.
    ASSERT_GE(added.size(), 3u);
    EXPECT_EQ(added[0], "the");
    EXPECT_EQ(added[1], "cat"); // the trailing comma is left out of the scan's word
    EXPECT_EQ(added[2], "and");
    EXPECT_EQ(added.size(), 37u); // the anchor, "cat", "and", w0..w32, and "met" from above the anchor
    EXPECT_EQ(std::count(added.begin(), added.end(), "the"), 1);
    EXPECT_EQ(std::count(added.begin(), added.end(), "cat"), 1);
    EXPECT_EQ(std::count(added.begin(), added.end(), "met"), 1);
    EXPECT_EQ(std::count(added.begin(), added.end(), "w33"), 0); // below the band's bottom edge
    EXPECT_EQ(std::count(added.begin(), added.end(), "Richard"), 0);
    // The names are re-read on their own clock: the load, the retry, and one 5 s refresh in eight seconds.
    EXPECT_EQ(nameReads, 3);
}

TEST(TrackCore, EmptyAnchorsAndMissedReadsSelfHeal)
{
    FakePageReader reader    = articleReader();
    reader.page->blankMiddle = true; // the middle of the page reads no word
    TrackCore core{reader};
    auto      t = kStart;

    for (int i = 0; i < 2; ++i) { // an empty anchor or two is kept: the page is not dropped yet
        step(core, t);
        EXPECT_EQ(reader.drops, 0);
    }
    auto const third = step(core, t);
    EXPECT_EQ(reader.drops, 1); // the third empty anchor in a row drops the page for a fresh search
    EXPECT_FALSE(third.frame);

    // The fresh search finds a healthy page, and the track anchors on it.
    reader.page->blankMiddle = false;
    std::optional<TrackOutput> anchored;
    for (int i = 0; i < 20 and not anchored; ++i) {
        auto const out = step(core, t);
        if (out.frame and not out.frame->added.empty()) anchored = out;
    }
    ASSERT_TRUE(anchored);

    // The anchor then reads empty, the way a scroll race reads: the drawn lines hide first, and the
    // track holds on until the sixth missed read.
    settleToFirstDraw(core, reader, t);
    reader.page->cells[2].box = Band{.x = 0, .y = 0, .width = 0, .height = 0};
    auto const firstMissed    = step(core, t);
    ASSERT_TRUE(firstMissed.frame);
    EXPECT_TRUE(firstMissed.frame->visible.empty());
    EXPECT_TRUE(firstMissed.frame->added.empty());
    EXPECT_FALSE(firstMissed.frame->lost);
    std::optional<TrackOutput> lost;
    for (int i = 0; i < 8; ++i) {
        auto const out = step(core, t);
        if (out.frame and out.frame->lost) {
            lost = out;
            break;
        }
    }
    ASSERT_TRUE(lost);
    EXPECT_EQ(lost->frame->added.size(), 0u);
    EXPECT_EQ(reader.drops, 1); // a lost read keeps the page: only the anchor is given up
}

}

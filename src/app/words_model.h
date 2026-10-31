/**
 * @file words_model.h
 * @brief The words popup's rows as an item model: deduplicated, filtered and updated in place.
 *
 * The model owns what the list shows. The controller hands it the history and the known set
 * after every statsChanged; rows are matched by lemma and the difference reaches the view as
 * removals, insertions and data changes, so a removal runs ListView's remove transition and the
 * delegates that stay keep their state. A reorder -- a word re-popped while the list is open
 * -- resets the model; QAbstractListModel's move bookkeeping is not worth it for a change the
 * old list rebuilt every delegate for anyway.
 *
 * The filter is the popup's segment, and its values are core::ExportScope's: a row the list
 * shows is a row an export carries (docs/adr/0122).
 */

#pragma once

#include <QAbstractListModel>
#include <QDate>
#include <QHash>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <string>
#include <unordered_set>
#include <vector>

#include "core/document/stats_store.h"
#include "words_view.h"

namespace lens::app {

class WordsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.wordsModel")
    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int allCount READ allCount NOTIFY allCountChanged)
    /// @brief How long the view takes to close a row. A filter switch waits that long between the
    ///        rows leaving and the rows arriving, so the two never share a line. Zero, the default,
    ///        lands both phases at once, which is what a case wants.
    Q_PROPERTY(int swapMs READ swapMs WRITE setSwapMs NOTIFY swapMsChanged)
    /// @brief Whether a filter switch is between its two phases. The list is the intersection of
    ///        the two sets while it holds, so an empty line must not be drawn yet.
    Q_PROPERTY(bool filterSwitching READ filterSwitching NOTIFY filterSwitchingChanged)
public:
    /// The roles WordsListRow declares. Keep the names and the enum together.
    enum Role {
        WordRole = Qt::UserRole + 1,
        DayRole,
        DateRole,
        TimeRole,
        VerdictRole,
        PopsRole,
    };

    explicit WordsModel(QObject* parent = nullptr);

    int                    rowCount(QModelIndex const& parent = QModelIndex{}) const override;
    QVariant               data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// @return Every row, before the filter, as the maps a case reads; not for the view.
    Q_INVOKABLE QVariantList allRows() const;

    int filter() const
    {
        return filter_;
    }
    /// @brief Show the rows one segment names: the rows the new filter hides close first, and the
    ///        rows it wants arrive after @c swapMs, so no word is drawn under a closing one.
    void setFilter(int value);
    int  allCount() const
    {
        return static_cast<int>(all_.size());
    }
    int swapMs() const
    {
        return swapMs_;
    }
    void setSwapMs(int value);
    bool filterSwitching() const
    {
        return switching_;
    }

    /// @brief Replace the rows with those @p history describes, emitting the difference.
    /// @param knownLemmas The lemmas currently marked known; the verdict each row carries is
    ///                    derived from this set, the same way an export's scope is.
    void update(QDate const&                           today,
                std::vector<core::HistoryEntry> const& history,
                std::unordered_set<std::string> const& knownLemmas);

signals:
    void filterChanged();
    void allCountChanged();
    void swapMsChanged();
    void filterSwitchingChanged();

private:
    enum Filter { All   = 0,
                  Known = 1,
                  New   = 2 };

    static bool                 matches(WordRow const& row, int filter);
    static std::vector<WordRow> filtered(std::vector<WordRow> const& rows, int filter);
    /// @brief Emit the difference between the visible rows and @p next, whose common rows keep their order.
    /// @param insertRows Whether the rows @p next adds may arrive; a switch's first phase holds them.
    void diffVisible(std::vector<WordRow> next, bool insertRows = true);
    /// @brief Land a held switch: the rows its filter wants arrive now.
    void finishSwitch();

    std::vector<WordRow> all_;
    std::vector<WordRow> visible_;
    /// The rows a switch is holding back until the closing ones have landed.
    std::vector<WordRow> pending_;
    bool                 hasPending_ = false;
    bool                 switching_  = false;
    int                  filter_     = All;
    int                  swapMs_     = 0;
    QTimer               swap_;
};

}

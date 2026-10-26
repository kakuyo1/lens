/**
 * @file words_model.h
 * @brief The words popup's rows as an item model: deduplicated, filtered and updated in place.
 *
 * The model owns what the list shows. The controller hands it the history after every
 * statsChanged; rows are matched by lemma and the difference reaches the view as removals,
 * insertions and data changes, so a removal runs ListView's remove transition and the
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
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

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
    void setFilter(int value);
    int  allCount() const
    {
        return static_cast<int>(all_.size());
    }

    /// @brief Replace the rows with those @p history describes, emitting the difference.
    void update(QDate const& today, std::vector<core::HistoryEntry> const& history);

signals:
    void filterChanged();
    void allCountChanged();

private:
    enum Filter { All   = 0,
                  Known = 1,
                  New   = 2 };

    static bool                 matches(WordRow const& row, int filter);
    static std::vector<WordRow> filtered(std::vector<WordRow> const& rows, int filter);
    /// @brief Emit the difference between the visible rows and @p next, whose common rows keep their order.
    void diffVisible(std::vector<WordRow> next);

    std::vector<WordRow> all_;
    std::vector<WordRow> visible_;
    int                  filter_ = All;
};

}

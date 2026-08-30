#pragma once

#include <QAbstractTableModel>
#include <QVector>

#include "types.hpp"

namespace scribble::gui {

/// Rows of corpus-wide speakers for the right-hand panel. The name column is
/// editable so a double-click renames in place; files and duration are read
/// only. Colour swatches come from the same palette the transcript uses.
class SpeakerModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColumnName = 0,
        ColumnFiles,
        ColumnDuration,
        ColumnCount,
    };

    enum Role {
        GlobalIdRole = Qt::UserRole + 1,
    };

    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setSpeakers(const std::vector<scribble::GlobalSpeaker> &speakers);
    std::int64_t globalIdAt(int row) const;
    const scribble::GlobalSpeaker *speakerAt(int row) const;

signals:
    /// Emitted when the name cell is edited so the panel can persist it.
    void renameRequested(std::int64_t globalId, const QString &name);

protected:
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

private:
    QVector<scribble::GlobalSpeaker> rows_;
};

}  // namespace scribble::gui

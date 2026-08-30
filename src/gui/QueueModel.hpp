#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QString>
#include <QVector>

#include "types.hpp"

namespace scribe::gui {

/// Table of files in the batch, one row per job (a multi-track file yields
/// several). Rows are keyed by file_id and updated in place as events arrive, so
/// discovery, start, stage progress and completion all land on the same row
/// without duplicating it.
class QueueModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColumnName = 0,
        ColumnDuration,
        ColumnStage,
        ColumnProgress,
        ColumnSpeakers,
        ColumnCount,
    };

    // Custom roles the progress delegate and the window read directly.
    enum Role {
        FileIdRole = Qt::UserRole + 1,
        ProgressRole,   ///< int 0..100
        FailedRole,     ///< bool
    };

    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void clear();

    void onDiscovered(const scribe::MediaJob &job);
    void onStarted(const scribe::MediaJob &job);
    void onStage(std::int64_t fileId, scribe::Stage stage, double fraction,
                 const QString &detail);
    void onFinished(std::int64_t fileId, scribe::Stage finalStage, const QString &error);
    void setSpeakerCount(std::int64_t fileId, int count);

    std::int64_t fileIdAt(int row) const;

private:
    struct Row {
        std::int64_t fileId = -1;
        QString path;
        int track = 0;
        int trackCount = 1;
        double duration = 0.0;
        scribe::Stage stage = scribe::Stage::Queued;
        double fraction = -1.0;
        int speakerCount = -1;
        QString detail;
        QString error;
        bool failed = false;
    };

    int indexOf(std::int64_t fileId) const;
    Row &ensureRow(std::int64_t fileId);
    int progressPercent(const Row &row) const;
    QString displayName(const Row &row) const;
    void touched(int row);

    QVector<Row> rows_;
    QHash<std::int64_t, int> byId_;
};

}  // namespace scribe::gui

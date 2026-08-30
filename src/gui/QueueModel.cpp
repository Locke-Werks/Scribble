#include "QueueModel.hpp"

#include <QColor>
#include <QFileInfo>

#include <algorithm>

#include "FormatUtil.hpp"

namespace scribble::gui {

int QueueModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

int QueueModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

int QueueModel::indexOf(std::int64_t fileId) const {
    const auto it = byId_.constFind(fileId);
    return it == byId_.constEnd() ? -1 : it.value();
}

std::int64_t QueueModel::fileIdAt(int row) const {
    if (row < 0 || row >= rows_.size()) {
        return -1;
    }
    return rows_[row].fileId;
}

QString QueueModel::displayName(const Row &row) const {
    QString name = QFileInfo(row.path).fileName();
    if (name.isEmpty()) {
        name = row.path;
    }
    if (row.trackCount > 1) {
        name += QStringLiteral(" [track %1/%2]").arg(row.track + 1).arg(row.trackCount);
    }
    return name;
}

int QueueModel::progressPercent(const Row &row) const {
    switch (row.stage) {
        case scribble::Stage::Queued:
            return 0;
        case scribble::Stage::Done:
        case scribble::Stage::Skipped:
            return 100;
        case scribble::Stage::Failed:
            break;  // hold whatever progress was reached
        default:
            break;
    }
    // Probing through Writing are the visible work stages. Map the stage ordinal
    // onto that span and add the intra-stage fraction when one is reported.
    const int first = static_cast<int>(scribble::Stage::Probing);
    const int last = static_cast<int>(scribble::Stage::Writing);
    const int span = last - first + 1;
    int ordinal = static_cast<int>(row.stage);
    if (ordinal < first) {
        return 0;
    }
    if (ordinal > last) {
        ordinal = last;
    }
    double base = static_cast<double>(ordinal - first);
    if (row.fraction >= 0.0) {
        base += std::clamp(row.fraction, 0.0, 1.0);
    }
    int pct = static_cast<int>((base / static_cast<double>(span)) * 100.0 + 0.5);
    return std::clamp(pct, 0, 99);
}

QVariant QueueModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    const Row &row = rows_[index.row()];

    switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
                case ColumnName:
                    return displayName(row);
                case ColumnDuration:
                    return formatDuration(row.duration);
                case ColumnStage:
                    return QString::fromUtf8(scribble::stage_name(row.stage));
                case ColumnSpeakers:
                    return row.speakerCount >= 0 ? QString::number(row.speakerCount) : QString();
                default:
                    return {};
            }
        case Qt::ToolTipRole:
            if (row.failed && !row.error.isEmpty()) {
                return row.error;
            }
            if (!row.detail.isEmpty()) {
                return row.detail;
            }
            return row.path;
        case Qt::TextAlignmentRole:
            if (index.column() == ColumnDuration || index.column() == ColumnSpeakers) {
                return int(Qt::AlignRight | Qt::AlignVCenter);
            }
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        case Qt::ForegroundRole:
            if (row.failed) {
                return QColor(0xe7, 0x82, 0x84);
            }
            return {};
        case FileIdRole:
            return QVariant::fromValue<qlonglong>(row.fileId);
        case ProgressRole:
            return progressPercent(row);
        case FailedRole:
            return row.failed;
        default:
            return {};
    }
}

QVariant QueueModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
        case ColumnName:
            return QStringLiteral("File");
        case ColumnDuration:
            return QStringLiteral("Length");
        case ColumnStage:
            return QStringLiteral("Stage");
        case ColumnProgress:
            return QStringLiteral("Progress");
        case ColumnSpeakers:
            return QStringLiteral("Speakers");
        default:
            return {};
    }
}

void QueueModel::clear() {
    beginResetModel();
    rows_.clear();
    byId_.clear();
    endResetModel();
}

QueueModel::Row &QueueModel::ensureRow(std::int64_t fileId) {
    const int existing = indexOf(fileId);
    if (existing >= 0) {
        return rows_[existing];
    }
    beginInsertRows({}, rows_.size(), rows_.size());
    Row row;
    row.fileId = fileId;
    rows_.push_back(row);
    byId_.insert(fileId, rows_.size() - 1);
    endInsertRows();
    return rows_.back();
}

void QueueModel::touched(int row) {
    if (row < 0 || row >= rows_.size()) {
        return;
    }
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void QueueModel::onDiscovered(const scribble::MediaJob &job) {
    Row &row = ensureRow(job.file_id);
    row.path = QString::fromStdString(job.source_path);
    row.track = job.track;
    row.trackCount = job.track_count;
    if (job.duration > 0.0) {
        row.duration = job.duration;
    }
    touched(indexOf(job.file_id));
}

void QueueModel::onStarted(const scribble::MediaJob &job) {
    Row &row = ensureRow(job.file_id);
    row.path = QString::fromStdString(job.source_path);
    row.track = job.track;
    row.trackCount = job.track_count;
    row.duration = job.duration;
    row.failed = false;
    row.error.clear();
    if (row.stage == scribble::Stage::Queued) {
        row.stage = scribble::Stage::Probing;
    }
    touched(indexOf(job.file_id));
}

void QueueModel::onStage(std::int64_t fileId, scribble::Stage stage, double fraction,
                         const QString &detail) {
    Row &row = ensureRow(fileId);
    row.stage = stage;
    row.fraction = fraction;
    row.detail = detail;
    if (stage == scribble::Stage::Failed) {
        row.failed = true;
    }
    touched(indexOf(fileId));
}

void QueueModel::onFinished(std::int64_t fileId, scribble::Stage finalStage, const QString &error) {
    Row &row = ensureRow(fileId);
    row.stage = finalStage;
    row.fraction = -1.0;
    if (finalStage == scribble::Stage::Failed) {
        row.failed = true;
        row.error = error;
    }
    touched(indexOf(fileId));
}

void QueueModel::setSpeakerCount(std::int64_t fileId, int count) {
    Row &row = ensureRow(fileId);
    row.speakerCount = count;
    touched(indexOf(fileId));
}

}  // namespace scribble::gui

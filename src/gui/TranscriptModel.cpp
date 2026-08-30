#include "TranscriptModel.hpp"

#include "SpeakerPalette.hpp"

namespace scribble::gui {

int TranscriptModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QString TranscriptModel::speakerLabel(const Row &row) const {
    if (row.globalId >= 0) {
        const auto it = names_.constFind(row.globalId);
        if (it != names_.constEnd() && !it.value().isEmpty()) {
            return it.value();
        }
        // Resolved but the display cache has not caught up yet: fall back to the
        // canonical placeholder form so the line is never left unattributed.
        return QStringLiteral("SPEAKER_%1").arg(row.globalId, 4, 10, QChar('0'));
    }
    if (!row.localLabel.isEmpty()) {
        return row.localLabel;
    }
    return {};
}

QColor TranscriptModel::speakerColor(const Row &row) const {
    if (row.globalId >= 0) {
        return SpeakerPalette::colorForGlobal(row.globalId);
    }
    if (!row.localLabel.isEmpty()) {
        return SpeakerPalette::colorForKey(row.localLabel);
    }
    return SpeakerPalette::unknown();
}

QVariant TranscriptModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    const Row &row = rows_[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case TextRole:
            return row.text;
        case StartRole:
            return row.start;
        case EndRole:
            return row.end;
        case SpeakerRole:
            return speakerLabel(row);
        case ColorRole:
            return speakerColor(row);
        case HasSpeakerRole:
            return row.globalId >= 0 || !row.localLabel.isEmpty();
        case SearchRole:
            return speakerLabel(row) + QLatin1Char(' ') + row.text;
        default:
            return {};
    }
}

void TranscriptModel::setNameResolver(QHash<std::int64_t, QString> names) {
    names_ = std::move(names);
    refreshSpeakers();
}

void TranscriptModel::reset(const QVector<scribble::Segment> &segments) {
    beginResetModel();
    rows_.clear();
    rows_.reserve(segments.size());
    for (const scribble::Segment &s : segments) {
        Row row;
        row.index = s.index;
        row.start = s.start;
        row.end = s.end;
        row.text = QString::fromStdString(s.text).trimmed();
        row.localLabel = QString::fromStdString(s.local_label);
        row.globalId = s.global_id;
        rows_.push_back(row);
    }
    endResetModel();
}

void TranscriptModel::appendSegment(const scribble::Segment &segment) {
    beginInsertRows({}, rows_.size(), rows_.size());
    Row row;
    row.index = segment.index;
    row.start = segment.start;
    row.end = segment.end;
    row.text = QString::fromStdString(segment.text).trimmed();
    row.localLabel = QString::fromStdString(segment.local_label);
    row.globalId = segment.global_id;
    rows_.push_back(row);
    endInsertRows();
}

void TranscriptModel::refreshRange(int first, int last) {
    if (rows_.isEmpty()) {
        return;
    }
    first = qMax(0, first);
    last = qMin(rows_.size() - 1, last);
    if (first > last) {
        return;
    }
    emit dataChanged(index(first), index(last),
                     {SpeakerRole, ColorRole, HasSpeakerRole, SearchRole});
}

void TranscriptModel::applyLabelled(const QVector<scribble::Segment> &segments) {
    // The labelled set is the same utterances with speaker fields filled. Match
    // by segment index and update in place; if diarization split or merged the
    // set, fall back to a full reset so nothing is dropped.
    bool alignable = segments.size() == rows_.size();
    if (alignable) {
        for (int i = 0; i < segments.size(); ++i) {
            if (segments[i].index != rows_[i].index) {
                alignable = false;
                break;
            }
        }
    }
    if (!alignable) {
        reset(segments);
        return;
    }
    for (int i = 0; i < segments.size(); ++i) {
        rows_[i].localLabel = QString::fromStdString(segments[i].local_label);
        rows_[i].globalId = segments[i].global_id;
    }
    refreshRange(0, rows_.size() - 1);
}

void TranscriptModel::applyResolutions(const QHash<QString, std::int64_t> &labelToGlobal) {
    if (labelToGlobal.isEmpty()) {
        return;
    }
    for (Row &row : rows_) {
        if (row.localLabel.isEmpty()) {
            continue;
        }
        const auto it = labelToGlobal.constFind(row.localLabel);
        if (it != labelToGlobal.constEnd()) {
            row.globalId = it.value();
        }
    }
    refreshRange(0, rows_.size() - 1);
}

void TranscriptModel::refreshSpeakers() {
    refreshRange(0, rows_.size() - 1);
}

}  // namespace scribble::gui

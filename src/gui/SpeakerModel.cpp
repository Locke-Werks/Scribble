#include "SpeakerModel.hpp"

#include <QColor>

#include "FormatUtil.hpp"
#include "SpeakerPalette.hpp"

namespace scribe::gui {

int SpeakerModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

int SpeakerModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant SpeakerModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    const scribe::GlobalSpeaker &sp = rows_[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            switch (index.column()) {
                case ColumnName:
                    // Edit starts from the assigned name, empty when unnamed, so
                    // the placeholder id is not what the user has to delete.
                    return role == Qt::EditRole ? QString::fromStdString(sp.name)
                                                : QString::fromStdString(sp.display());
                case ColumnFiles:
                    return sp.n_files;
                case ColumnDuration:
                    return formatDuration(sp.total_duration);
                default:
                    return {};
            }
        case Qt::DecorationRole:
            if (index.column() == ColumnName) {
                return SpeakerPalette::colorForGlobal(sp.id);
            }
            return {};
        case Qt::ToolTipRole:
            if (!sp.notes.empty()) {
                return QString::fromStdString(sp.notes);
            }
            return {};
        case Qt::TextAlignmentRole:
            if (index.column() != ColumnName) {
                return int(Qt::AlignRight | Qt::AlignVCenter);
            }
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        case GlobalIdRole:
            return QVariant::fromValue<qlonglong>(sp.id);
        default:
            return {};
    }
}

QVariant SpeakerModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
        case ColumnName:
            return QStringLiteral("Speaker");
        case ColumnFiles:
            return QStringLiteral("Files");
        case ColumnDuration:
            return QStringLiteral("Speech");
        default:
            return {};
    }
}

Qt::ItemFlags SpeakerModel::flags(const QModelIndex &index) const {
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == ColumnName) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

bool SpeakerModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (role != Qt::EditRole || index.column() != ColumnName || index.row() >= rows_.size()) {
        return false;
    }
    const QString name = value.toString().trimmed();
    emit renameRequested(rows_[index.row()].id, name);
    return true;
}

void SpeakerModel::setSpeakers(const std::vector<scribe::GlobalSpeaker> &speakers) {
    beginResetModel();
    rows_.clear();
    rows_.reserve(static_cast<int>(speakers.size()));
    for (const auto &sp : speakers) {
        rows_.push_back(sp);
    }
    endResetModel();
}

std::int64_t SpeakerModel::globalIdAt(int row) const {
    if (row < 0 || row >= rows_.size()) {
        return -1;
    }
    return rows_[row].id;
}

const scribe::GlobalSpeaker *SpeakerModel::speakerAt(int row) const {
    if (row < 0 || row >= rows_.size()) {
        return nullptr;
    }
    return &rows_[row];
}

}  // namespace scribe::gui

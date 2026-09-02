#include "SpeakerModel.hpp"

#include <QColor>
#include <QStringList>

#include "FormatUtil.hpp"
#include "SpeakerPalette.hpp"
#include "ThemeQt.hpp"

namespace scribble::gui {

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
    const scribble::GlobalSpeaker &sp = rows_[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            switch (index.column()) {
                case ColumnName: {
                    // Edit starts from the assigned name, empty when unnamed, so
                    // the placeholder id is not what the user has to delete.
                    if (role == Qt::EditRole) {
                        return QString::fromStdString(sp.name);
                    }
                    // An enrolled identity is marked in the list because it
                    // behaves differently from the ones the corpus invented:
                    // it matches at a lower threshold, it can take more than
                    // one speaker out of a file, and reclustering leaves it
                    // alone. None of that is guessable from a name.
                    const QString name = QString::fromStdString(sp.display());
                    // Built from a code point rather than written into the
                    // literal: what QStringLiteral makes of a non-ASCII source
                    // byte depends on the compiler execution charset, and this
                    // is not the place to find out.
                    return sp.enrolled ? QChar(0x25C6) + QStringLiteral(" ") + name : name;
                }
                case ColumnFiles:
                    return sp.n_files;
                case ColumnDuration:
                    return formatDuration(sp.total_duration);
                default:
                    return {};
            }
        case Qt::ForegroundRole:
            // The speaker's hue is ink, never fill: a decoration swatch here
            // would be a solid block of colour in a window that has none.
            if (index.column() == ColumnName) {
                return SpeakerPalette::colorForGlobal(sp.id);
            }
            return theme::c(scribble::theme::kFg4);
        case Qt::FontRole:
            if (index.column() != ColumnName) {
                return theme::mono(11);
            }
            return sp.enrolled ? theme::body(13, QFont::DemiBold) : theme::body(13);
        case Qt::ToolTipRole: {
            QStringList lines;
            if (sp.enrolled) {
                lines << QStringLiteral("Enrolled from %1 reference clip%2")
                             .arg(sp.n_clips)
                             .arg(sp.n_clips == 1 ? "" : "s");
            }
            if (!sp.notes.empty()) {
                lines << QString::fromStdString(sp.notes);
            }
            return lines.isEmpty() ? QVariant{} : QVariant(lines.join(QChar('\n')));
        }
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
            return QStringLiteral("SPEAKER");
        case ColumnFiles:
            return QStringLiteral("FILES");
        case ColumnDuration:
            return QStringLiteral("SPEECH");
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

void SpeakerModel::setSpeakers(const std::vector<scribble::GlobalSpeaker> &speakers) {
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

const scribble::GlobalSpeaker *SpeakerModel::speakerAt(int row) const {
    if (row < 0 || row >= rows_.size()) {
        return nullptr;
    }
    return &rows_[row];
}

}  // namespace scribble::gui

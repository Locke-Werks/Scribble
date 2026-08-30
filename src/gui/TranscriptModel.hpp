#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QString>
#include <QVector>

#include "types.hpp"

namespace scribe::gui {

/// One transcript, one row per segment. Text lands first from live Whisper
/// output with no speaker; diarization and identity resolution then update the
/// same rows in place so colours and names snap in without the list being torn
/// down and rebuilt.
class TranscriptModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        StartRole = Qt::UserRole + 1,
        EndRole,
        TextRole,
        SpeakerRole,   ///< display label, empty before diarization
        ColorRole,     ///< speaker colour
        HasSpeakerRole,
        SearchRole,    ///< speaker + text, for the filter proxy
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /// Names are resolved here rather than in the delegate so a rename or merge
    /// can recolour every row without another database read per paint.
    void setNameResolver(QHash<std::int64_t, QString> names);

    void reset(const QVector<scribe::Segment> &segments);
    void appendSegment(const scribe::Segment &segment);
    void applyLabelled(const QVector<scribe::Segment> &segments);
    void applyResolutions(const QHash<QString, std::int64_t> &labelToGlobal);
    void refreshSpeakers();

    bool isEmpty() const { return rows_.isEmpty(); }

private:
    struct Row {
        int index = 0;
        double start = 0.0;
        double end = 0.0;
        QString text;
        QString localLabel;
        std::int64_t globalId = -1;
    };

    QString speakerLabel(const Row &row) const;
    QColor speakerColor(const Row &row) const;
    void refreshRange(int first, int last);

    QVector<Row> rows_;
    QHash<std::int64_t, QString> names_;
};

}  // namespace scribe::gui

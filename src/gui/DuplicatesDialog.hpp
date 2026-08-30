#pragma once

#include <QDialog>
#include <QVector>

#include "types.hpp"

class QTableWidget;
class QLabel;

namespace scribble {
class Database;
}

namespace scribble::gui {

/// Lists global speaker pairs sitting in the uncertain band between the review
/// and match thresholds, the usual signature of one voice split across two
/// identities by a domain change. Each pair can be merged or dismissed, and a
/// dismissal is persisted through the database so the pair stays hidden across
/// restarts.
class DuplicatesDialog : public QDialog {
    Q_OBJECT
public:
    DuplicatesDialog(scribble::Database *db, float low, float high, QWidget *parent = nullptr);

signals:
    /// Raised after at least one merge so the panel and transcript can refresh.
    void merged();

private:
    void reload();
    void mergeSelected();
    void dismissSelected();

    scribble::Database *db_ = nullptr;
    float low_ = 0.0f;
    float high_ = 0.0f;
    bool anyMerged_ = false;

    QTableWidget *table_ = nullptr;
    QLabel *summary_ = nullptr;
    QVector<scribble::DuplicateCandidate> candidates_;
};

}  // namespace scribble::gui

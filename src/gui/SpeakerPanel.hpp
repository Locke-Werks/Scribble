#pragma once

#include <QWidget>

#include "types.hpp"

class QTableView;

namespace scribble {
class Database;
}

namespace scribble::gui {

class SpeakerModel;

/// Right-hand panel over the corpus-wide speaker store. Rename is inline; a
/// context menu merges, splits a file's speaker out, or attaches a note; and a
/// button opens the duplicate review. Every mutation reports back so the
/// transcript can recolour and relabel without a full reload.
class SpeakerPanel : public QWidget {
    Q_OBJECT
public:
    explicit SpeakerPanel(QWidget *parent = nullptr);

    void setDatabase(scribble::Database *db);
    void setCurrentFile(std::int64_t fileId) { currentFile_ = fileId; }
    void refresh();

signals:
    void speakersChanged();
    void reviewDuplicatesRequested();
    void status(const QString &message);

private slots:
    void onRenameRequested(std::int64_t globalId, const QString &name);
    void showContextMenu(const QPoint &pos);

private:
    std::int64_t selectedGlobal() const;
    void mergeInto(std::int64_t globalId);
    void splitThisFile(std::int64_t globalId);
    void addNote(std::int64_t globalId);

    scribble::Database *db_ = nullptr;
    std::int64_t currentFile_ = -1;
    SpeakerModel *model_ = nullptr;
    QTableView *view_ = nullptr;
};

}  // namespace scribble::gui

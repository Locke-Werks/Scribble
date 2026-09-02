#pragma once

#include <QDialog>
#include <QVector>

#include <memory>

#include "config.hpp"
#include "enroll.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace scribble {
class Database;
}

namespace scribble::gui {

/// Teaches Scribble a voice from reference audio of a known person.
///
/// The corpus normally enrols itself, which works and is the point of the
/// thing, but it has no way to know that four of the nine speakers it found in
/// a three-person recording are one person, because per-file diarization
/// already told it they were four. Reference audio breaks that tie: a profile
/// built from clips a human vouched for outranks a clustering threshold, and
/// the extra speakers collapse back into whoever they came from.
///
/// Clips are analysed before they can be committed, and the result is shown
/// rather than assumed. A clip with two voices in it produces a confident
/// profile of nobody, which is worse than no profile at all, so the panel
/// reports how much speech it found and whether the clip sounds like one
/// person throughout.
class EnrollDialog : public QDialog {
    Q_OBJECT
public:
    EnrollDialog(scribble::Database *db, scribble::Config config, QWidget *parent = nullptr);
    ~EnrollDialog() override;

    /// Preselects an existing identity, for "add reference clips" on a speaker
    /// that already exists.
    void selectSpeaker(std::int64_t globalId);

signals:
    /// Raised after a commit so the panel and transcript can refresh.
    void enrolled();

private slots:
    void addClips();
    void removeSelected();
    void onSpeakerChanged(int index);
    void commit();

private:
    struct Row {
        scribble::EnrollClipResult result;
        bool analysed = false;
        QString failure;
    };

    void reloadSpeakers();
    void refreshTable();
    void updateSummary();
    void setWorking(bool working, const QString &what = {});
    void analyse(const QStringList &paths);
    std::int64_t selectedGlobal() const;

    scribble::Database *db_ = nullptr;
    scribble::Config config_;
    /// Held across calls so the embedding model is loaded once per dialog
    /// rather than once per clip. Only ever touched on the worker thread.
    std::shared_ptr<scribble::Enroller> enroller_;

    QComboBox *speaker_ = nullptr;
    QLineEdit *name_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *summary_ = nullptr;
    QPushButton *addBtn_ = nullptr;
    QPushButton *removeBtn_ = nullptr;
    QPushButton *enrollBtn_ = nullptr;

    QVector<Row> rows_;
    QString lastDir_;
    bool working_ = false;
};

}  // namespace scribble::gui

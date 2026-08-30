#pragma once

#include <QHash>
#include <QMainWindow>
#include <QVector>

#include <memory>

#include "ScribeEvent.hpp"
#include "config.hpp"
#include "types.hpp"

class QAction;
class QLabel;
class QProgressBar;
class QSplitter;
class QTableView;
class QItemSelection;

namespace scribe::gui {

class PipelineController;
class QueueModel;
class ProgressDelegate;
class TranscriptView;
class SpeakerPanel;
class LogDock;

/// The single application window. Owns the controller, wires pipeline events to
/// the queue, transcript, speaker panel and log, and persists its layout.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onScribeEvent(const scribe::gui::ScribeEvent &e);
    void onBusyChanged(bool busy);
    void onQueueSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

    void addFiles();
    void addFolder();
    void openSettings();
    void openOutputFolder();
    void reviewDuplicates();
    void onSpeakersChanged();

private:
    /// Live transcript state for a file, held so switching back to a file that is
    /// still processing restores its partial text, not a blank pane.
    struct FileBuffer {
        QVector<scribe::Segment> segments;
        QHash<QString, std::int64_t> labelToGlobal;
    };

    void buildUi();
    void buildActions();
    void restoreLayout();
    void saveLayout();

    void enqueuePaths(const QStringList &paths);
    void refreshSpeakerNames();
    QVector<scribe::Segment> segmentsForFile(std::int64_t fileId);
    std::shared_ptr<FileBuffer> bufferFor(std::int64_t fileId, bool create);

    void handleSegment(const scribe::EvSegment &ev);
    void handleLabelled(const scribe::EvSegmentsLabelled &ev);
    void handleResolved(const scribe::EvSpeakersResolved &ev);

    PipelineController *controller_ = nullptr;

    QueueModel *queueModel_ = nullptr;
    ProgressDelegate *progressDelegate_ = nullptr;
    QTableView *queueView_ = nullptr;
    TranscriptView *transcript_ = nullptr;
    SpeakerPanel *speakerPanel_ = nullptr;
    LogDock *logDock_ = nullptr;
    QSplitter *splitter_ = nullptr;

    QLabel *statusLabel_ = nullptr;
    QProgressBar *overallProgress_ = nullptr;
    QLabel *modelLabel_ = nullptr;
    QProgressBar *modelProgress_ = nullptr;

    QAction *addFilesAct_ = nullptr;
    QAction *addFolderAct_ = nullptr;
    QAction *startAct_ = nullptr;
    QAction *pauseAct_ = nullptr;
    QAction *stopAct_ = nullptr;
    QAction *reclusterAct_ = nullptr;
    QAction *rerenderAct_ = nullptr;
    QAction *settingsAct_ = nullptr;
    QAction *openOutputAct_ = nullptr;

    QHash<std::int64_t, std::shared_ptr<FileBuffer>> buffers_;
    QHash<std::int64_t, QString> speakerNames_;

    QString lastFilesDir_;
    QString lastFolderDir_;
};

}  // namespace scribe::gui

#include "MainWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QUrl>

#include <algorithm>
#include <cmath>

#include "DuplicatesDialog.hpp"
#include "LogDock.hpp"
#include "PipelineController.hpp"
#include "ProgressDelegate.hpp"
#include "QueueModel.hpp"
#include "SettingsDialog.hpp"
#include "SpeakerPanel.hpp"
#include "TranscriptView.hpp"
#include "config.hpp"
#include "db.hpp"

namespace scribe::gui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ScribeEveryone"));
    setAcceptDrops(true);

    controller_ = new PipelineController(this);

    // Load configuration before opening the database so the database path and
    // every other tunable comes from the user's scribe.toml when it exists.
    scribe::Config cfg;
    const scribe::fs::path cfgPath = scribe::default_config_path();
    std::string cfgError;
    scribe::Config loaded = scribe::Config::load(cfgPath, &cfgError);
    if (cfgError.empty()) {
        cfg = loaded;
    }

    buildUi();
    buildActions();

    connect(controller_, &PipelineController::scribeEvent, this, &MainWindow::onScribeEvent);
    connect(controller_, &PipelineController::busyChanged, this, &MainWindow::onBusyChanged);
    connect(controller_, &PipelineController::failed, this, [this](const QString &message) {
        logDock_->append(scribe::LogLevel::Error, message);
        statusBar()->showMessage(message, 8000);
    });
    connect(controller_, &PipelineController::runFinished, this, [this] {
        statusBar()->showMessage(QStringLiteral("Run finished."), 5000);
        speakerPanel_->refresh();
        refreshSpeakerNames();
    });
    connect(controller_, &PipelineController::reclusterFinished, this, [this](const QString &s) {
        logDock_->append(scribe::LogLevel::Info, s);
        statusBar()->showMessage(s, 8000);
        speakerPanel_->refresh();
        refreshSpeakerNames();
    });
    connect(controller_, &PipelineController::rerenderFinished, this, [this](int count) {
        const QString msg = QStringLiteral("Re-rendered %1 file(s).").arg(count);
        logDock_->append(scribe::LogLevel::Info, msg);
        statusBar()->showMessage(msg, 5000);
    });

    QString openError;
    if (!controller_->open(cfg, &openError)) {
        QMessageBox::critical(this, QStringLiteral("ScribeEveryone"),
                              QStringLiteral("Could not open the database:\n%1").arg(openError));
    } else {
        speakerPanel_->setDatabase(controller_->database());
        refreshSpeakerNames();
    }
    if (!cfgError.empty()) {
        logDock_->append(scribe::LogLevel::Warn,
                         QStringLiteral("Configuration not loaded: %1")
                             .arg(QString::fromStdString(cfgError)));
    }

    restoreLayout();
    onBusyChanged(false);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    splitter_ = new QSplitter(Qt::Horizontal, this);

    queueModel_ = new QueueModel(this);
    queueView_ = new QTableView(splitter_);
    queueView_->setModel(queueModel_);
    queueView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    queueView_->setSelectionMode(QAbstractItemView::SingleSelection);
    queueView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    queueView_->verticalHeader()->setVisible(false);
    queueView_->horizontalHeader()->setStretchLastSection(false);
    queueView_->horizontalHeader()->setSectionResizeMode(QueueModel::ColumnName,
                                                         QHeaderView::Stretch);
    queueView_->horizontalHeader()->setSectionResizeMode(QueueModel::ColumnDuration,
                                                         QHeaderView::ResizeToContents);
    queueView_->horizontalHeader()->setSectionResizeMode(QueueModel::ColumnStage,
                                                         QHeaderView::ResizeToContents);
    queueView_->horizontalHeader()->setSectionResizeMode(QueueModel::ColumnProgress,
                                                         QHeaderView::Interactive);
    queueView_->horizontalHeader()->setSectionResizeMode(QueueModel::ColumnSpeakers,
                                                         QHeaderView::ResizeToContents);
    progressDelegate_ = new ProgressDelegate(this);
    queueView_->setItemDelegateForColumn(QueueModel::ColumnProgress, progressDelegate_);
    connect(queueView_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::onQueueSelectionChanged);
    splitter_->addWidget(queueView_);

    transcript_ = new TranscriptView(splitter_);
    splitter_->addWidget(transcript_);

    speakerPanel_ = new SpeakerPanel(splitter_);
    splitter_->addWidget(speakerPanel_);
    connect(speakerPanel_, &SpeakerPanel::speakersChanged, this, &MainWindow::onSpeakersChanged);
    connect(speakerPanel_, &SpeakerPanel::reviewDuplicatesRequested, this,
            &MainWindow::reviewDuplicates);
    connect(speakerPanel_, &SpeakerPanel::status, this,
            [this](const QString &m) { statusBar()->showMessage(m, 5000); });

    splitter_->setStretchFactor(0, 30);
    splitter_->setStretchFactor(1, 45);
    splitter_->setStretchFactor(2, 25);
    splitter_->setSizes({300, 450, 250});
    setCentralWidget(splitter_);

    logDock_ = new LogDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);

    statusLabel_ = new QLabel(QStringLiteral("Idle"), this);
    overallProgress_ = new QProgressBar(this);
    overallProgress_->setRange(0, 100);
    overallProgress_->setValue(0);
    overallProgress_->setFixedWidth(180);
    overallProgress_->setTextVisible(true);
    modelLabel_ = new QLabel(this);
    modelProgress_ = new QProgressBar(this);
    modelProgress_->setFixedWidth(140);
    modelLabel_->setVisible(false);
    modelProgress_->setVisible(false);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(modelLabel_);
    statusBar()->addPermanentWidget(modelProgress_);
    statusBar()->addPermanentWidget(overallProgress_);
}

void MainWindow::buildActions() {
    QStyle *style = this->style();

    addFilesAct_ = new QAction(style->standardIcon(QStyle::SP_FileIcon),
                               QStringLiteral("Add Files"), this);
    addFolderAct_ = new QAction(style->standardIcon(QStyle::SP_DirIcon),
                                QStringLiteral("Add Folder"), this);
    startAct_ = new QAction(style->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Start"),
                            this);
    pauseAct_ = new QAction(style->standardIcon(QStyle::SP_MediaPause), QStringLiteral("Pause"),
                            this);
    pauseAct_->setCheckable(true);
    stopAct_ = new QAction(style->standardIcon(QStyle::SP_MediaStop), QStringLiteral("Stop"), this);
    reclusterAct_ = new QAction(style->standardIcon(QStyle::SP_BrowserReload),
                                QStringLiteral("Recluster"), this);
    rerenderAct_ = new QAction(style->standardIcon(QStyle::SP_DialogSaveButton),
                               QStringLiteral("Re-render"), this);
    settingsAct_ = new QAction(style->standardIcon(QStyle::SP_FileDialogDetailedView),
                               QStringLiteral("Settings"), this);
    openOutputAct_ = new QAction(style->standardIcon(QStyle::SP_DirOpenIcon),
                                 QStringLiteral("Open Output Folder"), this);

    connect(addFilesAct_, &QAction::triggered, this, &MainWindow::addFiles);
    connect(addFolderAct_, &QAction::triggered, this, &MainWindow::addFolder);
    connect(startAct_, &QAction::triggered, controller_, &PipelineController::start);
    connect(pauseAct_, &QAction::toggled, controller_, &PipelineController::pause);
    connect(stopAct_, &QAction::triggered, controller_, &PipelineController::stop);
    connect(reclusterAct_, &QAction::triggered, controller_, &PipelineController::recluster);
    connect(rerenderAct_, &QAction::triggered, this, [this] { controller_->rerender(true); });
    connect(settingsAct_, &QAction::triggered, this, &MainWindow::openSettings);
    connect(openOutputAct_, &QAction::triggered, this, &MainWindow::openOutputFolder);

    auto *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolBar"));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(addFilesAct_);
    toolbar->addAction(addFolderAct_);
    toolbar->addSeparator();
    toolbar->addAction(startAct_);
    toolbar->addAction(pauseAct_);
    toolbar->addAction(stopAct_);
    toolbar->addSeparator();
    toolbar->addAction(reclusterAct_);
    toolbar->addAction(rerenderAct_);
    toolbar->addSeparator();
    toolbar->addAction(settingsAct_);
    toolbar->addAction(openOutputAct_);

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    fileMenu->addAction(addFilesAct_);
    fileMenu->addAction(addFolderAct_);
    fileMenu->addSeparator();
    fileMenu->addAction(openOutputAct_);
    fileMenu->addSeparator();
    auto *quitAct = fileMenu->addAction(QStringLiteral("Quit"));
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    auto *runMenu = menuBar()->addMenu(QStringLiteral("Run"));
    runMenu->addAction(startAct_);
    runMenu->addAction(pauseAct_);
    runMenu->addAction(stopAct_);
    runMenu->addSeparator();
    runMenu->addAction(reclusterAct_);
    runMenu->addAction(rerenderAct_);

    auto *toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
    toolsMenu->addAction(settingsAct_);
    auto *reviewAct = toolsMenu->addAction(QStringLiteral("Review Duplicates"));
    connect(reviewAct, &QAction::triggered, this, &MainWindow::reviewDuplicates);

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    viewMenu->addAction(logDock_->toggleViewAction());

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    auto *aboutAct = helpMenu->addAction(QStringLiteral("About"));
    connect(aboutAct, &QAction::triggered, this, [this] {
#ifdef SCRIBE_VERSION
        const QString ver = QStringLiteral(" " SCRIBE_VERSION);
#else
        const QString ver;
#endif
        QMessageBox::about(this, QStringLiteral("About ScribeEveryone"),
                           QStringLiteral("ScribeEveryone%1\nBatch transcription with diarization "
                                          "and corpus-wide speaker identity.")
                               .arg(ver));
    });
}

void MainWindow::onBusyChanged(bool busy) {
    addFilesAct_->setEnabled(!busy);
    addFolderAct_->setEnabled(!busy);
    startAct_->setEnabled(!busy);
    reclusterAct_->setEnabled(!busy);
    rerenderAct_->setEnabled(!busy);
    settingsAct_->setEnabled(!busy);
    pauseAct_->setEnabled(busy);
    stopAct_->setEnabled(busy);
    if (!busy && pauseAct_->isChecked()) {
        pauseAct_->setChecked(false);
    }
}

void MainWindow::enqueuePaths(const QStringList &paths) {
    if (paths.isEmpty()) {
        return;
    }
    if (controller_->busy()) {
        statusBar()->showMessage(QStringLiteral("Cannot add files while a run is active."), 5000);
        return;
    }
    controller_->addPaths(paths);
}

void MainWindow::addFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Add media files"), lastFilesDir_,
        QStringLiteral("Media files (*.mp3 *.wav *.flac *.m4a *.aac *.ogg *.opus *.mp4 *.mkv *.mov "
                       "*.avi *.webm);;All files (*.*)"));
    if (files.isEmpty()) {
        return;
    }
    lastFilesDir_ = QFileInfo(files.first()).absolutePath();
    enqueuePaths(files);
}

void MainWindow::addFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Add folder"),
                                                          lastFolderDir_);
    if (dir.isEmpty()) {
        return;
    }
    lastFolderDir_ = dir;
    enqueuePaths({dir});
}

void MainWindow::openSettings() {
    SettingsDialog dialog(controller_->config(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const scribe::Config updated = dialog.config();
    try {
        updated.save(scribe::default_config_path());
    } catch (const std::exception &e) {
        logDock_->append(scribe::LogLevel::Warn,
                         QStringLiteral("Could not save configuration: %1")
                             .arg(QString::fromUtf8(e.what())));
    }
    QString error;
    if (!controller_->updateConfig(updated, &error)) {
        QMessageBox::warning(this, QStringLiteral("Settings"),
                             QStringLiteral("Could not apply settings:\n%1").arg(error));
        return;
    }
    speakerPanel_->setDatabase(controller_->database());
    refreshSpeakerNames();
}

void MainWindow::openOutputFolder() {
    const QString dir = QString::fromStdWString(controller_->config().out_dir.wstring());
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::reviewDuplicates() {
    if (!controller_->database()) {
        return;
    }
    const scribe::Config &cfg = controller_->config();
    DuplicatesDialog dialog(controller_->database(), cfg.review_threshold, cfg.match_threshold,
                            this);
    connect(&dialog, &DuplicatesDialog::merged, this, &MainWindow::onSpeakersChanged);
    dialog.exec();
}

void MainWindow::onSpeakersChanged() {
    speakerPanel_->refresh();
    refreshSpeakerNames();
}

void MainWindow::refreshSpeakerNames() {
    speakerNames_.clear();
    if (controller_->database()) {
        for (const auto &g : controller_->database()->globals()) {
            speakerNames_.insert(g.id, QString::fromStdString(g.display()));
        }
    }
    transcript_->setSpeakerNames(speakerNames_);
}

std::shared_ptr<MainWindow::FileBuffer> MainWindow::bufferFor(std::int64_t fileId, bool create) {
    auto it = buffers_.find(fileId);
    if (it != buffers_.end()) {
        return it.value();
    }
    if (!create) {
        return nullptr;
    }
    auto buffer = std::make_shared<FileBuffer>();
    buffers_.insert(fileId, buffer);
    return buffer;
}

QVector<scribe::Segment> MainWindow::segmentsForFile(std::int64_t fileId) {
    if (auto buffer = bufferFor(fileId, false); buffer && !buffer->segments.isEmpty()) {
        return buffer->segments;
    }
    QVector<scribe::Segment> out;
    if (controller_->database()) {
        for (const auto &s : controller_->database()->segments(fileId)) {
            out.push_back(s);
        }
    }
    return out;
}

void MainWindow::onQueueSelectionChanged(const QItemSelection &selected, const QItemSelection &) {
    const QModelIndexList indexes = selected.indexes();
    if (indexes.isEmpty()) {
        return;
    }
    const std::int64_t fileId = queueModel_->fileIdAt(indexes.first().row());
    if (fileId < 0) {
        return;
    }
    speakerPanel_->setCurrentFile(fileId);
    transcript_->showFile(fileId, segmentsForFile(fileId));
    transcript_->setSpeakerNames(speakerNames_);
}

void MainWindow::handleSegment(const scribe::EvSegment &ev) {
    auto buffer = bufferFor(ev.file_id, true);
    buffer->segments.push_back(ev.segment);
    transcript_->appendLiveSegment(ev.file_id, ev.segment);
}

void MainWindow::handleLabelled(const scribe::EvSegmentsLabelled &ev) {
    auto buffer = bufferFor(ev.file_id, true);
    QVector<scribe::Segment> labelled;
    labelled.reserve(static_cast<int>(ev.segments.size()));
    QSet<QString> labels;
    for (const auto &s : ev.segments) {
        labelled.push_back(s);
        if (!s.local_label.empty()) {
            labels.insert(QString::fromStdString(s.local_label));
        }
    }
    buffer->segments = labelled;
    transcript_->applyLabelled(ev.file_id, labelled);
    queueModel_->setSpeakerCount(ev.file_id, labels.size());
}

void MainWindow::handleResolved(const scribe::EvSpeakersResolved &ev) {
    auto buffer = bufferFor(ev.file_id, true);
    QHash<QString, std::int64_t> map;
    for (const auto &r : ev.resolutions) {
        map.insert(QString::fromStdString(r.local_label), r.global_id);
    }
    buffer->labelToGlobal = map;
    for (scribe::Segment &seg : buffer->segments) {
        const auto it = map.constFind(QString::fromStdString(seg.local_label));
        if (it != map.constEnd()) {
            seg.global_id = it.value();
        }
    }
    transcript_->applyResolutions(ev.file_id, map);
    queueModel_->setSpeakerCount(ev.file_id, static_cast<int>(ev.resolutions.size()));

    // Resolution can mint new global identities, so refresh names and the panel.
    speakerPanel_->refresh();
    refreshSpeakerNames();
}

void MainWindow::onScribeEvent(const scribe::gui::ScribeEvent &e) {
    std::visit(
        [this](auto &&ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, scribe::EvFileDiscovered>) {
                queueModel_->onDiscovered(ev.job);
            } else if constexpr (std::is_same_v<T, scribe::EvFileStarted>) {
                queueModel_->onStarted(ev.job);
            } else if constexpr (std::is_same_v<T, scribe::EvStage>) {
                queueModel_->onStage(ev.file_id, ev.stage, ev.fraction,
                                     QString::fromStdString(ev.detail));
            } else if constexpr (std::is_same_v<T, scribe::EvSegment>) {
                handleSegment(ev);
            } else if constexpr (std::is_same_v<T, scribe::EvSegmentsLabelled>) {
                handleLabelled(ev);
            } else if constexpr (std::is_same_v<T, scribe::EvSpeakersResolved>) {
                handleResolved(ev);
            } else if constexpr (std::is_same_v<T, scribe::EvFileFinished>) {
                queueModel_->onFinished(ev.file_id, ev.final_stage,
                                        QString::fromStdString(ev.error));
                if (ev.final_stage == scribe::Stage::Failed) {
                    logDock_->append(scribe::LogLevel::Error,
                                     QStringLiteral("File %1 failed: %2")
                                         .arg(ev.file_id)
                                         .arg(QString::fromStdString(ev.error)));
                }
                // Free memory for finished files. The one on screen keeps its
                // buffer, which already holds the fully resolved segments; other
                // files reload from the database when reselected.
                if (ev.final_stage != scribe::Stage::Failed &&
                    transcript_->currentFile() != ev.file_id) {
                    buffers_.remove(ev.file_id);
                }
                speakerPanel_->refresh();
                refreshSpeakerNames();
            } else if constexpr (std::is_same_v<T, scribe::EvRunProgress>) {
                int pct = 0;
                if (ev.audio_total > 0.0) {
                    pct = static_cast<int>(100.0 * ev.audio_done / ev.audio_total + 0.5);
                } else if (ev.files_total > 0) {
                    pct = static_cast<int>(100.0 * ev.files_done / ev.files_total + 0.5);
                }
                overallProgress_->setValue(std::clamp(pct, 0, 100));
                statusLabel_->setText(QStringLiteral("Files %1/%2    %3x realtime")
                                          .arg(ev.files_done)
                                          .arg(ev.files_total)
                                          .arg(ev.realtime_factor, 0, 'f', 1));
            } else if constexpr (std::is_same_v<T, scribe::EvLog>) {
                logDock_->append(ev.level, QString::fromStdString(ev.text));
            } else if constexpr (std::is_same_v<T, scribe::EvModelDownload>) {
                if (ev.finished) {
                    modelLabel_->setVisible(false);
                    modelProgress_->setVisible(false);
                } else {
                    modelLabel_->setText(
                        QStringLiteral("Downloading %1").arg(QString::fromStdString(ev.name)));
                    modelLabel_->setVisible(true);
                    modelProgress_->setVisible(true);
                    if (ev.bytes_total > 0) {
                        modelProgress_->setRange(0, 100);
                        modelProgress_->setValue(static_cast<int>(
                            100.0 * static_cast<double>(ev.bytes_done) /
                            static_cast<double>(ev.bytes_total)));
                    } else {
                        modelProgress_->setRange(0, 0);  // indeterminate
                    }
                }
            } else if constexpr (std::is_same_v<T, scribe::EvRunFinished>) {
                const QString msg =
                    QStringLiteral("Done: %1 finished, %2 failed, %3 skipped%4")
                        .arg(ev.files_done)
                        .arg(ev.files_failed)
                        .arg(ev.files_skipped)
                        .arg(ev.cancelled ? QStringLiteral(" (cancelled)") : QString());
                logDock_->append(scribe::LogLevel::Info, msg);
                statusLabel_->setText(msg);
            }
        },
        e.ev);
}

void MainWindow::restoreLayout() {
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("mainwindow/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(1280, 800);
    }
    const QByteArray state = settings.value(QStringLiteral("mainwindow/state")).toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }
    const QByteArray splitterState =
        settings.value(QStringLiteral("mainwindow/splitter")).toByteArray();
    if (!splitterState.isEmpty()) {
        splitter_->restoreState(splitterState);
    }
    lastFilesDir_ = settings.value(QStringLiteral("paths/lastFiles")).toString();
    lastFolderDir_ = settings.value(QStringLiteral("paths/lastFolder")).toString();
}

void MainWindow::saveLayout() {
    QSettings settings;
    settings.setValue(QStringLiteral("mainwindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainwindow/state"), saveState());
    settings.setValue(QStringLiteral("mainwindow/splitter"), splitter_->saveState());
    settings.setValue(QStringLiteral("paths/lastFiles"), lastFilesDir_);
    settings.setValue(QStringLiteral("paths/lastFolder"), lastFolderDir_);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveLayout();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    QStringList paths;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        const QString local = url.toLocalFile();
        if (!local.isEmpty()) {
            paths << local;
        }
    }
    enqueuePaths(paths);
}

}  // namespace scribe::gui

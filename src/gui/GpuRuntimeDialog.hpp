#pragma once

#include <QDialog>
#include <QThread>

#include "events.hpp"
#include "gpu_runtime.hpp"

class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace scribble::gui {

/// Runs install_gpu_runtime, which blocks for roughly a gigabyte of download and
/// must not touch the GUI thread. It lives on its own QThread; the progress
/// callback fires there, so it is turned into a queued signal the dialog picks
/// up on the GUI thread, the same marshalling the pipeline events use.
class GpuInstaller : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    /// The token is owned by the dialog and outlives the call; the worker only
    /// reads it to poll for a requested stop.
    void setCancel(scribble::CancelToken *cancel) { cancel_ = cancel; }

    void install(scribble::GpuRuntimeStatus status);

signals:
    void progress(const QString &component, qint64 done, qint64 total);
    void finished(bool ok, const QString &error);

private:
    scribble::CancelToken *cancel_ = nullptr;
};

/// Tools entry and proactive offer for the downloadable GPU runtime. Shows what
/// is missing and how large the download is, installs it on a worker thread with
/// live per-component progress, and re-queries the status when it is done.
class GpuRuntimeDialog : public QDialog {
    Q_OBJECT
public:
    explicit GpuRuntimeDialog(QWidget *parent = nullptr);
    ~GpuRuntimeDialog() override;

    /// Starts the install as soon as the dialog is shown. Used by the Auto
    /// runtime mode and by the proactive offer, which have already decided.
    void beginInstallOnShow() { autoStart_ = true; }

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void startInstall();
    void onProgress(const QString &component, qint64 done, qint64 total);
    void onFinished(bool ok, const QString &error);

private:
    void refreshStatus();
    void setInstalling(bool installing);

    scribble::GpuRuntimeStatus status_;
    scribble::CancelToken cancel_;
    bool installing_ = false;
    bool autoStart_ = false;
    bool shown_ = false;

    QThread thread_;
    GpuInstaller *installer_ = nullptr;

    QLabel *summaryLabel_ = nullptr;
    QTreeWidget *componentList_ = nullptr;
    QLabel *totalLabel_ = nullptr;
    QLabel *locationLabel_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QPushButton *installButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
};

}  // namespace scribble::gui

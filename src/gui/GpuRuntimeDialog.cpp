#include "GpuRuntimeDialog.hpp"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <string>

namespace scribble::gui {

namespace {

QString formatBytes(std::int64_t bytes) {
    if (bytes <= 0) {
        return QStringLiteral("--");
    }
    // Decimal units, not binary. These are download sizes, and NVIDIA, GitHub
    // and curl all quote them decimally, so binary units here would disagree
    // with both the core summary line and everything the user sees elsewhere.
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1000.0 && u < 4) {
        v /= 1000.0;
        ++u;
    }
    if (u == 0) {
        return QString::asprintf("%.0f %s", v, units[u]);
    }
    return QString::asprintf("%.1f %s", v, units[u]);
}

QString toQString(const scribble::fs::path &p) {
    return QString::fromStdWString(p.wstring());
}

}  // namespace

void GpuInstaller::install(scribble::GpuRuntimeStatus status) {
    std::string error;
    scribble::CancelToken fallback;
    const scribble::CancelToken &cancel = cancel_ ? *cancel_ : fallback;
    const auto report = [this](const std::string &component, std::int64_t done,
                               std::int64_t total) {
        emit progress(QString::fromStdString(component), static_cast<qint64>(done),
                      static_cast<qint64>(total));
    };
    const bool ok = scribble::install_gpu_runtime(status, report, cancel, &error);
    emit finished(ok, QString::fromStdString(error));
}

GpuRuntimeDialog::GpuRuntimeDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("GPU acceleration"));
    resize(560, 420);

    auto *layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    componentList_ = new QTreeWidget(this);
    componentList_->setColumnCount(2);
    componentList_->setHeaderLabels({QStringLiteral("Component"), QStringLiteral("Download")});
    componentList_->setRootIsDecorated(false);
    componentList_->setUniformRowHeights(true);
    componentList_->setSelectionMode(QAbstractItemView::NoSelection);
    componentList_->setFocusPolicy(Qt::NoFocus);
    componentList_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    componentList_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(componentList_, 1);

    totalLabel_ = new QLabel(this);
    layout->addWidget(totalLabel_);

    locationLabel_ = new QLabel(this);
    locationLabel_->setWordWrap(true);
    locationLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(locationLabel_);

    progressLabel_ = new QLabel(this);
    progressLabel_->setWordWrap(true);
    progressLabel_->setVisible(false);
    layout->addWidget(progressLabel_);

    progressBar_ = new QProgressBar(this);
    progressBar_->setVisible(false);
    layout->addWidget(progressBar_);

    auto *buttons = new QHBoxLayout;
    installButton_ = new QPushButton(QStringLiteral("Install"), this);
    closeButton_ = new QPushButton(QStringLiteral("Close"), this);
    buttons->addWidget(installButton_);
    buttons->addStretch(1);
    buttons->addWidget(closeButton_);
    layout->addLayout(buttons);

    connect(installButton_, &QPushButton::clicked, this, &GpuRuntimeDialog::startInstall);
    connect(closeButton_, &QPushButton::clicked, this, [this] {
        if (installing_) {
            // Ask the worker to stop between components rather than tear the
            // dialog down while a download is in flight.
            cancel_.request_stop();
            closeButton_->setEnabled(false);
            progressLabel_->setText(QStringLiteral("Cancelling after the current component..."));
        } else {
            close();
        }
    });

    installer_ = new GpuInstaller;
    installer_->setCancel(&cancel_);
    installer_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, installer_, &QObject::deleteLater);
    connect(installer_, &GpuInstaller::progress, this, &GpuRuntimeDialog::onProgress);
    connect(installer_, &GpuInstaller::finished, this, &GpuRuntimeDialog::onFinished);
    thread_.start();

    refreshStatus();
}

GpuRuntimeDialog::~GpuRuntimeDialog() {
    cancel_.request_stop();
    thread_.quit();
    thread_.wait();
}

void GpuRuntimeDialog::refreshStatus() {
    status_ = scribble::gpu_runtime_status();

    locationLabel_->setText(
        QStringLiteral("Install location: %1").arg(toQString(scribble::gpu_runtime_dir())));

    if (status_.ready) {
        summaryLabel_->setText(
            QStringLiteral("GPU acceleration is installed and active for the onnxruntime stages."));
        componentList_->clear();
        componentList_->setVisible(false);
        totalLabel_->setVisible(false);
        installButton_->setEnabled(false);
        installButton_->setVisible(false);
        return;
    }

    summaryLabel_->setText(QString::fromStdString(status_.summary()));
    componentList_->setVisible(true);
    componentList_->clear();
    for (const scribble::GpuComponent &c : status_.missing) {
        auto *item = new QTreeWidgetItem(componentList_);
        item->setText(0, QString::fromStdString(c.name));
        item->setText(1, formatBytes(c.download_bytes));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    totalLabel_->setVisible(true);
    totalLabel_->setText(
        QStringLiteral("Total download: %1").arg(formatBytes(status_.download_bytes)));
    installButton_->setVisible(true);
    installButton_->setEnabled(!installing_);
}

void GpuRuntimeDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (shown_) {
        return;
    }
    shown_ = true;
    if (autoStart_ && !status_.ready && !installing_) {
        startInstall();
    }
}

void GpuRuntimeDialog::closeEvent(QCloseEvent *event) {
    if (installing_) {
        cancel_.request_stop();
        closeButton_->setEnabled(false);
        progressLabel_->setText(QStringLiteral("Cancelling after the current component..."));
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void GpuRuntimeDialog::startInstall() {
    if (installing_ || status_.ready) {
        return;
    }
    cancel_.reset();
    setInstalling(true);
    // status_ is copied into the worker; components already present are skipped
    // by install_gpu_runtime, so re-querying here would not change the outcome.
    QMetaObject::invokeMethod(
        installer_, [inst = installer_, status = status_] { inst->install(status); },
        Qt::QueuedConnection);
}

void GpuRuntimeDialog::onProgress(const QString &component, qint64 done, qint64 total) {
    progressLabel_->setText(
        QStringLiteral("Installing %1 (%2 of %3)")
            .arg(component, formatBytes(done), formatBytes(total)));
    if (total > 0) {
        progressBar_->setRange(0, 100);
        progressBar_->setValue(
            static_cast<int>(100.0 * static_cast<double>(done) / static_cast<double>(total)));
    } else {
        progressBar_->setRange(0, 0);  // size unknown, show an indeterminate bar
    }
}

void GpuRuntimeDialog::onFinished(bool ok, const QString &error) {
    setInstalling(false);
    if (!ok) {
        const QString detail = error.isEmpty() ? QStringLiteral("Installation was cancelled.")
                                               : error;
        progressLabel_->setVisible(true);
        progressLabel_->setText(QStringLiteral("Install did not finish: %1").arg(detail));
    } else {
        progressLabel_->setVisible(true);
        progressLabel_->setText(QStringLiteral("Install complete."));
    }
    refreshStatus();
}

void GpuRuntimeDialog::setInstalling(bool installing) {
    installing_ = installing;
    installButton_->setEnabled(!installing && !status_.ready);
    closeButton_->setEnabled(true);
    closeButton_->setText(installing ? QStringLiteral("Cancel") : QStringLiteral("Close"));
    progressLabel_->setVisible(installing);
    progressBar_->setVisible(installing);
    if (installing) {
        progressBar_->setRange(0, 0);
        progressLabel_->setText(QStringLiteral("Preparing download..."));
    }
}

}  // namespace scribble::gui

#include "PipelineController.hpp"

#include <exception>
#include <filesystem>
#include <vector>

namespace scribble::gui {

namespace {

std::vector<std::filesystem::path> toPaths(const QStringList &paths) {
    std::vector<std::filesystem::path> out;
    out.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString &p : paths) {
        out.emplace_back(p.toStdWString());
    }
    return out;
}

}  // namespace

bool PipelineWorker::enqueuePaths(const QStringList &paths, QString *error) {
    if (!pipeline_) {
        if (error) *error = QStringLiteral("Pipeline is not ready.");
        return false;
    }
    try {
        pipeline_->enqueue(toPaths(paths));
        return true;
    } catch (const std::exception &e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
}

void PipelineWorker::enqueue(const QStringList &paths) {
    QString error;
    if (!enqueuePaths(paths, &error)) {
        emit failed(error);
        return;
    }
    // Count is advisory; the queue view is populated from discovery events, not
    // from this number.
    emit enqueued(paths.size());
}

void PipelineWorker::run() {
    if (!pipeline_) {
        emit failed(QStringLiteral("Pipeline is not ready."));
        return;
    }
    try {
        pipeline_->run();
        emit runFinished();
    } catch (const std::exception &e) {
        emit failed(QString::fromUtf8(e.what()));
        emit runFinished();
    }
}

void PipelineWorker::enqueueAndRun(const QStringList &paths) {
    QString error;
    if (!enqueuePaths(paths, &error)) {
        emit failed(error);
        emit runFinished();
        return;
    }
    try {
        pipeline_->run();
        emit runFinished();
    } catch (const std::exception &e) {
        emit failed(QString::fromUtf8(e.what()));
        emit runFinished();
    }
}

void PipelineWorker::recluster() {
    if (!pipeline_) {
        emit failed(QStringLiteral("Pipeline is not ready."));
        return;
    }
    try {
        const scribble::Pipeline::ReclusterResult r = pipeline_->recluster();
        emit reclustered(
            QStringLiteral("Reclustered %1 voiceprints: %2 to %3 speakers, %4 merged, %5 split.")
                .arg(r.locals)
                .arg(r.globals_before)
                .arg(r.globals_after)
                .arg(r.merged)
                .arg(r.split));
    } catch (const std::exception &e) {
        emit failed(QString::fromUtf8(e.what()));
    }
}

void PipelineWorker::rerender(bool all) {
    if (!pipeline_) {
        emit failed(QStringLiteral("Pipeline is not ready."));
        return;
    }
    try {
        emit rerendered(pipeline_->rerender(all));
    } catch (const std::exception &e) {
        emit failed(QString::fromUtf8(e.what()));
    }
}

PipelineController::PipelineController(QObject *parent) : QObject(parent) {
    worker_ = new PipelineWorker;
    worker_->moveToThread(&thread_);

    connect(worker_, &PipelineWorker::enqueued, this, &PipelineController::onEnqueued);
    connect(worker_, &PipelineWorker::runFinished, this, &PipelineController::onRunFinished);
    connect(worker_, &PipelineWorker::reclustered, this, &PipelineController::onReclustered);
    connect(worker_, &PipelineWorker::rerendered, this, &PipelineController::onRerendered);
    connect(worker_, &PipelineWorker::failed, this, &PipelineController::onFailed);

    thread_.start();
}

PipelineController::~PipelineController() {
    // A running batch is asked to stop cooperatively before the thread is torn
    // down, otherwise thread_.wait() would block until a long file finished.
    if (pipeline_) {
        auto &token = pipeline_->cancel();
        token.set_paused(false);
        token.request_stop();
    }
    thread_.quit();
    thread_.wait();
    delete worker_;

    // Destroy the pipeline before the database and sink it borrows references to.
    pipeline_.reset();
    sink_.reset();
    db_.reset();
}

bool PipelineController::open(const scribble::Config &cfg, QString *error) {
    // Resetting the pipeline while the worker is inside run() would free it
    // under the running thread. The toolbar disables these actions during a
    // batch, but a disabled widget is not an invariant, so enforce it here.
    if (busy_) {
        if (error) *error = QStringLiteral("Cannot change configuration while a batch is running.");
        return false;
    }

    config_ = cfg;
    config_.resolve(std::filesystem::current_path());

    if (!sink_) {
        sink_ = std::make_unique<scribble::gui::GuiEventSink>();
        connect(sink_.get(), &scribble::gui::GuiEventSink::event, this,
                &PipelineController::scribeEvent, Qt::QueuedConnection);
    }

    try {
        db_ = std::make_unique<scribble::Database>(config_.db_path);
    } catch (const std::exception &e) {
        if (error) *error = QString::fromUtf8(e.what());
        db_.reset();
        return false;
    }

    pipeline_.reset();
    consumed_ = false;
    return true;
}

bool PipelineController::updateConfig(const scribble::Config &cfg, QString *error) {
    if (busy_) {
        if (error) *error = QStringLiteral("Cannot change configuration while a batch is running.");
        return false;
    }

    const std::filesystem::path oldDb = config_.db_path;
    scribble::Config resolved = cfg;
    resolved.resolve(std::filesystem::current_path());

    if (resolved.db_path != oldDb || !db_) {
        return open(cfg, error);
    }

    config_ = resolved;
    // Config changed, so the current pipeline is stale. Rebuild it lazily.
    pipeline_.reset();
    consumed_ = false;
    return true;
}

void PipelineController::recreatePipeline() {
    pipeline_ = std::make_unique<scribble::Pipeline>(config_, *db_, *sink_);
    consumed_ = false;
    worker_->setPipeline(pipeline_.get());
}

void PipelineController::setBusy(bool value) {
    if (busy_.exchange(value) != value) {
        emit busyChanged(value);
    }
}

void PipelineController::addPaths(const QStringList &paths) {
    if (busy_ || !db_ || paths.isEmpty()) {
        return;
    }
    sessionPaths_ += paths;

    QStringList toEnqueue;
    if (!pipeline_ || consumed_) {
        // A fresh pipeline has no memory of earlier adds, so replay all of them.
        recreatePipeline();
        toEnqueue = sessionPaths_;
    } else {
        toEnqueue = paths;
    }

    setBusy(true);
    QMetaObject::invokeMethod(
        worker_, [w = worker_, toEnqueue] { w->enqueue(toEnqueue); }, Qt::QueuedConnection);
}

void PipelineController::start() {
    if (busy_ || !db_) {
        return;
    }
    if (!pipeline_ || consumed_) {
        recreatePipeline();
        const QStringList paths = sessionPaths_;
        setBusy(true);
        QMetaObject::invokeMethod(
            worker_, [w = worker_, paths] { w->enqueueAndRun(paths); }, Qt::QueuedConnection);
        return;
    }
    setBusy(true);
    QMetaObject::invokeMethod(worker_, [w = worker_] { w->run(); }, Qt::QueuedConnection);
}

void PipelineController::pause(bool paused) {
    if (pipeline_) {
        pipeline_->cancel().set_paused(paused);
    }
}

void PipelineController::stop() {
    if (pipeline_) {
        auto &token = pipeline_->cancel();
        token.set_paused(false);  // a paused run must wake to observe the stop
        token.request_stop();
    }
}

void PipelineController::recluster() {
    if (busy_ || !db_) {
        return;
    }
    if (!pipeline_ || consumed_) {
        recreatePipeline();
    }
    setBusy(true);
    QMetaObject::invokeMethod(worker_, [w = worker_] { w->recluster(); }, Qt::QueuedConnection);
}

void PipelineController::rerender(bool all) {
    if (busy_ || !db_) {
        return;
    }
    if (!pipeline_ || consumed_) {
        recreatePipeline();
    }
    setBusy(true);
    QMetaObject::invokeMethod(
        worker_, [w = worker_, all] { w->rerender(all); }, Qt::QueuedConnection);
}

void PipelineController::onEnqueued(int count) {
    setBusy(false);
    emit enqueueFinished(count);
}

void PipelineController::onRunFinished() {
    consumed_ = true;
    setBusy(false);
    emit runFinished();
}

void PipelineController::onReclustered(const QString &summary) {
    // recluster() is not run(), so the pipeline can still process a batch after.
    setBusy(false);
    emit reclusterFinished(summary);
}

void PipelineController::onRerendered(int count) {
    setBusy(false);
    emit rerenderFinished(count);
}

void PipelineController::onFailed(const QString &message) {
    emit failed(message);
}

}  // namespace scribble::gui

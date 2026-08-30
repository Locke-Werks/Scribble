#pragma once

#include <QObject>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <memory>

#include "GuiEventSink.hpp"
#include "ScribeEvent.hpp"
#include "config.hpp"
#include "db.hpp"
#include "pipeline.hpp"

namespace scribe::gui {

/// Lives on the worker thread and does nothing but call the blocking pipeline
/// entry points. Kept separate from the pipeline so it can own a Qt event loop
/// while the pipeline stays a plain object.
class PipelineWorker : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void setPipeline(scribe::Pipeline *pipeline) { pipeline_ = pipeline; }

public slots:
    void enqueue(const QStringList &paths);
    void run();
    void enqueueAndRun(const QStringList &paths);
    void recluster();
    void rerender(bool all);

signals:
    void enqueued(int count);
    void runFinished();
    void reclustered(const QString &summary);
    void rerendered(int count);
    void failed(const QString &message);

private:
    bool enqueuePaths(const QStringList &paths, QString *error);
    scribe::Pipeline *pipeline_ = nullptr;
};

/// Owns the database, the event sink and the pipeline, and drives them from a
/// single background thread. Stop and pause reach the running pipeline through
/// its lock-free CancelToken, so they work even while run() blocks the worker.
class PipelineController : public QObject {
    Q_OBJECT
public:
    explicit PipelineController(QObject *parent = nullptr);
    ~PipelineController() override;

    /// Opens (or reopens) the database for this config. Returns false and fills
    /// `error` when the database cannot be opened.
    bool open(const scribe::Config &cfg, QString *error);
    bool isOpen() const { return db_ != nullptr; }

    scribe::Database *database() const { return db_.get(); }
    const scribe::Config &config() const { return config_; }

    /// Applies edited settings. Reopens the database when its path moved and
    /// drops the current pipeline so the next run picks up the new config.
    bool updateConfig(const scribe::Config &cfg, QString *error);

    bool busy() const { return busy_.load(); }

public slots:
    void addPaths(const QStringList &paths);
    void start();
    void pause(bool paused);
    void stop();
    void recluster();
    void rerender(bool all);

signals:
    void scribeEvent(const scribe::gui::ScribeEvent &e);
    void busyChanged(bool busy);
    void enqueueFinished(int count);
    void runFinished();
    void reclusterFinished(const QString &summary);
    void rerenderFinished(int count);
    void failed(const QString &message);

private slots:
    void onEnqueued(int count);
    void onRunFinished();
    void onReclustered(const QString &summary);
    void onRerendered(int count);
    void onFailed(const QString &message);

private:
    void recreatePipeline();
    void setBusy(bool value);

    scribe::Config config_;
    std::unique_ptr<scribe::Database> db_;
    std::unique_ptr<scribe::gui::GuiEventSink> sink_;
    std::unique_ptr<scribe::Pipeline> pipeline_;

    QThread thread_;
    PipelineWorker *worker_ = nullptr;

    QStringList sessionPaths_;  ///< every path the user added this session
    bool consumed_ = false;     ///< current pipeline already had run() called
    std::atomic<bool> busy_{false};
};

}  // namespace scribe::gui

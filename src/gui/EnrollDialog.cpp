#include "EnrollDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QtConcurrent>

#include <algorithm>
#include <functional>

#include "ThemeQt.hpp"
#include "ThemeWidgets.hpp"
#include "db.hpp"

namespace scribble::gui {
namespace {

constexpr int kColumnClip = 0;
constexpr int kColumnSpeech = 1;
constexpr int kColumnCoherence = 2;
constexpr int kColumnVerdict = 3;

/// What one worker pass produced. Carried back whole rather than emitted per
/// clip so the table is rebuilt once, not once per file.
///
/// The loaded Enroller comes back with it. The worker cannot store it on the
/// dialog directly: the dialog can be closed while a pass is in flight, and
/// writing to a member from the pool thread would be a data race on a good day
/// and a use-after-free on a bad one.
struct BatchResult {
    std::vector<scribble::EnrollClipResult> clips;
    std::vector<QString> failures;  ///< parallel to clips, empty when fine
    QString fatal;                  ///< model or ffmpeg missing, nothing ran
    std::shared_ptr<scribble::Enroller> enroller;
};

}  // namespace

EnrollDialog::EnrollDialog(scribble::Database *db, scribble::Config config, QWidget *parent)
    : QDialog(parent), db_(db), config_(std::move(config)) {
    setWindowTitle(QStringLiteral("Enroll a voice"));
    resize(620, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 16);
    layout->setSpacing(12);
    layout->addWidget(eyebrow(QStringLiteral("// Enroll"), 15, this));

    auto *blurb = new QLabel(
        QStringLiteral("Reference audio of one person, and only that person. Several short "
                       "clips from different recordings beat one long clip: a voice on a "
                       "phone and the same voice in a room are two different things to match "
                       "against, and enrolling both recognises both."),
        this);
    blurb->setWordWrap(true);
    blurb->setFont(theme::body(13));
    layout->addWidget(blurb);

    auto *who = new QHBoxLayout;
    who->setSpacing(8);
    who->addWidget(caption(QStringLiteral("Speaker"), scribble::theme::kFg3, 12, this));
    speaker_ = new QComboBox(this);
    speaker_->setFont(theme::body(13));
    who->addWidget(speaker_, 1);
    name_ = new QLineEdit(this);
    name_->setFont(theme::body(13));
    name_->setPlaceholderText(QStringLiteral("Name"));
    who->addWidget(name_, 1);
    layout->addLayout(who);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({QStringLiteral("CLIP"), QStringLiteral("SPEECH"),
                                       QStringLiteral("ONE VOICE"), QStringLiteral("VERDICT")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setMouseTracking(true);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->horizontalHeader()->setFont(theme::tracked(10, QFont::DemiBold, 0.16));
    table_->horizontalHeader()->setSectionResizeMode(kColumnClip, QHeaderView::Stretch);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(kColumnVerdict, QHeaderView::Stretch);
    layout->addWidget(table_, 1);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setFont(theme::body(12));
    layout->addWidget(summary_);

    auto *buttons = new QDialogButtonBox(this);
    addBtn_ = buttons->addButton(QStringLiteral("Add Clips"), QDialogButtonBox::ActionRole);
    removeBtn_ = buttons->addButton(QStringLiteral("Remove"), QDialogButtonBox::ActionRole);
    enrollBtn_ = buttons->addButton(QStringLiteral("Enroll"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    styleButtonBox(buttons);
    layout->addWidget(buttons);

    connect(addBtn_, &QPushButton::clicked, this, &EnrollDialog::addClips);
    connect(removeBtn_, &QPushButton::clicked, this, &EnrollDialog::removeSelected);
    connect(enrollBtn_, &QPushButton::clicked, this, &EnrollDialog::commit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(speaker_, &QComboBox::currentIndexChanged, this, &EnrollDialog::onSpeakerChanged);
    connect(name_, &QLineEdit::textChanged, this, [this] { updateSummary(); });

    reloadSpeakers();
    refreshTable();
}

EnrollDialog::~EnrollDialog() = default;

void EnrollDialog::reloadSpeakers() {
    const QSignalBlocker block(speaker_);
    speaker_->clear();
    speaker_->addItem(QStringLiteral("New person"), QVariant::fromValue<qlonglong>(-1));
    if (!db_) {
        return;
    }
    for (const auto &g : db_->globals()) {
        QString label = QString::fromStdString(g.display());
        if (g.enrolled) {
            label += QStringLiteral("  (%1 clip%2)").arg(g.n_clips).arg(g.n_clips == 1 ? "" : "s");
        }
        speaker_->addItem(label, QVariant::fromValue<qlonglong>(g.id));
    }
}

void EnrollDialog::selectSpeaker(std::int64_t globalId) {
    const int index = speaker_->findData(QVariant::fromValue<qlonglong>(globalId));
    if (index >= 0) {
        speaker_->setCurrentIndex(index);
    }
}

std::int64_t EnrollDialog::selectedGlobal() const {
    return speaker_->currentData().toLongLong();
}

void EnrollDialog::onSpeakerChanged(int) {
    // The name field only means anything when there is no identity to attach
    // to. Left editable on an existing unnamed speaker, which is the one case
    // where enrolling and naming happen in the same motion.
    const std::int64_t id = selectedGlobal();
    bool wantsName = id < 0;
    if (!wantsName && db_) {
        auto existing = db_->global(id);
        wantsName = existing && existing->name.empty();
    }
    name_->setEnabled(wantsName);
    if (!wantsName) {
        name_->clear();
    }
    updateSummary();
}

void EnrollDialog::addClips() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Reference audio"), lastDir_,
        QStringLiteral("Audio and video (*.wav *.mp3 *.m4a *.flac *.ogg *.opus *.aac *.wma "
                       "*.mp4 *.mkv *.mov *.avi *.webm);;All files (*)"));
    if (paths.isEmpty()) {
        return;
    }
    lastDir_ = QFileInfo(paths.front()).absolutePath();
    analyse(paths);
}

void EnrollDialog::analyse(const QStringList &paths) {
    setWorking(true, QStringLiteral("Analysing %1 clip%2. The speaker embedding model "
                                    "downloads on first use.")
                         .arg(paths.size())
                         .arg(paths.size() == 1 ? "" : "s"));

    // Everything the worker needs is copied in. The dialog can be closed while
    // a pass is in flight, and reaching back into it from the pool thread would
    // be a use-after-free.
    scribble::Config config = config_;
    auto enroller = enroller_;
    std::vector<std::string> inputs;
    for (const QString &p : paths) {
        inputs.push_back(p.toStdString());
    }

    auto *watcher = new QFutureWatcher<BatchResult>(this);
    connect(watcher, &QFutureWatcher<BatchResult>::finished, this, [this, watcher] {
        const BatchResult batch = watcher->result();
        watcher->deleteLater();
        setWorking(false);

        if (batch.enroller) {
            enroller_ = batch.enroller;
        }
        if (!batch.fatal.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Enroll"), batch.fatal);
            return;
        }
        for (size_t i = 0; i < batch.clips.size(); ++i) {
            Row row;
            row.result = batch.clips[i];
            row.analysed = true;
            row.failure = i < batch.failures.size() ? batch.failures[i] : QString();
            rows_.push_back(row);
        }
        refreshTable();
    });

    watcher->setFuture(QtConcurrent::run([config, enroller, inputs]() mutable {
        BatchResult batch;

        if (!enroller) {
            std::string error;
            auto created = scribble::Enroller::create(config, {}, &error);
            if (!created) {
                batch.fatal = QString::fromStdString(error);
                return batch;
            }
            enroller = std::shared_ptr<scribble::Enroller>(created.release());
        }
        batch.enroller = enroller;

        scribble::EnrollOptions opts;
        opts.window = config.enroll_window;
        opts.hop = std::max(1.0, config.enroll_window / 2.0);
        opts.min_speech = config.enroll_min_clip;

        for (const auto &input : inputs) {
            scribble::EnrollSource source;
            source.path = input;

            scribble::EnrollClipResult clip;
            std::string error;
            if (!enroller->analyse(source, opts, &clip, &error)) {
                clip.source = source.path;
                clip.usable = false;
                batch.clips.push_back(clip);
                batch.failures.emplace_back(QString::fromStdString(error));
                continue;
            }
            batch.clips.push_back(clip);
            batch.failures.emplace_back();
        }
        return batch;
    }));
}

void EnrollDialog::refreshTable() {
    table_->setRowCount(rows_.size());
    for (int i = 0; i < rows_.size(); ++i) {
        const Row &row = rows_[i];
        const scribble::EnrollClipResult &clip = row.result;

        auto *name = new QTableWidgetItem(
            QFileInfo(QString::fromStdString(clip.source.string())).fileName());
        name->setToolTip(QString::fromStdString(clip.source.string()));
        name->setFont(theme::body(13));

        auto *speech = new QTableWidgetItem(QStringLiteral("%1s").arg(clip.duration, 0, 'f', 1));
        auto *coherence = new QTableWidgetItem(
            clip.windows > 1 ? QString::number(clip.coherence, 'f', 2) : QStringLiteral("-"));

        QString verdict;
        if (!row.failure.isEmpty()) {
            verdict = row.failure;
        } else if (!clip.usable) {
            verdict = QString::fromStdString(clip.problem);
        } else if (!clip.problem.empty()) {
            verdict = QString::fromStdString(clip.problem);
        } else {
            verdict = QStringLiteral("good");
        }
        auto *state = new QTableWidgetItem(verdict);
        state->setToolTip(verdict);

        const QColor ink = clip.usable && row.failure.isEmpty() && clip.problem.empty()
                               ? theme::c(scribble::theme::kFg2)
                               : theme::c(scribble::theme::kRed);
        state->setForeground(ink);

        for (QTableWidgetItem *item : {speech, coherence}) {
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            item->setFont(theme::mono(11));
        }
        state->setFont(theme::body(12));

        table_->setItem(i, kColumnClip, name);
        table_->setItem(i, kColumnSpeech, speech);
        table_->setItem(i, kColumnCoherence, coherence);
        table_->setItem(i, kColumnVerdict, state);
    }
    updateSummary();
}

void EnrollDialog::removeSelected() {
    const auto selected = table_->selectionModel()->selectedRows();
    QVector<int> victims;
    for (const QModelIndex &idx : selected) {
        victims.push_back(idx.row());
    }
    std::sort(victims.begin(), victims.end(), std::greater<int>());
    for (int row : victims) {
        if (row >= 0 && row < rows_.size()) {
            rows_.remove(row);
        }
    }
    refreshTable();
}

void EnrollDialog::updateSummary() {
    int usable = 0;
    double speech = 0.0;
    for (const Row &row : rows_) {
        if (row.result.usable && row.failure.isEmpty()) {
            ++usable;
            speech += row.result.duration;
        }
    }

    if (rows_.isEmpty()) {
        summary_->setText(QStringLiteral("No clips yet."));
    } else {
        summary_->setText(QStringLiteral("%1 of %2 clip%3 usable, %4s of speech.")
                              .arg(usable)
                              .arg(rows_.size())
                              .arg(rows_.size() == 1 ? "" : "s")
                              .arg(speech, 0, 'f', 1));
    }

    const bool named = selectedGlobal() >= 0 || !name_->text().trimmed().isEmpty();
    enrollBtn_->setEnabled(!working_ && usable > 0 && named);
    removeBtn_->setEnabled(!working_ && !rows_.isEmpty());
}

void EnrollDialog::setWorking(bool working, const QString &what) {
    working_ = working;
    addBtn_->setEnabled(!working);
    speaker_->setEnabled(!working);
    if (working) {
        enrollBtn_->setEnabled(false);
        removeBtn_->setEnabled(false);
        summary_->setText(what);
    } else {
        updateSummary();
    }
    setCursor(working ? Qt::BusyCursor : Qt::ArrowCursor);
}

void EnrollDialog::commit() {
    if (!db_) {
        return;
    }

    std::int64_t globalId = selectedGlobal();
    const std::string name = name_->text().trimmed().toStdString();
    if (globalId < 0 && name.empty()) {
        QMessageBox::warning(this, QStringLiteral("Enroll"),
                             QStringLiteral("A new person needs a name."));
        return;
    }

    int added = 0;
    try {
        db_->begin();
        for (const Row &row : rows_) {
            if (!row.result.usable || !row.failure.isEmpty()) {
                continue;
            }
            globalId = scribble::commit_enrollment(*db_, globalId, name, row.result);
            ++added;
        }
        db_->commit();
    } catch (const std::exception &e) {
        db_->rollback();
        QMessageBox::warning(this, QStringLiteral("Enroll"),
                             QStringLiteral("Could not save: %1").arg(e.what()));
        return;
    }

    if (added == 0) {
        return;
    }

    emit enrolled();

    auto stored = db_->global(globalId);
    const QString who = stored ? QString::fromStdString(stored->display()) : QStringLiteral("that speaker");
    QMessageBox::information(
        this, QStringLiteral("Enrolled"),
        QStringLiteral("%1 is enrolled from %2 reference clip%3.\n\n"
                       "Files processed from now on match against it. Transcripts already "
                       "on disk keep the identities they were written with: re-run those "
                       "files to apply the profile to them.")
            .arg(who)
            .arg(stored ? stored->n_clips : added)
            .arg((stored ? stored->n_clips : added) == 1 ? "" : "s"));

    rows_.clear();
    reloadSpeakers();
    selectSpeaker(globalId);
    refreshTable();
}

}  // namespace scribble::gui

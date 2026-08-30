#include "DuplicatesDialog.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "db.hpp"
#include "speakers.hpp"

namespace scribe::gui {

DuplicatesDialog::DuplicatesDialog(scribe::Database *db, float low, float high, QWidget *parent)
    : QDialog(parent), db_(db), low_(low), high_(high) {
    setWindowTitle(QStringLiteral("Review duplicate speakers"));
    resize(560, 420);

    auto *layout = new QVBoxLayout(this);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Speaker A"), QStringLiteral("Speaker B"),
         QStringLiteral("Similarity"), QStringLiteral("Files")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(table_, 1);

    auto *buttons = new QDialogButtonBox(this);
    auto *mergeBtn = buttons->addButton(QStringLiteral("Merge"), QDialogButtonBox::ActionRole);
    auto *dismissBtn = buttons->addButton(QStringLiteral("Dismiss"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(mergeBtn, &QPushButton::clicked, this, &DuplicatesDialog::mergeSelected);
    connect(dismissBtn, &QPushButton::clicked, this, &DuplicatesDialog::dismissSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    reload();
}

void DuplicatesDialog::reload() {
    candidates_.clear();
    if (db_) {
        const auto found = scribe::duplicate_candidates(*db_, low_, high_);
        for (const auto &c : found) {
            candidates_.push_back(c);
        }
    }

    table_->setRowCount(candidates_.size());
    for (int i = 0; i < candidates_.size(); ++i) {
        const scribe::DuplicateCandidate &c = candidates_[i];
        auto *a = new QTableWidgetItem(
            QStringLiteral("%1 (%2 files)").arg(QString::fromStdString(c.left_display)).arg(c.left_files));
        auto *b = new QTableWidgetItem(
            QStringLiteral("%1 (%2 files)").arg(QString::fromStdString(c.right_display)).arg(c.right_files));
        auto *sim = new QTableWidgetItem(QString::number(c.similarity, 'f', 3));
        auto *files = new QTableWidgetItem(QString::number(c.left_files + c.right_files));
        sim->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        files->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(i, 0, a);
        table_->setItem(i, 1, b);
        table_->setItem(i, 2, sim);
        table_->setItem(i, 3, files);
    }

    if (candidates_.isEmpty()) {
        summary_->setText(QStringLiteral("No candidate pairs in the review band."));
    } else {
        summary_->setText(
            QStringLiteral("%1 candidate pair(s). Select a row, then Merge to fold them into one "
                           "identity or Dismiss to hide the pair from future reviews.")
                .arg(candidates_.size()));
    }
}

void DuplicatesDialog::mergeSelected() {
    const int row = table_->currentRow();
    if (row < 0 || row >= candidates_.size() || !db_) {
        return;
    }
    const scribe::DuplicateCandidate c = candidates_[row];

    // Keep the more established identity: a named one wins, otherwise the one
    // present in more files, so human naming is never discarded by a merge.
    const bool leftNamed = c.left_display.rfind("SPEAKER_", 0) != 0;
    const bool rightNamed = c.right_display.rfind("SPEAKER_", 0) != 0;
    std::int64_t into = c.left;
    std::int64_t from = c.right;
    if (rightNamed && !leftNamed) {
        into = c.right;
        from = c.left;
    } else if (leftNamed == rightNamed && c.right_files > c.left_files) {
        into = c.right;
        from = c.left;
    }

    db_->merge_globals(from, into);
    db_->delete_empty_globals();
    anyMerged_ = true;
    emit merged();
    reload();
}

void DuplicatesDialog::dismissSelected() {
    const int row = table_->currentRow();
    if (row < 0 || row >= candidates_.size() || !db_) {
        return;
    }
    const scribe::DuplicateCandidate c = candidates_[row];
    // Persisted so the pair stays hidden across restarts. duplicate_candidates
    // already excludes dismissed pairs, so reloading drops it from the list.
    db_->dismiss_pair(c.left, c.right);
    reload();
}

}  // namespace scribe::gui

#include "SpeakerPanel.hpp"

#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

#include <vector>

#include "SpeakerModel.hpp"
#include "db.hpp"

namespace scribe::gui {

SpeakerPanel::SpeakerPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *heading = new QLabel(QStringLiteral("Speakers"), this);
    heading->setContentsMargins(8, 6, 8, 0);
    QFont hf = heading->font();
    hf.setBold(true);
    heading->setFont(hf);
    layout->addWidget(heading);

    model_ = new SpeakerModel(this);
    view_ = new QTableView(this);
    view_->setModel(model_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->verticalHeader()->setVisible(false);
    view_->horizontalHeader()->setStretchLastSection(false);
    view_->horizontalHeader()->setSectionResizeMode(SpeakerModel::ColumnName, QHeaderView::Stretch);
    view_->horizontalHeader()->setSectionResizeMode(SpeakerModel::ColumnFiles,
                                                    QHeaderView::ResizeToContents);
    view_->horizontalHeader()->setSectionResizeMode(SpeakerModel::ColumnDuration,
                                                    QHeaderView::ResizeToContents);
    view_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(view_, 1);

    auto *reviewBtn = new QPushButton(QStringLiteral("Review duplicates"), this);
    layout->addWidget(reviewBtn);

    connect(model_, &SpeakerModel::renameRequested, this, &SpeakerPanel::onRenameRequested);
    connect(view_, &QTableView::customContextMenuRequested, this, &SpeakerPanel::showContextMenu);
    connect(reviewBtn, &QPushButton::clicked, this, &SpeakerPanel::reviewDuplicatesRequested);
}

void SpeakerPanel::setDatabase(scribe::Database *db) {
    db_ = db;
    refresh();
}

void SpeakerPanel::refresh() {
    if (!db_) {
        model_->setSpeakers({});
        return;
    }
    model_->setSpeakers(db_->globals());
}

std::int64_t SpeakerPanel::selectedGlobal() const {
    const QModelIndex idx = view_->currentIndex();
    if (!idx.isValid()) {
        return -1;
    }
    return model_->globalIdAt(idx.row());
}

void SpeakerPanel::onRenameRequested(std::int64_t globalId, const QString &name) {
    if (!db_) {
        return;
    }
    db_->rename_global(globalId, name.toStdString());
    refresh();
    emit speakersChanged();
}

void SpeakerPanel::showContextMenu(const QPoint &pos) {
    const QModelIndex idx = view_->indexAt(pos);
    if (!idx.isValid() || !db_) {
        return;
    }
    view_->setCurrentIndex(idx);
    const std::int64_t globalId = model_->globalIdAt(idx.row());

    QMenu menu(this);
    QAction *mergeAct = menu.addAction(QStringLiteral("Merge into..."));
    QAction *splitAct = menu.addAction(QStringLiteral("Split this file's speaker out"));
    splitAct->setEnabled(currentFile_ >= 0);
    menu.addSeparator();
    QAction *noteAct = menu.addAction(QStringLiteral("Add note..."));

    QAction *chosen = menu.exec(view_->viewport()->mapToGlobal(pos));
    if (chosen == mergeAct) {
        mergeInto(globalId);
    } else if (chosen == splitAct) {
        splitThisFile(globalId);
    } else if (chosen == noteAct) {
        addNote(globalId);
    }
}

void SpeakerPanel::mergeInto(std::int64_t globalId) {
    const auto speakers = db_->globals();
    QStringList labels;
    std::vector<std::int64_t> ids;
    for (const auto &sp : speakers) {
        if (sp.id == globalId) {
            continue;
        }
        labels << QString::fromStdString(sp.display());
        ids.push_back(sp.id);
    }
    if (labels.isEmpty()) {
        emit status(QStringLiteral("No other speaker to merge into."));
        return;
    }

    bool ok = false;
    const QString pick = QInputDialog::getItem(this, QStringLiteral("Merge speaker"),
                                               QStringLiteral("Fold this speaker into:"), labels,
                                               0, false, &ok);
    if (!ok) {
        return;
    }
    const int index = labels.indexOf(pick);
    if (index < 0) {
        return;
    }
    db_->merge_globals(globalId, ids[static_cast<std::size_t>(index)]);
    db_->delete_empty_globals();
    refresh();
    emit speakersChanged();
}

void SpeakerPanel::splitThisFile(std::int64_t globalId) {
    if (currentFile_ < 0) {
        return;
    }
    // Find the file-local speaker that resolved to this global and pull it out
    // into a fresh identity, undoing a clustering mistake for this file only.
    const auto locals = db_->local_speakers(currentFile_);
    std::int64_t localId = -1;
    for (const auto &ls : locals) {
        if (ls.global_id == globalId) {
            localId = ls.id;
            break;
        }
    }
    if (localId < 0) {
        emit status(QStringLiteral("This speaker does not appear in the selected file."));
        return;
    }
    db_->split_local(localId);
    refresh();
    emit speakersChanged();
}

void SpeakerPanel::addNote(std::int64_t globalId) {
    const auto current = db_->global(globalId);
    const QString existing = current ? QString::fromStdString(current->notes) : QString();

    bool ok = false;
    const QString note = QInputDialog::getMultiLineText(this, QStringLiteral("Speaker note"),
                                                        QStringLiteral("Note:"), existing, &ok);
    if (!ok) {
        return;
    }
    db_->set_global_notes(globalId, note.toStdString());
    refresh();
    emit speakersChanged();
}

}  // namespace scribe::gui

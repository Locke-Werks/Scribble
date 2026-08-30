#include "TranscriptView.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>

#include <functional>

#include "SegmentDelegate.hpp"
#include "TranscriptModel.hpp"

namespace scribble::gui {

namespace {

/// Filters on the combined speaker + text role so a search matches either.
class TranscriptFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &parent) const override {
        if (filterRegularExpression().pattern().isEmpty()) {
            return true;
        }
        const QModelIndex idx = sourceModel()->index(source_row, 0, parent);
        const QString hay = idx.data(TranscriptModel::SearchRole).toString();
        return hay.contains(filterRegularExpression());
    }
};

/// A list view that keeps a wrapping delegate informed of its width and relays
/// out on resize, which a plain QListView will not do for variable-height rows.
class TranscriptListView : public QListView {
public:
    explicit TranscriptListView(QWidget *parent = nullptr) : QListView(parent) {}
    std::function<void()> onResized;

protected:
    void resizeEvent(QResizeEvent *event) override {
        QListView::resizeEvent(event);
        if (onResized) {
            onResized();
        }
        scheduleDelayedItemsLayout();
    }
};

}  // namespace

TranscriptView::TranscriptView(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    heading_ = new QLabel(QStringLiteral("No file selected"), this);
    heading_->setContentsMargins(8, 6, 8, 6);
    QFont hf = heading_->font();
    hf.setBold(true);
    heading_->setFont(hf);
    layout->addWidget(heading_);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(QStringLiteral("Filter transcript"));
    search_->setClearButtonEnabled(true);
    search_->setContentsMargins(8, 0, 8, 4);
    layout->addWidget(search_);

    auto *view = new TranscriptListView(this);
    list_ = view;
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setUniformItemSizes(false);
    list_->setWordWrap(true);
    list_->setResizeMode(QListView::Adjust);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    layout->addWidget(list_, 1);

    model_ = new TranscriptModel(this);
    proxy_ = new TranscriptFilter(this);
    proxy_->setSourceModel(model_);
    proxy_->setDynamicSortFilter(true);
    list_->setModel(proxy_);

    delegate_ = new SegmentDelegate(this);
    list_->setItemDelegate(delegate_);

    view->onResized = [this, view] {
        delegate_->setViewportWidth(view->viewport()->width());
    };
    delegate_->setViewportWidth(view->viewport()->width());

    connect(search_, &QLineEdit::textChanged, this, &TranscriptView::applyFilter);
}

bool TranscriptView::atBottom() const {
    QScrollBar *sb = list_->verticalScrollBar();
    return sb->value() >= sb->maximum() - 4;
}

void TranscriptView::scrollToBottomIfFollowing(bool wasAtBottom) {
    if (wasAtBottom) {
        list_->scrollToBottom();
    }
}

void TranscriptView::showFile(std::int64_t fileId, const QVector<scribble::Segment> &segments) {
    fileId_ = fileId;
    model_->reset(segments);
    heading_->setText(QStringLiteral("%1 segments").arg(segments.size()));
    list_->scrollToBottom();
}

void TranscriptView::clearFile() {
    fileId_ = -1;
    model_->reset({});
    heading_->setText(QStringLiteral("No file selected"));
}

void TranscriptView::appendLiveSegment(std::int64_t fileId, const scribble::Segment &segment) {
    if (fileId != fileId_) {
        return;
    }
    const bool follow = atBottom();
    model_->appendSegment(segment);
    heading_->setText(QStringLiteral("%1 segments").arg(model_->rowCount()));
    scrollToBottomIfFollowing(follow);
}

void TranscriptView::applyLabelled(std::int64_t fileId,
                                   const QVector<scribble::Segment> &segments) {
    if (fileId != fileId_) {
        return;
    }
    const bool follow = atBottom();
    model_->applyLabelled(segments);
    scrollToBottomIfFollowing(follow);
}

void TranscriptView::applyResolutions(std::int64_t fileId,
                                      const QHash<QString, std::int64_t> &labelToGlobal) {
    if (fileId != fileId_) {
        return;
    }
    model_->applyResolutions(labelToGlobal);
}

void TranscriptView::setSpeakerNames(const QHash<std::int64_t, QString> &names) {
    model_->setNameResolver(names);
}

void TranscriptView::applyFilter(const QString &text) {
    proxy_->setFilterRegularExpression(
        QRegularExpression(QRegularExpression::escape(text),
                           QRegularExpression::CaseInsensitiveOption));
}

}  // namespace scribble::gui

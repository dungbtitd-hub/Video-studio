#include "Favorites.h"

#include <QAbstractItemModel>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QTimer>

#include "EditorController.h"

namespace {
constexpr int kBaseText = Qt::UserRole + 42;
constexpr char kStar[] = "★ ";
}  // namespace

namespace favorites {

void mark(QListWidget* list, EditorController* editor, const QString& category) {
    QList<QListWidgetItem*> favs, rest;
    while (list->count() > 0) {
        QListWidgetItem* it = list->takeItem(0);
        if (!it->data(kBaseText).isValid()) it->setData(kBaseText, it->text());
        const QString id = it->data(Qt::UserRole).toString();
        const bool fav = !id.isEmpty() && editor->isFavorite(category, id);
        it->setText(fav ? kStar + it->data(kBaseText).toString() : it->data(kBaseText).toString());
        (fav ? favs : rest).push_back(it);
    }
    // Favourites first, in the order they were starred (most recent first).
    const QStringList order = editor->favorites(category);
    std::stable_sort(favs.begin(), favs.end(), [&](QListWidgetItem* a, QListWidgetItem* b) {
        return order.indexOf(a->data(Qt::UserRole).toString()) < order.indexOf(b->data(Qt::UserRole).toString());
    });
    for (auto* it : favs) list->addItem(it);
    for (auto* it : rest) list->addItem(it);
    list->viewport()->update();
}

void attach(QListWidget* list, EditorController* editor, const QString& category) {
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(list, &QListWidget::customContextMenuRequested, list, [list, editor, category](const QPoint& pos) {
        QListWidgetItem* it = list->itemAt(pos);
        if (!it) return;
        const QString id = it->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        QMenu menu(list);
        const bool fav = editor->isFavorite(category, id);
        menu.addAction(fav ? "Bỏ khỏi Yêu thích" : "★ Thêm vào Yêu thích", list, [editor, category, id] { editor->toggleFavorite(category, id); });
        menu.exec(list->viewport()->mapToGlobal(pos));
    });
    // Re-mark after a refill (deferred: the panel may still be adding items).
    struct State {
        bool pending = false, marking = false;
    };
    auto st = std::make_shared<State>();
    QPointer<QListWidget> guard(list);
    auto schedule = [guard, editor, category, st] {
        if (st->pending || st->marking) return;  // our own re-ordering inserts rows too
        st->pending = true;
        QTimer::singleShot(0, guard, [guard, editor, category, st] {
            st->pending = false;
            if (!guard) return;
            st->marking = true;
            mark(guard, editor, category);
            st->marking = false;
        });
    };
    QObject::connect(list->model(), &QAbstractItemModel::rowsInserted, list, schedule);
    QObject::connect(editor, &EditorController::favoritesChanged, list, schedule);
    schedule();
}

}  // namespace favorites

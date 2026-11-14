// Favourites on any item list (looks, transitions, video effects, stickers, overlays):
// right-click an item to add/remove it; favourites are marked with a star and listed first.
// Items are identified by their Qt::UserRole id. The list is re-marked whenever it is refilled.
#pragma once

#include <QString>

class EditorController;
class QListWidget;

namespace favorites {
void attach(QListWidget* list, EditorController* editor, const QString& category);
// Stars and orders the items now (attach does it automatically after every refill).
void mark(QListWidget* list, EditorController* editor, const QString& category);
}  // namespace favorites

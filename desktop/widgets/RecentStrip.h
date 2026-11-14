// "Tiếp tục dự án": a strip that slides up at the bottom of the window when the app starts, with the
// projects not exported since their last change (poster, name, when, length). One click opens one.
// It slides away when a project is opened, when editing starts, or with ✕.
#pragma once

#include <QFrame>
#include <QVariantList>
#include <functional>

class QHBoxLayout;
class QLabel;
class QParallelAnimationGroup;
class QGraphicsOpacityEffect;

class RecentStrip : public QFrame {
    Q_OBJECT
public:
    using PosterLoader = std::function<QImage(const QString& path)>;
    explicit RecentStrip(QWidget* parent);

    // Cards: {path, name, savedAt, duration, clips, ...} (EditorController::recentProjects).
    void setCards(const QVariantList& cards, const PosterLoader& poster);
    [[nodiscard]] int cardCount() const { return count_; }
    void slideIn();
    void slideOut();
    [[nodiscard]] bool shown() const { return shown_; }
    // Keeps the strip at the bottom of the parent (call from the parent's resizeEvent).
    void place();

    static QString ago(qint64 savedAt, qint64 now);
    static QString length(double seconds);

signals:
    void openRequested(const QString& path);
    void forgetRequested(const QString& path);
    void browseRequested();

private:
    [[nodiscard]] QPoint restPos() const;
    QHBoxLayout* cards_ = nullptr;
    QLabel* title_ = nullptr;
    QParallelAnimationGroup* anim_ = nullptr;
    QGraphicsOpacityEffect* fade_ = nullptr;
    int count_ = 0;
    bool shown_ = false;
};

// Start page (desktop "home" of the approved prototype): the app opens here, not in the editor.
// Left: Video Studio, Dự án, Phím tắt. Right: "Dự án mới" and the usual frame shapes, "Mở tệp
// .vstudio", then the recent projects as cards (poster, name, length · shape · when); right-click
// or "⋯" on a card opens it, takes it off the list or deletes the project.
#pragma once

#include <QVariantList>
#include <QWidget>
#include <functional>

class QGridLayout;
class QLabel;

class HomePage : public QWidget {
    Q_OBJECT
public:
    using PosterLoader = std::function<QImage(const QString& path)>;
    explicit HomePage(QWidget* parent);
    // Cards: EditorController::recentProjects() entries.
    void setProjects(const QVariantList& projects, const PosterLoader& poster);
    [[nodiscard]] int projectCount() const { return count_; }

signals:
    void newProjectRequested(int width, int height);  // 0 x 0: ask (default 16:9)
    void openRequested(const QString& path);
    void browseRequested();
    void forgetRequested(const QString& path);
    void deleteRequested(const QString& path);
    void shortcutsRequested();

private:
    void cardMenu(const QVariantMap& p, const QPoint& globalPos);
    QGridLayout* grid_ = nullptr;
    QLabel* empty_ = nullptr;
    int count_ = 0;
};

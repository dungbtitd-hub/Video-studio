// "Scope": waveform, RGB parade, vectorscope or histogram of the preview frame.
// Computed off the UI thread from the newest frame only (latest wins), and only
// while the panel is visible, so playback never waits for it.
#pragma once

#include <QImage>
#include <QPointer>
#include <QWidget>
#include <atomic>
#include <memory>

class EditorController;
class QComboBox;
class QLabel;

class ScopesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScopesPanel(EditorController* editor, QWidget* parent = nullptr);
    ~ScopesPanel() override;
    [[nodiscard]] QComboBox* kind() const { return kind_; }
    [[nodiscard]] QImage scope() const { return scope_; }  // last drawn scope (tests)

signals:
    void scopeUpdated();

protected:
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;

private:
    void onFrame(const QImage& frame);
    void start();  // computes the pending frame on the thread pool

    EditorController* editor_;
    QComboBox* kind_;
    QLabel* view_;
    QImage pending_, last_, scope_;
    bool busy_ = false;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

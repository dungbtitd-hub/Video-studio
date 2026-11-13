// Shows the newest preview frame; painting only scales an existing image.
#pragma once

#include <QImage>
#include <QWidget>

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setFrame(const QImage& img);

signals:
    void previewSizeChanged(int w, int h);  // device pixels

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QImage frame_;
};

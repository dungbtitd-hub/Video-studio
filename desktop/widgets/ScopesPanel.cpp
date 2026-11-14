#include "ScopesPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QThreadPool>
#include <QVBoxLayout>
#include <cstring>

#include "EditorController.h"
#include "vstudio/scopes.hpp"

ScopesPanel::ScopesPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(6, 6, 6, 6);
    kind_ = new QComboBox(this);
    for (int k = 0; k < 4; ++k)
        kind_->addItem(QString::fromUtf8(vstudio::scopeName(static_cast<vstudio::ScopeKind>(k))), k);
    kind_->setToolTip("Dạng sóng: độ sáng theo chiều ngang khung hình (dưới = đen, trên = trắng).\n"
                      "Vectorscope: màu sắc và độ bão hoà; đường chéo là màu da.\n"
                      "Histogram: phân bố độ sáng của từng kênh.");
    l->addWidget(kind_);
    view_ = new QLabel(this);
    view_->setAlignment(Qt::AlignCenter);
    view_->setMinimumSize(200, 120);
    view_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    l->addWidget(view_, 1);
    connect(kind_, &QComboBox::currentIndexChanged, this, [this] {
        if (!last_.isNull()) onFrame(last_);
    });
    connect(editor_, &EditorController::frameReady, this, &ScopesPanel::onFrame);
}

ScopesPanel::~ScopesPanel() { *alive_ = false; }

void ScopesPanel::onFrame(const QImage& frame) {
    last_ = frame;  // implicitly shared: no copy
    if (!isVisible()) return;
    pending_ = frame;
    if (!busy_) start();
}

void ScopesPanel::showEvent(QShowEvent*) {
    if (!last_.isNull()) onFrame(last_);
}

void ScopesPanel::resizeEvent(QResizeEvent*) {
    if (!last_.isNull()) onFrame(last_);
}

void ScopesPanel::start() {
    if (pending_.isNull()) return;
    QImage frame = pending_.convertToFormat(QImage::Format_RGBA8888);
    pending_ = {};
    busy_ = true;
    const auto kind = static_cast<vstudio::ScopeKind>(kind_->currentData().toInt());
    const QSize size = view_->size().expandedTo(QSize(64, 48)) * devicePixelRatioF();
    QPointer<ScopesPanel> self(this);
    auto alive = alive_;
    QThreadPool::globalInstance()->start([self, alive, frame, kind, size] {
        vstudio::VideoFrame f;
        f.width = frame.width();
        f.height = frame.height();
        f.rgba.resize(static_cast<std::size_t>(f.width) * f.height * 4);
        for (int y = 0; y < f.height; ++y)
            std::memcpy(f.rgba.data() + static_cast<std::size_t>(y) * f.width * 4, frame.constScanLine(y), f.width * 4);
        const auto s = vstudio::renderScope(f, kind, size.width(), size.height());
        QImage img(s.width, s.height, QImage::Format_RGBA8888);
        for (int y = 0; y < s.height; ++y)
            std::memcpy(img.scanLine(y), s.rgba.data() + static_cast<std::size_t>(y) * s.width * 4, s.width * 4);
        if (!*alive) return;
        QMetaObject::invokeMethod(
            qApp, [self, img] {
                if (!self) return;
                self->busy_ = false;
                self->scope_ = img;
                QPixmap pm = QPixmap::fromImage(img);
                pm.setDevicePixelRatio(self->devicePixelRatioF());
                self->view_->setPixmap(pm);
                emit self->scopeUpdated();
                if (!self->pending_.isNull()) self->start();  // newest frame that arrived meanwhile
            },
            Qt::QueuedConnection);
    });
}

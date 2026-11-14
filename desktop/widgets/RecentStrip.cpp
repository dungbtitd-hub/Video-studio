#include "RecentStrip.h"

#include <QDateTime>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QStatusBar>
#include <QMainWindow>
#include <QToolButton>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

constexpr int kHeight = 204;
constexpr int kPosterW = 168, kPosterH = 94;

// One project: poster, name, "2 giờ trước · 0:42". Click opens, right click offers "Bỏ khỏi danh sách".
class Card : public QWidget {
public:
    Card(const QVariantMap& c, const QImage& poster, RecentStrip* strip, QWidget* parent)
        : QWidget(parent), strip_(strip), path_(c.value("path").toString()), name_(c.value("name").toString()) {
        setObjectName("recentCard");
        setFixedSize(kPosterW + 12, kPosterH + 50);
        setCursor(Qt::PointingHandCursor);
        setToolTip(path_);
        meta_ = RecentStrip::ago(c.value("savedAt").toLongLong(), QDateTime::currentSecsSinceEpoch());
        const double d = c.value("duration").toDouble();
        if (d > 0) meta_ += " · " + RecentStrip::length(d);
        if (!poster.isNull()) poster_ = QPixmap::fromImage(poster.scaled(kPosterW, kPosterH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        setAttribute(Qt::WA_Hover);
    }
    QString path() const { return path_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool hot = underMouse();
        p.setPen(QPen(hot ? Theme::accent() : Theme::line(), 1));
        p.setBrush(hot ? Theme::raised() : Theme::panel2());
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        const QRect box(6, 6, kPosterW, kPosterH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#0b0d10"));
        p.drawRoundedRect(box, 5, 5);
        if (!poster_.isNull()) {
            const QPoint at(box.x() + (box.width() - poster_.width()) / 2, box.y() + (box.height() - poster_.height()) / 2);
            p.drawPixmap(at, poster_);
        } else {
            p.setPen(Theme::fg3());
            p.drawText(box, Qt::AlignCenter, "Video Studio");
        }
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(Theme::fg());
        const QRect nameR(8, kPosterH + 12, width() - 16, 18);
        p.drawText(nameR, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(f).elidedText(name_, Qt::ElideRight, nameR.width()));
        f.setBold(false);
        f.setPointSizeF(f.pointSizeF() * 0.85);
        p.setFont(f);
        p.setPen(Theme::fg3());
        p.drawText(QRect(8, kPosterH + 30, width() - 16, 16), Qt::AlignLeft | Qt::AlignVCenter, meta_);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) emit strip_->openRequested(path_);
    }
    void contextMenuEvent(QContextMenuEvent* e) override {
        QMenu m(this);
        m.addAction("Mở", this, [this] { emit strip_->openRequested(path_); });
        m.addAction("Bỏ khỏi danh sách", this, [this] { emit strip_->forgetRequested(path_); });
        m.exec(e->globalPos());
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    RecentStrip* strip_;
    QString path_, name_, meta_;
    QPixmap poster_;
};

}  // namespace

RecentStrip::RecentStrip(QWidget* parent) : QFrame(parent) {
    setObjectName("recentStrip");
    setStyleSheet("#recentStrip { background: #171a20; border: 1px solid #2c323c; border-radius: 12px; }"
                  "#recentStrip QScrollArea, #recentStrip #recentCards { background: transparent; border: none; }");
    setFixedHeight(kHeight);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 10, 10, 10);
    v->setSpacing(8);
    auto* head = new QHBoxLayout();
    title_ = new QLabel(this);
    title_->setStyleSheet("font-weight: 600; color: #e7e9ee; background: transparent;");
    head->addWidget(title_);
    head->addStretch();
    auto* browse = new QToolButton(this);
    browse->setObjectName("recentBrowse");
    browse->setText("Mở dự án khác…");
    connect(browse, &QToolButton::clicked, this, [this] { emit browseRequested(); });
    head->addWidget(browse);
    auto* close = new QToolButton(this);
    close->setObjectName("recentClose");
    close->setText("✕");
    close->setToolTip("Ẩn");
    connect(close, &QToolButton::clicked, this, &RecentStrip::slideOut);
    head->addWidget(close);
    v->addLayout(head);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* inner = new QWidget(scroll);
    inner->setObjectName("recentCards");
    cards_ = new QHBoxLayout(inner);
    cards_->setContentsMargins(0, 0, 0, 0);
    cards_->setSpacing(10);
    cards_->addStretch();
    scroll->setWidget(inner);
    v->addWidget(scroll, 1);

    fade_ = new QGraphicsOpacityEffect(this);
    fade_->setOpacity(0);
    setGraphicsEffect(fade_);
    anim_ = new QParallelAnimationGroup(this);
    hide();
}

void RecentStrip::setCards(const QVariantList& cards, const PosterLoader& poster) {
    while (cards_->count() > 1) {
        QLayoutItem* it = cards_->takeAt(0);
        delete it->widget();
        delete it;
    }
    count_ = 0;
    for (const QVariant& v : cards) {
        const QVariantMap c = v.toMap();
        const QString path = c.value("path").toString();
        const QImage img = c.value("hasPoster").toBool() && poster ? poster(path) : QImage();
        cards_->insertWidget(count_++, new Card(c, img, this, cards_->parentWidget()));
    }
    title_->setText(QString("Tiếp tục dự án chưa xong (%1)").arg(count_));
}

QPoint RecentStrip::restPos() const {
    const QWidget* p = parentWidget();
    int bottom = p->height();
    if (auto* mw = qobject_cast<const QMainWindow*>(p); mw && mw->statusBar() && mw->statusBar()->isVisible())
        bottom -= mw->statusBar()->height();
    return {12, bottom - kHeight - 10};
}

void RecentStrip::place() {
    if (!parentWidget()) return;
    setFixedWidth(std::max(320, parentWidget()->width() - 24));
    if (anim_->state() != QAbstractAnimation::Running && shown_) move(restPos());
}

void RecentStrip::slideIn() {
    if (count_ == 0) return;
    shown_ = true;
    place();
    anim_->stop();
    anim_->clear();
    const QPoint to = restPos(), from = isVisible() ? this->pos() : to + QPoint(0, 40);
    auto* pos = new QPropertyAnimation(this, "pos");
    pos->setDuration(320);
    pos->setStartValue(from);
    pos->setEndValue(to);
    pos->setEasingCurve(QEasingCurve::OutCubic);
    auto* op = new QPropertyAnimation(fade_, "opacity");
    op->setDuration(260);
    op->setStartValue(fade_->opacity());
    op->setEndValue(1.0);
    anim_->addAnimation(pos);
    anim_->addAnimation(op);
    move(from);
    show();
    raise();
    anim_->start();
}

void RecentStrip::slideOut() {
    if (!shown_) return;
    shown_ = false;
    anim_->stop();
    anim_->clear();
    auto* pos = new QPropertyAnimation(this, "pos");
    pos->setDuration(240);
    pos->setStartValue(this->pos());
    pos->setEndValue(restPos() + QPoint(0, 40));
    pos->setEasingCurve(QEasingCurve::InCubic);
    auto* op = new QPropertyAnimation(fade_, "opacity");
    op->setDuration(240);
    op->setStartValue(fade_->opacity());
    op->setEndValue(0.0);
    anim_->addAnimation(pos);
    anim_->addAnimation(op);
    connect(anim_, &QAbstractAnimation::finished, this, [this] {
        if (!shown_) hide();
    }, Qt::SingleShotConnection);
    anim_->start();
}

QString RecentStrip::ago(qint64 savedAt, qint64 now) {
    const qint64 d = std::max<qint64>(0, now - savedAt);
    if (savedAt <= 0) return {};
    if (d < 60) return "Vừa xong";
    if (d < 3600) return QString("%1 phút trước").arg(d / 60);
    if (d < 86400) return QString("%1 giờ trước").arg(d / 3600);
    if (d < 2 * 86400) return "Hôm qua";
    if (d < 7 * 86400) return QString("%1 ngày trước").arg(d / 86400);
    return QDateTime::fromSecsSinceEpoch(savedAt).toString("dd/MM/yyyy");
}

QString RecentStrip::length(double seconds) {
    const int s = static_cast<int>(seconds + 0.5);
    if (s >= 3600) return QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

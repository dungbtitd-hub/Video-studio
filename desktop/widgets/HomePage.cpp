#include "HomePage.h"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "RecentStrip.h"
#include "Theme.h"

namespace {

QString shapeLabel(int w, int h) {
    if (w <= 0 || h <= 0) return {};
    const struct {
        const char* name;
        int w, h;
    } shapes[] = {{"16:9", 16, 9}, {"9:16", 9, 16}, {"1:1", 1, 1}, {"4:5", 4, 5}};
    for (const auto& s : shapes)
        if (std::abs(double(w) * s.h - double(h) * s.w) <= double(w) * s.h * 0.01) return s.name;
    return QString("%1×%2").arg(w).arg(h);
}

// A big button of the "new" row: a frame-shape drawing, a title and a line under it.
class ShapeCard : public QPushButton {
public:
    ShapeCard(const QString& title, const QString& sub, int w, int h, bool primary, QWidget* parent)
        : QPushButton(parent), title_(title), sub_(sub), w_(w), h_(h), primary_(primary) {
        setCursor(Qt::PointingHandCursor);
        setMinimumSize(200, 116);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(title);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool hover = underMouse();
        p.setPen(primary_ ? Qt::NoPen : QPen(hover ? Theme::accent() : Theme::line()));
        p.setBrush(primary_ ? (hover ? Theme::accent().lighter(110) : Theme::accent()) : (hover ? Theme::raised() : Theme::panel()));
        p.drawRoundedRect(r, 10, 10);
        const QColor ink = primary_ ? Theme::accentInk() : Theme::fg();
        int y = 18;
        if (w_ != 0) {
            // The frame shape (dashed for "open a file").
            const double k = 28.0 / std::max(std::abs(w_), std::abs(h_));
            QPen pen(ink, 1.6);
            if (w_ < 0) pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(18, y, std::abs(w_) * k, std::abs(h_) * k), 3, 3);
            y += 40;
        } else {
            QFont f = font();
            f.setPointSizeF(f.pointSizeF() * 1.05);
            f.setBold(true);
            p.setFont(f);
            p.setPen(ink);
            p.drawText(QRectF(18, y, width() - 36, 22), Qt::AlignLeft | Qt::AlignVCenter, "+ " + title_);
            QFont s = font();
            s.setPointSizeF(s.pointSizeF() * 0.9);
            p.setFont(s);
            p.setPen(primary_ ? QColor(27, 18, 6, 190) : Theme::fg2());
            p.drawText(QRectF(18, y + 24, width() - 36, 20), Qt::AlignLeft | Qt::AlignVCenter, sub_);
            return;
        }
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(ink);
        p.drawText(QRectF(18, y, width() - 36, 22), Qt::AlignLeft | Qt::AlignVCenter, title_);
        QFont s = font();
        s.setPointSizeF(s.pointSizeF() * 0.9);
        p.setFont(s);
        p.setPen(Theme::fg2());
        p.drawText(QRectF(18, y + 22, width() - 36, 20), Qt::AlignLeft | Qt::AlignVCenter, sub_);
    }
    void enterEvent(QEnterEvent* e) override {
        update();
        QPushButton::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        update();
        QPushButton::leaveEvent(e);
    }

private:
    QString title_, sub_;
    int w_, h_;
    bool primary_;
};

// One recent project: poster with its shape, name, "length · shape · when"; ⋯ for the menu.
class ProjectCard : public QWidget {
public:
    ProjectCard(const QVariantMap& p, const QImage& poster, QWidget* parent) : QWidget(parent), p_(p), poster_(poster) {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(212, 178);
        setObjectName("homeProject");
        setAccessibleName(p["name"].toString());
        more_ = new QToolButton(this);
        more_->setText("⋯");
        more_->setToolTip("Tùy chọn");
        more_->setAutoRaise(true);
        more_->setGeometry(width() - 34, 124, 30, 26);
    }
    std::function<void()> onOpen;
    std::function<void(QPoint)> onMenu;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF img(0, 0, width(), 118);
        QPainterPath clip;
        clip.addRoundedRect(img, 8, 8);
        p.save();
        p.setClipPath(clip);
        p.fillRect(img, QColor("#0b0d10"));
        if (!poster_.isNull()) {
            const QImage s = poster_.scaled(img.size().toSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            p.drawImage(QPointF((img.width() - s.width()) / 2, (img.height() - s.height()) / 2), s);
        } else {
            p.setPen(Theme::fg3());
            p.drawText(img, Qt::AlignCenter, "🎬");
        }
        p.restore();
        p.setPen(underMouse() ? Theme::accent() : Theme::line());
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(img.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        const QString shape = shapeLabel(p_["width"].toInt(), p_["height"].toInt());
        if (!shape.isEmpty()) {
            QFont b = font();
            b.setBold(true);
            b.setPointSizeF(b.pointSizeF() * 0.8);
            p.setFont(b);
            const QRectF tag(6, img.height() - 22, QFontMetricsF(b).horizontalAdvance(shape) + 10, 16);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 170));
            p.drawRoundedRect(tag, 3, 3);
            p.setPen(Qt::white);
            p.drawText(tag, Qt::AlignCenter, shape);
        }
        QFont n = font();
        n.setBold(true);
        p.setFont(n);
        p.setPen(Theme::fg());
        p.drawText(QRectF(0, 124, width() - 36, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(n).elidedText(p_["name"].toString(), Qt::ElideRight, width() - 36));
        QFont s = font();
        s.setPointSizeF(s.pointSizeF() * 0.85);
        p.setFont(s);
        p.setPen(Theme::fg3());
        QStringList meta;
        if (p_["duration"].toDouble() > 0) meta << RecentStrip::length(p_["duration"].toDouble());
        if (!shape.isEmpty()) meta << shape;
        meta << RecentStrip::ago(p_["savedAt"].toLongLong(), QDateTime::currentSecsSinceEpoch()).toLower();
        p.drawText(QRectF(0, 146, width() - 36, 18), Qt::AlignLeft | Qt::AlignVCenter, meta.join(" · "));
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && onOpen) onOpen();
        else if (e->button() == Qt::RightButton && onMenu) onMenu(e->globalPosition().toPoint());
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

public:
    QToolButton* more_ = nullptr;

private:
    QVariantMap p_;
    QImage poster_;
};

}  // namespace

HomePage::HomePage(QWidget* parent) : QWidget(parent) {
    setObjectName("homePage");
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::bg());
    setPalette(pal);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    // Sidebar
    auto* side = new QWidget(this);
    side->setFixedWidth(230);
    side->setAutoFillBackground(true);
    QPalette sp = side->palette();
    sp.setColor(QPalette::Window, Theme::panel());
    side->setPalette(sp);
    auto* sl = new QVBoxLayout(side);
    sl->setContentsMargins(20, 18, 16, 18);
    auto* brand = new QLabel("<span style='color:#f2a93b'>▶</span>&nbsp; <b>Video Studio</b>", side);
    brand->setStyleSheet("font-size: 16px;");
    sl->addWidget(brand);
    sl->addSpacing(18);
    auto navBtn = [&](const QString& text, bool on) {
        auto* b = new QPushButton(text, side);
        b->setFlat(true);
        b->setStyleSheet(QString("text-align: left; padding: 7px 10px; border-radius: 6px; %1")
                             .arg(on ? "background: #252a33; color: #e7e9ee;" : "color: #a4abb8;"));
        sl->addWidget(b);
        return b;
    };
    navBtn("Dự án", true);
    connect(navBtn("Hướng dẫn && phím tắt", false), &QPushButton::clicked, this, &HomePage::shortcutsRequested);
    sl->addStretch();
    auto* foot = new QLabel("Hoạt động ngoại tuyến. Không cần tài khoản để biên tập, lưu hay xuất video.", side);
    foot->setWordWrap(true);
    foot->setStyleSheet("color: #6e7685; font-size: 11px;");
    sl->addWidget(foot);
    row->addWidget(side);

    // Main
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* main = new QWidget(scroll);
    auto* ml = new QVBoxLayout(main);
    ml->setContentsMargins(36, 30, 36, 30);
    auto* hello = new QLabel("Chào bạn, hôm nay dựng gì?", main);
    hello->setStyleSheet("font-size: 24px; font-weight: 700; color: #e7e9ee;");
    ml->addWidget(hello);
    auto* sub = new QLabel("Tạo dự án mới theo tỉ lệ khung hình, hoặc mở lại dự án gần đây.", main);
    sub->setStyleSheet("color: #a4abb8;");
    ml->addWidget(sub);
    ml->addSpacing(18);
    auto* cards = new QHBoxLayout();
    cards->setSpacing(14);
    auto* newCard = new ShapeCard("Dự án mới", "Ngang 16:9, chọn video và ảnh", 0, 0, true, main);
    newCard->setObjectName("homeNew");
    auto* wide = new ShapeCard("Video ngang 16:9", "1920 × 1080 · 30 fps", 16, 9, false, main);
    wide->setObjectName("homeNew16x9");
    auto* tall = new ShapeCard("Video dọc 9:16", "1080 × 1920 · 30 fps", 9, 16, false, main);
    tall->setObjectName("homeNew9x16");
    auto* square = new ShapeCard("Vuông 1:1", "1080 × 1080 · 30 fps", 1, 1, false, main);
    square->setObjectName("homeNew1x1");
    auto* open = new ShapeCard("Mở tệp .vstudio", "Từ máy tính hoặc ổ ngoài", -1, -1, false, main);
    open->setObjectName("homeOpen");
    for (QPushButton* c : {static_cast<QPushButton*>(newCard), static_cast<QPushButton*>(wide), static_cast<QPushButton*>(tall),
                           static_cast<QPushButton*>(square), static_cast<QPushButton*>(open)})
        cards->addWidget(c);
    connect(newCard, &QPushButton::clicked, this, [this] { emit newProjectRequested(1920, 1080); });
    connect(wide, &QPushButton::clicked, this, [this] { emit newProjectRequested(1920, 1080); });
    connect(tall, &QPushButton::clicked, this, [this] { emit newProjectRequested(1080, 1920); });
    connect(square, &QPushButton::clicked, this, [this] { emit newProjectRequested(1080, 1080); });
    connect(open, &QPushButton::clicked, this, &HomePage::browseRequested);
    ml->addLayout(cards);
    ml->addSpacing(26);
    auto* recentTitle = new QLabel("Dự án gần đây", main);
    recentTitle->setStyleSheet("font-size: 15px; font-weight: 700; color: #e7e9ee;");
    ml->addWidget(recentTitle);
    ml->addSpacing(8);
    empty_ = new QLabel("Chưa có dự án nào. Bấm \"Dự án mới\" để bắt đầu.", main);
    empty_->setStyleSheet("color: #6e7685; padding: 18px 0;");
    ml->addWidget(empty_);
    auto* gridBox = new QWidget(main);
    grid_ = new QGridLayout(gridBox);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(16);
    grid_->setVerticalSpacing(14);
    grid_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    ml->addWidget(gridBox);
    ml->addStretch();
    scroll->setWidget(main);
    row->addWidget(scroll, 1);
}

void HomePage::setProjects(const QVariantList& projects, const PosterLoader& poster) {
    while (QLayoutItem* it = grid_->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    count_ = int(projects.size());
    empty_->setVisible(count_ == 0);
    const int cols = 5;
    for (int i = 0; i < count_; ++i) {
        const QVariantMap p = projects[i].toMap();
        const QString path = p["path"].toString();
        auto* c = new ProjectCard(p, p["hasPoster"].toBool() && poster ? poster(path) : QImage(), this);
        c->onOpen = [this, path] { emit openRequested(path); };
        c->onMenu = [this, p](QPoint g) { cardMenu(p, g); };
        connect(c->more_, &QToolButton::clicked, this, [this, p, c] { cardMenu(p, c->more_->mapToGlobal(QPoint(0, c->more_->height()))); });
        grid_->addWidget(c, i / cols, i % cols);
    }
}

void HomePage::cardMenu(const QVariantMap& p, const QPoint& g) {
    const QString path = p["path"].toString(), name = p["name"].toString();
    QMenu m(this);
    m.addAction("Mở dự án", this, [this, path] { emit openRequested(path); });
    m.addAction("Bỏ khỏi danh sách", this, [this, path] { emit forgetRequested(path); });
    m.addSeparator();
    QAction* del = m.addAction("Xóa dự án…");
    connect(del, &QAction::triggered, this, [this, path, name] {
        const auto ok = QMessageBox::question(
            this, "Xóa dự án?",
            QString("\"%1\" sẽ bị xóa khỏi máy: thư mục %2, kể cả media đã chép vào trong đó.\nFile video và ảnh gốc ở chỗ khác không bị xóa.").arg(name, path),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (ok == QMessageBox::Yes) emit deleteRequested(path);
    });
    m.exec(g);
}

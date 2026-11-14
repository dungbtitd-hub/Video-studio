#include "AnimationBox.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <algorithm>
#include <cstring>

#include "EditorController.h"
#include "vstudio/generators.hpp"
#include "vstudio/motion.hpp"

namespace {
constexpr int kPw = 192, kPh = 108;

QSlider* lengthSlider(QWidget* parent) {
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(1, 30);  // tenths of a second
    s->setToolTip(QObject::tr("Độ dài (giây)"));
    return s;
}
}  // namespace

QImage AnimationBox::render(const vstudio::Json& c, double local, double length, int w, int h) {
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    const auto gen = vstudio::generatorFor(nullptr, c.value("kind", std::string{}));
    if (!gen) return img;
    const auto b = gen->bounds(c);
    const double bw = std::max(1.0, b[2] - b[0]), bh = std::max(1.0, b[3] - b[1]);
    const double k = std::min(w * 0.8 / bw, h * 0.8 / bh);
    vstudio::VideoFrame f;
    f.allocate(w, h);
    gen->draw(c, local, length,
              vstudio::vec::Affine::translate(w / 2.0 - (b[0] + b[2]) / 2 * k, h / 2.0 - (b[1] + b[3]) / 2 * k) *
                  vstudio::vec::Affine::scale(k),
              f);
    std::memcpy(img.bits(), f.rgba.data(), f.rgba.size());
    return img;
}

AnimationBox::AnimationBox(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    preview_ = new QLabel(this);
    preview_->setFixedSize(kPw, kPh);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setStyleSheet("background: #2a3140; border-radius: 6px;");
    l->addWidget(preview_, 0, Qt::AlignHCenter);
    auto* form = new QFormLayout();
    in_ = new QComboBox(this);
    out_ = new QComboBox(this);
    loop_ = new QComboBox(this);
    for (auto* b : {in_, out_}) {
        b->addItem(tr("Không"), QString());
        for (const auto& a : vstudio::motion::animations()) b->addItem(QString::fromStdString(a.name), QString::fromStdString(a.id));
    }
    loop_->addItem(tr("Không"), QString());
    for (const auto& a : vstudio::motion::loops()) loop_->addItem(QString::fromStdString(a.name), QString::fromStdString(a.id));
    inLen_ = lengthSlider(this);
    outLen_ = lengthSlider(this);
    auto row = [&](QComboBox* box, QSlider* len) {
        auto* w = new QWidget(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(box, 2);
        if (len) h->addWidget(len, 1);
        return w;
    };
    form->addRow(tr("Hiện vào"), row(in_, inLen_));
    form->addRow(tr("Biến mất"), row(out_, outLen_));
    form->addRow(tr("Lặp lại"), row(loop_, nullptr));
    l->addLayout(form);

    connect(in_, &QComboBox::activated, this, [this] { commit("in", in_, inLen_); });
    connect(out_, &QComboBox::activated, this, [this] { commit("out", out_, outLen_); });
    connect(loop_, &QComboBox::activated, this, [this] { commit("loop", loop_, nullptr); });
    connect(inLen_, &QSlider::sliderReleased, this, [this] { commit("in", in_, inLen_); });
    connect(outLen_, &QSlider::sliderReleased, this, [this] { commit("out", out_, outLen_); });
    timer_ = new QTimer(this);
    timer_->setInterval(40);
    connect(timer_, &QTimer::timeout, this, &AnimationBox::tick);
    setContent(nullptr);
}

void AnimationBox::setContent(const vstudio::Json* c) {
    setEnabled(c != nullptr);
    content_ = c ? *c : vstudio::Json();
    auto load = [&](QComboBox* box, QSlider* len, const char* key, double def) {
        QSignalBlocker b1(box);
        const vstudio::Json spec = c && c->contains(key) && (*c)[key].is_object() ? (*c)[key] : vstudio::Json::object();
        const int i = box->findData(QString::fromStdString(spec.value("type", std::string{})));
        box->setCurrentIndex(std::max(0, i));
        if (len) {
            QSignalBlocker b2(len);
            len->setValue(int(std::lround(spec.value("duration", def) * 10)));
        }
    };
    load(in_, inLen_, "in", vstudio::motion::kDefaultIn);
    load(out_, outLen_, "out", vstudio::motion::kDefaultOut);
    load(loop_, nullptr, "loop", 0);
    if (c) {
        if (!timer_->isActive()) t_ = 0;
        timer_->start();
    } else {
        timer_->stop();
        preview_->clear();
    }
}

void AnimationBox::commit(const char* key, QComboBox* box, QSlider* duration) {
    vstudio::Json spec{{"type", box->currentData().toString().toStdString()}};
    if (duration) spec["duration"] = duration->value() / 10.0;
    else spec["speed"] = 1.0;
    editor_->updateContent({{key, spec}});
    t_ = 0;  // replay from the start
}

void AnimationBox::tick() {
    if (content_.is_null()) return;
    // In, ~1.5 s at rest (loops show), out, a short pause.
    auto len = [&](const char* key, double def) {
        const auto& s = content_.contains(key) ? content_[key] : vstudio::Json();
        return s.is_object() && !s.value("type", std::string{}).empty() ? s.value("duration", def) : 0.0;
    };
    const double length = len("in", vstudio::motion::kDefaultIn) + 1.5 + len("out", vstudio::motion::kDefaultOut);
    t_ += 0.04;
    if (t_ > length + 0.5) t_ = 0;
    preview_->setPixmap(QPixmap::fromImage(render(content_, std::min(t_, length), length, kPw, kPh)));
}

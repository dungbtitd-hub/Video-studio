#include "TextPanel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "AnimationBox.h"
#include "EditorController.h"
#include "vstudio/fonts.hpp"
#include "vstudio/text.hpp"

using vstudio::Json;

namespace {
QSlider* slider(int lo, int hi, QWidget* parent) {
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(lo, hi);
    return s;
}
QString colorOf(const Json& c, const char* key, const char* def) {
    return QString::fromStdString(c.value(key, std::string(def)));
}
}  // namespace

TextPanel::TextPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* body = new QWidget(scroll);
    scroll->setWidget(body);
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(8, 8, 8, 8);

    add_ = new QPushButton(tr("＋ Thêm chữ"), body);
    add_->setObjectName("primary");
    add_->setToolTip(tr("Thêm chữ tại đầu phát, trên một track riêng phía trên video"));
    l->addWidget(add_);
    hint_ = new QLabel(tr("Bấm “Thêm chữ”, hoặc chọn một clip chữ trên Timeline để sửa. "
                          "Kéo chữ trên khung xem trước để di chuyển, kéo góc để phóng to."),
                       body);
    hint_->setWordWrap(true);
    hint_->setStyleSheet("color: #a4abb8;");
    l->addWidget(hint_);

    editorBox_ = new QWidget(body);
    auto* el = new QVBoxLayout(editorBox_);
    el->setContentsMargins(0, 0, 0, 0);
    text_ = new QPlainTextEdit(editorBox_);
    text_->setPlaceholderText(tr("Nhập chữ…"));
    text_->setFixedHeight(64);
    el->addWidget(text_);
    typing_ = new QTimer(this);
    typing_->setSingleShot(true);
    typing_->setInterval(600);

    auto* form = new QFormLayout();
    font_ = new QComboBox(editorBox_);
    for (const auto& f : vstudio::text::fontCatalog())
        font_->addItem(QString::fromStdString(f.name), QString::fromStdString(f.id));
    form->addRow(tr("Phông"), font_);
    size_ = slider(16, 400, editorBox_);
    form->addRow(tr("Cỡ chữ"), size_);
    auto* look = new QWidget(editorBox_);
    auto* lh = new QHBoxLayout(look);
    lh->setContentsMargins(0, 0, 0, 0);
    color_ = new QPushButton(look);
    color_->setFixedWidth(44);
    color_->setToolTip(tr("Màu chữ"));
    lh->addWidget(color_);
    auto* group = new QButtonGroup(this);
    const char* glyphs[] = {"⯇", "≡", "⯈"};
    const char* tips[] = {"Căn trái", "Căn giữa", "Căn phải"};
    for (int i = 0; i < 3; ++i) {
        align_[i] = new QToolButton(look);
        align_[i]->setText(QString::fromUtf8(glyphs[i]));
        align_[i]->setToolTip(tr(tips[i]));
        align_[i]->setCheckable(true);
        group->addButton(align_[i], i);
        lh->addWidget(align_[i]);
    }
    italic_ = new QCheckBox(tr("Nghiêng"), look);
    lh->addWidget(italic_);
    lh->addStretch();
    form->addRow(tr("Màu, căn"), look);
    outline_ = slider(0, 20, editorBox_);
    form->addRow(tr("Viền"), outline_);
    shadow_ = slider(0, 100, editorBox_);
    form->addRow(tr("Bóng đổ"), shadow_);
    box_ = slider(0, 100, editorBox_);
    form->addRow(tr("Nền chữ"), box_);
    el->addLayout(form);

    auto* stylesLabel = new QLabel(tr("Kiểu chữ"), editorBox_);
    stylesLabel->setStyleSheet("color: #a4abb8;");
    el->addWidget(stylesLabel);
    styles_ = new QListWidget(editorBox_);
    styles_->setViewMode(QListView::IconMode);
    styles_->setIconSize(QSize(96, 40));
    styles_->setGridSize(QSize(108, 70));
    styles_->setResizeMode(QListView::Adjust);
    styles_->setMovement(QListView::Static);
    styles_->setMinimumHeight(160);
    for (const auto& p : vstudio::text::textPresets()) {
        Json c = vstudio::text::defaultTextContent("Aa Chữ");
        c.update(p.style);
        c["in"] = Json::object();
        c["out"] = Json::object();
        auto* it = new QListWidgetItem(QString::fromStdString(p.name), styles_);
        QImage img(96, 40, QImage::Format_RGBA8888);
        img.fill(QColor(70, 92, 120));
        QPainter painter(&img);
        painter.drawImage(0, 0, AnimationBox::render(c, 1, 2, 96, 40));
        painter.end();
        it->setIcon(QPixmap::fromImage(img));
        it->setData(Qt::UserRole, QString::fromStdString(p.id));
    }
    el->addWidget(styles_);
    auto* animLabel = new QLabel(tr("Hiệu ứng"), editorBox_);
    animLabel->setStyleSheet("color: #a4abb8;");
    el->addWidget(animLabel);
    anim_ = new AnimationBox(editor_, editorBox_);
    el->addWidget(anim_);
    l->addWidget(editorBox_);
    l->addStretch();

    connect(add_, &QPushButton::clicked, this, [this] { editor_->addText(); });
    connect(text_, &QPlainTextEdit::textChanged, typing_, qOverload<>(&QTimer::start));
    connect(typing_, &QTimer::timeout, this, &TextPanel::commitText);
    connect(font_, &QComboBox::activated, this,
            [this] { editor_->updateContent({{"font", font_->currentData().toString().toStdString()}}); });
    connect(size_, &QSlider::sliderReleased, this, [this] { editor_->updateContent({{"size", double(size_->value())}}); });
    connect(color_, &QPushButton::clicked, this, [this] { setColor("color"); });
    connect(group, &QButtonGroup::idClicked, this, [this](int i) {
        const char* a[] = {"left", "center", "right"};
        editor_->updateContent({{"align", a[i]}});
    });
    connect(italic_, &QCheckBox::toggled, this, [this](bool on) { editor_->updateContent({{"italic", on}}); });
    connect(outline_, &QSlider::sliderReleased, this,
            [this] { editor_->updateContent({{"stroke", {{"width", outline_->value() / 100.0}}}}); });
    connect(shadow_, &QSlider::sliderReleased, this,
            [this] { editor_->updateContent({{"shadow", {{"opacity", shadow_->value() / 100.0}}}}); });
    connect(box_, &QSlider::sliderReleased, this,
            [this] { editor_->updateContent({{"box", {{"opacity", box_->value() / 100.0}}}}); });
    connect(styles_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        for (const auto& p : vstudio::text::textPresets())
            if (QString::fromStdString(p.id) == it->data(Qt::UserRole).toString()) {
                Json patch = p.style;
                if (!patch.contains("font")) patch["font"] = nullptr;  // back to the default font
                patch["color2"] = p.style.value("color2", Json());       // null clears a gradient
                editor_->updateContent(patch);
            }
    });
    connect(editor_, &EditorController::selectionChanged, this, &TextPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &TextPanel::refresh);
    refresh();
}

void TextPanel::commitText() {
    typing_->stop();
    const auto c = editor_->selectedContent();
    if (!c || c->value("kind", std::string{}) != "text") return;
    const std::string t = text_->toPlainText().toStdString();
    if (t != c->value("text", std::string{})) editor_->updateContent({{"text", t}});
}

void TextPanel::setColor(const char* key) {
    const auto c = editor_->selectedContent();
    if (!c) return;
    const QColor now(colorOf(*c, key, "#ffffff"));
    const QColor picked = QColorDialog::getColor(now, this, tr("Màu chữ"));
    if (picked.isValid()) editor_->updateContent({{key, picked.name().toStdString()}, {"color2", nullptr}});
}

void TextPanel::refresh() {
    const auto c = editor_->selectedContent();
    const bool text = c && c->value("kind", std::string{}) == "text";
    if (!text && typing_->isActive()) commitText();
    editorBox_->setVisible(text);
    hint_->setVisible(!text);
    if (!text) {
        shownClip_.clear();
        anim_->setContent(nullptr);
        return;
    }
    // Typing in progress on this clip: keep what is being typed.
    const QString t = QString::fromStdString(c->value("text", std::string{}));
    if (shownClip_ != editor_->selectedClip() || (!typing_->isActive() && !text_->hasFocus() && text_->toPlainText() != t)) {
        QSignalBlocker b(text_);
        text_->setPlainText(t);
    }
    shownClip_ = editor_->selectedClip();
    {
        QSignalBlocker b(font_);
        font_->setCurrentIndex(std::max(0, font_->findData(QString::fromStdString(c->value("font", std::string(vstudio::text::defaultFontId()))))));
    }
    if (!size_->isSliderDown()) {
        QSignalBlocker b(size_);
        size_->setValue(int(c->value("size", 96.0)));
    }
    color_->setStyleSheet(QString("background: %1; border: 1px solid #2c323c;").arg(colorOf(*c, "color", "#ffffff")));
    const std::string align = c->value("align", std::string("center"));
    align_[0]->setChecked(align == "left");
    align_[1]->setChecked(align == "center");
    align_[2]->setChecked(align == "right");
    {
        QSignalBlocker b(italic_);
        italic_->setChecked(c->value("italic", false));
    }
    auto sub = [&](const char* key, const char* field) {
        return c->contains(key) && (*c)[key].is_object() ? (*c)[key].value(field, 0.0) : 0.0;
    };
    for (auto [s, v] : {std::pair{outline_, sub("stroke", "width") * 100}, std::pair{shadow_, sub("shadow", "opacity") * 100},
                        std::pair{box_, sub("box", "opacity") * 100}}) {
        if (s->isSliderDown()) continue;
        QSignalBlocker b(s);
        s->setValue(int(std::lround(v)));
    }
    anim_->setContent(&*c);
}

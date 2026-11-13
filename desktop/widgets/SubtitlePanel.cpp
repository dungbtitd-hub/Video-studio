#include "SubtitlePanel.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "AnimationBox.h"
#include "EditorController.h"
#include "vstudio/fonts.hpp"
#include "vstudio/subtitles.hpp"

using vstudio::Json;
namespace sub = vstudio::subtitles;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QString clockText(vstudio::Time t) {
    const qint64 ms = qRound64(t.seconds() * 1000);
    return QString("%1:%2:%3.%4")
        .arg(ms / 3'600'000, 2, 10, QChar('0'))
        .arg(ms / 60'000 % 60, 2, 10, QChar('0'))
        .arg(ms / 1000 % 60, 2, 10, QChar('0'))
        .arg(ms % 1000 / 10, 2, 10, QChar('0'));
}

QSlider* slider(int lo, int hi, QWidget* parent) {
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(lo, hi);
    return s;
}

QLabel* caption(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet("color: #a4abb8;");
    return l;
}

Json objectAt(const Json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_object() ? *it : Json::object();
}

}  // namespace

// The lines of one subtitle track, read straight from the project snapshot.
class CueModel : public QAbstractTableModel {
public:
    CueModel(EditorController* editor, QObject* parent) : QAbstractTableModel(parent), editor_(editor) {}

    void reset(const QString& trackId) {
        beginResetModel();
        project_ = editor_->project();
        track_ = project_ ? project_->findTrack(trackId.toStdString()) : nullptr;
        endResetModel();
    }
    [[nodiscard]] const vstudio::Clip* clip(int row) const {
        return track_ && row >= 0 && row < int(track_->clips.size()) ? &track_->clips[std::size_t(row)] : nullptr;
    }
    [[nodiscard]] int rowOf(const QString& clipId) const {
        if (!track_) return -1;
        for (std::size_t i = 0; i < track_->clips.size(); ++i)
            if (q(track_->clips[i].id) == clipId) return int(i);
        return -1;
    }
    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() || !track_ ? 0 : int(track_->clips.size()); }
    int columnCount(const QModelIndex& = {}) const override { return 3; }
    QVariant headerData(int section, Qt::Orientation o, int role) const override {
        if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
        static const char* names[] = {"Bắt đầu", "Kết thúc", "Nội dung"};
        return QObject::tr(names[section]);
    }
    QVariant data(const QModelIndex& i, int role) const override {
        const vstudio::Clip* c = clip(i.row());
        if (!c) return {};
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            if (i.column() == 0) return clockText(c->timelineIn);
            if (i.column() == 1) return clockText(c->timelineOut());
            const QString t = q(c->content.value("text", std::string{}));
            return role == Qt::EditRole ? t : QString(t).replace('\n', QStringLiteral(" ⏎ "));
        }
        if (role == Qt::ToolTipRole && i.column() == 2) return q(c->content.value("text", std::string{}));
        if (role == Qt::ForegroundRole && i.column() < 2) return QColor("#a4abb8");
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex& i) const override {
        auto f = QAbstractTableModel::flags(i);
        return i.column() == 2 ? f | Qt::ItemIsEditable : f;
    }
    bool setData(const QModelIndex& i, const QVariant& v, int role) override {
        const vstudio::Clip* c = clip(i.row());
        if (!c || role != Qt::EditRole || i.column() != 2) return false;
        editor_->setSubtitleText(q(c->id), v.toString().replace(QStringLiteral(" ⏎ "), "\n"));
        return true;
    }

private:
    EditorController* editor_;
    std::shared_ptr<const vstudio::Project> project_;  // keeps track_ alive
    const vstudio::Track* track_ = nullptr;
};

// ===================================================================== panel

SubtitlePanel::SubtitlePanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
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

    // Track + file actions.
    auto* top = new QHBoxLayout();
    track_ = new QComboBox(body);
    track_->setToolTip(tr("Track phụ đề đang sửa (mỗi ngôn ngữ một track)"));
    top->addWidget(track_, 1);
    newTrack_ = new QToolButton(body);
    newTrack_->setText(tr("＋ Track"));
    newTrack_->setToolTip(tr("Thêm một track phụ đề mới (ví dụ ngôn ngữ thứ hai)"));
    top->addWidget(newTrack_);
    l->addLayout(top);
    auto* actions = new QHBoxLayout();
    add_ = new QPushButton(tr("＋ Thêm dòng"), body);
    add_->setObjectName("primary");
    add_->setToolTip(tr("Thêm một dòng phụ đề tại đầu phát (Ctrl+Enter khi đang gõ: dòng kế tiếp)"));
    actions->addWidget(add_, 1);
    import_ = new QPushButton(tr("Nhập tệp…"), body);
    import_->setToolTip(tr("Nhập phụ đề .srt, .vtt hoặc .ass thành một track mới"));
    actions->addWidget(import_);
    export_ = new QPushButton(tr("Xuất…"), body);
    export_->setToolTip(tr("Lưu track phụ đề ra tệp .srt hoặc .vtt"));
    actions->addWidget(export_);
    l->addLayout(actions);
    hint_ = new QLabel(tr("Bấm “Thêm dòng” để chèn phụ đề tại đầu phát, hoặc “Nhập tệp…” để mở tệp .srt/.vtt/.ass. "
                          "Phụ đề nằm trên một track riêng phía trên video."),
                       body);
    hint_->setWordWrap(true);
    hint_->setStyleSheet("color: #a4abb8;");
    l->addWidget(hint_);

    model_ = new CueModel(editor_, this);
    list_ = new QTableView(body);
    list_->setModel(model_);
    list_->setSelectionBehavior(QAbstractItemView::SelectRows);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    list_->verticalHeader()->setVisible(false);
    list_->verticalHeader()->setDefaultSectionSize(24);
    list_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->horizontalHeader()->setStretchLastSection(true);
    list_->setMinimumHeight(180);
    l->addWidget(list_);

    // The selected line.
    lineBox_ = new QWidget(body);
    auto* ll = new QVBoxLayout(lineBox_);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(caption(tr("Dòng đang chọn"), lineBox_));
    text_ = new QPlainTextEdit(lineBox_);
    text_->setPlaceholderText(tr("Nhập phụ đề… (Enter xuống dòng, Ctrl+Enter thêm dòng tiếp theo)"));
    text_->setFixedHeight(58);
    text_->installEventFilter(this);
    ll->addWidget(text_);
    l->addWidget(lineBox_);
    typing_ = new QTimer(this);
    typing_->setSingleShot(true);
    typing_->setInterval(600);

    // Look.
    styleBox_ = new QWidget(body);
    auto* sl = new QVBoxLayout(styleBox_);
    sl->setContentsMargins(0, 0, 0, 0);
    only_ = new QCheckBox(tr("Chỉ áp dụng cho dòng đang chọn"), styleBox_);
    only_->setToolTip(tr("Bỏ chọn: kiểu và vị trí áp dụng cho cả track"));
    sl->addWidget(only_);
    sl->addWidget(caption(tr("Kiểu phụ đề"), styleBox_));
    styles_ = new QListWidget(styleBox_);
    styles_->setViewMode(QListView::IconMode);
    styles_->setIconSize(QSize(104, 40));
    styles_->setGridSize(QSize(114, 66));
    styles_->setResizeMode(QListView::Adjust);
    styles_->setMovement(QListView::Static);
    styles_->setMinimumHeight(150);
    for (const auto& st : sub::subtitleStyles()) {
        Json c = sub::defaultTrackSettings()["style"];
        c.update(st.style);
        c["kind"] = "subtitle";
        c["text"] = "Phụ đề Aa";
        c["in"] = Json::object();
        c["out"] = Json::object();
        auto* it = new QListWidgetItem(q(st.name), styles_);
        QImage img(104, 40, QImage::Format_RGBA8888);
        img.fill(QColor(70, 92, 120));
        QPainter painter(&img);
        painter.drawImage(0, 0, AnimationBox::render(c, 1, 2, 104, 40));
        painter.end();
        it->setIcon(QPixmap::fromImage(img));
        it->setData(Qt::UserRole, q(st.id));
    }
    sl->addWidget(styles_);
    auto* form = new QFormLayout();
    auto* fontRow = new QWidget(styleBox_);
    auto* fr = new QHBoxLayout(fontRow);
    fr->setContentsMargins(0, 0, 0, 0);
    font_ = new QComboBox(fontRow);
    fr->addWidget(font_, 1);
    auto* addFont = new QToolButton(fontRow);
    addFont->setText(tr("Thêm phông…"));
    addFont->setToolTip(tr("Dùng phông của bạn (.ttf, .otf), ví dụ phông công ty"));
    fr->addWidget(addFont);
    form->addRow(tr("Phông"), fontRow);
    size_ = slider(16, 200, styleBox_);
    form->addRow(tr("Cỡ chữ"), size_);
    color_ = new QPushButton(styleBox_);
    color_->setFixedWidth(44);
    color_->setToolTip(tr("Màu chữ"));
    form->addRow(tr("Màu"), color_);
    outline_ = slider(0, 20, styleBox_);
    form->addRow(tr("Viền"), outline_);
    box_ = slider(0, 100, styleBox_);
    form->addRow(tr("Nền"), box_);
    sl->addLayout(form);

    // Position.
    sl->addWidget(caption(tr("Vị trí (hoặc kéo phụ đề trên khung xem trước)"), styleBox_));
    auto* grid = new QGridLayout();
    grid->setSpacing(4);
    const auto& positions = sub::subtitlePositions();
    // Laid out like the frame: top row, middle, bottom row.
    const int cell[7][2] = {{2, 1}, {2, 0}, {2, 2}, {1, 1}, {0, 1}, {0, 0}, {0, 2}};
    for (int i = 0; i < 7; ++i) {
        pos_[i] = new QPushButton(q(positions[std::size_t(i)].name), styleBox_);
        pos_[i]->setCheckable(true);
        grid->addWidget(pos_[i], cell[i][0], cell[i][1]);
        connect(pos_[i], &QPushButton::clicked, this, [this, i] {
            Json p = sub::subtitlePositions()[std::size_t(i)].position;
            applyPosition(p);
        });
    }
    sl->addLayout(grid);
    auto* form2 = new QFormLayout();
    margin_ = slider(0, 30, styleBox_);
    margin_->setToolTip(tr("Khoảng cách tới mép khung hình"));
    form2->addRow(tr("Cách mép"), margin_);
    auto* shiftRow = new QWidget(styleBox_);
    auto* sr = new QHBoxLayout(shiftRow);
    sr->setContentsMargins(0, 0, 0, 0);
    shift_ = new QDoubleSpinBox(shiftRow);
    shift_->setRange(-600, 600);
    shift_->setDecimals(2);
    shift_->setSingleStep(0.1);
    shift_->setSuffix(tr(" giây"));
    sr->addWidget(shift_, 1);
    auto* shiftBtn = new QPushButton(tr("Dời cả track"), shiftRow);
    shiftBtn->setToolTip(tr("Phụ đề hiện sớm/muộn so với lời nói: dời mọi dòng một khoảng"));
    sr->addWidget(shiftBtn);
    form2->addRow(tr("Khớp giờ"), shiftRow);
    sl->addLayout(form2);

    // AI translation.
    sl->addWidget(caption(tr("Dịch phụ đề bằng AI"), styleBox_));
    translate_ = new QPushButton(tr("Dịch bằng AI…"), styleBox_);
    translate_->setToolTip(tr("Kết nối Claude, ChatGPT, Gemini, DeepSeek, model chạy trên máy (Ollama, LM Studio) "
                              "hoặc ứng dụng AI của bạn qua API mở"));
    sl->addWidget(translate_);
    auto* progRow = new QHBoxLayout();
    progress_ = new QProgressBar(styleBox_);
    progress_->setVisible(false);
    progRow->addWidget(progress_, 1);
    cancel_ = new QPushButton(tr("Dừng"), styleBox_);
    cancel_->setVisible(false);
    progRow->addWidget(cancel_);
    sl->addLayout(progRow);
    aiStatus_ = new QLabel(styleBox_);
    aiStatus_->setWordWrap(true);
    aiStatus_->setStyleSheet("color: #a4abb8;");
    sl->addWidget(aiStatus_);
    l->addWidget(styleBox_);
    l->addStretch();

    // ---- wiring
    connect(newTrack_, &QToolButton::clicked, this, [this] { editor_->addSubtitleTrack(); });
    connect(track_, &QComboBox::activated, this,
            [this] { editor_->setCurrentSubtitleTrack(track_->currentData().toString()); });
    connect(add_, &QPushButton::clicked, this, [this] {
        commitText();
        editor_->addSubtitle();
        text_->setFocus();
    });
    connect(import_, &QPushButton::clicked, this, [this] {
        const QUrl f = QFileDialog::getOpenFileUrl(this, tr("Nhập phụ đề"), {},
                                                   tr("Phụ đề (*.srt *.vtt *.ass *.ssa);;Mọi tệp (*)"));
        if (!f.isEmpty()) editor_->importSubtitles(f);
    });
    connect(export_, &QPushButton::clicked, this, [this] {
        const QString t = editor_->currentSubtitleTrack();
        if (t.isEmpty()) return;
        const QUrl f = QFileDialog::getSaveFileUrl(this, tr("Xuất phụ đề"), QUrl::fromLocalFile(track_->currentText() + ".srt"),
                                                   tr("SubRip (*.srt);;WebVTT (*.vtt)"));
        if (!f.isEmpty()) editor_->exportSubtitles(t, f);
    });
    connect(list_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& i) {
        const vstudio::Clip* c = model_->clip(i.row());
        if (!c || q(c->id) == editor_->selectedClip()) return;
        commitText();
        editor_->setSelectedClip(q(c->id));
        editor_->seek(c->timelineIn.seconds() + 0.001);
    });
    connect(text_, &QPlainTextEdit::textChanged, typing_, qOverload<>(&QTimer::start));
    connect(typing_, &QTimer::timeout, this, &SubtitlePanel::commitText);
    connect(styles_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        for (const auto& st : sub::subtitleStyles())
            if (q(st.id) == it->data(Qt::UserRole).toString()) {
                Json patch = st.style;
                // A style keeps the user's font, except the fonts that only some styles use.
                const std::string font = effectiveStyle().value("font", std::string{});
                if (!patch.contains("font") && (font == "patrick-hand" || font == "lobster")) patch["font"] = "be-vietnam-pro-bold";
                applyStyle(patch);
            }
    });
    connect(font_, &QComboBox::activated, this, [this] { applyStyle({{"font", font_->currentData().toString().toStdString()}}); });
    connect(addFont, &QToolButton::clicked, this, [this] {
        const QUrl f = QFileDialog::getOpenFileUrl(this, tr("Thêm phông chữ"), {}, tr("Phông chữ (*.ttf *.otf)"));
        QString id;
        if (!f.isEmpty() && editor_->addFontFile(f, &id)) {
            refresh();
            applyStyle({{"font", id.toStdString()}});
        }
    });
    connect(size_, &QSlider::sliderReleased, this, [this] { applyStyle({{"size", double(size_->value())}}); });
    connect(color_, &QPushButton::clicked, this, [this] {
        const QColor now(q(effectiveStyle().value("color", std::string("#ffffff"))));
        const QColor picked = QColorDialog::getColor(now, this, tr("Màu phụ đề"));
        if (picked.isValid()) applyStyle({{"color", picked.name().toStdString()}, {"color2", nullptr}});
    });
    connect(outline_, &QSlider::sliderReleased, this,
            [this] { applyStyle({{"stroke", {{"width", outline_->value() / 100.0}}}}); });
    connect(box_, &QSlider::sliderReleased, this, [this] { applyStyle({{"box", {{"opacity", box_->value() / 100.0}}}}); });
    connect(margin_, &QSlider::sliderReleased, this, [this] { applyPosition({{"margin", margin_->value() / 100.0}}); });
    connect(shiftBtn, &QPushButton::clicked, this, [this] {
        const QString t = editor_->currentSubtitleTrack();
        if (!t.isEmpty() && shift_->value() != 0) editor_->shiftSubtitles(t, shift_->value());
    });
    connect(translate_, &QPushButton::clicked, this, &SubtitlePanel::openTranslate);
    connect(cancel_, &QPushButton::clicked, editor_, &EditorController::cancelTranslation);
    connect(editor_, &EditorController::translationProgress, this, [this](int done, int total) {
        progress_->setRange(0, std::max(1, total));
        progress_->setValue(done);
        aiStatus_->setText(tr("Đang dịch… %1/%2 nhóm dòng").arg(done).arg(total));
    });
    connect(editor_, &EditorController::translationFinished, this, [this](bool, const QString& msg) {
        progress_->setVisible(false);
        cancel_->setVisible(false);
        translate_->setEnabled(true);
        aiStatus_->setText(msg);
    });
    connect(editor_, &EditorController::selectionChanged, this, &SubtitlePanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &SubtitlePanel::refresh);
    connect(editor_, &EditorController::subtitleTrackChanged, this, &SubtitlePanel::refresh);
    refresh();
}

bool SubtitlePanel::lineOnly() const {
    if (!only_->isChecked()) return false;
    const auto c = editor_->selectedContent();
    return c && c->value("kind", std::string{}) == "subtitle";
}

Json SubtitlePanel::effectiveStyle() const {
    const auto p = editor_->project();
    const auto* t = p ? p->findTrack(editor_->currentSubtitleTrack().toStdString()) : nullptr;
    Json st = t ? objectAt(objectAt(t->extra, sub::kExtraKey), "style") : Json::object();
    if (const auto c = editor_->selectedContent(); c && c->value("kind", std::string{}) == "subtitle")
        st.merge_patch(objectAt(*c, "style"));
    return st;
}

void SubtitlePanel::applyStyle(const Json& patch) {
    if (lineOnly()) {
        editor_->updateContent({{"style", patch}});
        return;
    }
    const QString t = editor_->currentSubtitleTrack();
    if (!t.isEmpty()) editor_->setSubtitleSettings(t, {{"style", patch}});
}

void SubtitlePanel::applyPosition(const Json& patch) {
    if (lineOnly()) {
        const auto c = editor_->selectedContent();
        Json pos = objectAt(*c, "position");
        if (pos.empty()) {  // starts from the track's place
            const auto p = editor_->project();
            if (const auto* t = p ? p->findTrack(editor_->currentSubtitleTrack().toStdString()) : nullptr)
                pos = objectAt(objectAt(t->extra, sub::kExtraKey), "position");
        }
        pos.merge_patch(patch);
        editor_->updateContent({{"position", pos}});
        return;
    }
    const QString t = editor_->currentSubtitleTrack();
    if (!t.isEmpty()) editor_->setSubtitleSettings(t, {{"position", patch}});
}

void SubtitlePanel::commitText() {
    typing_->stop();
    if (shownClip_.isEmpty()) return;
    editor_->setSubtitleText(shownClip_, text_->toPlainText());
}

bool SubtitlePanel::eventFilter(QObject* o, QEvent* e) {
    if (o == text_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && (k->modifiers() & Qt::ControlModifier)) {
            // Next line: right after this one.
            commitText();
            if (const auto p = editor_->project())
                if (const auto* c = p->findClip(shownClip_.toStdString())) editor_->seek(c->timelineOut().seconds());
            editor_->addSubtitle();
            text_->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void SubtitlePanel::refreshTracks() {
    const auto p = editor_->project();
    const QString current = editor_->currentSubtitleTrack();
    QSignalBlocker b(track_);
    track_->clear();
    if (!p) return;
    for (auto it = p->tracks.rbegin(); it != p->tracks.rend(); ++it) {
        if (!sub::isSubtitleTrack(*it)) continue;
        const std::string lang = objectAt(it->extra, sub::kExtraKey).value("lang", std::string{});
        QString name = q(it->name);
        if (!lang.empty()) name += " (" + q(lang) + ")";
        if (!it->enabled) name += tr(" — đang ẩn");
        track_->addItem(name, q(it->id));
    }
    track_->setCurrentIndex(std::max(0, track_->findData(current)));
}

void SubtitlePanel::refresh() {
    refreshTracks();
    const QString trackId = editor_->currentSubtitleTrack();
    const bool hasTrack = !trackId.isEmpty();
    const auto c = editor_->selectedContent();
    const bool line = c && c->value("kind", std::string{}) == "subtitle";
    if (!line && typing_->isActive()) commitText();
    hint_->setVisible(!hasTrack);
    list_->setVisible(hasTrack);
    export_->setEnabled(hasTrack);
    styleBox_->setVisible(hasTrack);
    lineBox_->setVisible(line);
    translate_->setEnabled(hasTrack && !editor_->translating());

    model_->reset(trackId);
    if (line) {
        const int row = model_->rowOf(editor_->selectedClip());
        if (row >= 0) {
            QSignalBlocker b(list_->selectionModel());
            list_->selectRow(row);
            list_->scrollTo(model_->index(row, 0));
        }
        const QString t = q(c->value("text", std::string{}));
        if (shownClip_ != editor_->selectedClip() || (!typing_->isActive() && !text_->hasFocus() && text_->toPlainText() != t)) {
            QSignalBlocker b(text_);
            text_->setPlainText(t);
        }
        shownClip_ = editor_->selectedClip();
    } else {
        shownClip_.clear();
    }
    if (!hasTrack) return;

    const Json st = effectiveStyle();
    {
        QSignalBlocker b(font_);
        font_->clear();
        for (const auto& f : vstudio::text::fontCatalog()) font_->addItem(q(f.name), q(f.id));
        font_->setCurrentIndex(std::max(0, font_->findData(q(st.value("font", std::string("be-vietnam-pro-bold"))))));
    }
    if (!size_->isSliderDown()) {
        QSignalBlocker b(size_);
        size_->setValue(int(st.value("size", 56.0)));
    }
    color_->setStyleSheet(QString("background: %1; border: 1px solid #2c323c;").arg(q(st.value("color", std::string("#ffffff")))));
    if (!outline_->isSliderDown()) {
        QSignalBlocker b(outline_);
        outline_->setValue(int(std::lround(objectAt(st, "stroke").value("width", 0.0) * 100)));
    }
    if (!box_->isSliderDown()) {
        QSignalBlocker b(box_);
        box_->setValue(int(std::lround(objectAt(st, "box").value("opacity", 0.0) * 100)));
    }
    // Position: the track's, or the line's own.
    const auto p = editor_->project();
    const auto* t = p ? p->findTrack(trackId.toStdString()) : nullptr;
    Json pos = t ? objectAt(objectAt(t->extra, sub::kExtraKey), "position") : Json::object();
    if (line && only_->isChecked() && c->contains("position")) pos.merge_patch(objectAt(*c, "position"));
    const auto& presets = sub::subtitlePositions();
    for (int i = 0; i < 7; ++i) {
        const Json& pp = presets[std::size_t(i)].position;
        const bool on = pos.value("anchor", std::string("bottom")) == pp.value("anchor", std::string{}) &&
                        std::abs(pos.value("x", 0.5) - pp.value("x", 0.5)) < 0.01 &&
                        pos.value("h", std::string("center")) == pp.value("h", std::string("center"));
        pos_[i]->setChecked(on);
    }
    if (!margin_->isSliderDown()) {
        QSignalBlocker b(margin_);
        margin_->setValue(int(std::lround(pos.value("margin", 0.06) * 100)));
    }
}

void SubtitlePanel::openTranslate() {
    const QString trackId = editor_->currentSubtitleTrack();
    if (trackId.isEmpty()) return;
    TranslateDialog d(this);
    if (d.exec() != QDialog::Accepted) return;
    d.save();
    progress_->setRange(0, 0);
    progress_->setVisible(true);
    cancel_->setVisible(true);
    translate_->setEnabled(false);
    aiStatus_->setText(tr("Đang gửi tới dịch vụ AI…"));
    editor_->translateSubtitles(trackId, d.config(), d.from(), d.to(), d.notes(), d.mode(), d.batchSize());
}

// ===================================================================== translate dialog

TranslateDialog::TranslateDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Dịch phụ đề bằng AI"));
    setMinimumWidth(460);
    auto* l = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    preset_ = new QComboBox(this);
    for (const auto& p : sub::providerPresets()) preset_->addItem(q(p.value("name", std::string{})), q(p.value("id", std::string{})));
    preset_->addItem(tr("Chương trình trên máy này (lệnh)"), "command");
    form->addRow(tr("Dịch vụ"), preset_);
    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    hint_->setStyleSheet("color: #a4abb8;");
    form->addRow(QString(), hint_);
    urlRow_ = new QWidget(this);
    auto* ur = new QVBoxLayout(urlRow_);
    ur->setContentsMargins(0, 0, 0, 0);
    url_ = new QLineEdit(urlRow_);
    url_->setPlaceholderText("https://…");
    ur->addWidget(url_);
    form->addRow(tr("Địa chỉ API"), urlRow_);
    model_ = new QLineEdit(this);
    model_->setPlaceholderText(tr("tên model"));
    form->addRow(tr("Model"), model_);
    key_ = new QLineEdit(this);
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText(tr("khóa API (lưu trên máy này, không lưu trong dự án)"));
    form->addRow(tr("Khóa API"), key_);
    commandRow_ = new QWidget(this);
    auto* cr = new QVBoxLayout(commandRow_);
    cr->setContentsMargins(0, 0, 0, 0);
    command_ = new QLineEdit(commandRow_);
    command_->setPlaceholderText(tr("ví dụ: python3 /duong/dan/dich.py"));
    cr->addWidget(command_);
    form->addRow(tr("Lệnh"), commandRow_);
    from_ = new QComboBox(this);
    from_->addItem(tr("Tự nhận biết"), QString());
    to_ = new QComboBox(this);
    for (const auto& lang : sub::languages()) {
        from_->addItem(q(lang.value("name", std::string{})), q(lang.value("code", std::string{})));
        to_->addItem(q(lang.value("name", std::string{})), q(lang.value("code", std::string{})));
    }
    to_->setEditable(true);  // any language the model knows
    form->addRow(tr("Từ"), from_);
    form->addRow(tr("Sang"), to_);
    notes_ = new QPlainTextEdit(this);
    notes_->setPlaceholderText(tr("Ghi chú cho AI (không bắt buộc): tên riêng, thuật ngữ, cách xưng hô…"));
    notes_->setFixedHeight(56);
    form->addRow(tr("Ghi chú"), notes_);
    auto* modes = new QWidget(this);
    auto* mr = new QVBoxLayout(modes);
    mr->setContentsMargins(0, 0, 0, 0);
    modes_[2] = new QRadioButton(tr("Song ngữ: thêm bản dịch phía trên phụ đề gốc"), modes);
    modes_[1] = new QRadioButton(tr("Track mới: chỉ hiện bản dịch (ẩn bản gốc)"), modes);
    modes_[0] = new QRadioButton(tr("Thay thế chữ trên track này"), modes);
    for (int i : {2, 1, 0}) mr->addWidget(modes_[i]);
    form->addRow(tr("Kết quả"), modes);
    batch_ = new QSpinBox(this);
    batch_->setRange(5, 200);
    batch_->setValue(40);
    batch_->setToolTip(tr("Số dòng gửi mỗi lần; giảm nếu model hay trả thiếu dòng"));
    form->addRow(tr("Dòng mỗi lần"), batch_);
    l->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Dịch"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(buttons);

    QSettings set;
    preset_->setCurrentIndex(std::max(0, preset_->findData(set.value("translate/preset", "anthropic").toString())));
    to_->setCurrentIndex(std::max(0, to_->findData(set.value("translate/to", "en").toString())));
    from_->setCurrentIndex(std::max(0, from_->findData(set.value("translate/from", QString()).toString())));
    modes_[std::clamp(set.value("translate/mode", 2).toInt(), 0, 2)]->setChecked(true);
    batch_->setValue(set.value("translate/batch", 40).toInt());
    connect(preset_, &QComboBox::currentIndexChanged, this, &TranslateDialog::presetChanged);
    presetChanged();
}

void TranslateDialog::presetChanged() {
    QSettings set;
    if (!shown_.isEmpty()) {  // keep what was typed for the previous connector
        set.setValue("translate/" + shown_ + "/url", url_->text());
        set.setValue("translate/" + shown_ + "/model", model_->text());
        set.setValue("translate/" + shown_ + "/key", key_->text());
        set.setValue("translate/" + shown_ + "/command", command_->text());
    }
    shown_ = preset_->currentData().toString();
    Json preset = Json::object();
    for (const auto& p : sub::providerPresets())
        if (q(p.value("id", std::string{})) == shown_) preset = p;
    const bool command = shown_ == "command";
    hint_->setText(command ? tr("Chương trình nhận JSON qua stdin và trả JSON qua stdout theo giao thức mở "
                                "(docs/subtitles.md). Dùng để nối bất kỳ AI nào bằng script của bạn.")
                           : q(preset.value("hint", std::string{})));
    urlRow_->setVisible(!command);
    commandRow_->setVisible(command);
    url_->setText(set.value("translate/" + shown_ + "/url", q(preset.value("url", std::string{}))).toString());
    model_->setText(set.value("translate/" + shown_ + "/model", q(preset.value("model", std::string{}))).toString());
    key_->setText(set.value("translate/" + shown_ + "/key").toString());
    command_->setText(set.value("translate/" + shown_ + "/command").toString());
    key_->setEnabled(command ? false : true);
}

Json TranslateDialog::config() const {
    std::string provider = "command";
    for (const auto& p : sub::providerPresets())
        if (q(p.value("id", std::string{})) == shown_) provider = p.value("provider", std::string{});
    return {{"provider", provider},
            {"url", url_->text().trimmed().toStdString()},
            {"model", model_->text().trimmed().toStdString()},
            {"api_key", key_->text().trimmed().toStdString()},
            {"command", command_->text().trimmed().toStdString()}};
}

QString TranslateDialog::from() const { return from_->currentData().toString(); }
QString TranslateDialog::to() const {
    const int i = to_->findText(to_->currentText());
    return i >= 0 ? to_->itemData(i).toString() : to_->currentText().trimmed();
}
QString TranslateDialog::notes() const { return notes_->toPlainText().trimmed(); }
int TranslateDialog::mode() const {
    for (int i = 0; i < 3; ++i)
        if (modes_[i]->isChecked()) return i;
    return 2;
}
int TranslateDialog::batchSize() const { return batch_->value(); }

void TranslateDialog::save() const {
    QSettings set;
    set.setValue("translate/preset", shown_);
    set.setValue("translate/" + shown_ + "/url", url_->text());
    set.setValue("translate/" + shown_ + "/model", model_->text());
    set.setValue("translate/" + shown_ + "/key", key_->text());
    set.setValue("translate/" + shown_ + "/command", command_->text());
    set.setValue("translate/from", from());
    set.setValue("translate/to", to());
    set.setValue("translate/mode", mode());
    set.setValue("translate/batch", batchSize());
}

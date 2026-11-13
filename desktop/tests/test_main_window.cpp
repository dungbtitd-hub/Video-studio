// Workspace: every shortcut is unique and documented, keys drive navigation and
// edits through the window's actions, timeline tools, and layout reset.
#include <QAction>
#include "raw_sample.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
#include <QToolButton>
#include <QCheckBox>
#include <QPushButton>
#include <QMenuBar>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>

#include "EditorController.h"
#include "CoverPanel.h"
#include "GradePanel.h"
#include "ScopesPanel.h"
#include "GradeWidgets.h"
#include "LooksPanel.h"
#include "MainWindow.h"
#include "PreviewWidget.h"
#include "TimelineWidget.h"
#include "TransitionsPanel.h"
#include "AnimationBox.h"
#include "StickerPanel.h"
#include "TextPanel.h"
#include <QPlainTextEdit>
#include <QMimeData>

class MainWindowTest : public QObject {
    Q_OBJECT
    EditorController* ctrl_ = nullptr;
    MainWindow* win_ = nullptr;

    void key(int k, Qt::KeyboardModifiers mods = {}) {
        QTest::keyClick(win_, Qt::Key(k), mods);
        QCoreApplication::processEvents();
    }
    // Synthetic project (EditorController::loadSyntheticProject(6)):
    //   Video 1: [0,3) [3,9)   Video 2: [0,2) [2,7)   Audio 1: [0,4) [4,6)

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init() {
        ctrl_ = new EditorController;
        ctrl_->loadSyntheticProject(6);
        win_ = new MainWindow(ctrl_);
        win_->show();
        QApplication::setActiveWindow(win_);
        QVERIFY(QTest::qWaitForWindowExposed(win_));
    }
    void cleanup() {
        delete win_;
        delete ctrl_;
    }

    void shortcutsAreUniqueAndListed() {
        QSet<QString> seen;
        for (QAction* a : win_->findChildren<QAction*>())
            for (const QKeySequence& k : a->shortcuts()) {
                const QString s = k.toString(QKeySequence::PortableText);
                QVERIFY2(!seen.contains(s), qPrintable("duplicate shortcut " + s));
                seen.insert(s);
            }
        const auto rows = win_->shortcutTable();
        QVERIFY(rows.size() >= 20);
        bool ripple = false;
        for (const auto& r : rows) ripple |= r[1] == "Xóa và dồn khoảng trống";
        QVERIFY(ripple);
    }

    void packAndOpenPackageInBackground() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const auto tracks = ctrl_->project()->tracks.size();
        QSignalSpy done(ctrl_, &EditorController::transferFinished);
        ctrl_->packProject(QUrl::fromLocalFile(tmp.filePath("move")));
        QVERIFY(done.wait(5000));
        QVERIFY(done.last().at(0).toBool());
        const QString pkg = done.last().at(1).toString();
        QVERIFY(pkg.endsWith("move.vstudiopkg") && QFileInfo::exists(pkg));
        QVERIFY(!ctrl_->transferring());

        QDir(tmp.path()).mkdir("phone");
        ctrl_->openPackage(QUrl::fromLocalFile(pkg), QUrl::fromLocalFile(tmp.filePath("phone")));
        QVERIFY(done.wait(5000));
        QVERIFY2(done.last().at(0).toBool(), qPrintable(done.last().at(2).toString()));
        QVERIFY(done.last().at(1).toString().startsWith(tmp.filePath("phone")));
        QCOMPARE(ctrl_->project()->tracks.size(), tracks);

        ctrl_->openPackage(QUrl::fromLocalFile(tmp.filePath("nope.vstudiopkg")));
        QVERIFY(done.wait(5000));
        QVERIFY(!done.last().at(0).toBool());
        QCOMPARE(ctrl_->project()->tracks.size(), tracks);  // the open project is untouched
    }

    void keysNavigateEditPoints() {
        ctrl_->seek(0);
        key(Qt::Key_Down);
        QCOMPARE(ctrl_->playhead(), 2.0);
        key(Qt::Key_Down);
        QCOMPARE(ctrl_->playhead(), 3.0);
        key(Qt::Key_Up);
        QCOMPARE(ctrl_->playhead(), 2.0);
        key(Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(ctrl_->playhead(), 3.0);
        key(Qt::Key_End);
        QCOMPARE(ctrl_->playhead(), ctrl_->duration());
        key(Qt::Key_Home);
        QCOMPARE(ctrl_->playhead(), 0.0);
    }

    void rippleDeleteByKey() {
        const auto& audio = ctrl_->project()->tracks.back();
        QCOMPARE(QString::fromStdString(audio.name), QString("Audio 1"));
        ctrl_->setSelectedClip(QString::fromStdString(audio.clips.front().id));  // [0,4)
        key(Qt::Key_Delete, Qt::ShiftModifier);
        const auto& after = ctrl_->project()->tracks.back();
        QCOMPARE(after.clips.size(), size_t(1));
        QCOMPARE(after.clips.front().timelineIn.seconds(), 0.0);  // [4,6) pulled to 0
        QVERIFY(ctrl_->selectedClip().isEmpty());
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(ctrl_->project()->tracks.back().clips.size(), size_t(2));
    }

    void timelineTools() {
        TimelineWidget* tl = win_->timeline();
        QVERIFY(tl->snapping());
        key(Qt::Key_N);
        QVERIFY(!tl->snapping());
        key(Qt::Key_N);
        QVERIFY(tl->snapping());
        key(Qt::Key_Z, Qt::ShiftModifier);  // fit
        const double fit = tl->pxPerSec();
        QVERIFY(fit * ctrl_->duration() <= tl->width());
        QVERIFY(fit * ctrl_->duration() > tl->width() * 0.6);
        key(Qt::Key_Equal);
        QVERIFY(tl->pxPerSec() > fit);
        key(Qt::Key_Minus);
        QVERIFY(std::abs(tl->pxPerSec() - fit) < 1e-6);
    }

    void importCameraRaw() {
        // A camera RAW photo imports like any picture (LibRaw / Apple RAW behind the backend).
        const QString path = QDir::temp().filePath("vstudio_desktop_test.dng");
        vtest::writeTestDng(path.toStdString(), 640, 360);
        const int before = ctrl_->assets().size();
        ctrl_->importMedia({QUrl::fromLocalFile(path)});
        QTRY_COMPARE(ctrl_->assets().size(), before + 1);
        const QVariantMap m = ctrl_->assets().back().toMap();
        QCOMPARE(m["name"].toString(), QString("vstudio_desktop_test.dng"));
        QCOMPARE(m["type"].toString(), QString("image"));
        QCOMPARE(m["resolution"].toString(), QString("640×360"));
    }

    void exportPresetsForProject() {
        const QVariantList presets = ctrl_->exportPresets();
        QCOMPARE(presets.size(), 6);
        for (const QVariant& v : presets) {
            const QVariantMap m = v.toMap();
            if (m["id"] == "fhd1080") {
                QCOMPARE(m["width"].toInt(), 1920);  // synthetic project is 1920x1080
                QCOMPARE(m["height"].toInt(), 1080);
                QVERIFY(m["estimatedBytes"].toLongLong() > 0);
            }
        }
        // Formats: H.265 10-bit is offered and sized smaller than H.264 at the same preset.
        const QVariantList formats = ctrl_->exportFormats();
        QCOMPARE(formats.size(), 3);
        QCOMPARE(formats.back().toMap()["id"].toString(), QString("hevc10"));
        auto bitrate = [&](const QString& fmt) {
            for (const QVariant& v : ctrl_->exportPresets(fmt))
                if (v.toMap()["id"] == "fhd1080") return v.toMap()["bitrate"].toLongLong();
            return qint64(0);
        };
        QVERIFY(bitrate("hevc10") > 0 && bitrate("hevc10") < bitrate("h264"));
    }

    void oneTapLooks() {
        LooksPanel* panel = win_->looks();
        QListWidget* list = panel->list();
        const auto& v1 = ctrl_->project()->tracks.front();  // Video 1 (top)
        const QString clip = QString::fromStdString(v1.clips.front().id);
        ctrl_->setSelectedClip(clip);
        QListWidgetItem* teal = nullptr;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole) == "teal_orange") teal = list->item(i);
        QVERIFY(teal);
        QVERIFY(!teal->icon().isNull());  // preview rendered
        emit list->itemClicked(teal);     // one tap
        QCOMPARE(ctrl_->clipLook(clip).first, QString("teal_orange"));
        QCOMPARE(list->currentItem(), teal);
        // Strength is committed when the slider is released.
        panel->strength()->setValue(40);
        emit panel->strength()->sliderReleased();
        QCOMPARE(ctrl_->clipLook(clip).second, 0.4);
        // "Áp dụng cho tất cả": every clip on unlocked video tracks, one undo step.
        ctrl_->setClipLook(ctrl_->lookableClips(), "teal_orange", 0.4);
        for (const QString& c : ctrl_->lookableClips()) QCOMPARE(ctrl_->clipLook(c).first, QString("teal_orange"));
        ctrl_->undo();
        QCOMPARE(ctrl_->clipLook(clip).first, QString("teal_orange"));  // only the bulk step undone
        emit list->itemClicked(list->item(0));  // "Gốc": no look
        QVERIFY(ctrl_->clipLook(clip).first.isEmpty());
    }

    void proGrade() {
        GradePanel* panel = win_->grade();
        const QString clip = QString::fromStdString(ctrl_->project()->tracks.front().clips.front().id);
        ctrl_->setSelectedClip(clip);
        QVERIFY(panel->tabs()->isEnabled());
        const auto undoDepth = [&] { return ctrl_->undoLabel(); };
        // Dragging a slider previews live without an undo step; release commits one.
        QSlider* sat = panel->slider("saturation");
        sat->setSliderDown(true);
        sat->setValue(150);
        QVERIFY(ctrl_->clipGrade(clip).empty());  // still uncommitted
        sat->setSliderDown(false);
        emit sat->sliderReleased();
        QCOMPARE(ctrl_->clipGrade(clip).value("saturation", 0.0), 1.5);
        // A warm push on the highlights wheel.
        panel->wheel(2)->dragPuckTo(30, 0.5);
        const auto gain = ctrl_->clipGrade(clip)["gain"];
        QVERIFY(gain.is_array() && gain[0].get<double>() > gain[2].get<double>());
        // Curves: a point on the master curve is stored and drawn from the same spline.
        panel->tabs()->setCurrentIndex(1);
        panel->curve()->setPoints({{0, 0}, {0.5, 0.65}, {1, 1}});
        emit panel->curve()->committed();
        QCOMPARE(ctrl_->clipGrade(clip)["curves"]["master"].size(), std::size_t(3));
        // Eyedropper -> qualifier selects that colour (kept even before any change inside it).
        panel->pickColor(QColor(60, 120, 230));
        const auto q = ctrl_->clipGrade(clip)["qualifier"];
        QVERIFY(q.is_object());
        QVERIFY(std::abs(q["hue"][0].get<double>() - 219) < 3);
        // Undo goes back one control at a time; reset removes the grade.
        ctrl_->undo();
        QVERIFY(!ctrl_->clipGrade(clip).contains("qualifier"));
        QVERIFY(ctrl_->clipGrade(clip).contains("curves"));
        ctrl_->setClipGrade({clip}, vstudio::Json());
        QVERIFY(ctrl_->clipGrade(clip).empty());
        QCOMPARE(panel->slider("saturation")->value(), 100);  // panel follows the project
        (void)undoDepth;
    }

    void scopesFollowPreview() {
        ScopesPanel* sc = win_->scopes();
        QVERIFY(sc->isVisible());
        QSignalSpy spy(sc, &ScopesPanel::scopeUpdated);
        for (int k = 0; k < 4; ++k) {
            sc->kind()->setCurrentIndex(k);
            ctrl_->refreshPreview();
            QVERIFY(spy.wait(5000));
            QVERIFY(!sc->scope().isNull());
        }
    }

    void logInputAndLut() {
        LooksPanel* panel = win_->looks();
        const QString clip = QString::fromStdString(ctrl_->project()->tracks.front().clips.front().id);
        ctrl_->setSelectedClip(clip);
        ctrl_->setClipLook({clip}, "vivid", 1.0);
        QComboBox* input = panel->input();
        QVERIFY(input->isEnabled());
        const int slog3 = input->findData("slog3");
        QVERIFY(slog3 > 0);
        input->setCurrentIndex(slog3);
        emit input->activated(slog3);
        QCOMPARE(ctrl_->clipColorSpace(clip).first, QString("slog3"));
        // The conversion runs before the look.
        auto effects = [&] { return ctrl_->project()->findClip(clip.toStdString())->effects; };
        QCOMPARE(QString::fromStdString(effects()[0]["type"].get<std::string>()), QString("builtin.color_space"));
        // A LUT file is validated before it is used.
        QTemporaryDir tmp;
        QFile bad(tmp.filePath("bad.cube"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not a lut");
        bad.close();
        ctrl_->setClipLut({clip}, bad.fileName());
        QVERIFY(ctrl_->clipLut(clip).isEmpty());
        QFile good(tmp.filePath("film.cube"));
        QVERIFY(good.open(QIODevice::WriteOnly));
        good.write("LUT_3D_SIZE 2\n");
        for (int i = 0; i < 8; ++i) good.write("0.1 0.2 0.3\n");
        good.close();
        ctrl_->setClipLut({clip}, good.fileName());
        QVERIFY(ctrl_->clipLut(clip).endsWith("film.cube"));
        QCOMPARE(effects().size(), std::size_t{3});  // Log conversion, look, LUT
    }

    // Keyframes on the quick-edit screen: ◇ at the start, drag a box on the picture
    // later on, and the clip zooms smoothly between the two; audio keys are separate.
    void keyframesFromPreviewAndBar() {
        const auto& clip0 = ctrl_->project()->tracks.front().clips.front();
        const QString clip = QString::fromStdString(clip0.id);
        const double end = clip0.timelineOut().seconds();
        auto* pic = win_->findChild<QToolButton*>("kfPicture");
        auto* aud = win_->findChild<QToolButton*>("kfAudio");
        auto* vol = win_->findChild<QSlider*>("kfVolume");
        auto* preview = win_->findChild<PreviewWidget*>();
        QVERIFY(pic && aud && vol && preview);
        QVERIFY(!pic->isEnabled());  // nothing selected
        ctrl_->setSelectedClip(clip);
        ctrl_->seek(0.2);
        QCoreApplication::processEvents();
        QVERIFY(pic->isEnabled() && preview->regionSelectEnabled());
        pic->click();
        QVERIFY(ctrl_->keyState().pictureHere);
        QVERIFY(pic->isChecked());
        QCOMPARE(pic->text(), QString("◆ Hình"));

        // Later: drag a frame-shaped box over the middle half of the picture.
        const double t1 = end - 0.2;
        ctrl_->seek(t1);
        QCoreApplication::processEvents();
        QVERIFY(!pic->isChecked());
        QImage frame(480, 270, QImage::Format_RGBA8888);
        frame.fill(Qt::darkGray);
        preview->setFrame(frame);
        const QRectF f = preview->frameRect();
        QVERIFY(!f.isEmpty());
        QSignalSpy region(preview, &PreviewWidget::regionSelected);
        const QPoint a = (f.topLeft() + QPointF(f.width() * 0.25, f.height() * 0.25)).toPoint();
        const QPoint b = (f.topLeft() + QPointF(f.width() * 0.75, f.height() * 0.75)).toPoint();
        QTest::mousePress(preview, Qt::LeftButton, {}, a);
        QTest::mouseMove(preview, b);
        QTest::mouseRelease(preview, Qt::LeftButton, {}, b);
        QCOMPARE(region.count(), 1);
        auto k = ctrl_->keyState();
        QVERIFY(k.pictureHere);  // auto-keyed: the picture was already animated
        QVERIFY2(std::abs(k.scale - 2.0) < 0.1, qPrintable(QString::number(k.scale)));
        // In between, the zoom is on its way; before the first key it is untouched.
        ctrl_->seek((0.2 + t1) / 2);
        QVERIFY(ctrl_->keyState().scale > 1.1 && ctrl_->keyState().scale < 1.9);
        ctrl_->seek(0.1);
        QCOMPARE(ctrl_->keyState().scale, 1.0);
        QCOMPARE(ctrl_->keyframeTimes(clip, false).size(), 2);
        QVERIFY(ctrl_->keyframeTimes(clip, true).isEmpty());

        // Audio: own keys, louder then quieter, picture keys untouched.
        ctrl_->seek(0.5);
        QCoreApplication::processEvents();
        aud->click();
        ctrl_->seek(t1);
        QCoreApplication::processEvents();
        vol->setValue(30);  // not dragging: committed at once
        QVERIFY(std::abs(ctrl_->keyState().volume - 0.3) < 1e-6);
        QVERIFY(ctrl_->keyState().audioHere);
        QCOMPARE(ctrl_->keyframeTimes(clip, true).size(), 2);
        QCOMPARE(ctrl_->keyframeTimes(clip, false).size(), 2);

        // ] / [ jump between keys of the selected clip.
        ctrl_->seek(0);
        key(Qt::Key_BracketRight);
        QVERIFY(std::abs(ctrl_->playhead() - 0.2) < 0.02);
        key(Qt::Key_BracketRight);
        QVERIFY(std::abs(ctrl_->playhead() - 0.5) < 0.02);
        key(Qt::Key_BracketLeft);
        QVERIFY(std::abs(ctrl_->playhead() - 0.2) < 0.02);
        // ◆ again removes the key at the playhead.
        QCoreApplication::processEvents();
        QVERIFY(pic->isChecked());
        pic->click();
        QCOMPARE(ctrl_->keyframeTimes(clip, false).size(), 1);
        ctrl_->undo();
        QCOMPARE(ctrl_->keyframeTimes(clip, false).size(), 2);
    }

    // Covers: add from the panel, pick a style, drag the region on the preview, follow, remove.
    void coversFromPanelAndPreview() {
        CoverPanel* panel = win_->coverPanel();
        PreviewWidget* preview = win_->preview();
        QVERIFY(!panel->addButton()->isEnabled());
        const QString clip = QString::fromStdString(ctrl_->project()->tracks.front().clips.front().id);
        ctrl_->setSelectedClip(clip);
        ctrl_->seek(0.5);
        QCoreApplication::processEvents();
        QVERIFY(panel->addButton()->isEnabled());
        QVERIFY(panel->styles()->count() >= 36);
        QVERIFY(!panel->styles()->item(0)->icon().isNull());
        panel->addButton()->click();
        QCOMPARE(ctrl_->covers(clip).size(), 1);
        QVERIFY(!ctrl_->activeCover().isEmpty());
        QCOMPARE(panel->coverList()->count(), 1);
        // One tap on a style changes the active cover.
        QListWidgetItem* black = nullptr;
        for (int i = 0; i < panel->styles()->count(); ++i)
            if (panel->styles()->item(i)->data(Qt::UserRole) == "black") black = panel->styles()->item(i);
        QVERIFY(black);
        emit panel->styles()->itemClicked(black);
        QCOMPARE(ctrl_->coverState()["style"].toString(), QString("black"));
        // The preview edits the region: drag it right by a quarter of the frame.
        QImage frame(480, 270, QImage::Format_RGBA8888);
        frame.fill(Qt::darkGray);
        preview->setFrame(frame);
        QCoreApplication::processEvents();
        QVERIFY(preview->coverEditing());
        const double x0 = ctrl_->coverState()["x"].toDouble();
        const QRectF f = preview->frameRect();
        const QRectF c = ctrl_->coverCanvasRect();
        const QPoint from = (f.topLeft() + QPointF((c.center().x()) * f.width(), c.center().y() * f.height())).toPoint();
        const QPoint to = from + QPoint(int(f.width() / 4), 0);
        QTest::mousePress(preview, Qt::LeftButton, {}, from);
        QTest::mouseMove(preview, to);
        QTest::mouseRelease(preview, Qt::LeftButton, {}, to);
        QVERIFY2(std::abs(ctrl_->coverState()["x"].toDouble() - (x0 + 0.25)) < 0.02,
                 qPrintable(QString::number(ctrl_->coverState()["x"].toDouble())));
        // Follow by keyframes, then remove.
        auto* follow = panel->findChild<QCheckBox*>();
        QVERIFY(follow);
        ctrl_->setCoverFollowing(true);
        QVERIFY(ctrl_->coverState()["following"].toBool());
        QVERIFY(panel->trackButton()->isEnabled());
        ctrl_->removeCover(ctrl_->activeCover());
        QVERIFY(ctrl_->covers(clip).isEmpty());
        QCoreApplication::processEvents();
        QVERIFY(!preview->coverEditing());
    }

    void transitionsFromPanelDropAndBadge() {
        TransitionsPanel* panel = win_->transitionsPanel();
        TimelineWidget* tl = win_->timeline();
        QVERIFY(panel->list()->count() >= 30);
        QVERIFY(!panel->list()->item(0)->icon().isNull());
        // Rows: top = the last video track in the project (the top layer), then the one below it.
        const auto& tracks = ctrl_->project()->tracks;
        const QString v1b = QString::fromStdString(tracks[0].clips[1].id);  // second clip, lower row
        const QString v2b = QString::fromStdString(tracks[1].clips[1].id);  // second clip, top row
        const double cut1 = tracks[0].clips[1].timelineIn.seconds(), cut2 = tracks[1].clips[1].timelineIn.seconds();
        // Select a clip, click a transition: it goes on the cut at the clip's start.
        ctrl_->setSelectedClip(v1b);
        QListWidgetItem* fade = nullptr;
        for (int i = 0; i < panel->list()->count(); ++i)
            if (panel->list()->item(i)->data(Qt::UserRole) == "dip_black") fade = panel->list()->item(i);
        QVERIFY(fade);
        emit panel->list()->itemClicked(fade);
        auto [type, secs] = ctrl_->transitionAt({v1b, 0});
        QCOMPARE(type, QString("dip_black"));
        QCOMPARE(secs, 0.5);
        QCOMPARE(ctrl_->selectedCut().clipId, v1b);
        QVERIFY(panel->placeLabel()->text().contains(QString::fromUtf8("→")));
        // Length: committed on release, one undo step.
        panel->duration()->setValue(12);
        emit panel->duration()->sliderReleased();
        QCOMPARE(ctrl_->transitionAt({v1b, 0}).second, 1.2);
        // Drag-and-drop from the panel onto the cut of the top row.
        const double x = 120 + (cut2 - tl->scrollSeconds()) * tl->pxPerSec();
        const QPointF at(x + 10, 24 + 54 - 14);  // near the cut, lower part of the row
        QMimeData mime;
        mime.setData(TimelineWidget::kTransitionMime, "wipe_left");
        QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(tl, &enter);
        QVERIFY(enter.isAccepted());
        QDragMoveEvent move(at.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(tl, &move);
        QVERIFY(move.isAccepted());
        QDropEvent drop(at, Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(tl, &drop);
        QCOMPARE(ctrl_->transitionAt({v2b, 0}).first, QString("wipe_left"));
        QCOMPARE(ctrl_->selectedCut().clipId, v2b);
        QCOMPARE(ctrl_->selectedClip(), v2b);
        // Clicking the badge of the lower row's cut selects that cut again.
        const double x1 = 120 + (cut1 - tl->scrollSeconds()) * tl->pxPerSec();
        QTest::mouseMove(tl, QPoint(int(x1), 24 + 54 + 54 - 14));
        QTest::mouseClick(tl, Qt::LeftButton, {}, QPoint(int(x1), 24 + 54 + 54 - 6 - 8 - 3));
        QCOMPARE(ctrl_->selectedCut().clipId, v1b);
        auto* dock = win_->findChild<QDockWidget*>("transitions");
        QVERIFY(dock && dock->isVisible());
        // Apply to all cuts, then remove one.
        QCOMPARE(ctrl_->setTransitionEverywhere("fade", 0.5), 2);
        QCOMPARE(ctrl_->transitionAt({v2b, 0}).first, QString("fade"));
        panel->removeButton()->click();
        QVERIFY(ctrl_->transitionAt({v1b, 0}).first.isEmpty());
        ctrl_->undo();
        QCOMPARE(ctrl_->transitionAt({v1b, 0}).first, QString("fade"));
    }

    void textAndStickers() {
        TextPanel* tp = win_->textPanel();
        StickerPanel* sp = win_->stickerPanel();
        ctrl_->seek(1.0);
        const int tracksBefore = int(ctrl_->project()->tracks.size());
        tp->addButton()->click();
        const QString text = ctrl_->selectedClip();
        QVERIFY(!text.isEmpty());
        QCOMPARE(int(ctrl_->project()->tracks.size()), tracksBefore + 1);  // its own overlay track
        auto* textDock = win_->findChild<QDockWidget*>("text");
        QVERIFY(textDock && textDock->isVisible());
        QVERIFY(tp->textEdit()->isVisibleTo(tp));
        // Typing is committed after a pause (or right away on commit), one undo step.
        tp->textEdit()->setPlainText(QString::fromUtf8("Xin chào"));
        tp->commitText();
        QCOMPARE(QString::fromStdString(ctrl_->selectedContent()->value("text", std::string{})), QString::fromUtf8("Xin chào"));
        // Timeline label shows the words.
        QVERIFY(win_->timeline()->clipLabel(text).contains(QString::fromUtf8("Xin chào")));
        // One-tap style, font, animation.
        QListWidgetItem* neon = nullptr;
        for (int i = 0; i < tp->styles()->count(); ++i)
            if (tp->styles()->item(i)->data(Qt::UserRole) == "neon_pink") neon = tp->styles()->item(i);
        QVERIFY(neon);
        emit tp->styles()->itemClicked(neon);
        QCOMPARE((*ctrl_->selectedContent())["glow"].value("opacity", 0.0), 1.0);
        tp->fontBox()->setCurrentIndex(tp->fontBox()->findData("pacifico"));
        emit tp->fontBox()->activated(tp->fontBox()->currentIndex());
        QCOMPARE(QString::fromStdString(ctrl_->selectedContent()->value("font", std::string{})), QString("pacifico"));
        AnimationBox* ab = tp->animations();
        ab->inBox()->setCurrentIndex(ab->inBox()->findData("typewriter"));
        emit ab->inBox()->activated(ab->inBox()->currentIndex());
        QCOMPARE(QString::fromStdString((*ctrl_->selectedContent())["in"].value("type", std::string{})), QString("typewriter"));
        QTRY_VERIFY(ab->preview()->pixmap().width() > 0);
        // The preview shows the text: handles around it; dragging moves it.
        const QRectF r = ctrl_->objectCanvasRect();
        QVERIFY(r.width() > 0.05 && r.center().x() > 0.4 && r.center().x() < 0.6);
        ctrl_->seek(1.02);
        QTRY_VERIFY(win_->preview()->objectEditing());
        QRectF to = r.translated(0.1, -0.2);
        ctrl_->setObjectCanvasRect(r, to);
        QVERIFY(std::abs(ctrl_->objectCanvasRect().center().x() - to.center().x()) < 1e-3);
        QVERIFY(std::abs(ctrl_->objectCanvasRect().center().y() - to.center().y()) < 1e-3);
        to = QRectF(to.center().x() - to.width(), to.center().y() - to.height(), to.width() * 2, to.height() * 2);
        ctrl_->setObjectCanvasRect(ctrl_->objectCanvasRect(), to);
        QVERIFY(std::abs(ctrl_->objectCanvasRect().width() - to.width()) < 1e-3);
        QVERIFY(std::abs(ctrl_->objectCanvasRect().center().x() - to.center().x()) < 1e-3);
        // Stickers: the library, click adds at the playhead on a free overlay track.
        QVERIFY(sp->list()->count() >= 120);
        QVERIFY(!sp->list()->item(0)->icon().isNull());
        sp->category()->setCurrentIndex(sp->category()->findData(QString::fromUtf8("Hoa lá")));
        QVERIFY(sp->list()->count() >= 10 && sp->list()->count() < 40);
        emit sp->list()->itemClicked(sp->list()->item(0));
        const auto c = ctrl_->selectedContent();
        QVERIFY(c && c->value("kind", std::string{}) == "sticker");
        QVERIFY(win_->findChild<QDockWidget*>("stickers")->isVisible());
        QVERIFY(sp->animations()->isEnabled());
        // Everything undoes back to before.
        while (int(ctrl_->project()->tracks.size()) > tracksBefore) ctrl_->undo();
        QVERIFY(!ctrl_->project()->findClip(text.toStdString()));
    }

    void layoutResets() {
        auto* media = win_->findChild<QDockWidget*>("media");
        QVERIFY(media);
        media->hide();
        win_->saveLayout();
        win_->resetLayout();
        QVERIFY(media->isVisible());
    }
};

QTEST_MAIN(MainWindowTest)
#include "test_main_window.moc"

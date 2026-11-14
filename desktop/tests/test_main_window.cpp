// Workspace: every shortcut is unique and documented, keys drive navigation and
// edits through the window's actions, timeline tools, and layout reset.
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDockWidget>
#include <QListWidget>
#include <QSlider>
#include <QToolButton>
#include <QMenuBar>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>

#include "EditorController.h"
#include "LooksPanel.h"
#include "MainWindow.h"
#include "PreviewWidget.h"
#include "TimelineWidget.h"

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

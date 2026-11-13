// Workspace: every shortcut is unique and documented, keys drive navigation and
// edits through the window's actions, timeline tools, and layout reset.
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QMenuBar>
#include <QSet>
#include <QStandardPaths>
#include <QTest>

#include "EditorController.h"
#include "MainWindow.h"
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

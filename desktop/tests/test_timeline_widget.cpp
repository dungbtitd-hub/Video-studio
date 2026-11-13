// Drives TimelineWidget with real mouse/key events on a synthetic project:
// header toggles, snapping (and Shift to bypass), moves across tracks of the
// same type, and locked tracks refusing edits.
#include <QApplication>
#include <QMouseEvent>
#include <QTest>

#include "EditorController.h"
#include "TimelineWidget.h"

namespace {
// Layout constants of TimelineWidget (header 120 px, ruler 24 px, tracks 54 px, 60 px/s).
constexpr int kHeaderW = 120, kRulerH = 24, kTrackH = 54;
constexpr double kPx = 60;
int rowY(int row) { return kRulerH + row * kTrackH + kTrackH / 2; }
int timeX(double s) { return int(kHeaderW + s * kPx); }

void mouse(QWidget* w, QEvent::Type type, QPoint pos, Qt::KeyboardModifiers mods = {}) {
    const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(pos), w->mapToGlobal(QPointF(pos)), Qt::LeftButton, buttons, mods);
    QApplication::sendEvent(w, &e);
}
void drag(QWidget* w, QPoint from, QPoint to, Qt::KeyboardModifiers mods = {}) {
    mouse(w, QEvent::MouseButtonPress, from, mods);
    mouse(w, QEvent::MouseMove, (from + to) / 2, mods);
    mouse(w, QEvent::MouseMove, to, mods);
    mouse(w, QEvent::MouseButtonRelease, to, mods);
}
}  // namespace

class TimelineWidgetTest : public QObject {
    Q_OBJECT
    EditorController* ctrl_ = nullptr;
    TimelineWidget* w_ = nullptr;

    const vstudio::Track& track(const char* name) const {
        for (const auto& t : ctrl_->project()->tracks)
            if (t.name == name) return t;
        qFatal("no track %s", name);
    }
    // Synthetic project (see EditorController::loadSyntheticProject(6)), rows top-down:
    //   0 Video 1: [0,3) [3,9)    1 Video 2: [0,2) [2,7)    2 Audio 1: [0,4) [4,6)
private slots:
    void init() {
        ctrl_ = new EditorController;
        ctrl_->loadSyntheticProject(6);
        w_ = new TimelineWidget(ctrl_);
        w_->resize(1200, 300);
        w_->show();
        QVERIFY(QTest::qWaitForWindowExposed(w_));
        QCOMPARE(track("Video 1").clips.size(), size_t(2));
    }
    void cleanup() {
        delete w_;
        delete ctrl_;
    }

    void headerToggles() {
        const QPoint lock(10 + 22, kRulerH + kTrackH - 24 + 8), hide(58 + 22, lock.y());
        QTest::mouseClick(w_, Qt::LeftButton, {}, lock);
        QVERIFY(track("Video 1").locked);
        QTest::mouseClick(w_, Qt::LeftButton, {}, lock);
        QVERIFY(!track("Video 1").locked);
        QTest::mouseClick(w_, Qt::LeftButton, {}, hide);
        QVERIFY(!track("Video 1").enabled);
        QTest::mouseClick(w_, Qt::LeftButton, {}, hide + QPoint(0, 2 * kTrackH));  // audio row: mute
        QVERIFY(track("Audio 1").muted);
        QVERIFY(track("Audio 1").enabled);
        ctrl_->undo();
        QVERIFY(!track("Audio 1").muted);
    }

    void snapsToEdgeAndShiftBypasses() {
        // Audio clip [4,6) dragged right by 3.1 s: its start lands 0.1 s from Video 2's end (7) and snaps.
        drag(w_, {timeX(5), rowY(2)}, {timeX(5 + 3.1), rowY(2)});
        QCOMPARE(track("Audio 1").clips.back().timelineIn.seconds(), 7.0);
        ctrl_->undo();
        drag(w_, {timeX(5), rowY(2)}, {timeX(5 + 3.1), rowY(2)}, Qt::ShiftModifier);
        QVERIFY(std::abs(track("Audio 1").clips.back().timelineIn.seconds() - 7.1) < 0.02);
        ctrl_->undo();
        QTest::keyClick(w_, Qt::Key_N);  // snapping off
        drag(w_, {timeX(5), rowY(2)}, {timeX(5 + 3.1), rowY(2)});
        QVERIFY(std::abs(track("Audio 1").clips.back().timelineIn.seconds() - 7.1) < 0.02);
    }

    void movesAcrossSameTypeTracksOnly() {
        // Video 2 [0,2) -> Video 1 at 10 s.
        drag(w_, {timeX(1), rowY(1)}, {timeX(11), rowY(0)});
        QCOMPARE(track("Video 2").clips.size(), size_t(1));
        QCOMPARE(track("Video 1").clips.size(), size_t(3));
        QCOMPARE(track("Video 1").clips.back().timelineIn.seconds(), 10.0);
        // A video clip dropped on the audio row stays on its video track.
        drag(w_, {timeX(5), rowY(1)}, {timeX(15), rowY(2)});
        QCOMPARE(track("Audio 1").clips.size(), size_t(2));
        QCOMPARE(track("Video 2").clips.back().timelineIn.seconds(), 12.0);
    }

    void lockedTrackRefusesEdits() {
        ctrl_->setTrackFlags(QString::fromStdString(track("Audio 1").id), true, true, false);
        drag(w_, {timeX(5), rowY(2)}, {timeX(8), rowY(2)});
        QCOMPARE(track("Audio 1").clips.back().timelineIn.seconds(), 4.0);
        // Nor does a locked track accept clips: the drop stays on the source track.
        ctrl_->setTrackFlags(QString::fromStdString(track("Video 1").id), true, true, false);
        drag(w_, {timeX(1), rowY(1)}, {timeX(11), rowY(0)});
        QCOMPARE(track("Video 1").clips.size(), size_t(2));
        QCOMPARE(track("Video 2").clips.back().timelineIn.seconds(), 10.0);
    }
};

QTEST_MAIN(TimelineWidgetTest)
#include "test_timeline_widget.moc"

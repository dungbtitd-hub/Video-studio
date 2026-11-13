#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QUrl>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "EditorController.h"
#include "PreviewItem.h"

namespace {

void report(const char* name, std::vector<double> v) {
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x : v) sum += x;
    std::printf("qml     | %-42s | avg %7.2f ms | p95 %7.2f ms | max %7.2f ms\n", name, sum / v.size(),
                v[size_t(v.size() * 0.95)], v.back());
}

// Same workload as the Widgets bench: load a synthetic project, scroll the
// timeline 300 steps of 7 s, then 50 clip moves. Each sample is the time from
// the change until the window has swapped a new frame (scene graph sync +
// render), which is what the user feels.
void runTimelineBench(EditorController& editor, QQuickWindow* win, int clips, std::function<void()> done) {
    QElapsedTimer t;
    t.start();
    editor.loadSyntheticProject(clips);
    QCoreApplication::processEvents();
    auto* flick = win->findChild<QObject*>("timelineFlick");
    struct State {
        int step = 0;
        std::vector<double> scroll, edit;
        QElapsedTimer t;
        QVariantList tracks;
        QVariantList clipsV1;
        QMetaObject::Connection conn;
    };
    auto st = std::make_shared<State>();
    // The first frame after the load pays for creating every clip delegate.
    st->t.start();
    win->update();
    st->conn = QObject::connect(win, &QQuickWindow::frameSwapped, win, [=, &editor]() mutable {
        const double ms = st->t.nsecsElapsed() / 1e6;
        if (st->step == 0) {
            std::printf("qml     | load %d clips + build UI + first frame: %lld ms\n", clips,
                        (long long)(t.elapsed()));
            st->tracks = editor.tracks();
            st->clipsV1 = st->tracks[0].toMap()["clips"].toList();
        } else if (st->step <= 300) {
            st->scroll.push_back(ms);
        } else {
            st->edit.push_back(ms);
        }
        ++st->step;
        if (st->step > 350) {
            QObject::disconnect(st->conn);
            report("scroll timeline (frame swapped)", st->scroll);
            report("move clip -> frame swapped", st->edit);
            done();
            return;
        }
        // Queue the next change after this frame, so samples do not overlap.
        QTimer::singleShot(0, win, [=, &editor] {
            st->t.restart();
            if (st->step <= 300) {
                flick->setProperty("contentX", (st->step - 1) * 7.0 * 60.0);
            } else {
                const QVariantMap c = st->clipsV1[(st->step - 301) % st->clipsV1.size()].toMap();
                editor.moveClip(c["id"].toString(), st->tracks[0].toMap()["id"].toString(), editor.duration() + 5);
            }
            win->update();
        });
    });
}

}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Video Studio");
    QGuiApplication::setOrganizationName("Video Studio");
    QQuickStyle::setStyle("Basic");

    EditorController editor;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("editor", &editor);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    // The module is embedded under qrc:/VideoStudio (Qt 6.4-compatible
    // resource prefix), which is not on the default import path of Qt >= 6.5.
    engine.addImportPath(QStringLiteral("qrc:/"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("VideoStudio", "Main");
#else
    engine.load(QUrl(QStringLiteral("qrc:/VideoStudio/qml/Main.qml")));
#endif
    if (auto* root = engine.rootObjects().value(0)) {
        if (auto* preview = root->findChild<PreviewItem*>("preview")) {
            QObject::connect(&editor, &EditorController::frameReady, preview, &PreviewItem::setFrame);
            QObject::connect(preview, &PreviewItem::sizeHintChanged, &editor, [&editor, preview] {
                const qreal dpr = preview->window() ? preview->window()->devicePixelRatio() : 1.0;
                editor.setPreviewSize(int(preview->width() * dpr), int(preview->height() * dpr));
            });
            editor.refreshPreview();
        }
    }

    // Command line: files to import (appended to the timeline), and
    // --screenshot <png> [--seek <seconds>] for CI smoke tests.
    QList<QUrl> files;
    QString shot;
    double seekTo = -1;
    int bench = 0;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--screenshot" && i + 1 < args.size()) shot = args[++i];
        else if (args[i] == "--bench-timeline" && i + 1 < args.size()) bench = args[++i].toInt();
        else if (args[i] == "--seek" && i + 1 < args.size()) seekTo = args[++i].toDouble();
        else files << QUrl::fromLocalFile(args[i]);
    }
    if (bench > 0) {
        auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        if (!w) return 1;
        QTimer::singleShot(500, &app, [&, w] {
            runTimelineBench(editor, w, bench, [&, w] {
                if (!shot.isEmpty()) w->grabWindow().save(shot);
                QCoreApplication::exit(0);
            });
        });
        return app.exec();
    }
    if (!files.isEmpty()) editor.importMedia(files, true);
    if (seekTo >= 0) QTimer::singleShot(2500, &editor, [&] { editor.seek(seekTo); });
    if (!shot.isEmpty()) {
        QTimer::singleShot(4000, &app, [&] {
            auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
            const bool ok = w && w->grabWindow().save(shot);
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}

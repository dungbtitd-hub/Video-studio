#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "EditorController.h"

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
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("VideoStudio", "Main");
#else
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.load(QUrl(QStringLiteral("qrc:/VideoStudio/qml/Main.qml")));
#endif
    // Command line: files to import (appended to the timeline), and
    // --screenshot <png> [--seek <seconds>] for CI smoke tests.
    QList<QUrl> files;
    QString shot;
    double seekTo = -1;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--screenshot" && i + 1 < args.size()) shot = args[++i];
        else if (args[i] == "--seek" && i + 1 < args.size()) seekTo = args[++i].toDouble();
        else files << QUrl::fromLocalFile(args[i]);
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

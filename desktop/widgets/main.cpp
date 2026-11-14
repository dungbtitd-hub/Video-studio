#include <QApplication>
#include <QIcon>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cstdio>
#include <vector>

#include "EditorController.h"
#include "GradePanel.h"
#include "MainWindow.h"

#include <QDockWidget>
#include "Theme.h"
#include "TimelineWidget.h"

namespace {

void report(const char* name, std::vector<double> v) {
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x : v) sum += x;
    std::printf("widgets | %-42s | avg %7.2f ms | p95 %7.2f ms | max %7.2f ms\n", name, sum / v.size(),
                v[size_t(v.size() * 0.95)], v.back());
}

// Scroll the timeline across the project, then move clips, timing each repaint.
void runTimelineBench(EditorController& editor, MainWindow& win, int clips) {
    QElapsedTimer t;
    t.start();
    editor.loadSyntheticProject(clips);
    QApplication::processEvents();
    std::printf("widgets | load %d clips + build UI: %lld ms\n", clips, (long long)t.elapsed());
    TimelineWidget* tl = win.timeline();
    std::vector<double> scroll, edit;
    for (int i = 0; i < 300; ++i) {
        t.restart();
        tl->setScrollSeconds(i * 7.0);
        tl->repaint();
        scroll.push_back(t.nsecsElapsed() / 1e6);
    }
    const auto proj = editor.project();
    const vstudio::Track& track = proj->tracks[0];
    for (int i = 0; i < 50; ++i) {
        const vstudio::Clip& c = track.clips[size_t(i) % track.clips.size()];
        t.restart();
        editor.moveClip(QString::fromStdString(c.id), QString::fromStdString(track.id), editor.duration() + 5);
        tl->repaint();
        edit.push_back(t.nsecsElapsed() / 1e6);
    }
    report("scroll timeline (repaint)", scroll);
    report("move clip -> timeline repainted", edit);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Video Studio");
    QApplication::setApplicationVersion(QStringLiteral(VSTUDIO_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/videostudio.png")));
    app.setStyleSheet(Theme::styleSheet());

    EditorController editor;
    MainWindow win(&editor);
    win.show();

    QList<QUrl> files, subtitleFiles;
    QString shot;
    int bench = 0;
    double seekTo = -1;
    QString look;  // --look <id>: select the first video clip and apply a look (screenshots, CI)
    QString grade;  // --grade <json>: same with a colour grade; shows the Chỉnh màu panel
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--screenshot" && i + 1 < args.size()) shot = args[++i];
        else if (args[i] == "--seek" && i + 1 < args.size()) seekTo = args[++i].toDouble();
        else if (args[i] == "--bench-timeline" && i + 1 < args.size()) bench = args[++i].toInt();
        else if (args[i] == "--look" && i + 1 < args.size()) look = args[++i];
        else if (args[i] == "--grade" && i + 1 < args.size()) grade = args[++i];
        else if (QRegularExpression("\\.(srt|vtt|ass|ssa)$", QRegularExpression::CaseInsensitiveOption).match(args[i]).hasMatch())
            subtitleFiles << QUrl::fromLocalFile(args[i]);  // subtitle files open as subtitle tracks
        else files << QUrl::fromLocalFile(args[i]);
    }
    if (bench > 0) {
        QTimer::singleShot(500, &app, [&] {
            runTimelineBench(editor, win, bench);
            if (!shot.isEmpty()) win.grab().save(shot);
            QApplication::exit(0);
        });
        return app.exec();
    }
    if (!files.isEmpty()) editor.importMedia(files, true);
    // Plain start: unfinished projects slide up at the bottom, once the window has painted.
    else if (subtitleFiles.isEmpty()) QTimer::singleShot(150, &win, [&win] { win.showRecentProjects(); });
    if (!subtitleFiles.isEmpty())
        QTimer::singleShot(1500, &editor, [&] {
            for (const QUrl& f : subtitleFiles) editor.importSubtitles(f);
            // Show the subtitle panel with the first line selected.
            const auto p = editor.project();
            const auto* t = p ? p->findTrack(editor.currentSubtitleTrack().toStdString()) : nullptr;
            if (t && !t->clips.empty()) editor.setSelectedClip(QString::fromStdString(t->clips.front().id));
        });
    if (seekTo >= 0) QTimer::singleShot(2500, &editor, [&] { editor.seek(seekTo); });
    if (!look.isEmpty())
        QTimer::singleShot(2000, &editor, [&] {
            const QStringList clips = editor.lookableClips();
            if (clips.isEmpty()) return;
            editor.setSelectedClip(clips.front());
            editor.setClipLook({clips.front()}, look, 1.0);
        });
    if (!grade.isEmpty())
        QTimer::singleShot(2000, &editor, [&] {
            const QStringList clips = editor.lookableClips();
            if (clips.isEmpty()) return;
            editor.setSelectedClip(clips.front());
            editor.setClipGrade({clips.front()}, vstudio::Json::parse(grade.toStdString(), nullptr, false));
            if (auto* d = qobject_cast<QDockWidget*>(win.grade()->parentWidget())) d->raise();
        });
    if (!shot.isEmpty()) {
        QTimer::singleShot(4000, &app, [&] {
            const bool ok = win.grab().save(shot);
            QApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}

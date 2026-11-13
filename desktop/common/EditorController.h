// Bridges a desktop UI (QML or Widgets) to Video Studio Core.
// UI-thread rule: everything here returns immediately. Probing, decoding,
// audio and export run on core background threads; results come back to the
// UI thread through queued signals.
#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>
#include <optional>

#include "vstudio/editor.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/preview.hpp"
#include "vstudio/scheduler.hpp"

class AudioOutput;

class EditorController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QVariantList assets READ assets NOTIFY projectChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectChanged)
    Q_PROPERTY(double duration READ duration NOTIFY projectChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(double playhead READ playhead NOTIFY playheadChanged)
    Q_PROPERTY(QString timecode READ timecode NOTIFY playheadChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString selectedClip READ selectedClip WRITE setSelectedClip NOTIFY selectionChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int pendingJobs READ pendingJobs NOTIFY jobsChanged)
    Q_PROPERTY(double fps READ fps NOTIFY projectChanged)

public:
    explicit EditorController(QObject* parent = nullptr);
    ~EditorController() override;

    // Preview output: frames arrive on the UI thread through frameReady().
    Q_INVOKABLE void setPreviewSize(int width, int height);
    Q_INVOKABLE void refreshPreview();
    // Builds a timeline-only stress project with `clips` clips (benchmarks).
    Q_INVOKABLE void loadSyntheticProject(int clips);
    Q_INVOKABLE void newProject(const QString& name, int width, int height, int fps);
    Q_INVOKABLE void openProject(const QUrl& folder);
    Q_INVOKABLE void saveProject(const QUrl& folder);
    Q_INVOKABLE void importMedia(const QList<QUrl>& files, bool appendToTimeline = false);
    Q_INVOKABLE void addToTimeline(const QString& assetId);
    Q_INVOKABLE void splitAtPlayhead();
    Q_INVOKABLE void deleteSelected();
    Q_INVOKABLE void moveClip(const QString& clipId, const QString& trackId, double seconds);
    Q_INVOKABLE void trimClip(const QString& clipId, bool start, double seconds);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stepFrames(int frames);
    Q_INVOKABLE void exportTo(const QUrl& file, int width, int height);
    Q_INVOKABLE void cancelExport();

    QString projectName() const;
    // QVariant models for QML, built lazily on first read after an edit, so a
    // front-end that reads the snapshot directly (Widgets) never pays for them.
    QVariantList assets() const;
    QVariantList tracks() const;
    // Immutable snapshot of the current project; cheap to hold, safe to read.
    [[nodiscard]] std::shared_ptr<const vstudio::Project> project() const { return snapshot_; }
    double duration() const;
    bool canUndo() const { return editor_->canUndo(); }
    bool canRedo() const { return editor_->canRedo(); }
    QString undoLabel() const { return QString::fromStdString(editor_->undoLabel()); }
    bool dirty() const { return editor_->dirty(); }
    double playhead() const { return playhead_; }
    QString timecode() const;
    bool playing() const;
    QString selectedClip() const { return selected_; }
    void setSelectedClip(const QString& id);
    double exportProgress() const { return exportProgress_; }
    bool exporting() const { return exporting_; }
    QString status() const { return status_; }
    int pendingJobs() const { return pendingJobs_; }
    double fps() const;

signals:
    void projectChanged();
    void playheadChanged();
    void playingChanged();
    void selectionChanged();
    void exportChanged();
    void statusChanged();
    void jobsChanged();
    void frameReady(const QImage& frame);

private:
    template <class F>
    bool edit(F&& f);  // runs an edit, reports EditError in the status bar
    void published();  // take a new snapshot, hand it to the preview, invalidate QML models
    void setStatus(const QString& s);
    void buildAssetsModel() const;
    void buildTracksModel() const;
    void tick();
    void onProbed(const std::optional<vstudio::MediaAsset>& asset, const std::string& err, const std::string& path,
                  bool appendToTimeline);
    void resetSession(vstudio::Project p, const QString& packageDir);

    vstudio::ModuleRegistry modules_;
    std::shared_ptr<vstudio::IMediaBackend> backend_;
    std::unique_ptr<vstudio::Editor> editor_;
    std::unique_ptr<vstudio::JobScheduler> jobs_;
    std::unique_ptr<vstudio::PreviewEngine> preview_;
    std::unique_ptr<AudioOutput> audio_;
    int previewW_ = 960, previewH_ = 540;
    QString packageDir_;
    std::shared_ptr<const vstudio::Project> snapshot_;
    mutable QVariantList assets_, tracks_;
    mutable bool assetsDirty_ = true, tracksDirty_ = true;
    double playhead_ = 0;
    bool wasPlaying_ = false;
    QString selected_;
    double exportProgress_ = 0;
    bool exporting_ = false;
    vstudio::CancelToken exportCancel_;
    QString status_;
    int pendingJobs_ = 0;
    QTimer timer_;
};

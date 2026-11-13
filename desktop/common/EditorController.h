// Bridges a desktop UI (QML or Widgets) to Video Studio Core.
// UI-thread rule: everything here returns immediately. Probing, decoding,
// audio and export run on core background threads; results come back to the
// UI thread through queued signals.
#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <functional>

#include "vstudio/transfer.hpp"
#include <QVariantList>
#include <memory>
#include <optional>

#include "vstudio/editor.hpp"
#include "vstudio/media_library.hpp"
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
    Q_INVOKABLE void rippleDeleteSelected();  // delete and close the gap on its track
    Q_INVOKABLE void seekBy(double seconds);
    Q_INVOKABLE void jumpEditPoint(bool forward);  // previous/next clip edge on any track
    Q_INVOKABLE void moveClip(const QString& clipId, const QString& trackId, double seconds);
    Q_INVOKABLE void trimClip(const QString& clipId, bool start, double seconds);
    // ---- tracks (each one undo step) ----
    Q_INVOKABLE void addTrack(bool audio);
    Q_INVOKABLE void removeTrack(const QString& trackId);
    Q_INVOKABLE void renameTrack(const QString& trackId, const QString& name);
    Q_INVOKABLE void setTrackFlags(const QString& trackId, bool enabled, bool locked, bool muted);
    Q_INVOKABLE void moveTrack(const QString& trackId, int indexInType);
    // Snapping while dragging: reads the current snapshot, never edits. Returns the time to use.
    double snapClipMove(const QString& clipId, double proposedStart, double tolerance, bool* snapped,
                        double* target = nullptr) const;
    double snapTime(double t, double tolerance, const QString& excludeClip, bool* snapped, double* target = nullptr) const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stepFrames(int frames);
    Q_INVOKABLE void exportTo(const QUrl& file, int width, int height);
    // Export with a shared preset (vstudio/export_presets.hpp), sized for this project.
    Q_INVOKABLE void exportWithPreset(const QUrl& file, const QString& presetId);
    // For the export dialog: id, name, description, width, height, fps, bitrate, estimatedBytes.
    [[nodiscard]] QVariantList exportPresets() const;
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
    // Colour looks (vstudio/looks.hpp): one undo step for any number of clips; "" removes.
    void setClipLook(const QStringList& clipIds, const QString& lookId, double strength);
    // Look on a clip: (look id, strength); empty id if none.
    [[nodiscard]] QPair<QString, double> clipLook(const QString& clipId) const;
    [[nodiscard]] QString clipAsset(const QString& clipId) const;
    // Every clip that can take a look: video tracks that are not locked.
    [[nodiscard]] QStringList lookableClips() const;
    void setSelectedClip(const QString& id);
    double exportProgress() const { return exportProgress_; }
    bool exporting() const { return exporting_; }
    QString status() const { return status_; }
    int pendingJobs() const { return pendingJobs_; }
    double fps() const;

    // ---- media library: thumbnails / waveforms built in the background ----
    [[nodiscard]] std::shared_ptr<const vstudio::ThumbnailStrip> thumbnails(const QString& assetId) const;
    [[nodiscard]] std::shared_ptr<const vstudio::Waveform> waveform(const QString& assetId) const;
    [[nodiscard]] bool assetMissing(const QString& assetId) const { return missing_.contains(assetId); }
    // Searches `folder` (in the background) for the moved file and relinks it.
    Q_INVOKABLE void relinkFromFolder(const QString& assetId, const QUrl& folder);
    Q_INVOKABLE void relinkToFile(const QString& assetId, const QUrl& file);

    // ---- moving a project to another device (.vstudiopkg, vstudio/transfer.hpp) ----
    // Both run in the background; progress goes to the status bar, the end to transferFinished.
    Q_INVOKABLE void packProject(const QUrl& file);
    // Unpacks next to the package (or into `destRoot`) and opens the new project.
    Q_INVOKABLE void openPackage(const QUrl& file, const QUrl& destRoot = {});
    [[nodiscard]] bool transferring() const { return transferring_; }
    void cancelTransfer() { transferCancel_.cancel(); }

signals:
    void projectChanged();
    void playheadChanged();
    void playingChanged();
    void selectionChanged();
    void exportChanged();
    void statusChanged();
    void jobsChanged();
    void frameReady(const QImage& frame);
    void mediaLibraryChanged(const QString& assetId);  // a thumbnail strip or waveform is ready
    // ok: packed / unpacked and opened; `path` is the package file or the opened project folder.
    void transferFinished(bool ok, const QString& path, const QString& message);

private:
    template <class F>
    bool edit(F&& f);  // runs an edit, reports EditError in the status bar
    void published();  // take a new snapshot, hand it to the preview, invalidate QML models
    void setStatus(const QString& s);
    void startExport(const QUrl& file, const vstudio::ExportSettings& es);
    void buildAssetsModel() const;
    void buildTracksModel() const;
    void tick();
    void onProbed(const std::optional<vstudio::MediaAsset>& asset, const std::string& err, const std::string& path,
                  bool appendToTimeline);
    void resetSession(vstudio::Project p, const QString& packageDir);
    void refreshLibrary();  // missing files + background requests, only when the asset list changed
    [[nodiscard]] std::string resolve(const vstudio::MediaAsset& a) const;

    vstudio::ModuleRegistry modules_;
    std::shared_ptr<vstudio::IMediaBackend> backend_;
    std::unique_ptr<vstudio::Editor> editor_;
    std::unique_ptr<vstudio::JobScheduler> jobs_;
    std::unique_ptr<vstudio::JobScheduler> libJobs_;  // thumbnails, waveforms, relink search
    std::unique_ptr<vstudio::MediaLibrary> library_;
    QSet<QString> missing_;
    std::size_t assetsSignature_ = 0;
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
    bool transferring_ = false;
    vstudio::CancelToken transferCancel_;
    void runTransfer(const QString& what, std::function<vstudio::TransferResult(const vstudio::TransferProgress&,
                                                                                 const std::atomic<bool>*)> work,
                     std::function<void(const vstudio::TransferResult&)> done);
    QTimer timer_;
};

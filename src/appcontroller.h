#pragma once
#include "markermodel.h"
#include "undostack.h"
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqml.h>

class QQmlEngine;
class QJSEngine;

// Makes the core MarkerModel type known to QML (it lives in a library without QML).
struct MarkerModelForeign
{
    Q_GADGET
    QML_FOREIGN(MarkerModel)
    QML_NAMED_ELEMENT(MarkerModel)
    QML_UNCREATABLE("Provided by App.markers")
};

// App-level logic that is not playback: the folder queue, resume points,
// in/out points, markers and exports (clip, EDL, CSV, chapters, frame grab).
class AppController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS // exposed to QML as the "App" singleton instance in main.cpp
    Q_PROPERTY(MarkerModel *markers READ markers CONSTANT)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentFileChanged)
    Q_PROPERTY(QStringList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(int queueIndex READ queueIndex NOTIFY currentFileChanged)
    Q_PROPERTY(double inPoint READ inPoint WRITE setInPoint NOTIFY inOutChanged)
    Q_PROPERTY(double outPoint READ outPoint WRITE setOutPoint NOTIFY inOutChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double busyProgress READ busyProgress NOTIFY busyChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)
    Q_PROPERTY(bool ffmpegAvailable READ ffmpegAvailable CONSTANT)
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(int ratingsRevision READ ratingsRevision NOTIFY ratingsChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool isWindows READ isWindows CONSTANT)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    // One shared instance for QML and C++.
    static AppController *instance();
    static AppController *create(QQmlEngine *, QJSEngine *);

    MarkerModel *markers() { return &m_markers; }
    QString currentFile() const { return m_current; }
    QString currentName() const;
    QStringList queue() const { return m_queue; }
    int queueIndex() const { return m_queue.indexOf(m_current); }
    double inPoint() const { return m_in; }
    double outPoint() const { return m_out; }
    void setInPoint(double t);
    void setOutPoint(double t);
    bool busy() const { return m_proc != nullptr; }
    double busyProgress() const { return m_progress; }
    QString busyText() const { return m_busyText; }
    bool ffmpegAvailable() const { return !ffmpegPath().isEmpty(); }
    QStringList recentFiles() const;
    int ratingsRevision() const { return m_ratingsRev; }
    QString version() const;
    bool isWindows() const;
    bool animationsEnabled() const;
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    QString undoText() const { return m_undo.undoText(); }
    QString redoText() const { return m_undo.redoText(); }
    UndoStack *undoStack() { return &m_undo; }

    // Returns the local path or URL to hand to mpv, and remembers it. Empty if not playable.
    Q_INVOKABLE QString prepareOpen(const QVariant &urlOrPath);
    Q_INVOKABLE QString nextInQueue(int direction) const;
    Q_INVOKABLE double resumePosition(const QString &file) const;
    Q_INVOKABLE void savePosition(double position, double duration);
    Q_INVOKABLE void clearInOut();

    Q_INVOKABLE void exportClip(bool lossless);
    // Each range [{ start, end, note }] of the open clip (or of "file") as its own file.
    Q_INVOKABLE void exportRanges(const QVariantList &ranges, bool lossless, const QString &file = QString(), const QString &folder = QString());
    Q_INVOKABLE void exportMarkersEdl(double fps);
    Q_INVOKABLE void exportMarkersCsv(double fps);
    Q_INVOKABLE void copyYouTubeChapters(double duration);
    Q_INVOKABLE QString frameGrabPath(qint64 frame) const;
    Q_INVOKABLE void revealInFolder(const QString &path) const;
    Q_INVOKABLE QString formatTimecode(double seconds, double fps) const;
    Q_INVOKABLE QString formatClock(double seconds) const;
    Q_INVOKABLE QString formatBytes(double bytes) const;
    Q_INVOKABLE double parseTime(const QString &text, double fps) const; // seconds or -1
    Q_INVOKABLE void cancelJob();
    Q_INVOKABLE void copyText(const QString &text) const;

    // Settings (QSettings, under "prefs/").
    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    // Star rating 0-5 for a clip, kept per file path.
    Q_INVOKABLE int rating(const QString &file) const;
    Q_INVOKABLE void setRating(const QString &file, int stars);
    // Review data for any clip (not only the open one).
    Q_INVOKABLE QVariantList markersFor(const QString &file) const;
    Q_INVOKABLE int selectCountFor(const QString &file) const;
    Q_INVOKABLE void addMarkersTo(const QString &file, const QVariantList &list, const QString &label);
    Q_INVOKABLE QString fingerprint(const QString &file) const;
    // Undo / redo: returns what was undone ("delete marker"), or empty when there was nothing.
    Q_INVOKABLE QString undo();
    Q_INVOKABLE QString redo();
    // Wrap a drag of the In / Out edges so it is one undo step.
    // Turns In to Out into a select and clears In / Out, as one undo step. Returns false without a range.
    Q_INVOKABLE bool makeSelectFromInOut(const QString &note = QString());
    Q_INVOKABLE void beginInOutEdit();
    Q_INVOKABLE void endInOutEdit();
    Q_INVOKABLE void removeRecent(const QString &file);
    Q_INVOKABLE void clearRecent();
    Q_INVOKABLE bool isSubtitleFile(const QVariant &urlOrPath) const;
    Q_INVOKABLE QString localPath(const QVariant &urlOrPath) const;
    Q_INVOKABLE void openDefaultAppsSettings() const;
    Q_INVOKABLE void openFolder(const QString &path) const;
    Q_INVOKABLE QString mpvConfigDir() const;
    Q_INVOKABLE QString exportFolderLabel() const;
    Q_INVOKABLE QString exportFolderFor(const QString &file) const; // where exports for this clip go (created)
    // Facts the Resolve dialog sorts and filters by: name, modified (ms), size, duration (s, -1 unknown), rating.
    Q_INVOKABLE QVariantMap clipFacts(const QString &file) const;
    Q_INVOKABLE QString keyText(int key, int modifiers) const; // key event -> "Ctrl+Shift+E"

    // Called by the single-instance server when another launch hands over a file.
    void handOver(const QString &file) { emit openRequested(file); }

    QString ffmpegPath() const;
    static QString uniquePath(const QString &wanted);

signals:
    void currentFileChanged();
    void queueChanged();
    void inOutChanged();
    void busyChanged();
    void recentFilesChanged();
    void message(const QString &text, const QString &revealPath);
    void ratingsChanged();
    void openRequested(const QString &file);
    void undoChanged();

private:
    void writeFile(const QString &path, const QString &content, const QString &what);
    struct ClipJob { QString src, out; double in = 0, out_ = 0; bool lossless = true; };
    void runNextClip();
    void pushInOut(const QString &label, double oldIn, double oldOut);
    void apply(const UndoStack::Entry &e, bool undo);
    void setMarkersOf(const QString &file, const QJsonArray &arr);
    QString baseOut() const;

    MarkerModel m_markers;
    QString m_current;
    QStringList m_queue;
    double m_in = -1, m_out = -1;
    QPointer<QProcess> m_proc;
    double m_progress = 0;
    QString m_busyText;
    QVector<ClipJob> m_clipJobs;
    int m_clipTotal = 0, m_clipDone = 0, m_clipFailed = 0;
    QString m_lastClipOut;
    int m_ratingsRev = 0;
    UndoStack m_undo;
    int m_inOutEdit = 0;
    double m_editIn = -1, m_editOut = -1;
    bool m_applying = false;
};

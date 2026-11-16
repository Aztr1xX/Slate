#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>

// "Send to DaVinci Resolve": probes every clip on the planned timeline with ffprobe (frame rate,
// size, audio, embedded start timecode), then writes an FCPXML timeline (and, when asked, a
// coloured marker EDL and separate clip files).
class ResolveExport : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS // exposed to QML as the "ResolveExport" singleton instance in main.cpp
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    static ResolveExport *instance();

    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }

    // items: [{ file, start, end (-1 = to the end), name, markers: [{ time, end, kind, note }], keywords: [] }]
    // opts: title, folder, fps ("match" or a number), resolution ("match", "1080p", "4k", "vertical"), gap (s),
    //       handles, handlesUnit ("frames" / "seconds"), clipMarkers, markClipStarts, output ("timeline", "both", "files")
    Q_INVOKABLE void start(const QVariantList &items, const QVariantMap &opts);
    Q_INVOKABLE void cancel();

    struct Probe { bool ok = false; double fps = 0, duration = 0; int width = 0, height = 0, rotation = 0; bool video = false, audio = false; int channels = 2, rate = 48000; QString timecode; };
    static Probe parseProbe(const QByteArray &json);

signals:
    void changed();
    void finished(bool ok, const QString &message, const QString &path);

private:
    explicit ResolveExport(QObject *parent = nullptr);
    void probeNext();
    void build();
    void fail(const QString &why);

    bool m_busy = false;
    double m_progress = 0;
    QString m_status;
    QVariantList m_items;
    QVariantMap m_opts;
    QStringList m_todo;
    int m_total = 0;
    QHash<QString, Probe> m_probes; // by path + modified time
    QPointer<QProcess> m_proc;
};

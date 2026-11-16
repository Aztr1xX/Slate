#pragma once
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVariantList>
#include <QtQml/qqml.h>

// Thumbnail frames: finds the best-looking frames of a clip (sharp, well exposed, spread out),
// saves one as a PNG or a YouTube-ready JPG, and can embed it in the file as cover art so
// File Explorer shows it as the file's icon (MP4 / MOV / M4V cover, MKV attachment).
//
// Embedding remuxes into a temporary copy (no re-encoding), checks the copy with ffprobe, and
// only then replaces the original, keeping its created and modified dates and its review notes.
class Thumbnails : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS // exposed to QML as the "Thumbnails" singleton instance in main.cpp
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString file READ file NOTIFY changed)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY candidatesChanged)
public:
    static Thumbnails *instance();

    bool busy() const { return m_proc || m_embedProc; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QString file() const { return m_file; }
    QVariantList candidates() const { return m_candidates; }

    // Samples about 150 frames and keeps the best 12: [{ time, url, score }] in time order.
    Q_INVOKABLE void find(const QString &file, double duration);
    Q_INVOKABLE void cancel();
    // Full-resolution frame at time t. kind "png" (original size) or "youtube" (1280x720 JPG under 2 MB).
    // Returns at once; finished(ok, message, path) follows.
    Q_INVOKABLE void saveFrame(const QString &file, double t, const QString &kind);
    Q_INVOKABLE void copyFrame(const QString &file, double t);
    Q_INVOKABLE bool canEmbed(const QString &file) const;
    Q_INVOKABLE double embeddedTime(const QString &file) const; // the time set as the Explorer thumbnail, or -1
    // Writes the new copy next to the file. When it checks out, swapReady(file) asks QML to let go of
    // the file (if it's playing), then QML calls commitSwap(file).
    Q_INVOKABLE void embed(const QString &file, double t);
    Q_INVOKABLE void removeEmbedded(const QString &file);
    // Replaces the original with the checked copy. If the file is still busy and lastTry is false,
    // returns false and keeps the copy so it can be tried again a moment later.
    Q_INVOKABLE bool commitSwap(const QString &file, bool lastTry = true);

    // Pure helpers (tested in --selftest)
    static double scoreFrame(const QImage &img);
    static QList<int> pickSpread(const QList<double> &times, const QList<double> &scores, int count, double minGap);

signals:
    void changed();
    void candidatesChanged();
    void finished(bool ok, const QString &message, const QString &path);
    void swapReady(const QString &file);

private:
    explicit Thumbnails(QObject *parent = nullptr);
    void setStatus(const QString &s, double p = 0);
    void scoreAll(const QString &dir, const QList<double> &times);
    void remux(const QString &file, double t, bool remove);
    void grab(const QString &file, double t, const QString &out, const QStringList &filters, std::function<void(bool)> done);
    QString tempPathFor(const QString &file) const;

    QString m_file, m_status;
    double m_progress = 0, m_duration = 0;
    QVariantList m_candidates;
    QPointer<QProcess> m_proc, m_embedProc;
    // pending swap
    QString m_swapFile, m_swapTemp;
    double m_swapTime = -1;
};

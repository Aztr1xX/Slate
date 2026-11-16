#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqml.h>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

// Offline speech-to-text with whisper.cpp (whisper-cli, run as a separate process).
// The model is downloaded once on first use; transcripts are kept per file (by contents).
//
// One clip is shown at a time (load), but transcription runs from a queue, so a whole folder can
// be transcribed in the background ("Transcribe all clips") while you keep reviewing. Once clips
// have transcripts they can be searched together, and lines said more than once (retakes) found.
class Transcript : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS // exposed to QML as the "Transcript" singleton instance in main.cpp
    // State of the shown clip: idle, queued, downloading, preparing, transcribing, ready, error
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY segmentsChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool modelReady READ modelReady NOTIFY stateChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    // The background queue
    Q_PROPERTY(bool busy READ busy NOTIFY queueChanged)
    Q_PROPERTY(QString jobFile READ jobFile NOTIFY queueChanged)
    Q_PROPERTY(double jobProgress READ jobProgress NOTIFY queueChanged)
    Q_PROPERTY(int batchTotal READ batchTotal NOTIFY queueChanged)
    Q_PROPERTY(int batchDone READ batchDone NOTIFY queueChanged)
    Q_PROPERTY(int revision READ revision NOTIFY transcriptsChanged)

public:
    static Transcript *instance();

    QString state() const { return m_state; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    QVariantList segments() const { return m_segments; }
    bool available() const;
    bool modelReady() const;
    QString model() const { return m_model; }
    void setModel(const QString &m);
    bool busy() const { return !m_job.isEmpty(); }
    QString jobFile() const { return m_job; }
    double jobProgress() const { return m_jobProgress; }
    int batchTotal() const { return m_batchTotal; }
    int batchDone() const { return m_batchDone; }
    int revision() const { return m_rev; }

    Q_INVOKABLE void load(const QString &file);  // show a clip's transcript, if it has one
    Q_INVOKABLE void start(const QString &file); // transcribe this clip next (downloads the model if needed)
    Q_INVOKABLE int transcribeAll(const QStringList &files); // queues every clip without a transcript; returns how many
    Q_INVOKABLE void cancel();                   // stops the current job and empties the queue
    Q_INVOKABLE void rescan();                   // forget what's cached and look at the transcript files again
    Q_INVOKABLE int indexAt(double t) const;
    Q_INVOKABLE QString exportAs(const QString &format); // "srt" or "txt"; returns the written path
    Q_INVOKABLE static QString modelSizeLabel(const QString &model);
    Q_INVOKABLE bool hasTranscript(const QString &file) const;
    Q_INVOKABLE int countWithTranscripts(const QStringList &files) const;
    Q_INVOKABLE QVariantList segmentsFor(const QString &file) const;
    // Lines containing every word of query, across files: [{ file, start, end, text }]
    Q_INVOKABLE QVariantList search(const QStringList &files, const QString &query) const;
    // Lines said more than once: [{ text, members: [{ file, start, end, text, keeper, complete }] }]
    Q_INVOKABLE QVariantList repeats(const QStringList &files) const;

    QString modelPath() const;
    static QString parseWhisperJson(const QByteArray &json, QVariantList *out); // returns error text or empty
    static QString toSrt(const QVariantList &segments);
    // Retake detection on plain data: lines = [{ file, start, end, text }] in order. Pure, unit tested in --selftest.
    static QVariantList findRepeats(const QVariantList &lines);

signals:
    void stateChanged();
    void segmentsChanged();
    void modelChanged();
    void queueChanged();
    void transcriptsChanged();
    void finished(bool ok, const QString &message);

private:
    explicit Transcript(QObject *parent = nullptr);
    void setState(const QString &s, double progress = 0, const QString &err = QString());
    void refreshShownState();
    void setJobProgress(const QString &stage, double p);
    void next();
    void download();
    void extractAudio();
    void runWhisper();
    void jobDone(bool ok, const QString &err, const QVariantList &segs);
    QString cachePath(const QString &file) const;

    QString m_state = QStringLiteral("idle"), m_error, m_file, m_wav, m_model = QStringLiteral("base.en");
    double m_progress = 0;
    QVariantList m_segments;
    // queue
    QString m_job, m_jobStage;
    double m_jobProgress = 0;
    QStringList m_queue;
    int m_batchTotal = 0, m_batchDone = 0, m_rev = 0;
    bool m_batch = false;
    mutable QHash<QString, QVariantList> m_cache; // segments by cache file path
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile *m_part = nullptr;
    QPointer<QProcess> m_proc;
};

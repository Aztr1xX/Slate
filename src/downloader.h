#pragma once
#include <QHash>
#include <QNetworkReply>
#include <functional>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>
#include <QQmlEngine>

// Downloads videos (YouTube and the other sites yt-dlp knows) at the best quality on offer.
//
// How it gets the best quality: yt-dlp picks the highest resolution, frame rate and bit rate
// video stream plus the best audio stream, and FFmpeg joins them. Nothing is re-encoded unless
// the chosen format can't hold the original streams (MOV, AVI, the MP3/WAV/FLAC/OGG audio
// formats, or "convert for editing").
//
// Tools on Windows live in %LOCALAPPDATA%\Slate\Slate\tools so they can update themselves:
//  - yt-dlp.exe: copied from the one bundled with Slate, then kept current with "yt-dlp -U"
//    (YouTube changes often and an old yt-dlp stops working).
//  - deno.exe: fetched on first use. yt-dlp needs a JavaScript runtime to unlock YouTube's
//    full list of formats; without it some of the high resolutions are missing.
class Downloader : public QObject
{
    Q_OBJECT
    // idle, preparing, probing, ready, downloading, converting, done, error
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString speed READ speed NOTIFY changed)
    Q_PROPERTY(QString eta READ eta NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(QString outputFile READ outputFile NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QString folderLabel READ folderLabel NOTIFY folderChanged)
    Q_PROPERTY(QString toolsNote READ toolsNote NOTIFY changed)

public:
    explicit Downloader(QObject *parent = nullptr);
    static Downloader *instance();

    QString state() const { return m_state; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    double progress() const { return m_progress; }
    QString speed() const { return m_speed; }
    QString eta() const { return m_eta; }
    QVariantMap info() const { return m_info; }
    QString outputFile() const { return m_output; }
    bool busy() const;
    QString folder() const;
    void setFolder(const QString &f);
    QString folderLabel() const;
    QString toolsNote() const { return m_toolsNote; }

    // Gets yt-dlp (and deno) ready in the background; cheap to call again.
    Q_INVOKABLE void prepare();
    Q_INVOKABLE void lookUp(const QString &url);
    // kind: "video" or "audio"; format: mp4 mkv webm mov avi / mp3 m4a wav flac opus ogg.
    // maxHeight 0 = best available. editFriendly: MP4 only, converts VP9/AV1 to H.264.
    Q_INVOKABLE void start(const QString &kind, const QString &format, int maxHeight, bool editFriendly);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset();          // back to an empty dialog (keeps tools ready)
    Q_INVOKABLE void backToOptions();  // after done/error: same video, pick again
    Q_INVOKABLE QString clipboardUrl() const;
    Q_INVOKABLE bool looksLikeUrl(const QString &text) const;
    Q_INVOKABLE void resetFolder();

    // Path of the yt-dlp to use (the self-updating copy when there is one). Also used for playback.
    static QString ytdlpPath();

    // ---- pure helpers, unit-tested in the self-test ----
    // Everything yt-dlp needs to pick and package the streams for one format choice.
    struct Plan {
        QStringList args;         // format selection and post-processing
        QString finalExt;         // extension of the file the user gets
        QString convert;          // "" (yt-dlp's file is final), "mov", "avi" or "mp4-edit"
    };
    static Plan plan(const QString &kind, const QString &format, int maxHeight, bool editFriendly);
    // Title, channel, duration, thumbnail and the list of resolutions from "yt-dlp -J".
    static QVariantMap parseInfo(const QByteArray &json, QString *error, QHash<QString, double> *sizes = nullptr);
    // One "SLATE|status|format|done|total|estimate|speed|eta" progress line.
    struct Progress { QString status, formatId; double done = -1, total = -1, speed = -1, eta = -1; };
    static bool parseProgress(const QString &line, Progress *p);
    // FFmpeg arguments to turn a downloaded file into the target ("mov", "avi", "mp4-edit").
    static QStringList convertArgs(const QString &target, const QString &vcodec, const QString &acodec,
                                   const QString &in, const QString &out);
    static QString friendlyError(const QString &raw);

signals:
    void changed();
    void folderChanged();
    void finished(bool ok, const QString &file, const QString &message);

private:
    void set(const QString &state, const QString &status = {});
    void fail(const QString &message);
    void afterTools();
    void ensureYtdlp();
    void updateYtdlp();
    void ensureDeno();
    void unzipDeno(const QString &zip, bool withPowerShell);
    void fetch(const QString &url, const QString &dest, const QString &what, std::function<void(bool)> done);
    QStringList commonArgs() const;
    QProcess *newProcess();
    void killTree(QProcess *p);
    void runProbe();
    void onDownloadOutput();
    void startConvert(const QString &file);
    void runConvert(const QString &in, const QString &vcodec, const QString &acodec);
    void complete(const QString &file);
    QString tempDir() const;
    static QString toolsDir();
    static QString denoPath();

    QNetworkAccessManager m_net;
    QString m_state = QStringLiteral("idle");
    QString m_status, m_error, m_speed, m_eta, m_output, m_toolsNote;
    double m_progress = 0;
    QVariantMap m_info;
    QHash<QString, double> m_sizes;       // format id -> bytes (exact or estimated)
    QString m_url, m_pendingUrl;
    bool m_toolsReady = false, m_preparing = false;
    QPointer<QProcess> m_proc;
    QPointer<QNetworkReply> m_reply;
    Plan m_plan;
    QStringList m_formatIds;              // streams being fetched, e.g. 401, 251
    QHash<QString, double> m_done;        // bytes per stream
    QHash<QString, double> m_total;
    QString m_lineBuf, m_errBuf, m_lastErr;
    double m_duration = 0;
    QString m_convertOut;
    int m_job = 0;
};

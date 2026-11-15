#include "downloader.h"
#include "appcontroller.h"
#include "media.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace {

QString exeName(const QString &name)
{
#ifdef Q_OS_WIN
    return name + QStringLiteral(".exe");
#else
    return name;
#endif
}

// Rank of a video codec, best first, the same order yt-dlp uses.
int codecRank(const QString &vc)
{
    if (vc.startsWith(QLatin1String("av01"))) return 5;
    if (vc.startsWith(QLatin1String("vp09.02")) || vc.startsWith(QLatin1String("vp9.2"))) return 4;
    if (vc.startsWith(QLatin1String("vp9")) || vc.startsWith(QLatin1String("vp09"))) return 3;
    if (vc.startsWith(QLatin1String("hev")) || vc.startsWith(QLatin1String("hvc")) || vc.startsWith(QLatin1String("h265"))) return 2;
    if (vc.startsWith(QLatin1String("avc")) || vc.startsWith(QLatin1String("h264"))) return 1;
    return 0;
}

QString codecName(const QString &c)
{
    if (c.startsWith(QLatin1String("av01"))) return QStringLiteral("AV1");
    if (c.startsWith(QLatin1String("vp9")) || c.startsWith(QLatin1String("vp09"))) return QStringLiteral("VP9");
    if (c.startsWith(QLatin1String("hev")) || c.startsWith(QLatin1String("hvc"))) return QStringLiteral("HEVC");
    if (c.startsWith(QLatin1String("avc")) || c.startsWith(QLatin1String("h264"))) return QStringLiteral("H.264");
    if (c.startsWith(QLatin1String("opus"))) return QStringLiteral("Opus");
    if (c.startsWith(QLatin1String("mp4a")) || c == QLatin1String("aac")) return QStringLiteral("AAC");
    if (c.startsWith(QLatin1String("vorbis"))) return QStringLiteral("Vorbis");
    if (c.startsWith(QLatin1String("mp3"))) return QStringLiteral("MP3");
    return c.section(QLatin1Char('.'), 0, 0).toUpper();
}

QString fmtBytes(double b)
{
    if (b <= 0) return {};
    if (b >= 1e9) return QString::number(b / 1e9, 'f', 1) + QStringLiteral(" GB");
    if (b >= 1e6) return QString::number(b / 1e6, 'f', b >= 1e8 ? 0 : 1) + QStringLiteral(" MB");
    return QString::number(b / 1e3, 'f', 0) + QStringLiteral(" KB");
}

QString fmtEta(double s)
{
    if (s < 0) return {};
    const int t = int(s + 0.5);
    return t >= 3600 ? QStringLiteral("%1:%2:%3 left").arg(t / 3600).arg(t / 60 % 60, 2, 10, QLatin1Char('0')).arg(t % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2 left").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
}

// A title that is safe as a Windows file name.
QString safeName(QString t)
{
    static const QRegularExpression bad(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    t.replace(bad, QStringLiteral(" "));
    t = t.simplified();
    if (t.size() > 150) t = t.left(150).trimmed();
    while (t.endsWith(QLatin1Char('.')) || t.endsWith(QLatin1Char(' '))) t.chop(1);
    return t.isEmpty() ? QStringLiteral("video") : t;
}

} // namespace

Downloader::Downloader(QObject *parent) : QObject(parent) {}

Downloader *Downloader::instance()
{
    static Downloader *d = new Downloader(QCoreApplication::instance());
    QQmlEngine::setObjectOwnership(d, QQmlEngine::CppOwnership);
    return d;
}

bool Downloader::busy() const
{
    return m_state == QLatin1String("downloading") || m_state == QLatin1String("converting");
}

// ---------------------------------------------------------------------------------------------
// folders and tools

QString Downloader::folder() const
{
    const QString custom = AppController::instance()->setting(QStringLiteral("downloadFolder")).toString();
    if (!custom.isEmpty()) return custom;
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/Slate");
}

void Downloader::setFolder(const QString &f)
{
    AppController::instance()->setSetting(QStringLiteral("downloadFolder"), f);
    emit folderChanged();
}

void Downloader::resetFolder() { setFolder(QString()); }

QString Downloader::folderLabel() const { return QDir::toNativeSeparators(folder()); }

QString Downloader::toolsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/tools");
}

QString Downloader::ytdlpPath()
{
    const QString own = toolsDir() + QLatin1Char('/') + exeName(QStringLiteral("yt-dlp"));
    if (QFileInfo::exists(own)) return own;
    const QString bundled = QCoreApplication::applicationDirPath() + QLatin1Char('/') + exeName(QStringLiteral("yt-dlp"));
    if (QFileInfo::exists(bundled)) return bundled;
    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}

QString Downloader::denoPath()
{
    const QString own = toolsDir() + QLatin1Char('/') + exeName(QStringLiteral("deno"));
    if (QFileInfo::exists(own)) return own;
    return QStandardPaths::findExecutable(QStringLiteral("deno"));
}

QString Downloader::tempDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/Slate-download-%1").arg(QCoreApplication::applicationPid());
}

QProcess *Downloader::newProcess()
{
    auto *p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));        // titles with any characters
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    p->setProcessEnvironment(env);
    connect(p, &QProcess::finished, p, &QObject::deleteLater);
    return p;
}

void Downloader::killTree(QProcess *p)
{
    if (!p || p->state() == QProcess::NotRunning) return;
#ifdef Q_OS_WIN
    // yt-dlp.exe is a launcher that runs the real program (and FFmpeg) as child processes.
    QProcess::execute(QStringLiteral("taskkill"), { QStringLiteral("/PID"), QString::number(p->processId()), QStringLiteral("/T"), QStringLiteral("/F") });
#endif
    p->kill();
    p->waitForFinished(2000);
}

void Downloader::set(const QString &state, const QString &status)
{
    m_state = state;
    if (!status.isNull()) m_status = status;
    if (state != QLatin1String("error")) m_error.clear();
    emit changed();
}

void Downloader::fail(const QString &message)
{
    const bool wasBusy = busy();
    killTree(m_proc);
    QDir(tempDir()).removeRecursively();
    if (!m_convertOut.isEmpty()) { QFile::remove(m_convertOut); m_convertOut.clear(); }
    m_error = message;
    m_state = QStringLiteral("error");
    m_speed.clear(); m_eta.clear();
    emit changed();
    if (wasBusy) emit finished(false, QString(), message);
}

void Downloader::fetch(const QString &url, const QString &dest, const QString &what, std::function<void(bool)> done)
{
    QDir().mkpath(QFileInfo(dest).absolutePath());
    auto *out = new QFile(dest + QStringLiteral(".part"), this);
    if (!out->open(QIODevice::WriteOnly)) { delete out; done(false); return; }
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "Slate");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = m_net.get(req);
    m_reply = r;
    connect(r, &QNetworkReply::readyRead, this, [r, out] { out->write(r->readAll()); });
    connect(r, &QNetworkReply::downloadProgress, this, [this, what](qint64 got, qint64 total) {
        if (total <= 0) return;
        m_toolsNote = QStringLiteral("Getting %1 (first time only) %2%").arg(what).arg(int(100.0 * got / total));
        emit changed();
    });
    connect(r, &QNetworkReply::finished, this, [this, r, out, dest, done] {
        r->deleteLater();
        out->write(r->readAll());
        out->close();
        const bool ok = r->error() == QNetworkReply::NoError && out->size() > 100000;
        out->deleteLater();
        if (ok) { QFile::remove(dest); QFile::rename(dest + QStringLiteral(".part"), dest); }
        else QFile::remove(dest + QStringLiteral(".part"));
        m_toolsNote.clear();
        emit changed();
        done(ok && QFileInfo::exists(dest));
    });
}

void Downloader::prepare()
{
    if (m_toolsReady || m_preparing) return;
    m_preparing = true;
    ensureYtdlp();
}

void Downloader::ensureYtdlp()
{
#ifdef Q_OS_WIN
    const QString own = toolsDir() + QStringLiteral("/yt-dlp.exe");
    if (!QFileInfo::exists(own)) {
        QDir().mkpath(toolsDir());
        const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/yt-dlp.exe");
        if (!QFileInfo::exists(bundled) || !QFile::copy(bundled, own)) {
            fetch(QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"), own, QStringLiteral("yt-dlp"),
                  [this](bool) { updateYtdlp(); });
            return;
        }
    }
#endif
    updateYtdlp();
}

void Downloader::updateYtdlp()
{
    // YouTube changes often, so keep yt-dlp current (at most one check a day). Only the copy in
    // Slate's tools folder updates itself; one from PATH is left to its owner.
    const QString exe = ytdlpPath();
    const qint64 last = QSettings().value(QStringLiteral("downloader/lastUpdate"), 0).toLongLong();
    const bool due = QDateTime::currentSecsSinceEpoch() - last > 20 * 3600;
    if (exe.isEmpty() || !exe.startsWith(toolsDir()) || !due) { ensureDeno(); return; }
    m_toolsNote = QStringLiteral("Updating yt-dlp");
    emit changed();
    QProcess *p = newProcess();
    connect(p, &QProcess::finished, this, [this](int code) {
        if (code == 0) QSettings().setValue(QStringLiteral("downloader/lastUpdate"), QDateTime::currentSecsSinceEpoch());
        m_toolsNote.clear();
        ensureDeno();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) { p->deleteLater(); m_toolsNote.clear(); ensureDeno(); }
    });
    p->start(exe, { QStringLiteral("-U") });
}

void Downloader::ensureDeno()
{
#ifdef Q_OS_WIN
    if (denoPath().isEmpty()) {
        const QString zip = toolsDir() + QStringLiteral("/deno.zip");
        fetch(QStringLiteral("https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip"), zip,
              QStringLiteral("the YouTube helper (about 45 MB)"), [this, zip](bool ok) {
            if (!ok) { afterTools(); return; }
            m_toolsNote = QStringLiteral("Unpacking the YouTube helper");
            emit changed();
            unzipDeno(zip, false);
        });
        return;
    }
#endif
    afterTools();
}

// tar.exe ships with Windows 10 and 11 and reads zip files; PowerShell is the fallback.
void Downloader::unzipDeno(const QString &zip, bool withPowerShell)
{
    auto next = [this, zip, withPowerShell] {
        if (denoPath().isEmpty() && !withPowerShell) { unzipDeno(zip, true); return; }
        QFile::remove(zip);
        afterTools();
    };
    QProcess *p = newProcess();
    connect(p, &QProcess::finished, this, next);
    connect(p, &QProcess::errorOccurred, this, [p, next](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) { p->deleteLater(); next(); }
    });
    if (!withPowerShell) {
        p->start(QStringLiteral("tar"), { QStringLiteral("-xf"), QDir::toNativeSeparators(zip), QStringLiteral("-C"), QDir::toNativeSeparators(toolsDir()) });
    } else {
        auto quoted = [](QString path) { return QLatin1Char('\'') + QDir::toNativeSeparators(path).replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\''); };
        p->start(QStringLiteral("powershell"), { QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
                 QStringLiteral("Expand-Archive -Force -LiteralPath %1 -DestinationPath %2").arg(quoted(zip), quoted(toolsDir())) });
    }
}

void Downloader::afterTools()
{
    m_preparing = false;
    m_toolsReady = true;
    m_toolsNote.clear();
    emit changed();
    if (!m_pendingUrl.isEmpty()) {
        m_pendingUrl.clear();
        runProbe();
    }
}

QStringList Downloader::commonArgs() const
{
    QStringList a{ QStringLiteral("--ignore-config"), QStringLiteral("--no-playlist"), QStringLiteral("--color"), QStringLiteral("never") };
    const QString ff = Media::toolPath(QStringLiteral("ffmpeg"));
    if (!ff.isEmpty()) a << QStringLiteral("--ffmpeg-location") << QDir::toNativeSeparators(QFileInfo(ff).absolutePath());
    // A JavaScript runtime unlocks YouTube's full format list (all resolutions).
    const QString deno = denoPath();
    if (!deno.isEmpty()) a << QStringLiteral("--js-runtimes") << QStringLiteral("deno:") + QDir::toNativeSeparators(deno);
    else if (!QStandardPaths::findExecutable(QStringLiteral("node")).isEmpty()) a << QStringLiteral("--js-runtimes") << QStringLiteral("node");
    return a;
}

// ---------------------------------------------------------------------------------------------
// look up

bool Downloader::looksLikeUrl(const QString &text) const
{
    const QUrl u(text.trimmed(), QUrl::StrictMode);
    return u.isValid() && (u.scheme() == QLatin1String("https") || u.scheme() == QLatin1String("http")) && u.host().contains(QLatin1Char('.'));
}

QString Downloader::clipboardUrl() const
{
    const QString t = QGuiApplication::clipboard()->text().trimmed();
    return looksLikeUrl(t) && !t.contains(QLatin1Char('\n')) ? t : QString();
}

void Downloader::lookUp(const QString &url)
{
    if (busy()) return;
    const QString u = url.trimmed();
    if (!looksLikeUrl(u)) { fail(QStringLiteral("That doesn't look like a link. Copy the address of the video and paste it here.")); return; }
    ++m_job;
    killTree(m_proc);
    m_url = u;
    m_info.clear();
    m_sizes.clear();
    m_output.clear();
    set(QStringLiteral("probing"), QStringLiteral("Looking up the video"));
    if (!m_toolsReady) { m_pendingUrl = u; prepare(); return; }
    runProbe();
}

void Downloader::runProbe()
{
    const QString exe = ytdlpPath();
    if (exe.isEmpty()) { fail(QStringLiteral("yt-dlp is missing. Reinstall Slate to get it back.")); return; }
    QProcess *p = newProcess();
    m_proc = p;
    const int job = m_job;
    connect(p, &QProcess::errorOccurred, this, [this, p, job](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && job == m_job) { p->deleteLater(); fail(QStringLiteral("Couldn't start yt-dlp.")); }
    });
    connect(p, &QProcess::finished, this, [this, p, job](int code, QProcess::ExitStatus st) {
        if (job != m_job) return;
        const QByteArray out = p->readAllStandardOutput();
        const QString err = QString::fromUtf8(p->readAllStandardError());
        if (st != QProcess::NormalExit || code != 0 || out.trimmed().isEmpty()) { fail(friendlyError(err)); return; }
        QString perr;
        QVariantMap info = parseInfo(out, &perr, &m_sizes);
        if (!perr.isEmpty()) { fail(perr); return; }
        m_info = info;
        m_duration = info.value(QStringLiteral("duration")).toDouble();
        set(QStringLiteral("ready"), QString());
    });
    p->start(exe, commonArgs() << QStringLiteral("-J") << QStringLiteral("--no-warnings") << QStringLiteral("--") << m_url);
}

QVariantMap Downloader::parseInfo(const QByteArray &json, QString *error, QHash<QString, double> *sizes)
{
    QJsonParseError pe{};
    const QJsonObject o = QJsonDocument::fromJson(json, &pe).object();
    if (pe.error != QJsonParseError::NoError || o.isEmpty()) { *error = QStringLiteral("yt-dlp gave an answer Slate couldn't read. Try again."); return {}; }
    if (o.value(QStringLiteral("_type")).toString() == QLatin1String("playlist")) {
        *error = QStringLiteral("That link is a playlist or channel. Paste the link of a single video.");
        return {};
    }
    if (o.value(QStringLiteral("is_live")).toBool()) {
        *error = QStringLiteral("This is a live stream. It can be downloaded once the stream has ended.");
        return {};
    }

    QVariantMap info;
    info[QStringLiteral("title")] = o.value(QStringLiteral("title")).toString();
    QString channel = o.value(QStringLiteral("channel")).toString();
    if (channel.isEmpty()) channel = o.value(QStringLiteral("uploader")).toString();
    info[QStringLiteral("channel")] = channel;
    info[QStringLiteral("duration")] = o.value(QStringLiteral("duration")).toDouble();
    info[QStringLiteral("site")] = o.value(QStringLiteral("extractor_key")).toString();

    // Thumbnail: the largest JPEG (Qt can't show WebP without an extra plugin).
    QString thumb;
    int bestPref = INT_MIN;
    for (const QJsonValue &v : o.value(QStringLiteral("thumbnails")).toArray()) {
        const QJsonObject t = v.toObject();
        const QString u = t.value(QStringLiteral("url")).toString();
        if (!QUrl(u).path().endsWith(QLatin1String(".jpg"))) continue;
        const int pref = t.value(QStringLiteral("preference")).toInt(0) * 100000 + t.value(QStringLiteral("width")).toInt(0);
        if (pref >= bestPref) { bestPref = pref; thumb = u; }
    }
    if (thumb.isEmpty() && !o.value(QStringLiteral("thumbnail")).toString().endsWith(QLatin1String(".webp")))
        thumb = o.value(QStringLiteral("thumbnail")).toString();
    info[QStringLiteral("thumbnail")] = thumb;

    struct V { int h = 0, shortSide = 0; double fps = 0, size = 0, tbr = 0; bool hdr = false; QString codec; };
    QHash<int, V> byHeight;
    double bestAbr = -1, bestAudioSize = 0, aacSize = 0;
    QString bestAcodec;
    QJsonArray formats = o.value(QStringLiteral("formats")).toArray();
    if (formats.isEmpty()) formats.append(o); // single-format sites
    for (const QJsonValue &v : std::as_const(formats)) {
        const QJsonObject f = v.toObject();
        const QString id = f.value(QStringLiteral("format_id")).toString();
        const QString vc = f.value(QStringLiteral("vcodec")).toString(), ac = f.value(QStringLiteral("acodec")).toString();
        double size = f.value(QStringLiteral("filesize")).toDouble(0);
        if (size <= 0) size = f.value(QStringLiteral("filesize_approx")).toDouble(0);
        if (sizes && !id.isEmpty()) sizes->insert(id, size);
        const int h = f.value(QStringLiteral("height")).toInt(0), w = f.value(QStringLiteral("width")).toInt(0);
        const bool hasV = !vc.isEmpty() && vc != QLatin1String("none") && h > 0;
        const bool hasA = !ac.isEmpty() && ac != QLatin1String("none");
        if (hasV) {
            V c;
            c.h = h;
            c.shortSide = w > 0 ? qMin(w, h) : h;
            c.fps = f.value(QStringLiteral("fps")).toDouble(0);
            c.hdr = f.value(QStringLiteral("dynamic_range")).toString(QStringLiteral("SDR")) != QLatin1String("SDR") && !f.value(QStringLiteral("dynamic_range")).isNull();
            c.codec = vc;
            c.size = size;
            c.tbr = f.value(QStringLiteral("tbr")).toDouble(0);
            auto better = [](const V &a, const V &b) { // a better than b
                if (qRound(a.fps) != qRound(b.fps)) return a.fps > b.fps;
                if (a.hdr != b.hdr) return a.hdr;
                if (codecRank(a.codec) != codecRank(b.codec)) return codecRank(a.codec) > codecRank(b.codec);
                return a.tbr > b.tbr;
            };
            if (!byHeight.contains(h) || better(c, byHeight[h])) byHeight[h] = c;
        } else if (hasA) {
            const double abr = f.value(QStringLiteral("abr")).toDouble(f.value(QStringLiteral("tbr")).toDouble(0));
            if (abr > bestAbr) { bestAbr = abr; bestAcodec = ac; bestAudioSize = size; }
            if (ac.startsWith(QLatin1String("mp4a")) && size > aacSize) aacSize = size;
        }
    }
    QList<int> heights = byHeight.keys();
    std::sort(heights.begin(), heights.end(), std::greater<int>());
    QVariantList qualities;
    for (int h : std::as_const(heights)) {
        const V &c = byHeight[h];
        if (c.shortSide < 144) continue;
        QString label = QString::number(c.shortSide) + QLatin1Char('p');
        if (c.fps > 31) label += QString::number(qRound(c.fps));
        QString tag;
        if (c.shortSide >= 4320) tag = QStringLiteral("8K");
        else if (c.shortSide >= 2160) tag = QStringLiteral("4K");
        else if (c.shortSide >= 1440) tag = QStringLiteral("2K");
        else if (c.shortSide >= 720) tag = QStringLiteral("HD");
        QVariantMap q;
        q[QStringLiteral("height")] = h;
        q[QStringLiteral("label")] = label;
        q[QStringLiteral("tag")] = tag;
        q[QStringLiteral("hdr")] = c.hdr;
        q[QStringLiteral("codec")] = codecName(c.codec);
        q[QStringLiteral("size")] = c.size > 0 ? fmtBytes(c.size + bestAudioSize) : QString();
        qualities << q;
    }
    if (qualities.isEmpty()) {
        // Direct links to a file: no codec details, but it is a video if the extension says so.
        static const QStringList videoExts{ QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mkv"), QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("flv"), QStringLiteral("avi") };
        for (const QJsonValue &v : std::as_const(formats)) {
            const QJsonObject f = v.toObject();
            if (f.value(QStringLiteral("vcodec")).toString() != QLatin1String("none") && videoExts.contains(f.value(QStringLiteral("ext")).toString())) {
                QVariantMap q;
                q[QStringLiteral("height")] = 0;
                q[QStringLiteral("label")] = QStringLiteral("Original");
                q[QStringLiteral("tag")] = QString();
                q[QStringLiteral("hdr")] = false;
                q[QStringLiteral("codec")] = f.value(QStringLiteral("ext")).toString().toUpper();
                q[QStringLiteral("size")] = QString();
                qualities << q;
                break;
            }
        }
    }
    info[QStringLiteral("qualities")] = qualities;
    info[QStringLiteral("hasVideo")] = !qualities.isEmpty();
    if (!qualities.isEmpty()) {
        const QVariantMap b = qualities.first().toMap();
        info[QStringLiteral("bestVideo")] = b.value(QStringLiteral("label")).toString() + (b.value(QStringLiteral("hdr")).toBool() ? QStringLiteral(" HDR") : QString())
                                            + QStringLiteral(" ") + b.value(QStringLiteral("codec")).toString();
    }
    info[QStringLiteral("bestAudio")] = bestAbr > 0 ? QStringLiteral("%1 %2 kbps").arg(codecName(bestAcodec)).arg(qRound(bestAbr)) : QString();
    info[QStringLiteral("audioSize")] = fmtBytes(bestAudioSize);
    return info;
}

// ---------------------------------------------------------------------------------------------
// download

Downloader::Plan Downloader::plan(const QString &kind, const QString &format, int maxHeight, bool editFriendly)
{
    Plan p;
    const QString h = maxHeight > 0 ? QStringLiteral("[height<=%1]").arg(maxHeight) : QString();
    auto f = [](const QString &sel) { return QStringList{ QStringLiteral("-f"), sel }; };
    if (kind == QLatin1String("audio")) {
        // The best audio stream, converted only when the format needs it.
        p.finalExt = format == QLatin1String("ogg") ? QStringLiteral("ogg") : format;
        const QString codec = format == QLatin1String("ogg") ? QStringLiteral("vorbis") : format;
        QString sel = QStringLiteral("ba/b"), quality;
        if (format == QLatin1String("mp3")) quality = QStringLiteral("320K");
        else if (format == QLatin1String("m4a")) { sel = QStringLiteral("ba[acodec^=mp4a]/ba/b"); quality = QStringLiteral("256K"); }
        else if (format == QLatin1String("opus")) { sel = QStringLiteral("ba[acodec=opus]/ba/b"); quality = QStringLiteral("192K"); }
        else if (format == QLatin1String("ogg")) quality = QStringLiteral("0");
        p.args << f(sel) << QStringLiteral("-x") << QStringLiteral("--audio-format") << codec;
        if (!quality.isEmpty()) p.args << QStringLiteral("--audio-quality") << quality;
        p.args << QStringLiteral("--embed-metadata");
        if (format == QLatin1String("mp3") || format == QLatin1String("m4a") || format == QLatin1String("flac"))
            p.args << QStringLiteral("--embed-thumbnail") << QStringLiteral("--convert-thumbnails") << QStringLiteral("jpg");
        return p;
    }

    p.finalExt = format;
    const QString fallback = QStringLiteral("/b%1/bv*+ba/b").arg(h);
    if (format == QLatin1String("mkv")) {
        // Untouched: the best video and best audio streams, exactly as YouTube has them.
        p.args << f(QStringLiteral("bv*%1+ba").arg(h) + fallback) << QStringLiteral("--merge-output-format") << QStringLiteral("mkv")
               << QStringLiteral("--remux-video") << QStringLiteral("mkv");
    } else if (format == QLatin1String("webm")) {
        p.args << f(QStringLiteral("bv*%1[ext=webm]+ba[ext=webm]/bv*%1+ba").arg(h) + fallback)
               << QStringLiteral("--merge-output-format") << QStringLiteral("webm/mkv") << QStringLiteral("--recode-video") << QStringLiteral("webm");
    } else if (format == QLatin1String("mp4") && !editFriendly) {
        // Best video stream (AV1, VP9 or H.264) with the AAC audio stream, no re-encoding.
        p.args << f(QStringLiteral("bv*%1+ba[acodec^=mp4a]/bv*%1+ba").arg(h) + fallback) << QStringLiteral("--merge-output-format") << QStringLiteral("mp4")
               << QStringLiteral("--remux-video") << QStringLiteral("mp4");
    } else {
        // MOV, AVI and MP4 for editing: highest resolution and frame rate first, then H.264 if it
        // exists at that size (so nothing needs converting). Slate converts the rest afterwards.
        p.convert = format == QLatin1String("mp4") ? QStringLiteral("mp4-edit") : format;
        p.args << f(QStringLiteral("bv*%1+ba[acodec^=mp4a]/bv*%1+ba").arg(h) + fallback)
               << QStringLiteral("-S") << QStringLiteral("res,fps,vcodec:h264,acodec:aac")
               << QStringLiteral("--merge-output-format") << QStringLiteral("mkv");
    }
    p.args << QStringLiteral("--embed-metadata") << QStringLiteral("--embed-chapters");
    return p;
}

bool Downloader::parseProgress(const QString &line, Progress *p)
{
    if (!line.startsWith(QLatin1String("SLATE|"))) return false;
    const QStringList f = line.split(QLatin1Char('|'));
    if (f.size() < 8) return false;
    auto num = [](const QString &s) { bool ok = false; const double d = s.toDouble(&ok); return ok ? d : -1.0; };
    p->status = f[1];
    p->formatId = f[2];
    p->done = num(f[3]);
    p->total = num(f[4]) > 0 ? num(f[4]) : num(f[5]);
    p->speed = num(f[6]);
    p->eta = num(f[7]);
    return true;
}

void Downloader::start(const QString &kind, const QString &format, int maxHeight, bool editFriendly)
{
    if (m_state != QLatin1String("ready") && m_state != QLatin1String("done") && m_state != QLatin1String("error")) return;
    if (m_info.isEmpty()) return;
    const QString exe = ytdlpPath();
    if (exe.isEmpty()) { fail(QStringLiteral("yt-dlp is missing. Reinstall Slate to get it back.")); return; }
    if (!QDir().mkpath(folder())) { fail(QStringLiteral("Couldn't create the folder %1. Choose another one.").arg(folderLabel())); return; }
    ++m_job;
    m_plan = plan(kind, format, maxHeight, editFriendly);
    QDir(tempDir()).removeRecursively();
    QDir().mkpath(tempDir());

    // Our own file name (the title), made unique so an earlier download is never reused or replaced.
    const QString base = safeName(m_info.value(QStringLiteral("title")).toString());
    QString name = base;
    for (int i = 2; QFileInfo::exists(folder() + QLatin1Char('/') + name + QLatin1Char('.') + m_plan.finalExt)
                    || (!m_plan.convert.isEmpty() && QFileInfo::exists(folder() + QLatin1Char('/') + name + QStringLiteral(".mkv"))); ++i)
        name = QStringLiteral("%1 (%2)").arg(base).arg(i);
    QString tmpl = name;
    tmpl.replace(QLatin1Char('%'), QStringLiteral("%%"));
    tmpl += QStringLiteral(".%(ext)s");

    QStringList args = commonArgs();
    args << m_plan.args
         << QStringLiteral("--newline") << QStringLiteral("--progress")
         << QStringLiteral("--progress-template")
         << QStringLiteral("download:SLATE|%(progress.status)s|%(info.format_id)s|%(progress.downloaded_bytes)s|%(progress.total_bytes)s|%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s")
         << QStringLiteral("--print") << QStringLiteral("before_dl:SLATEFMT|%(format_id)s")
         << QStringLiteral("--print") << QStringLiteral("after_move:SLATEFILE|%(filepath)s")
         << QStringLiteral("--no-simulate")
         << QStringLiteral("-N") << QStringLiteral("4")
         << QStringLiteral("-P") << QStringLiteral("home:") + QDir::toNativeSeparators(folder())
         << QStringLiteral("-P") << QStringLiteral("temp:") + QDir::toNativeSeparators(tempDir())
         << QStringLiteral("-o") << tmpl
         << QStringLiteral("--") << m_url;
    // Self-test only: fetch a few seconds instead of the whole video.
    if (qEnvironmentVariableIsSet("SLATE_TEST_SECTION"))
        args.insert(args.size() - 2, QStringLiteral("--download-sections=*0-%1").arg(qEnvironmentVariable("SLATE_TEST_SECTION")));
    if (qEnvironmentVariableIsSet("SLATE_TEST_RATE")) // screenshots of the progress view
        args.insert(args.size() - 2, QStringLiteral("--limit-rate=") + qEnvironmentVariable("SLATE_TEST_RATE"));

    m_formatIds.clear(); m_done.clear(); m_total.clear();
    m_lineBuf.clear(); m_errBuf.clear(); m_lastErr.clear();
    m_output.clear(); m_convertOut.clear();
    m_progress = 0; m_speed.clear(); m_eta.clear();
    set(QStringLiteral("downloading"), QStringLiteral("Starting the download"));

    QProcess *p = newProcess();
    m_proc = p;
    const int job = m_job;
    connect(p, &QProcess::readyReadStandardOutput, this, [this, job] { if (job == m_job) onDownloadOutput(); });
    connect(p, &QProcess::readyReadStandardError, this, [this, p, job] {
        if (job != m_job) return;
        m_errBuf += QString::fromUtf8(p->readAllStandardError());
        if (m_errBuf.size() > 20000) m_errBuf = m_errBuf.right(10000);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, job](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && job == m_job) { p->deleteLater(); fail(QStringLiteral("Couldn't start yt-dlp.")); }
    });
    connect(p, &QProcess::finished, this, [this, job](int code, QProcess::ExitStatus st) {
        if (job != m_job) return;
        onDownloadOutput();
        if (st == QProcess::NormalExit && code == 0 && !m_output.isEmpty() && QFileInfo::exists(m_output)) {
            if (m_plan.convert.isEmpty()) complete(m_output);
            else startConvert(m_output);
        } else {
            fail(friendlyError(m_errBuf));
        }
    });
    p->start(exe, args);
}

void Downloader::onDownloadOutput()
{
    if (!m_proc) return;
    m_lineBuf += QString::fromUtf8(m_proc->readAllStandardOutput());
    m_lineBuf.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    int nl;
    bool touched = false;
    while ((nl = m_lineBuf.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_lineBuf.left(nl).trimmed();
        m_lineBuf.remove(0, nl + 1);
        if (line.startsWith(QLatin1String("SLATEFMT|"))) {
            m_formatIds = line.mid(9).split(QLatin1Char('+'), Qt::SkipEmptyParts);
            continue;
        }
        if (line.startsWith(QLatin1String("SLATEFILE|"))) {
            m_output = QDir::fromNativeSeparators(line.mid(10));
            continue;
        }
        Progress pr;
        if (!parseProgress(line, &pr)) continue;
        touched = true;
        if (!m_formatIds.contains(pr.formatId)) m_formatIds << pr.formatId;
        if (pr.done >= 0) m_done[pr.formatId] = pr.done;
        if (pr.total > 0) m_total[pr.formatId] = pr.total;
        if (pr.status == QLatin1String("finished")) m_done[pr.formatId] = m_total.value(pr.formatId, m_done.value(pr.formatId));
        m_speed = pr.speed > 0 ? fmtBytes(pr.speed) + QStringLiteral("/s") : QString();
        m_eta = fmtEta(pr.eta);

        // Overall progress across the video and audio streams, weighted by size.
        double sumDone = 0, sumTotal = 0;
        bool known = true;
        for (const QString &id : std::as_const(m_formatIds)) {
            const double t = m_total.value(id, m_sizes.value(id, 0));
            if (t <= 0) { known = false; break; }
            sumTotal += t;
            sumDone += qMin(t, m_done.value(id, 0));
        }
        if (known && sumTotal > 0) {
            m_progress = sumDone / sumTotal;
        } else {
            const int i = qMax(0, int(m_formatIds.indexOf(pr.formatId)));
            const double t = m_total.value(pr.formatId, 0);
            m_progress = (i + (t > 0 ? qMin(1.0, m_done.value(pr.formatId) / t) : 0)) / qMax<qsizetype>(1, m_formatIds.size());
        }
        const bool last = m_formatIds.indexOf(pr.formatId) == m_formatIds.size() - 1;
        const bool audioOnlyKind = m_plan.args.contains(QStringLiteral("-x"));
        if (pr.status == QLatin1String("finished") && last) {
            m_speed.clear(); m_eta.clear();
            m_status = audioOnlyKind ? QStringLiteral("Saving as %1").arg(m_plan.finalExt.toUpper())
                                     : (m_formatIds.size() > 1 ? QStringLiteral("Joining video and audio") : QStringLiteral("Finishing"));
        } else {
            const bool isAudio = audioOnlyKind || (m_formatIds.size() > 1 && last);
            m_status = isAudio ? QStringLiteral("Downloading audio") : QStringLiteral("Downloading video");
        }
    }
    if (touched) emit changed();
}

QStringList Downloader::convertArgs(const QString &target, const QString &vcodec, const QString &acodec, const QString &in, const QString &out)
{
    QStringList a{ QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                   QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"),
                   QStringLiteral("-i"), in,
                   QStringLiteral("-map"), QStringLiteral("0:v:0?"), QStringLiteral("-map"), QStringLiteral("0:a:0?"),
                   QStringLiteral("-map_metadata"), QStringLiteral("0") };
    const bool h264 = vcodec == QLatin1String("h264");
    if (h264) {
        a << QStringLiteral("-c:v") << QStringLiteral("copy");
        if (target == QLatin1String("avi")) a << QStringLiteral("-bsf:v") << QStringLiteral("h264_mp4toannexb");
    } else {
        // Visually lossless H.264 that every editor opens.
        a << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-preset") << QStringLiteral("medium")
          << QStringLiteral("-crf") << QStringLiteral("16") << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    }
    if (target == QLatin1String("avi")) {
        if (acodec == QLatin1String("mp3")) a << QStringLiteral("-c:a") << QStringLiteral("copy");
        else a << QStringLiteral("-c:a") << QStringLiteral("libmp3lame") << QStringLiteral("-b:a") << QStringLiteral("320k");
    } else {
        if (acodec == QLatin1String("aac")) a << QStringLiteral("-c:a") << QStringLiteral("copy");
        else a << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("320k");
        a << QStringLiteral("-map_chapters") << QStringLiteral("0") << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }
    a << out;
    return a;
}

void Downloader::startConvert(const QString &file)
{
    const QString probe = Media::toolPath(QStringLiteral("ffprobe"));
    if (probe.isEmpty() || Media::toolPath(QStringLiteral("ffmpeg")).isEmpty()) { complete(file); return; }
    set(QStringLiteral("converting"), QStringLiteral("Checking the streams"));
    m_progress = 0; m_speed.clear(); m_eta.clear();
    QProcess *p = newProcess();
    m_proc = p;
    const int job = m_job;
    connect(p, &QProcess::finished, this, [this, p, job, file](int) {
        if (job != m_job) return;
        QString vc, ac;
        const QJsonArray streams = QJsonDocument::fromJson(p->readAllStandardOutput()).object().value(QStringLiteral("streams")).toArray();
        for (const QJsonValue &v : streams) {
            const QJsonObject s = v.toObject();
            const QString type = s.value(QStringLiteral("codec_type")).toString();
            if (type == QLatin1String("video") && vc.isEmpty()) vc = s.value(QStringLiteral("codec_name")).toString();
            if (type == QLatin1String("audio") && ac.isEmpty()) ac = s.value(QStringLiteral("codec_name")).toString();
        }
        runConvert(file, vc, ac);
    });
    p->start(probe, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"), QStringLiteral("stream=codec_type,codec_name"),
                      QStringLiteral("-of"), QStringLiteral("json"), file });
}

void Downloader::runConvert(const QString &in, const QString &vcodec, const QString &acodec)
{
    const QString ext = m_plan.convert == QLatin1String("mp4-edit") ? QStringLiteral("mp4") : m_plan.convert;
    const QFileInfo fi(in);
    QString out = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1Char('.') + ext;
    if (QFileInfo::exists(out)) out = AppController::uniquePath(out);
    m_convertOut = out;
    const bool reencode = vcodec != QLatin1String("h264") && !vcodec.isEmpty();
    m_status = reencode ? QStringLiteral("Converting %1 to H.264 for editing").arg(codecName(vcodec))
                        : QStringLiteral("Packaging as %1").arg(ext.toUpper());
    emit changed();

    QProcess *p = newProcess();
    m_proc = p;
    const int job = m_job;
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p, job] {
        if (job != m_job) return;
        const QString chunk = QString::fromUtf8(p->readAllStandardOutput());
        static const QRegularExpression re(QStringLiteral("out_time_us=(\\d+)"));
        auto it = re.globalMatch(chunk);
        double t = -1;
        while (it.hasNext()) t = it.next().captured(1).toDouble() / 1e6;
        if (t >= 0 && m_duration > 0) { m_progress = qBound(0.0, t / m_duration, 1.0); emit changed(); }
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, job](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && job == m_job) { p->deleteLater(); fail(QStringLiteral("Couldn't start FFmpeg to convert the video.")); }
    });
    connect(p, &QProcess::finished, this, [this, p, job, in, out](int code, QProcess::ExitStatus st) {
        if (job != m_job) return;
        if (st == QProcess::NormalExit && code == 0 && QFileInfo(out).size() > 0) {
            m_convertOut.clear();
            QFile::remove(in);
            complete(out);
        } else {
            const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1);
            fail(QStringLiteral("Converting the video failed. %1").arg(err));
        }
    });
    p->start(Media::toolPath(QStringLiteral("ffmpeg")), convertArgs(m_plan.convert, vcodec, acodec, in, out));
}

void Downloader::complete(const QString &file)
{
    QDir(tempDir()).removeRecursively();
    m_output = file;
    m_progress = 1;
    m_speed.clear(); m_eta.clear();
    set(QStringLiteral("done"), QStringLiteral("Saved %1").arg(QFileInfo(file).fileName()));
    emit finished(true, file, QStringLiteral("Downloaded %1").arg(QFileInfo(file).fileName()));
}

void Downloader::cancel()
{
    ++m_job;
    if (m_reply) m_reply->abort();
    killTree(m_proc);
    QDir(tempDir()).removeRecursively();
    if (!m_convertOut.isEmpty()) { QFile::remove(m_convertOut); m_convertOut.clear(); }
    m_progress = 0; m_speed.clear(); m_eta.clear();
    if (m_state == QLatin1String("probing") || m_info.isEmpty()) { m_pendingUrl.clear(); set(QStringLiteral("idle"), QString()); return; }
    set(QStringLiteral("ready"), QStringLiteral("Download cancelled"));
}

void Downloader::reset()
{
    if (busy()) return;
    ++m_job;
    killTree(m_proc);
    m_pendingUrl.clear();
    m_info.clear();
    m_output.clear();
    m_url.clear();
    m_progress = 0;
    set(QStringLiteral("idle"), QString());
}

void Downloader::backToOptions()
{
    if (busy()) return;
    if (m_info.isEmpty()) { reset(); return; }
    m_progress = 0;
    set(QStringLiteral("ready"), QString());
}

QString Downloader::friendlyError(const QString &raw)
{
    // Last "ERROR:" line from yt-dlp, made readable.
    QString line;
    for (const QString &l : raw.split(QLatin1Char('\n'))) if (l.contains(QLatin1String("ERROR:"))) line = l.trimmed();
    if (line.isEmpty()) {
        const QStringList lines = raw.trimmed().split(QLatin1Char('\n'));
        line = lines.isEmpty() ? QString() : lines.last().trimmed();
    }
    line.remove(QRegularExpression(QStringLiteral("^.*ERROR:\\s*(\\[[^\\]]+\\]\\s*)?([\\w-]{6,}:\\s*)?")));
    const QString low = line.toLower();
    if (low.contains(QLatin1String("confirm you")) && low.contains(QLatin1String("bot")))
        return QStringLiteral("YouTube is asking to confirm you're not a bot. Try again in a few minutes, or open the video in your browser first.");
    if (low.contains(QLatin1String("private video"))) return QStringLiteral("This video is private.");
    if (low.contains(QLatin1String("sign in to confirm your age")) || low.contains(QLatin1String("age-restricted")))
        return QStringLiteral("This video is age-restricted, so YouTube only shows it to signed-in accounts.");
    if (low.contains(QLatin1String("video unavailable")) || low.contains(QLatin1String("not available")))
        return QStringLiteral("This video isn't available. It may have been removed, or blocked in your country.");
    if (low.contains(QLatin1String("unsupported url"))) return QStringLiteral("Slate can't download from this link. Try a YouTube video link.");
    if (low.contains(QLatin1String("unable to download")) || low.contains(QLatin1String("getaddrinfo")) || low.contains(QLatin1String("timed out")))
        return QStringLiteral("Couldn't reach the site. Check your internet connection and try again.");
    if (low.contains(QLatin1String("no space left"))) return QStringLiteral("The disk is full. Free some space or choose another folder.");
    if (line.isEmpty()) return QStringLiteral("The download failed. Try again.");
    return line;
}

#include "thumbnails.h"
#include "media.h"
#include "reviewstore.h"
#include "appcontroller.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>
#include <functional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

Thumbnails::Thumbnails(QObject *parent) : QObject(parent) {}

Thumbnails *Thumbnails::instance()
{
    static Thumbnails *t = new Thumbnails();
    return t;
}

void Thumbnails::setStatus(const QString &s, double p)
{
    m_status = s;
    m_progress = p;
    emit changed();
}

// Sharp, well-exposed, not flat. Works on a small greyscale copy.
double Thumbnails::scoreFrame(const QImage &src)
{
    if (src.isNull()) return 0;
    const QImage g = (src.width() > 320 ? src.scaledToWidth(320, Qt::SmoothTransformation) : src).convertToFormat(QImage::Format_Grayscale8);
    const int w = g.width(), h = g.height();
    if (w < 3 || h < 3) return 0;
    double sum = 0, sum2 = 0, lap = 0, lap2 = 0;
    qint64 n = 0, ln = 0;
    for (int y = 0; y < h; ++y) {
        const uchar *row = g.constScanLine(y);
        for (int x = 0; x < w; ++x) { sum += row[x]; sum2 += double(row[x]) * row[x]; ++n; }
    }
    for (int y = 1; y < h - 1; ++y) {
        const uchar *up = g.constScanLine(y - 1), *row = g.constScanLine(y), *dn = g.constScanLine(y + 1);
        for (int x = 1; x < w - 1; ++x) {
            const double l = 4.0 * row[x] - row[x - 1] - row[x + 1] - up[x] - dn[x];
            lap += l; lap2 += l * l; ++ln;
        }
    }
    const double mean = sum / n;
    const double sd = std::sqrt(qMax(0.0, sum2 / n - mean * mean));
    const double lm = lap / ln;
    const double sharp = qMax(0.0, lap2 / ln - lm * lm);
    // exposure: best between 70 and 180, falls off towards black or white
    double expo = 1;
    if (mean < 70) expo = qMax(0.05, (mean - 15) / 55.0);
    else if (mean > 180) expo = qMax(0.05, (240 - mean) / 60.0);
    const double contrast = qMin(1.0, sd / 45.0);
    if (sd < 6) return 0; // a flat frame (black, a fade, a lens cap)
    return std::log1p(sharp) * expo * (0.4 + 0.6 * contrast);
}

// Best first, skipping any too close to one already chosen; returned in time order.
QList<int> Thumbnails::pickSpread(const QList<double> &times, const QList<double> &scores, int count, double minGap)
{
    QList<int> order;
    for (int i = 0; i < times.size(); ++i) if (scores.value(i) > 0) order << i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return scores[a] > scores[b]; });
    QList<int> out;
    for (double gap = minGap; out.size() < count && gap >= minGap / 8; gap /= 2) {
        for (int i : order) {
            if (out.size() >= count) break;
            if (out.contains(i)) continue;
            bool ok = true;
            for (int j : out) if (std::fabs(times[i] - times[j]) < gap) { ok = false; break; }
            if (ok) out << i;
        }
    }
    std::sort(out.begin(), out.end(), [&](int a, int b) { return times[a] < times[b]; });
    return out;
}

void Thumbnails::cancel()
{
    if (m_proc) { m_proc->disconnect(this); m_proc->kill(); m_proc->deleteLater(); m_proc = nullptr; }
    setStatus(QString());
}

void Thumbnails::find(const QString &file, double duration)
{
    cancel();
    m_file = file;
    m_duration = duration;
    m_candidates.clear();
    emit candidatesChanged();
    if (!QFileInfo(file).isFile() || duration <= 0) { setStatus(QString()); emit finished(false, QStringLiteral("Thumbnails need a local video"), QString()); return; }

    const QString dir = Media::cacheDir() + QStringLiteral("/thumbpick/") + Media::cacheKey(file);
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const double interval = qMax(duration / 150.0, 0.1);
    const bool keyframesOnly = duration > 180; // long clips: only decode keyframes, it's far faster
    QStringList args = { QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y") };
    if (keyframesOnly) args << QStringLiteral("-skip_frame") << QStringLiteral("nokey");
    args << QStringLiteral("-i") << file << QStringLiteral("-an") << QStringLiteral("-sn") << QStringLiteral("-dn")
         << QStringLiteral("-vf")
         << QStringLiteral("select='isnan(prev_selected_t)+gte(t-prev_selected_t\\,%1)',scale=320:-2,showinfo").arg(interval, 0, 'f', 3)
         << QStringLiteral("-fps_mode") << QStringLiteral("vfr") << QStringLiteral("-q:v") << QStringLiteral("4")
         << dir + QStringLiteral("/%04d.jpg");

    auto *p = new QProcess(this);
    Media::makeBackground(p);
    m_proc = p;
    auto times = std::make_shared<QList<double>>();
    auto buf = std::make_shared<QByteArray>();
    setStatus(QStringLiteral("Looking through the clip…"), 0);
    connect(p, &QProcess::readyReadStandardError, this, [this, p, times, buf, duration] {
        buf->append(p->readAllStandardError());
        static const QRegularExpression re(QStringLiteral("pts_time:\\s*([-0-9.]+)"));
        int nl;
        while ((nl = buf->indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(buf->left(nl));
            buf->remove(0, nl + 1);
            if (!line.contains(QLatin1String("Parsed_showinfo"))) continue;
            const auto m = re.match(line);
            if (m.hasMatch()) { times->append(m.captured(1).toDouble()); setStatus(m_status, qMin(0.95, times->last() / duration)); }
        }
    });
    connect(p, &QProcess::finished, this, [this, p, dir, times](int code) {
        p->deleteLater();
        m_proc = nullptr;
        if (code != 0 || times->isEmpty()) {
            setStatus(QString());
            emit finished(false, QStringLiteral("Couldn't read frames from this clip"), QString());
            return;
        }
        scoreAll(dir, *times);
    });
    p->start(Media::toolPath(QStringLiteral("ffmpeg")), args);
}

void Thumbnails::scoreAll(const QString &dir, const QList<double> &times)
{
    setStatus(QStringLiteral("Picking the best frames…"), 0.97);
    QList<double> ts, scores;
    QStringList paths;
    for (int i = 0; i < times.size(); ++i) {
        const QString path = dir + QStringLiteral("/%1.jpg").arg(i + 1, 4, 10, QLatin1Char('0'));
        QImage img(path);
        if (img.isNull()) continue;
        double s = scoreFrame(img);
        // the first and last moments are often a fade, a hand on the camera or a clap
        const double edge = qMin(times[i], m_duration - times[i]);
        if (edge < m_duration * 0.03) s *= 0.7;
        ts << times[i];
        scores << s;
        paths << path;
    }
    const QList<int> pick = pickSpread(ts, scores, 12, m_duration / 24.0);
    double best = 0;
    for (int i : pick) best = qMax(best, scores[i]);
    m_candidates.clear();
    for (int i : pick)
        m_candidates.append(QVariantMap{ { "time", ts[i] }, { "url", QUrl::fromLocalFile(paths[i]).toString() }, { "score", best > 0 ? scores[i] / best : 0 } });
    emit candidatesChanged();
    setStatus(QString());
    emit finished(!m_candidates.isEmpty(), m_candidates.isEmpty() ? QStringLiteral("Every frame looked flat or black") : QString(), QString());
}

void Thumbnails::grab(const QString &file, double t, const QString &out, const QStringList &filters, std::function<void(bool)> done)
{
    auto *p = new QProcess(this);
    Media::makeBackground(p);
    connect(p, &QProcess::finished, this, [p, out, done](int code) {
        p->deleteLater();
        if (code != 0) qWarning("frame grab failed: %s", qPrintable(QString::fromUtf8(p->readAllStandardError()).trimmed()));
        done(code == 0 && QFileInfo(out).size() > 0);
    });
    QStringList args = { QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                         QStringLiteral("-ss"), QString::number(qMax(0.0, t), 'f', 3), QStringLiteral("-i"), file, QStringLiteral("-frames:v"), QStringLiteral("1"),
                         QStringLiteral("-an"), QStringLiteral("-sn") };
    if (!filters.isEmpty()) args << QStringLiteral("-vf") << filters.join(QLatin1Char(','));
    if (out.endsWith(QLatin1String(".jpg"))) args << QStringLiteral("-q:v") << QStringLiteral("2");
    args << out;
    p->start(Media::toolPath(QStringLiteral("ffmpeg")), args);
}

static QString stamp(double t)
{
    const int ms = int(std::lround(t * 1000));
    return QStringLiteral("%1m%2s%3").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

void Thumbnails::saveFrame(const QString &file, double t, const QString &kind)
{
    const bool yt = kind == QLatin1String("youtube");
    const QString folder = AppController::instance()->exportFolderFor(file);
    const QString base = folder + QLatin1Char('/') + QFileInfo(file).completeBaseName() + QStringLiteral(" thumbnail ") + stamp(t);
    const QString out = AppController::uniquePath(base + (yt ? QStringLiteral(" youtube.jpg") : QStringLiteral(".png")));
    QStringList filters;
    if (yt) filters << QStringLiteral("scale=1280:720:force_original_aspect_ratio=decrease:flags=lanczos")
                    << QStringLiteral("pad=1280:720:(ow-iw)/2:(oh-ih)/2:black") << QStringLiteral("setsar=1");
    grab(file, t, out, filters, [this, out, yt](bool ok) {
        if (ok && yt && QFileInfo(out).size() > 2 * 1024 * 1024) {
            // YouTube's limit is 2 MB: shrink quality until it fits
            QImage img(out);
            for (int q = 85; q >= 40 && QFileInfo(out).size() > 2 * 1024 * 1024; q -= 15) img.save(out, "JPG", q);
        }
        emit finished(ok, ok ? (yt ? QStringLiteral("Saved a 1280x720 YouTube thumbnail") : QStringLiteral("Saved the frame as PNG")) : QStringLiteral("Couldn't save the frame"), ok ? out : QString());
    });
}

void Thumbnails::copyFrame(const QString &file, double t)
{
    const QString out = QDir::tempPath() + QStringLiteral("/slate-copy-frame.png");
    QFile::remove(out);
    grab(file, t, out, {}, [this, out](bool ok) {
        if (ok) QGuiApplication::clipboard()->setImage(QImage(out));
        emit finished(ok, ok ? QStringLiteral("Frame copied") : QStringLiteral("Couldn't copy the frame"), QString());
    });
}

bool Thumbnails::canEmbed(const QString &file) const
{
    static const QStringList ok = { QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"), QStringLiteral("mkv") };
    const QFileInfo fi(file);
    return fi.isFile() && fi.isWritable() && ok.contains(fi.suffix().toLower());
}

double Thumbnails::embeddedTime(const QString &file) const
{
    const QJsonValue v = ReviewStore::instance().value(file, QStringLiteral("thumbTime"));
    return v.isDouble() ? v.toDouble() : -1;
}

QString Thumbnails::tempPathFor(const QString &file) const
{
    const QFileInfo fi(file);
    return fi.absolutePath() + QStringLiteral("/.") + fi.completeBaseName() + QStringLiteral(".slate-tmp.") + fi.suffix();
}

void Thumbnails::embed(const QString &file, double t) { remux(file, t, false); }
void Thumbnails::removeEmbedded(const QString &file) { remux(file, -1, true); }

struct StreamInfo { int index; QString type; bool pic; };
static QList<StreamInfo> probeStreams(const QString &file, double *duration)
{
    QProcess p;
    p.start(Media::toolPath(QStringLiteral("ffprobe")), { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"), QStringLiteral("json"),
                                                         QStringLiteral("-show_format"), QStringLiteral("-show_streams"), file });
    QList<StreamInfo> out;
    if (!p.waitForFinished(20000) || p.exitCode() != 0) return out;
    const QJsonObject root = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    if (duration) *duration = root.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject s = v.toObject();
        out.append({ s.value(QStringLiteral("index")).toInt(), s.value(QStringLiteral("codec_type")).toString(),
                     s.value(QStringLiteral("disposition")).toObject().value(QStringLiteral("attached_pic")).toInt() == 1
                         || s.value(QStringLiteral("codec_type")).toString() == QLatin1String("attachment") });
    }
    return out;
}

void Thumbnails::remux(const QString &file, double t, bool remove)
{
    if (m_embedProc) return;
    if (!canEmbed(file)) { emit finished(false, QStringLiteral("Explorer thumbnails work with MP4, MOV, M4V and MKV files you can write to"), QString()); return; }
    const QFileInfo fi(file);
    const bool mkv = fi.suffix().toLower() == QLatin1String("mkv");
    const QString temp = tempPathFor(file);
    const QString cover = QDir::tempPath() + QStringLiteral("/slate-cover.jpg");
    QFile::remove(temp);
    setStatus(remove ? QStringLiteral("Removing the thumbnail…") : QStringLiteral("Adding the thumbnail…"), 0.2);
    m_embedProc = new QProcess(this); // placeholder so busy is true while the cover is grabbed
    emit changed();

    auto run = [this, file, temp, cover, mkv, remove, t]() {
        double dur = 0;
        const QList<StreamInfo> streams = probeStreams(file, &dur);
        if (streams.isEmpty()) { delete m_embedProc; m_embedProc = nullptr; setStatus(QString()); emit finished(false, QStringLiteral("Couldn't read the file"), QString()); return; }
        QStringList args = { QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y"), QStringLiteral("-i"), file };
        if (!remove && !mkv) args << QStringLiteral("-i") << cover;
        int videoOut = 0;
        for (const StreamInfo &s : streams) {
            if (s.pic) continue; // an old cover is replaced (or removed)
            if (!mkv && s.type != QLatin1String("video") && s.type != QLatin1String("audio") && s.type != QLatin1String("subtitle")) continue;
            args << QStringLiteral("-map") << QStringLiteral("0:%1").arg(s.index);
            if (s.type == QLatin1String("video")) ++videoOut;
        }
        args << QStringLiteral("-map_metadata") << QStringLiteral("0") << QStringLiteral("-map_chapters") << QStringLiteral("0") << QStringLiteral("-c") << QStringLiteral("copy");
        if (!remove) {
            if (mkv) {
                args << QStringLiteral("-attach") << cover << QStringLiteral("-metadata:s:t") << QStringLiteral("mimetype=image/jpeg")
                     << QStringLiteral("-metadata:s:t") << QStringLiteral("filename=cover.jpg");
            } else {
                args << QStringLiteral("-map") << QStringLiteral("1:0") << QStringLiteral("-c:v:%1").arg(videoOut) << QStringLiteral("mjpeg")
                     << QStringLiteral("-disposition:v:%1").arg(videoOut) << QStringLiteral("attached_pic");
            }
        }
        args << temp;
        delete m_embedProc;
        auto *p = new QProcess(this);
        Media::makeBackground(p);
        m_embedProc = p;
        setStatus(m_status, 0.5);
        connect(p, &QProcess::finished, this, [this, p, file, temp, dur, remove, t](int code) {
            p->deleteLater();
            m_embedProc = nullptr;
            const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
            double newDur = 0;
            const QList<StreamInfo> after = code == 0 ? probeStreams(temp, &newDur) : QList<StreamInfo>();
            bool hasPic = false;
            for (const StreamInfo &s : after) hasPic |= s.pic;
            const bool ok = code == 0 && !after.isEmpty() && std::fabs(newDur - dur) < 0.5 && hasPic != remove;
            if (!ok) {
                qWarning("thumbnail remux failed (%d): %s", code, qPrintable(err));
                QFile::remove(temp);
                setStatus(QString());
                emit finished(false, QStringLiteral("Couldn't add the thumbnail. The original file wasn't touched"), QString());
                return;
            }
            m_swapFile = file;
            m_swapTemp = temp;
            m_swapTime = remove ? -1 : t;
            setStatus(m_status, 0.9);
            emit swapReady(file);
        });
        p->start(Media::toolPath(QStringLiteral("ffmpeg")), args);
    };
    if (remove) { run(); return; }
    // the cover: the frame at t, at most 1280 wide (Explorer shows it small; keeps the file lean)
    QFile::remove(cover);
    grab(file, t, cover, { QStringLiteral("scale='min(1280,iw)':-2") }, [this, run](bool ok) {
        if (!ok) { delete m_embedProc; m_embedProc = nullptr; setStatus(QString()); emit finished(false, QStringLiteral("Couldn't grab that frame"), QString()); return; }
        run();
    });
}

bool Thumbnails::commitSwap(const QString &file, bool lastTry)
{
    if (file != m_swapFile || m_swapTemp.isEmpty()) return false;
    const QString temp = m_swapTemp;
    const QFileInfo fi(file);
    const QDateTime born = fi.birthTime(), modified = fi.lastModified();
    ReviewStore &store = ReviewStore::instance();
    const QString oldFp = store.fingerprintFor(file);

    const QString old = fi.absolutePath() + QStringLiteral("/.") + fi.fileName() + QStringLiteral(".slate-old");
    QFile::remove(old);
    bool ok = QFile::rename(file, old);
    if (ok && !QFile::rename(temp, file)) { QFile::rename(old, file); ok = false; }
    if (!ok && !lastTry) return false;
    m_swapTemp.clear();
    m_swapFile.clear();
    if (!ok) {
        QFile::remove(temp);
        setStatus(QString());
        emit finished(false, QStringLiteral("Couldn't replace the file. Is it open in another app?"), QString());
        return false;
    }
    QFile::remove(old);
    {
        QFile f(file);
        if (f.open(QIODevice::ReadWrite)) {
            if (born.isValid()) f.setFileTime(born, QFileDevice::FileBirthTime);
            f.setFileTime(modified, QFileDevice::FileModificationTime);
        }
    }
    // review notes and the transcript follow the file's new contents
    store.fileRewritten(file);
    const QString newFp = store.fingerprintFor(file);
    if (!oldFp.isEmpty() && newFp != oldFp)
        QFile::rename(store.root() + QStringLiteral("/transcripts/") + oldFp + QStringLiteral(".json"), store.root() + QStringLiteral("/transcripts/") + newFp + QStringLiteral(".json"));
    store.setValue(file, QStringLiteral("thumbTime"), m_swapTime >= 0 ? QJsonValue(m_swapTime) : QJsonValue());
#ifdef Q_OS_WIN
    const std::wstring w = QDir::toNativeSeparators(file).toStdWString();
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, w.c_str(), nullptr);
#endif
    setStatus(QString());
    const bool removed = m_swapTime < 0;
    qInfo("Explorer thumbnail %s: %s", removed ? "removed" : "set", qPrintable(file));
    emit finished(true, removed ? QStringLiteral("Thumbnail removed from the file") : QStringLiteral("Explorer will show this frame as the file's thumbnail"), file);
    return true;
}

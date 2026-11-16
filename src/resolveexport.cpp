#include "resolveexport.h"
#include "appcontroller.h"
#include "fcpxml.h"
#include "media.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>

ResolveExport::ResolveExport(QObject *parent) : QObject(parent) {}

ResolveExport *ResolveExport::instance()
{
    static ResolveExport *r = new ResolveExport();
    return r;
}

static QString probeKey(const QString &file)
{
    const QFileInfo fi(file);
    return fi.absoluteFilePath() + QLatin1Char('|') + QString::number(fi.lastModified().toMSecsSinceEpoch());
}

static double ratio(const QString &r)
{
    const QStringList p = r.split(QLatin1Char('/'));
    if (p.size() == 2 && p[1].toDouble() > 0) return p[0].toDouble() / p[1].toDouble();
    return r.toDouble();
}

ResolveExport::Probe ResolveExport::parseProbe(const QByteArray &json)
{
    Probe p;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonObject fmt = root.value(QStringLiteral("format")).toObject();
    p.duration = fmt.value(QStringLiteral("duration")).toString().toDouble();
    p.timecode = fmt.value(QStringLiteral("tags")).toObject().value(QStringLiteral("timecode")).toString();
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject s = v.toObject();
        const QString type = s.value(QStringLiteral("codec_type")).toString();
        const QJsonObject tags = s.value(QStringLiteral("tags")).toObject();
        if (p.timecode.isEmpty()) p.timecode = tags.value(QStringLiteral("timecode")).toString();
        if (type == QLatin1String("video") && !p.video && s.value(QStringLiteral("disposition")).toObject().value(QStringLiteral("attached_pic")).toInt() == 0) {
            p.video = true;
            p.width = s.value(QStringLiteral("width")).toInt();
            p.height = s.value(QStringLiteral("height")).toInt();
            const double avg = ratio(s.value(QStringLiteral("avg_frame_rate")).toString());
            const double real = ratio(s.value(QStringLiteral("r_frame_rate")).toString());
            p.fps = avg > 0 && avg < 1000 ? avg : real;
            p.rotation = tags.value(QStringLiteral("rotate")).toString().toInt();
            for (const QJsonValue &sd : s.value(QStringLiteral("side_data_list")).toArray()) {
                const QJsonObject o = sd.toObject();
                if (o.contains(QStringLiteral("rotation"))) p.rotation = o.value(QStringLiteral("rotation")).toInt();
            }
            if (p.duration <= 0) p.duration = s.value(QStringLiteral("duration")).toString().toDouble();
        } else if (type == QLatin1String("audio") && !p.audio) {
            p.audio = true;
            p.channels = qMax(1, s.value(QStringLiteral("channels")).toInt());
            p.rate = s.value(QStringLiteral("sample_rate")).toString().toInt();
            if (p.rate <= 0) p.rate = 48000;
        }
    }
    p.ok = p.duration > 0 && (p.video || p.audio);
    return p;
}

void ResolveExport::start(const QVariantList &items, const QVariantMap &opts)
{
    if (m_busy) return;
    if (items.isEmpty()) {
        emit finished(false, QStringLiteral("Nothing to put on the timeline"), QString());
        return;
    }
    m_items = items;
    m_opts = opts;
    m_todo.clear();
    for (const QVariant &v : items) {
        const QString f = v.toMap().value(QStringLiteral("file")).toString();
        if (!m_todo.contains(f) && !m_probes.contains(probeKey(f))) m_todo << f;
    }
    m_total = qMax(1, int(m_todo.size()));
    m_busy = true;
    m_progress = 0;
    m_status = QStringLiteral("Reading clips…");
    emit changed();
    probeNext();
}

void ResolveExport::cancel()
{
    if (!m_busy) return;
    m_todo.clear();
    if (m_proc) { m_proc->disconnect(this); m_proc->kill(); m_proc->deleteLater(); }
    m_busy = false;
    m_status.clear();
    emit changed();
}

void ResolveExport::fail(const QString &why)
{
    m_busy = false;
    m_status.clear();
    emit changed();
    qWarning("Resolve export failed: %s", qPrintable(why));
    emit finished(false, why, QString());
}

void ResolveExport::probeNext()
{
    if (!m_busy) return;
    if (m_todo.isEmpty()) { build(); return; }
    const QString f = m_todo.takeFirst();
    m_status = QStringLiteral("Reading %1").arg(QFileInfo(f).fileName());
    m_progress = 1.0 - double(m_todo.size() + 1) / m_total;
    emit changed();
    auto *p = new QProcess(this);
    m_proc = p;
    connect(p, &QProcess::finished, this, [this, p, f](int code) {
        p->deleteLater();
        m_proc = nullptr;
        const Probe pr = parseProbe(p->readAllStandardOutput());
        if (code != 0 || !pr.ok) {
            qWarning("ffprobe failed for %s: %s", qPrintable(f), qPrintable(QString::fromUtf8(p->readAllStandardError()).trimmed()));
            fail(QStringLiteral("Couldn't read %1").arg(QFileInfo(f).fileName()));
            return;
        }
        m_probes.insert(probeKey(f), pr);
        probeNext();
    });
    p->start(Media::toolPath(QStringLiteral("ffprobe")), { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"), QStringLiteral("json"),
                                                           QStringLiteral("-show_format"), QStringLiteral("-show_streams"), f });
    if (!p->waitForStarted(5000)) {
        p->deleteLater();
        fail(QStringLiteral("ffprobe wasn't found next to Slate"));
    }
}

static QString safeFileName(QString s)
{
    static const QRegularExpression bad(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    s.replace(bad, QStringLiteral(" "));
    s = s.simplified();
    return s.isEmpty() ? QStringLiteral("Slate timeline") : s.left(80);
}

void ResolveExport::build()
{
    m_status = QStringLiteral("Writing the timeline…");
    m_progress = 1;
    emit changed();

    const QString output = m_opts.value(QStringLiteral("output"), QStringLiteral("timeline")).toString();
    const double handles = m_opts.value(QStringLiteral("handles")).toDouble();
    const bool handlesInFrames = m_opts.value(QStringLiteral("handlesUnit")).toString() == QLatin1String("frames");

    QVector<Fcpxml::Item> items;
    Probe first;
    for (const QVariant &v : m_items) {
        const QVariantMap m = v.toMap();
        const QString f = m.value(QStringLiteral("file")).toString();
        const Probe pr = m_probes.value(probeKey(f));
        if (!first.video && pr.video) first = pr;
        Fcpxml::Item it;
        it.path = QFileInfo(f).absoluteFilePath();
        it.name = m.value(QStringLiteral("name")).toString();
        it.duration = pr.duration;
        it.hasVideo = pr.video;
        it.hasAudio = pr.audio;
        it.width = pr.width;
        it.height = pr.height;
        it.channels = pr.channels;
        it.sampleRate = pr.rate;
        it.startTimecode = pr.timecode;
        it.rate = Fcpxml::rateFor(pr.fps > 0 ? pr.fps : 30);
        const double h = handlesInFrames ? handles / it.rate.fps() : handles;
        const double a = m.value(QStringLiteral("start")).toDouble(), b = m.value(QStringLiteral("end"), -1).toDouble();
        it.in = qMax(0.0, a - h);
        it.out = b < 0 ? -1 : qMin(pr.duration, b + h);
        for (const QVariant &mv : m.value(QStringLiteral("markers")).toList()) {
            const QVariantMap mm = mv.toMap();
            it.markers.push_back({ mm.value(QStringLiteral("time")).toDouble(), mm.value(QStringLiteral("end"), -1).toDouble(),
                                   mm.value(QStringLiteral("kind")).toInt(), mm.value(QStringLiteral("note")).toString() });
        }
        it.keywords = m.value(QStringLiteral("keywords")).toStringList();
        items.push_back(it);
    }

    Fcpxml::Options o;
    o.title = m_opts.value(QStringLiteral("title")).toString().trimmed();
    if (o.title.isEmpty()) o.title = QStringLiteral("Slate selects");
    o.event = QStringLiteral("Slate");
    const QString fps = m_opts.value(QStringLiteral("fps"), QStringLiteral("match")).toString();
    o.rate = Fcpxml::rateFor(fps == QLatin1String("match") ? (first.fps > 0 ? first.fps : 30) : fps.toDouble());
    const QString res = m_opts.value(QStringLiteral("resolution"), QStringLiteral("match")).toString();
    if (res == QLatin1String("1080p")) { o.width = 1920; o.height = 1080; }
    else if (res == QLatin1String("4k")) { o.width = 3840; o.height = 2160; }
    else if (res == QLatin1String("vertical")) { o.width = 1080; o.height = 1920; }
    else if (first.video && first.width > 0) {
        const bool turned = std::abs(first.rotation) % 180 == 90; // phone footage filmed upright
        o.width = turned ? first.height : first.width;
        o.height = turned ? first.width : first.height;
    }
    o.gap = m_opts.value(QStringLiteral("gap")).toDouble();
    o.clipMarkers = m_opts.value(QStringLiteral("clipMarkers"), true).toBool();
    o.markClipStarts = m_opts.value(QStringLiteral("markClipStarts"), false).toBool();

    QString folder = m_opts.value(QStringLiteral("folder")).toString();
    if (folder.isEmpty()) folder = AppController::instance()->exportFolderFor(items.first().path);
    QDir().mkpath(folder);
    const QString base = folder + QLatin1Char('/') + safeFileName(o.title);

    QString saved;
    if (output != QLatin1String("files")) {
        const Fcpxml::Result r = Fcpxml::build(items, o);
        const QString xmlPath = AppController::uniquePath(base + QStringLiteral(".fcpxml"));
        QSaveFile f(xmlPath);
        if (!f.open(QIODevice::WriteOnly)) { fail(QStringLiteral("Couldn't save the timeline: %1").arg(f.errorString())); return; }
        f.write(r.xml.toUtf8());
        if (!f.commit()) { fail(QStringLiteral("Couldn't save the timeline")); return; }
        saved = xmlPath;
        if (!o.clipMarkers && !r.timelineEdl.isEmpty()) {
            QSaveFile e(QFileInfo(xmlPath).absolutePath() + QLatin1Char('/') + QFileInfo(xmlPath).completeBaseName() + QStringLiteral(" markers.edl"));
            if (e.open(QIODevice::WriteOnly)) { e.write(r.timelineEdl.toUtf8()); e.commit(); }
        }
        qInfo("Resolve timeline written: %s (%d clips, %d markers, %.1f s)", qPrintable(xmlPath), r.clips, r.markers, r.duration);
    }
    int fileJobs = 0;
    if (output != QLatin1String("timeline")) {
        // Each item as its own lossless file, grouped per source clip so the export queue runs in order.
        const QString clipDir = base + QStringLiteral(" clips");
        QHash<QString, QVariantList> perFile;
        QStringList order;
        for (const Fcpxml::Item &it : items) {
            if (!perFile.contains(it.path)) order << it.path;
            perFile[it.path].append(QVariantMap{ { "start", it.in }, { "end", it.out < 0 ? it.duration : it.out }, { "note", it.name } });
            ++fileJobs;
        }
        for (const QString &p : order)
            AppController::instance()->exportRanges(perFile.value(p), true, p, clipDir);
        if (saved.isEmpty()) saved = clipDir;
    }

    m_busy = false;
    m_status.clear();
    emit changed();
    QString msg;
    if (output == QLatin1String("files")) msg = QStringLiteral("Exporting %1 clips to \"%2\"").arg(fileJobs).arg(QFileInfo(saved).fileName());
    else msg = QStringLiteral("Saved %1. In Resolve: File > Import > Timeline").arg(QFileInfo(saved).fileName());
    emit finished(true, msg, saved);
}

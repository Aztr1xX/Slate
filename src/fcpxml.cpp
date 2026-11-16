#include "fcpxml.h"
#include "markermodel.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>
#include <numeric>

namespace Fcpxml {

static QString QFileInfoName(const QString &path) { return QFileInfo(path).fileName(); }

int Rate::nominal() const
{
    return int(std::lround(fps()));
}

Rate rateFor(double fps)
{
    static const Rate rates[] = { { 1001, 24000 }, { 1, 24 }, { 1, 25 }, { 1001, 30000 }, { 1, 30 }, { 1001, 48000 }, { 1, 48 },
                                  { 1, 50 }, { 1001, 60000 }, { 1, 60 }, { 1, 100 }, { 1001, 120000 }, { 1, 120 } };
    if (!(fps > 0) || !std::isfinite(fps))
        return { 1, 30 };
    Rate best = rates[0];
    double bestD = 1e9;
    for (const Rate &r : rates) {
        const double d = std::fabs(r.fps() - fps);
        if (d < bestD) { bestD = d; best = r; }
    }
    // Far from every standard rate (e.g. 15 fps screen recordings): use it as it is.
    if (bestD > 0.5) return { 1, qint64(std::lround(fps)) };
    return best;
}

QString timeString(qint64 frames, const Rate &r)
{
    if (frames == 0) return QStringLiteral("0s");
    qint64 n = frames * r.num, d = r.den;
    const qint64 g = std::gcd(n < 0 ? -n : n, d);
    if (g > 1) { n /= g; d /= g; }
    return d == 1 ? QStringLiteral("%1s").arg(n) : QStringLiteral("%1/%2s").arg(n).arg(d);
}

qint64 framesAt(double seconds, const Rate &r)
{
    return qint64(std::llround(seconds * double(r.den) / double(r.num)));
}

double secondsOf(qint64 frames, const Rate &r)
{
    return double(frames) * double(r.num) / double(r.den);
}

qint64 timecodeToFrames(const QString &tc, const Rate &r)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{1,2})[:](\\d{2})[:](\\d{2})([:;.,])(\\d{2,3})$"));
    const auto m = re.match(tc.trimmed());
    if (!m.hasMatch()) return -1;
    const qint64 h = m.captured(1).toLongLong(), mi = m.captured(2).toLongLong(), s = m.captured(3).toLongLong(), f = m.captured(5).toLongLong();
    const int nom = r.nominal();
    const bool drop = m.captured(4) != QLatin1String(":") && r.num == 1001 && (nom == 30 || nom == 60);
    qint64 frames = ((h * 60 + mi) * 60 + s) * nom + f;
    if (drop) {
        const qint64 perMin = nom == 60 ? 4 : 2;
        const qint64 minutes = h * 60 + mi;
        frames -= perMin * (minutes - minutes / 10);
    }
    return frames;
}

QString kindLabel(int kind)
{
    return MarkerModel::kindName(kind);
}

static QString esc(QString s)
{
    static const QRegularExpression ctrl(QStringLiteral("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f]"));
    s.remove(ctrl);
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return s.toHtmlEscaped();
}

Result build(const QVector<Item> &items, const Options &opt)
{
    Result res;
    const Rate seq = opt.rate;
    const qint64 start = qint64(std::llround(opt.startHours * 3600.0)) * seq.nominal(); // 01:00:00:00 as a frame count

    QString formats, assets, spine;
    QHash<QString, QString> formatIds; // "num/den wxh" -> id
    QHash<QString, QString> assetIds;  // path -> id
    int nextId = 1;
    auto newId = [&] { return QStringLiteral("r%1").arg(nextId++); };
    auto formatFor = [&](const Rate &r, int w, int h) {
        const QString key = QStringLiteral("%1/%2 %3x%4").arg(r.num).arg(r.den).arg(w).arg(h);
        auto it = formatIds.constFind(key);
        if (it != formatIds.constEnd()) return *it;
        const QString id = newId();
        formats += QStringLiteral("    <format id=\"%1\" frameDuration=\"%2\" width=\"%3\" height=\"%4\"/>\n")
                       .arg(id, timeString(1, r)).arg(w).arg(h);
        formatIds.insert(key, id);
        return id;
    };
    const QString seqFormat = formatFor(seq, opt.width, opt.height);

    QVector<MarkerModel::Marker> edlMarkers;
    qint64 pos = 0; // timeline frames since the start
    int markerCount = 0;

    for (int i = 0; i < items.size(); ++i) {
        const Item &it = items[i];
        const Rate src = it.hasVideo ? it.rate : seq;
        qint64 srcTc = it.startTimecode.isEmpty() ? 0 : timecodeToFrames(it.startTimecode, src);
        if (srcTc < 0) srcTc = 0;
        const qint64 total = qMax<qint64>(1, framesAt(it.duration, src));
        const qint64 inF = qBound<qint64>(0, framesAt(qMax(0.0, it.in), src), total - 1);
        const qint64 outF = qBound<qint64>(inF + 1, it.out < 0 ? total : framesAt(it.out, src), total);
        const qint64 lenSeq = qMax<qint64>(1, framesAt(secondsOf(outF - inF, src), seq));
        const double inSec = secondsOf(inF, src), outSec = secondsOf(outF, src);

        QString assetId = assetIds.value(it.path);
        if (assetId.isEmpty()) {
            assetId = newId();
            assetIds.insert(it.path, assetId);
            QString a = QStringLiteral("    <asset id=\"%1\" name=\"%2\" start=\"%3\" duration=\"%4\" hasVideo=\"%5\"")
                            .arg(assetId, esc(QFileInfoName(it.path)), timeString(srcTc, src), timeString(total, src), it.hasVideo ? QStringLiteral("1") : QStringLiteral("0"));
            if (it.hasVideo) a += QStringLiteral(" format=\"%1\"").arg(formatFor(src, it.width > 0 ? it.width : opt.width, it.height > 0 ? it.height : opt.height));
            if (it.hasAudio)
                a += QStringLiteral(" hasAudio=\"1\" audioSources=\"1\" audioChannels=\"%1\" audioRate=\"%2\"").arg(qMax(1, it.channels)).arg(it.sampleRate > 0 ? it.sampleRate : 48000);
            else
                a += QStringLiteral(" hasAudio=\"0\"");
            a += QStringLiteral(">\n      <media-rep kind=\"original-media\" src=\"%1\"/>\n    </asset>\n")
                     .arg(esc(QUrl::fromLocalFile(it.path).toString(QUrl::FullyEncoded)));
            assets += a;
        }

        QString clip = QStringLiteral("        <asset-clip ref=\"%1\" name=\"%2\" offset=\"%3\" start=\"%4\" duration=\"%5\" tcFormat=\"NDF\"")
                           .arg(assetId, esc(it.name.isEmpty() ? QFileInfoName(it.path) : it.name), timeString(start + pos, seq),
                                timeString(srcTc + inF, src), timeString(lenSeq, seq));
        if (it.hasVideo) clip += QStringLiteral(" format=\"%1\"").arg(formatFor(src, it.width > 0 ? it.width : opt.width, it.height > 0 ? it.height : opt.height));
        QString inner;
        const qint64 clipLenSrc = outF - inF;
        auto addMarker = [&](double t, double end, const QString &value, int kind) {
            const qint64 mf = framesAt(t, src);
            if (mf < inF || mf >= outF) return;
            const qint64 len = end > t ? qBound<qint64>(1, qMin(framesAt(end, src), outF) - mf, clipLenSrc) : 1;
            if (opt.clipMarkers)
                inner += QStringLiteral("          <marker start=\"%1\" duration=\"%2\" value=\"%3\"/>\n")
                             .arg(timeString(srcTc + mf, src), timeString(len, src), esc(value));
            // the same marker where it lands on the new timeline, in timecode seconds
            MarkerModel::Marker em;
            em.time = double(pos + framesAt(secondsOf(mf - inF, src), seq)) / seq.nominal();
            em.end = end > t ? em.time + double(framesAt(secondsOf(len, src), seq)) / seq.nominal() : -1;
            em.kind = kind;
            em.note = value;
            edlMarkers.push_back(em);
            ++markerCount;
        };
        if (opt.markClipStarts)
            addMarker(inSec, -1, it.name.isEmpty() ? QFileInfoName(it.path) : it.name, 0);
        for (const Marker &m : it.markers) {
            if (m.time < inSec - 1e-6 || m.time >= outSec) continue;
            const QString label = m.note.isEmpty() || m.note == kindLabel(m.kind) ? kindLabel(m.kind)
                                  : (m.kind == 0 ? m.note : kindLabel(m.kind) + QStringLiteral(": ") + m.note);
            addMarker(m.time, m.end, label, m.kind);
        }
        for (const QString &k : it.keywords)
            inner += QStringLiteral("          <keyword start=\"%1\" duration=\"%2\" value=\"%3\"/>\n")
                         .arg(timeString(srcTc + inF, src), timeString(clipLenSrc, src), esc(k));
        clip += inner.isEmpty() ? QStringLiteral("/>\n") : QStringLiteral(">\n") + inner + QStringLiteral("        </asset-clip>\n");
        spine += clip;
        pos += lenSeq;
        ++res.clips;

        if (opt.gap > 0 && i + 1 < items.size()) {
            const qint64 g = qMax<qint64>(1, framesAt(opt.gap, seq));
            spine += QStringLiteral("        <gap name=\"Gap\" offset=\"%1\" start=\"%2\" duration=\"%3\"/>\n")
                         .arg(timeString(start + pos, seq), timeString(start, seq), timeString(g, seq));
            pos += g;
        }
    }

    QString x;
    x += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE fcpxml>\n<fcpxml version=\"1.9\">\n");
    x += QStringLiteral("  <resources>\n") + formats + assets + QStringLiteral("  </resources>\n");
    x += QStringLiteral("  <library>\n    <event name=\"%1\">\n      <project name=\"%2\">\n").arg(esc(opt.event), esc(opt.title));
    x += QStringLiteral("        <sequence format=\"%1\" duration=\"%2\" tcStart=\"%3\" tcFormat=\"NDF\" audioLayout=\"stereo\" audioRate=\"48k\">\n")
             .arg(seqFormat, timeString(pos, seq), timeString(start, seq));
    x += QStringLiteral("          <spine>\n");
    // indent the spine one level further
    for (const QString &line : spine.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        x += QStringLiteral("    ") + line + QLatin1Char('\n');
    x += QStringLiteral("          </spine>\n        </sequence>\n      </project>\n    </event>\n  </library>\n</fcpxml>\n");

    res.xml = x;
    res.duration = secondsOf(pos, seq);
    res.markers = markerCount;
    if (!edlMarkers.isEmpty())
        res.timelineEdl = MarkerModel::toEdl(edlMarkers, seq.fps(), opt.title, start);
    return res;
}

} // namespace Fcpxml

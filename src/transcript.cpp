#include "transcript.h"
#include "media.h"
#include "reviewstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <QThread>
#include <functional>
#include <cmath>

Transcript::Transcript(QObject *parent) : QObject(parent) {}

Transcript *Transcript::instance()
{
    static Transcript *t = new Transcript();
    return t;
}

bool Transcript::available() const { return !Media::toolPath(QStringLiteral("whisper-cli")).isEmpty(); }

QString Transcript::modelPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/models");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/ggml-%1.bin").arg(m_model);
}

bool Transcript::modelReady() const { return QFileInfo(modelPath()).size() > 50 * 1024 * 1024; }

void Transcript::setModel(const QString &m)
{
    static const QStringList ok = { QStringLiteral("base.en"), QStringLiteral("base"), QStringLiteral("small.en"), QStringLiteral("small") };
    if (!ok.contains(m) || m == m_model) return;
    m_model = m;
    emit modelChanged();
    emit stateChanged();
}

QString Transcript::modelSizeLabel(const QString &model)
{
    return model.startsWith(QLatin1String("small")) ? QStringLiteral("466 MB") : QStringLiteral("142 MB");
}

// Transcripts are slow to make, so they're kept with the review data, keyed by the file's
// contents: moving or renaming the folder doesn't lose them.
QString Transcript::cachePath(const QString &file) const
{
    const QString fp = ReviewStore::instance().fingerprintFor(file);
    const QString dir = ReviewStore::instance().root() + QStringLiteral("/transcripts");
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + (fp.isEmpty() ? Media::cacheKey(file) : fp) + QStringLiteral(".json");
    if (!QFileInfo::exists(path)) {
        // made by an older Slate, keyed by path and date
        const QString old = Media::cacheDir() + QLatin1Char('/') + Media::cacheKey(file) + QStringLiteral(".transcript.json");
        if (QFileInfo::exists(old)) QFile::copy(old, path);
    }
    return path;
}

void Transcript::setState(const QString &s, double progress, const QString &err)
{
    m_state = s;
    m_progress = progress;
    m_error = err;
    emit stateChanged();
}

// The shown clip's state follows the queue: its own job's stage, "queued", or ready / idle.
void Transcript::refreshShownState()
{
    if (!m_file.isEmpty() && m_file == m_job) setState(m_jobStage, m_jobProgress);
    else if (!m_segments.isEmpty()) setState(QStringLiteral("ready"), 1);
    else if (m_queue.contains(m_file)) setState(QStringLiteral("queued"));
    else if (m_state != QLatin1String("error")) setState(QStringLiteral("idle"));
}

void Transcript::setJobProgress(const QString &stage, double p)
{
    m_jobStage = stage;
    m_jobProgress = p;
    emit queueChanged();
    if (m_job == m_file) setState(stage, p);
}

bool Transcript::hasTranscript(const QString &file) const
{
    return QFileInfo(file).isFile() && QFileInfo::exists(cachePath(file));
}

int Transcript::countWithTranscripts(const QStringList &files) const
{
    int n = 0;
    for (const QString &f : files) if (hasTranscript(f)) ++n;
    return n;
}

QVariantList Transcript::segmentsFor(const QString &file) const
{
    if (file == m_file && !m_segments.isEmpty()) return m_segments;
    const QString path = cachePath(file);
    auto it = m_cache.constFind(path);
    if (it != m_cache.constEnd()) return *it;
    QVariantList segs;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) segs = QJsonDocument::fromJson(f.readAll()).array().toVariantList();
    if (!segs.isEmpty()) m_cache.insert(path, segs);
    return segs;
}

void Transcript::rescan()
{
    m_cache.clear();
    ++m_rev;
    emit transcriptsChanged();
    load(m_file);
}

void Transcript::load(const QString &file)
{
    m_file = file;
    m_error.clear();
    m_segments = QFileInfo(file).isFile() ? segmentsFor(file) : QVariantList();
    emit segmentsChanged();
    m_state.clear();
    refreshShownState();
}

void Transcript::start(const QString &file)
{
    if (!available()) {
        if (file == m_file) setState(QStringLiteral("error"), 0, QStringLiteral("whisper-cli wasn't found next to Slate"));
        return;
    }
    if (!QFileInfo(file).isFile()) {
        if (file == m_file) setState(QStringLiteral("error"), 0, QStringLiteral("Transcripts work on local files only"));
        return;
    }
    if (file == m_job) return;
    m_queue.removeAll(file);
    m_queue.prepend(file);
    if (!m_batch) { m_batchTotal = 0; m_batchDone = 0; }
    else ++m_batchTotal;
    if (file == m_file) { m_segments.clear(); emit segmentsChanged(); }
    emit queueChanged();
    refreshShownState();
    if (m_job.isEmpty()) next();
}

int Transcript::transcribeAll(const QStringList &files)
{
    if (!available()) return 0;
    int added = 0;
    // the shown clip first, then folder order
    QStringList ordered = files;
    if (ordered.contains(m_file)) { ordered.removeAll(m_file); ordered.prepend(m_file); }
    for (const QString &f : ordered) {
        if (!QFileInfo(f).isFile() || f == m_job || m_queue.contains(f) || hasTranscript(f)) continue;
        m_queue.append(f);
        ++added;
    }
    if (added == 0) return 0;
    if (!m_batch) { m_batch = true; m_batchTotal = 0; m_batchDone = 0; if (!m_job.isEmpty()) m_batchTotal = 1; }
    m_batchTotal += added;
    emit queueChanged();
    refreshShownState();
    if (m_job.isEmpty()) next();
    return added;
}

void Transcript::next()
{
    if (m_queue.isEmpty()) {
        m_job.clear();
        m_batch = false;
        emit queueChanged();
        refreshShownState();
        return;
    }
    m_job = m_queue.takeFirst();
    emit queueChanged();
    if (!modelReady()) download();
    else extractAudio();
}

void Transcript::cancel()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); }
    if (m_part) { m_part->close(); m_part->remove(); delete m_part; m_part = nullptr; }
    if (m_proc) { m_proc->disconnect(this); m_proc->kill(); m_proc->deleteLater(); }
    if (!m_wav.isEmpty()) { QFile::remove(m_wav); m_wav.clear(); }
    m_queue.clear();
    m_job.clear();
    m_batch = false;
    m_batchTotal = m_batchDone = 0;
    emit queueChanged();
    m_state.clear();
    refreshShownState();
}

void Transcript::jobDone(bool ok, const QString &err, const QVariantList &segs)
{
    const QString file = m_job;
    if (ok) {
        const QString path = cachePath(file);
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(QJsonArray::fromVariantList(segs)).toJson(QJsonDocument::Compact));
            f.commit();
        }
        m_cache.insert(path, segs);
        ++m_rev;
        emit transcriptsChanged();
        if (file == m_file) { m_segments = segs; emit segmentsChanged(); }
    } else {
        qWarning("transcription failed for %s: %s", qPrintable(file), qPrintable(err));
        if (file == m_file) setState(QStringLiteral("error"), 0, err);
    }
    if (m_batch) ++m_batchDone;
    const bool last = m_queue.isEmpty();
    const int total = m_batchTotal, done = m_batchDone;
    m_job.clear();
    m_jobStage.clear();
    if (!m_batch || last) {
        if (m_batch) emit finished(ok, QStringLiteral("Transcribed %1 of %2 clips").arg(done).arg(total));
        else emit finished(ok, ok ? QStringLiteral("Transcript ready: %1 lines").arg(segs.size()) : err);
    }
    if (ok || file != m_file) refreshShownState();
    next();
}

void Transcript::download()
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    const QUrl url(QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-%1.bin").arg(m_model));
    m_part = new QFile(modelPath() + QStringLiteral(".part"));
    if (!m_part->open(QIODevice::WriteOnly)) {
        delete m_part; m_part = nullptr;
        m_queue.clear();
        jobDone(false, QStringLiteral("Couldn't save the speech model"), {});
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_net->get(req);
    setJobProgress(QStringLiteral("downloading"), 0);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] { if (m_part && m_reply) m_part->write(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) setJobProgress(QStringLiteral("downloading"), double(got) / total);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->deleteLater();
        if (!m_part) return;
        m_part->write(r->readAll());
        m_part->close();
        const bool ok = r->error() == QNetworkReply::NoError && m_part->size() > 50 * 1024 * 1024;
        if (!ok) {
            m_part->remove();
            delete m_part; m_part = nullptr;
            const QString why = QStringLiteral("Couldn't download the speech model (%1)").arg(r->errorString());
            m_queue.clear();
            jobDone(false, why, {});
            return;
        }
        QFile::remove(modelPath());
        m_part->rename(modelPath());
        delete m_part; m_part = nullptr;
        extractAudio();
    });
}

void Transcript::extractAudio()
{
    // whisper wants 16 kHz mono 16-bit WAV.
    m_wav = QDir::tempPath() + QStringLiteral("/slate-%1.wav").arg(Media::cacheKey(m_job));
    auto *p = new QProcess(this);
    Media::makeBackground(p);
    m_proc = p;
    setJobProgress(QStringLiteral("preparing"), 0);
    connect(p, &QProcess::finished, this, [this, p](int code) {
        p->deleteLater();
        if (code != 0 || QFileInfo(m_wav).size() < 1000) {
            QFile::remove(m_wav);
            m_wav.clear();
            jobDone(false, QStringLiteral("This file has no audio to transcribe"), {});
            return;
        }
        runWhisper();
    });
    p->start(Media::toolPath(QStringLiteral("ffmpeg")), { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                                                         QStringLiteral("-i"), m_job, QStringLiteral("-map"), QStringLiteral("0:a:0"), QStringLiteral("-ac"), QStringLiteral("1"),
                                                         QStringLiteral("-ar"), QStringLiteral("16000"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), m_wav });
}

void Transcript::runWhisper()
{
    const QString outBase = m_wav.left(m_wav.size() - 4);
    auto *p = new QProcess(this);
    Media::makeBackground(p);
    m_proc = p;
    setJobProgress(QStringLiteral("transcribing"), 0);
    connect(p, &QProcess::readyReadStandardError, this, [this, p] {
        static const QRegularExpression re(QStringLiteral("progress\\s*=\\s*(\\d+)%"));
        auto it = re.globalMatch(QString::fromUtf8(p->readAllStandardError()));
        double pr = m_jobProgress;
        while (it.hasNext()) pr = it.next().captured(1).toDouble() / 100.0;
        setJobProgress(QStringLiteral("transcribing"), pr);
    });
    connect(p, &QProcess::finished, this, [this, p, outBase](int code) {
        p->deleteLater();
        QFile::remove(m_wav);
        m_wav.clear();
        QFile json(outBase + QStringLiteral(".json"));
        QVariantList segs;
        QString err = code == 0 && json.open(QIODevice::ReadOnly) ? parseWhisperJson(json.readAll(), &segs) : QStringLiteral("Transcription failed");
        json.close();
        json.remove();
        jobDone(err.isEmpty(), err, segs);
    });
    const int threads = qBound(1, QThread::idealThreadCount() - 1, 8);
    p->start(Media::toolPath(QStringLiteral("whisper-cli")),
             { QStringLiteral("-m"), modelPath(), QStringLiteral("-f"), m_wav, QStringLiteral("-oj"), QStringLiteral("-of"), outBase,
               QStringLiteral("-t"), QString::number(threads), QStringLiteral("-pp"), QStringLiteral("-np"),
               QStringLiteral("-l"), m_model.endsWith(QLatin1String(".en")) ? QStringLiteral("en") : QStringLiteral("auto") });
}

QString Transcript::parseWhisperJson(const QByteArray &json, QVariantList *out)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError)
        return QStringLiteral("Couldn't read the transcript");
    out->clear();
    for (const QJsonValue &v : doc.object().value(QStringLiteral("transcription")).toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonObject off = o.value(QStringLiteral("offsets")).toObject();
        const QString text = o.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty() || text == QLatin1String("[BLANK_AUDIO]")) continue;
        QVariantMap seg;
        seg.insert(QStringLiteral("start"), off.value(QStringLiteral("from")).toDouble() / 1000.0);
        seg.insert(QStringLiteral("end"), off.value(QStringLiteral("to")).toDouble() / 1000.0);
        seg.insert(QStringLiteral("text"), text);
        out->append(seg);
    }
    return {};
}

int Transcript::indexAt(double t) const
{
    int lo = 0, hi = int(m_segments.size()) - 1, found = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (m_segments[mid].toMap().value(QStringLiteral("start")).toDouble() <= t) { found = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return found;
}

QVariantList Transcript::search(const QStringList &files, const QString &query) const
{
    QVariantList out;
    const QStringList words = query.toLower().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (words.isEmpty()) return out;
    for (const QString &f : files) {
        for (const QVariant &v : segmentsFor(f)) {
            const QVariantMap m = v.toMap();
            const QString text = m.value(QStringLiteral("text")).toString();
            const QString low = text.toLower();
            bool all = true;
            for (const QString &w : words) if (!low.contains(w)) { all = false; break; }
            if (!all) continue;
            out.append(QVariantMap{ { "file", f }, { "start", m.value(QStringLiteral("start")) }, { "end", m.value(QStringLiteral("end")) }, { "text", text } });
            if (out.size() >= 500) return out;
        }
    }
    return out;
}

QVariantList Transcript::repeats(const QStringList &files) const
{
    QVariantList lines;
    for (const QString &f : files)
        for (const QVariant &v : segmentsFor(f)) {
            QVariantMap m = v.toMap();
            m.insert(QStringLiteral("file"), f);
            lines.append(m);
        }
    return findRepeats(lines);
}

static QStringList wordsOf(const QString &text)
{
    static const QRegularExpression nonWord(QStringLiteral("[^\\p{L}\\p{N}']+"));
    static const QSet<QString> fillers = { QStringLiteral("um"), QStringLiteral("uh"), QStringLiteral("erm"), QStringLiteral("er"), QStringLiteral("ah"), QStringLiteral("hmm"), QStringLiteral("mm") };
    QStringList out;
    for (const QString &w : text.toLower().split(nonWord, Qt::SkipEmptyParts))
        if (!fillers.contains(w)) out << w;
    return out;
}

static int lcs(const QVector<int> &a, const QVector<int> &b)
{
    QVector<int> prev(b.size() + 1, 0), cur(b.size() + 1, 0);
    for (int i = 1; i <= a.size(); ++i) {
        for (int j = 1; j <= b.size(); ++j)
            cur[j] = a[i - 1] == b[j - 1] ? prev[j - 1] + 1 : qMax(prev[j], cur[j - 1]);
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

QVariantList Transcript::findRepeats(const QVariantList &lines)
{
    // Words as ids, so comparing two lines is cheap.
    QHash<QString, int> ids;
    QVector<QVector<int>> seq(lines.size());
    QVector<QSet<int>> sets(lines.size());
    for (int i = 0; i < lines.size(); ++i) {
        for (const QString &w : wordsOf(lines[i].toMap().value(QStringLiteral("text")).toString())) {
            auto it = ids.find(w);
            if (it == ids.end()) it = ids.insert(w, ids.size());
            seq[i].append(*it);
        }
        sets[i] = QSet<int>(seq[i].begin(), seq[i].end());
    }
    QVector<int> parent(lines.size());
    for (int i = 0; i < parent.size(); ++i) parent[i] = i;
    std::function<int(int)> root = [&](int x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
    for (int i = 0; i < lines.size(); ++i) {
        if (seq[i].size() < 4) continue;
        for (int j = i + 1; j < lines.size(); ++j) {
            if (seq[j].size() < 4) continue;
            // cheap filter: enough words in common
            int common = 0;
            const QSet<int> &small = sets[i].size() < sets[j].size() ? sets[i] : sets[j];
            const QSet<int> &big = sets[i].size() < sets[j].size() ? sets[j] : sets[i];
            for (int w : small) if (big.contains(w)) ++common;
            if (common < 3 || common * 2 < small.size()) continue;
            const int l = lcs(seq[i], seq[j]);
            const int mn = qMin(seq[i].size(), seq[j].size()), tot = seq[i].size() + seq[j].size();
            const bool same = 2.0 * l / tot >= 0.6;               // the same line, said again
            const bool abandoned = mn >= 4 && double(l) / mn >= 0.8; // a take that stopped part-way
            if (same || abandoned) parent[root(i)] = root(j);
        }
    }
    QHash<int, QVector<int>> groups;
    for (int i = 0; i < lines.size(); ++i) if (seq[i].size() >= 4) groups[root(i)].append(i);
    QVector<QVector<int>> list;
    for (auto it = groups.begin(); it != groups.end(); ++it) if (it->size() >= 2) list.append(*it);
    std::sort(list.begin(), list.end(), [](const QVector<int> &a, const QVector<int> &b) { return a.first() < b.first(); });
    QVariantList out;
    for (const QVector<int> &g : list) {
        int longest = 0;
        for (int i : g) longest = qMax(longest, int(seq[i].size()));
        int keeper = g.last();
        for (int k = g.size() - 1; k >= 0; --k) if (seq[g[k]].size() >= 0.8 * longest) { keeper = g[k]; break; }
        QVariantList members;
        for (int i : g) {
            QVariantMap m = lines[i].toMap();
            m.insert(QStringLiteral("keeper"), i == keeper);
            m.insert(QStringLiteral("complete"), seq[i].size() >= 0.8 * longest);
            members.append(m);
        }
        out.append(QVariantMap{ { "text", lines[keeper].toMap().value(QStringLiteral("text")) }, { "members", members } });
    }
    return out;
}

QString Transcript::toSrt(const QVariantList &segments)
{
    auto ts = [](double s) {
        const qint64 ms = qint64(std::llround(s * 1000));
        return QStringLiteral("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QLatin1Char('0')).arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
            .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0'));
    };
    QString out;
    int n = 1;
    for (const QVariant &v : segments) {
        const QVariantMap m = v.toMap();
        out += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(n++).arg(ts(m.value(QStringLiteral("start")).toDouble()),
                                                           ts(m.value(QStringLiteral("end")).toDouble()), m.value(QStringLiteral("text")).toString());
    }
    return out;
}

QString Transcript::exportAs(const QString &format)
{
    if (m_segments.isEmpty() || m_file.isEmpty()) return {};
    const QFileInfo fi(m_file);
    QString path = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + (format == QLatin1String("srt") ? QStringLiteral(".srt") : QStringLiteral(".txt"));
    QString text;
    if (format == QLatin1String("srt")) text = toSrt(m_segments);
    else for (const QVariant &v : m_segments) text += v.toMap().value(QStringLiteral("text")).toString() + QLatin1Char('\n');
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(text.toUtf8());
    return f.commit() ? path : QString();
}

#include "tempo.h"
#include "media.h"
#include "reviewstore.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>
#include <QThread>
#include <QUrl>
#include <cmath>
#include <complex>
#include <cstring>

static constexpr double kPi = 3.14159265358979323846;

Tempo::Tempo(QObject *parent) : QObject(parent) {}

Tempo *Tempo::instance()
{
    static Tempo *t = new Tempo();
    return t;
}

// ---- detection ---------------------------------------------------------------------------

static void fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * float(kPi) / float(len);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Mean of the onset envelope at phase, phase + period, phase + 2 period... (linear interpolation)
static double comb(const std::vector<float> &e, double period, double phase)
{
    double sum = 0;
    int n = 0;
    for (double x = phase; x < e.size() - 1; x += period) {
        const int i = int(x);
        const double f = x - i;
        sum += e[i] * (1 - f) + e[i + 1] * f;
        ++n;
    }
    return n ? sum / n : 0;
}

double Tempo::detectTempo(const QVector<float> &mono, int sr, double *first, double *confidence)
{
    if (first) *first = 0;
    if (confidence) *confidence = 0;
    const int N = 1024, hop = 128;
    if (sr <= 0 || mono.size() < N * 8) return 0;
    const double hopSec = double(hop) / sr;
    const int frames = (mono.size() - N) / hop;

    // 1. Onset strength: how much louder each frequency got since the last frame (spectral flux).
    std::vector<float> win(N);
    for (int i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * std::cos(2.0f * float(kPi) * i / (N - 1));
    std::vector<float> prev(N / 2, 0.0f), cur(N / 2), prevLin(N / 2, 0.0f), curLin(N / 2);
    std::vector<float> env(frames, 0.0f), envLow(frames, 0.0f);
    const int lowBins = qMax(2, int(200.0 * N / sr)); // below about 200 Hz: kick drums and bass
    std::vector<std::complex<float>> buf(N);
    for (int f = 0; f < frames; ++f) {
        const float *s = mono.constData() + qsizetype(f) * hop;
        for (int i = 0; i < N; ++i) buf[i] = std::complex<float>(s[i] * win[i], 0);
        fft(buf);
        float flux = 0, low = 0;
        for (int k = 1; k < N / 2; ++k) {
            curLin[k] = std::abs(buf[k]);
            cur[k] = std::log1p(100.0f * curLin[k]);
            const float d = cur[k] - prev[k];
            if (d > 0 && f > 0) flux += d;
            // low end on a linear scale, so a strong kick outweighs the faint low end of a hi-hat
            if (k <= lowBins && f > 0 && curLin[k] > prevLin[k]) low += curLin[k] - prevLin[k];
        }
        std::swap(prev, cur);
        std::swap(prevLin, curLin);
        env[f] = flux;
        envLow[f] = low;
    }
    // 2. Remove the slow trend (about half a second), keep only the rises.
    const int half = int(0.25 / hopSec);
    auto detrend = [&](const std::vector<float> &in) {
        std::vector<float> out(frames, 0.0f);
        double run = 0;
        int lo = 0, hi = -1;
        for (int i = 0; i < frames; ++i) {
            while (hi < qMin(frames - 1, i + half)) run += in[++hi];
            while (lo < i - half) run -= in[lo++];
            const double mean = run / (hi - lo + 1);
            out[i] = float(qMax(0.0, in[i] - mean));
        }
        return out;
    };
    const std::vector<float> e = detrend(env), eLow = detrend(envLow);
    double total = 0;
    for (float v : e) total += v;
    const double meanE = total / frames;
    if (meanE <= 1e-9) return 0;

    // 3. Autocorrelation over 60 to 200 BPM, gently preferring tempos near 120.
    const int lagMin = int(std::floor(60.0 / (200.0 * hopSec))), lagMax = int(std::ceil(60.0 / (60.0 * hopSec)));
    std::vector<double> r(lagMax + 2, 0.0);
    for (int l = lagMin - 1; l <= lagMax + 1; ++l) {
        double s = 0;
        for (int i = 0; i + l < frames; ++i) s += double(e[i]) * e[i + l];
        r[l] = s / (frames - l);
    }
    int bestLag = lagMin;
    double bestScore = -1;
    for (int l = lagMin; l <= lagMax; ++l) {
        const double bpm = 60.0 / (l * hopSec);
        const double w = std::exp(-0.5 * std::pow(std::log2(bpm / 120.0) / 0.9, 2));
        const double sc = r[l] * w;
        if (sc > bestScore) { bestScore = sc; bestLag = l; }
    }
    // Half-time check: a slow answer whose double is also strongly periodic is usually the
    // double (drum and bass at 136 shows up as 68, a 130 groove as 65). Below 80 BPM, take
    // the faster tempo when its own beat is at least 40% as strong. Halve is a click away.
    if (60.0 / (bestLag * hopSec) < 80.0) {
        const double hl = bestLag / 2.0;
        const int h0 = int(std::floor(hl));
        const double fr = hl - h0;
        const double rh = r[h0] * (1 - fr) + r[h0 + 1] * fr;
        if (h0 >= lagMin - 1 && rh >= 0.4 * r[bestLag]) {
            int l2 = h0;
            for (int l = qMax(lagMin, h0 - 1); l <= h0 + 2; ++l) if (r[l] > r[l2]) l2 = l;
            bestLag = l2;
        }
    }
    double lag = bestLag;
    {
        const double a = r[bestLag - 1], b = r[bestLag], c = r[bestLag + 1];
        const double den = a - 2 * b + c;
        if (std::fabs(den) > 1e-12) lag += qBound(-0.5, 0.5 * (a - c) / den, 0.5);
    }
    double bpm = 60.0 / (lag * hopSec);

    // 4. Fine search: the BPM and phase whose beats land on the strongest onsets.
    auto bestPhase = [&](double b, double *score) {
        const double period = 60.0 / (b * hopSec);
        double best = -1, bestP = 0;
        const int steps = 48;
        for (int k = 0; k < steps; ++k) {
            const double p = period * k / steps;
            const double c = comb(e, period, p);
            if (c > best) { best = c; bestP = p; }
        }
        // refine around the best phase
        for (double d = -period / steps; d <= period / steps; d += period / (steps * 8)) {
            const double p = bestP + d;
            if (p < 0) continue;
            const double c = comb(e, period, p);
            if (c > best) { best = c; bestP = p; }
        }
        *score = best;
        return bestP;
    };
    double bestComb = -1, phase = 0;
    for (double b = bpm * 0.985; b <= bpm * 1.015; b += 0.02) {
        double sc;
        const double p = bestPhase(b, &sc);
        if (sc > bestComb) { bestComb = sc; phase = p; bpm = b; }
    }
    // Most music sits on a whole BPM: prefer it when it fits about as well.
    const double whole = std::round(bpm);
    if (std::fabs(whole - bpm) < 0.15) {
        double sc;
        const double p = bestPhase(whole, &sc);
        if (sc >= bestComb * 0.97) { bpm = whole; phase = p; bestComb = sc; }
    }
    // Hi-hats on the off-beats can pull the phase half a beat off. The beat is where the low end hits.
    {
        const double period = 60.0 / (bpm * hopSec);
        const double on = comb(eLow, period, phase), off = comb(eLow, period, phase + period / 2);
        if (off > on * 1.3) phase += period / 2;
    }
    const double periodSec = 60.0 / bpm;
    // The flux of a frame peaks once the onset is about three quarters into its window (measured on clicks).
    double t = phase * hopSec + 0.74 * double(N) / sr;
    t = std::fmod(t, periodSec);
    if (t < 0) t += periodSec;
    if (first) *first = t;
    if (confidence) *confidence = qBound(0.0, (bestComb / meanE - 1.0) / 2.5, 1.0);
    return bpm;
}

// ---- state ---------------------------------------------------------------------------------

void Tempo::load(const QString &file)
{
    if (m_proc) { m_proc->disconnect(this); m_proc->kill(); m_proc->deleteLater(); m_proc = nullptr; }
    m_file = file;
    m_error.clear();
    const QJsonObject t = ReviewStore::instance().value(file, QStringLiteral("tempo")).toObject();
    m_bpm = t.value(QStringLiteral("bpm")).toDouble();
    m_first = t.value(QStringLiteral("first")).toDouble();
    m_bar = qBound(1, t.value(QStringLiteral("bar")).toInt(4), 16);
    m_conf = t.value(QStringLiteral("confidence")).toDouble();
    m_source = m_bpm > 0 ? t.value(QStringLiteral("source")).toString() : QString();
    emit changed();
    if (m_specFile != file) {
        m_spec.clear();
        m_specFile.clear();
        if (m_specProc) { m_specProc->disconnect(this); m_specProc->kill(); m_specProc->deleteLater(); m_specProc = nullptr; }
        emit spectrogramChanged();
    }
}

void Tempo::save()
{
    if (m_file.isEmpty() || !QFileInfo(m_file).isFile()) return;
    if (m_bpm <= 0) { ReviewStore::instance().setValue(m_file, QStringLiteral("tempo"), QJsonValue()); return; }
    ReviewStore::instance().setValue(m_file, QStringLiteral("tempo"), QJsonObject{
        { "bpm", m_bpm }, { "first", m_first }, { "bar", m_bar }, { "confidence", m_conf }, { "source", m_source } });
}

void Tempo::setBpm(double bpm, const QString &source)
{
    if (!(bpm >= 20 && bpm <= 400)) return;
    m_bpm = std::round(bpm * 100) / 100;
    m_source = source;
    if (source != QLatin1String("detected")) m_conf = 0;
    setFirstBeat(m_first); // keeps it inside one beat
    save();
    emit changed();
}

void Tempo::scale(double factor)
{
    if (m_bpm <= 0) return;
    const double b = m_bpm * factor;
    if (b < 20 || b > 400) return;
    m_bpm = std::round(b * 100) / 100;
    setFirstBeat(m_first);
    save();
    emit changed();
}

void Tempo::setFirstBeat(double t)
{
    if (m_bpm <= 0) { m_first = qMax(0.0, t); return; }
    const double period = 60.0 / m_bpm;
    t = std::fmod(t, period);
    if (t < 0) t += period;
    m_first = t;
    save();
    emit changed();
}

void Tempo::setBeatsPerBar(int n)
{
    m_bar = qBound(1, n, 16);
    save();
    emit changed();
}

void Tempo::clear()
{
    m_bpm = 0; m_first = 0; m_conf = 0; m_source.clear();
    save();
    emit changed();
}

void Tempo::detect(const QString &file)
{
    if (busy() || !QFileInfo(file).isFile()) return;
    m_file = file;
    m_error.clear();
    const int sr = 11025;
    auto *p = new QProcess(this);
    Media::makeBackground(p);
    m_proc = p;
    emit changed();
    auto data = std::make_shared<QByteArray>();
    connect(p, &QProcess::readyReadStandardOutput, this, [p, data] { data->append(p->readAllStandardOutput()); });
    connect(p, &QProcess::finished, this, [this, p, data, file, sr](int code) {
        data->append(p->readAllStandardOutput());
        p->deleteLater();
        m_proc = nullptr;
        if (code != 0 || data->size() < 4 * sr * 4) {
            m_error = QStringLiteral("Not enough audio to find a tempo");
            emit changed();
            emit detected(false, m_error);
            return;
        }
        QVector<float> mono(data->size() / 4);
        memcpy(mono.data(), data->constData(), size_t(mono.size()) * 4);
        m_working = true;
        emit changed();
        QPointer<Tempo> self(this);
        QThread *th = QThread::create([self, mono, sr, file] {
            double first = 0, conf = 0;
            const double bpm = detectTempo(mono, sr, &first, &conf);
            QMetaObject::invokeMethod(Tempo::instance(), [self, bpm, first, conf, file] {
                if (!self) return;
                self->m_working = false;
                if (file != self->m_file) { emit self->changed(); return; }
                if (bpm <= 0) {
                    self->m_error = QStringLiteral("Couldn't hear a steady beat");
                    emit self->changed();
                    emit self->detected(false, self->m_error);
                    return;
                }
                self->m_bpm = std::round(bpm * 100) / 100;
                self->m_first = first;
                self->m_conf = conf;
                self->m_source = QStringLiteral("detected");
                self->save();
                emit self->changed();
                emit self->detected(true, QStringLiteral("%1 BPM").arg(self->m_bpm, 0, 'f', self->m_bpm == std::round(self->m_bpm) ? 0 : 1));
            });
        });
        connect(th, &QThread::finished, th, &QObject::deleteLater);
        th->start(QThread::LowPriority);
    });
    p->start(Media::toolPath(QStringLiteral("ffmpeg")), { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-i"), file,
                                                         QStringLiteral("-map"), QStringLiteral("0:a:0"), QStringLiteral("-t"), QStringLiteral("600"),
                                                         QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"), QString::number(sr),
                                                         QStringLiteral("-f"), QStringLiteral("f32le"), QStringLiteral("-") });
}

void Tempo::makeSpectrogram(const QString &file)
{
    if (m_specProc || !QFileInfo(file).isFile()) return;
    const QString out = Media::cacheDir() + QLatin1Char('/') + Media::cacheKey(file) + QStringLiteral(".spectrum.png");
    m_specFile = file;
    if (QFileInfo(out).size() > 0) {
        m_spec = QUrl::fromLocalFile(out).toString();
        emit spectrogramChanged();
        return;
    }
    auto *p = new QProcess(this);
    Media::makeBackground(p);
    m_specProc = p;
    emit spectrogramChanged();
    connect(p, &QProcess::finished, this, [this, p, out, file](int code) {
        p->deleteLater();
        m_specProc = nullptr;
        if (code == 0 && QFileInfo(out).size() > 0 && file == m_specFile) m_spec = QUrl::fromLocalFile(out).toString();
        else if (code != 0) qWarning("spectrogram failed: %s", qPrintable(QString::fromUtf8(p->readAllStandardError()).trimmed()));
        emit spectrogramChanged();
    });
    p->start(Media::toolPath(QStringLiteral("ffmpeg")), { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                                                         QStringLiteral("-i"), file,
                                                         QStringLiteral("-filter_complex"), QStringLiteral("[0:a:0]showspectrumpic=s=2048x512:legend=0:mode=combined:color=magma:scale=log:fscale=log[s]"),
                                                         QStringLiteral("-map"), QStringLiteral("[s]"),
                                                         QStringLiteral("-frames:v"), QStringLiteral("1"), out });
}

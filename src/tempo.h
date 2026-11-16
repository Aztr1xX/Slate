#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVector>
#include <QtQml/qqml.h>

// Music tools: tempo (BPM) and first-beat detection, a beat grid the timeline can show and snap
// to, and a spectrogram picture of the whole file. The tempo of each file is remembered with its
// review notes, so a corrected BPM or a moved first beat sticks.
class Tempo : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS // exposed to QML as the "Tempo" singleton instance in main.cpp
    Q_PROPERTY(QString file READ file NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double bpm READ bpm NOTIFY changed)
    Q_PROPERTY(double firstBeat READ firstBeat NOTIFY changed)
    Q_PROPERTY(int beatsPerBar READ beatsPerBar NOTIFY changed)
    Q_PROPERTY(double confidence READ confidence NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed) // "detected", "tapped", "typed" or ""
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString spectrogram READ spectrogram NOTIFY spectrogramChanged) // file URL or ""
    Q_PROPERTY(bool spectrogramBusy READ spectrogramBusy NOTIFY spectrogramChanged)
public:
    static Tempo *instance();

    QString file() const { return m_file; }
    bool busy() const { return m_proc != nullptr || m_working; }
    double bpm() const { return m_bpm; }
    double firstBeat() const { return m_first; }
    int beatsPerBar() const { return m_bar; }
    double confidence() const { return m_conf; }
    QString source() const { return m_source; }
    QString error() const { return m_error; }
    QString spectrogram() const { return m_spec; }
    bool spectrogramBusy() const { return m_specProc != nullptr; }

    Q_INVOKABLE void load(const QString &file);   // the remembered tempo, if any (cheap)
    Q_INVOKABLE void detect(const QString &file); // listens to the audio (up to 10 minutes)
    Q_INVOKABLE void setBpm(double bpm, const QString &source = QStringLiteral("typed"));
    Q_INVOKABLE void scale(double factor);        // 0.5 halves, 2 doubles
    Q_INVOKABLE void setFirstBeat(double t);      // keeps it within one beat of the start
    Q_INVOKABLE void setBeatsPerBar(int n);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void makeSpectrogram(const QString &file);

    // Pure: mono samples at rate sr -> BPM (0 if none found); first beat and confidence (0..1) out.
    static double detectTempo(const QVector<float> &mono, int sr, double *first, double *confidence);

signals:
    void changed();
    void spectrogramChanged();
    void detected(bool ok, const QString &message);

private:
    explicit Tempo(QObject *parent = nullptr);
    void save();

    QString m_file, m_source, m_error, m_spec, m_specFile;
    double m_bpm = 0, m_first = 0, m_conf = 0;
    int m_bar = 4;
    bool m_working = false;
    QPointer<QProcess> m_proc, m_specProc;
};

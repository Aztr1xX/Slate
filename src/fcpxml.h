#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

// Builds a Final Cut Pro XML (FCPXML 1.9) timeline that DaVinci Resolve imports with
// File > Import > Timeline. Every time is written as a whole number of frames: clip offsets and
// lengths in the timeline's frame rate, source in-points in the clip's own frame rate. Clips keep
// their embedded start timecode so Resolve links each one to the right frames.
// Pure functions, unit tested.
namespace Fcpxml {

// A frame rate as the length of one frame: num / den seconds (1001/30000 for 29.97).
struct Rate {
    qint64 num = 1, den = 30;
    double fps() const { return double(den) / double(num); }
    int nominal() const;           // 24, 25, 30, 60...
    bool operator==(const Rate &o) const { return num == o.num && den == o.den; }
};
Rate rateFor(double fps);                       // snaps to the standard rate nearest fps
QString timeString(qint64 frames, const Rate &r); // "1001/30000s", "3600s", "0s"
qint64 framesAt(double seconds, const Rate &r);   // nearest frame
double secondsOf(qint64 frames, const Rate &r);
// "01:00:00:00" or drop-frame "00:59:56;12" to a frame count at r. -1 when it doesn't parse.
qint64 timecodeToFrames(const QString &tc, const Rate &r);

struct Marker {
    double time = 0;      // seconds into the file
    double end = -1;      // > time for a range
    int kind = 0;         // 0 note, 1 good take, 2 mistake, 3 select
    QString note;
};

struct Item {
    QString path;
    QString name;              // name shown on the timeline clip
    Rate rate;                 // the clip's frame rate (audio-only: the timeline's)
    double duration = 0;       // whole file, seconds
    QString startTimecode;     // embedded source timecode, or empty
    int width = 0, height = 0;
    bool hasVideo = true, hasAudio = true;
    int channels = 2, sampleRate = 48000;
    double in = 0, out = -1;   // the part used, seconds into the file (out < 0: to the end)
    QVector<Marker> markers;   // markers to put on this clip (seconds into the file)
    QStringList keywords;
};

struct Options {
    QString title = QStringLiteral("Slate selects");
    QString event = QStringLiteral("Slate");
    Rate rate;                 // timeline frame rate
    int width = 1920, height = 1080;
    double gap = 0;            // seconds of black between clips
    double startHours = 1;     // timeline starts at 01:00:00:00
    bool clipMarkers = true;   // markers on the clips (true) or none in the XML (false)
    bool markClipStarts = false; // a marker with the clip name where each clip starts
};

struct Result {
    QString xml;
    QString timelineEdl;     // coloured markers placed on the new timeline (Resolve: Import Timeline Markers from EDL)
    double duration = 0;     // timeline length in seconds
    int clips = 0;
    int markers = 0;
};

Result build(const QVector<Item> &items, const Options &opt);

QString kindLabel(int kind);
}

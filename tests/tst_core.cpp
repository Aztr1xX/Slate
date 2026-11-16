#include "folderqueue.h"
#include "markermodel.h"
#include "timecode.h"
#include "reviewstore.h"
#include "undostack.h"
#include "fcpxml.h"
#include <QXmlStreamReader>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

static void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
static QByteArray fakeMedia(int seed, int size = 300000)
{
    QByteArray b(size, 0);
    for (int i = 0; i < size; ++i) b[i] = char((i * 31 + seed * 7) & 0xff);
    return b;
}

class TestCore : public QObject
{
    Q_OBJECT
private slots:
    void timecodeBasics()
    {
        QCOMPARE(Timecode::fromFrame(0, 30), QStringLiteral("00:00:00:00"));
        QCOMPARE(Timecode::fromFrame(29, 30), QStringLiteral("00:00:00:29"));
        QCOMPARE(Timecode::fromFrame(30, 30), QStringLiteral("00:00:01:00"));
        QCOMPARE(Timecode::fromFrame(30 * 3661 + 5, 30), QStringLiteral("01:01:01:05"));
        QCOMPARE(Timecode::fromFrame(0, 25, 25 * 3600), QStringLiteral("01:00:00:00"));
    }
    void timecodeNominalRates()
    {
        QCOMPARE(Timecode::nominalFps(29.97), 30);
        QCOMPARE(Timecode::nominalFps(23.976), 24);
        QCOMPARE(Timecode::nominalFps(59.94), 60);
        QCOMPARE(Timecode::nominalFps(0), 30);
        QCOMPARE(Timecode::nominalFps(qQNaN()), 30);
    }
    void frameAtEdges()
    {
        QCOMPARE(Timecode::frameAt(1.0, 30), qint64(30));      // no off-by-one from float error
        QCOMPARE(Timecode::frameAt(1.0 / 30 * 7, 30), qint64(7));
        QCOMPARE(Timecode::frameAt(-1, 30), qint64(0));
        QCOMPARE(Timecode::fromSeconds(2.5, 24), QStringLiteral("00:00:02:12"));
    }
    void parseFlexible()
    {
        QCOMPARE(Timecode::parseFlexible(QStringLiteral("90"), 30), 90.0);
        QCOMPARE(Timecode::parseFlexible(QStringLiteral(" 1:30 "), 30), 90.0);
        QCOMPARE(Timecode::parseFlexible(QStringLiteral("1:02:03"), 30), 3723.0);
        QCOMPARE(Timecode::parseFlexible(QStringLiteral("2,5"), 30), 2.5);
        QCOMPARE(Timecode::parseFlexible(QStringLiteral("00:00:02:15"), 30), 2.5);
        QCOMPARE(Timecode::parseFlexible(QStringLiteral("f60"), 30), 2.0);
        QCOMPARE(Timecode::parseFlexible(QStringLiteral("1:75"), 30), -1.0);
        QCOMPARE(Timecode::parseFlexible(QStringLiteral("abc"), 30), -1.0);
        QCOMPARE(Timecode::parseFlexible(QString(), 30), -1.0);
        QCOMPARE(Timecode::parseFlexible(QStringLiteral("00:00:02:40"), 30), -1.0);
    }
    void parseRoundTrip()
    {
        for (qint64 f : { 0LL, 1LL, 29LL, 30LL, 107999LL }) {
            QCOMPARE(Timecode::parse(Timecode::fromFrame(f, 30), 30), f);
        }
        QCOMPARE(Timecode::parse(QStringLiteral("00:00:00:30"), 30), qint64(-1));
        QCOMPARE(Timecode::parse(QStringLiteral("bad"), 30), qint64(-1));
    }
    void clockFormat()
    {
        QCOMPARE(Timecode::clock(0), QStringLiteral("0:00"));
        QCOMPARE(Timecode::clock(75.9), QStringLiteral("1:15"));
        QCOMPARE(Timecode::clock(3725), QStringLiteral("1:02:05"));
    }
    void naturalSort()
    {
        const QStringList in{ "part 10.mp4", "part 2.mp4", "Part 1.mp4", "clip_003.mov", "clip_01.mov" };
        const QStringList out = FolderQueue::naturalSorted(in);
        QCOMPARE(out, (QStringList{ "clip_01.mov", "clip_003.mov", "Part 1.mp4", "part 2.mp4", "part 10.mp4" }));
        QVERIFY(FolderQueue::isMediaFile("A.MKV"));
        QVERIFY(!FolderQueue::isMediaFile("notes.txt"));
    }
    void markerSortingAndPersistence()
    {
        QTemporaryDir dir;
        ReviewStore::instance().setRoot(dir.path() + "/store");
        const QString clip = dir.path() + "/clip.mp4";
        writeFile(clip, fakeMedia(1));
        MarkerModel m;
        m.loadFor(clip);
        QSignalSpy spy(&m, &MarkerModel::countChanged);
        QSignalSpy edits(&m, &MarkerModel::edited);
        m.add(20, MarkerModel::Good, "b");
        m.add(5, MarkerModel::Note, "a");
        m.add(30, MarkerModel::Mistake, "c");
        QCOMPARE(m.count(), 3);
        QCOMPARE(spy.count(), 3);
        QCOMPARE(edits.count(), 3);
        QCOMPARE(m.timeAt(0), 5.0);
        QCOMPARE(m.timeAt(2), 30.0);
        QCOMPARE(m.nextAfter(5), 20.0);
        QCOMPARE(m.prevBefore(20), 5.0);
        QCOMPARE(m.nextAfter(30), -1.0);
        m.setNote(1, "  edited  ");
        m.cycleKind(0);
        // a select
        const int row = m.add(40, MarkerModel::Select, "Intro take", 52.5);
        QCOMPARE(m.selectCount(), 1);
        QCOMPARE(m.endAt(row), 52.5);
        QCOMPARE(m.selectAt(45), row);
        QCOMPARE(m.selectAt(53), -1);
        // dragging the start of the select earlier than the others reorders it, as one step
        const int id = m.idAt(row);
        m.beginGesture();
        m.setRange(m.rowOfId(id), 30, 52.5);
        m.setRange(m.rowOfId(id), 2, 52.5);
        const int before = edits.count();
        m.endGesture("move select");
        QCOMPARE(edits.count(), before + 1);
        QCOMPARE(m.rowOfId(id), 0);
        QCOMPARE(m.selects().size(), 1);

        ReviewStore::instance().flush();
        MarkerModel m2;
        m2.loadFor(clip);
        QCOMPARE(m2.count(), 4);
        QCOMPARE(m2.items()[0].end, 52.5);
        QCOMPARE(m2.items()[2].note, QStringLiteral("edited"));
        QCOMPARE(m2.items()[1].kind, int(MarkerModel::Good));
        while (m2.count()) m2.remove(0);
        QVERIFY(ReviewStore::instance().record(clip).value("markers").toArray().isEmpty());
    }
    void reviewSurvivesMovesAndRenames()
    {
        QTemporaryDir dir;
        ReviewStore &st = ReviewStore::instance();
        st.setRoot(dir.path() + "/store");
        st.setSidecarEnabled(true);
        const QString a = dir.path() + "/shoot/take 1.mp4";
        writeFile(a, fakeMedia(2));
        const QString fp = ReviewStore::computeFingerprint(a);
        QVERIFY(!fp.isEmpty());
        QCOMPARE(fp, st.fingerprintFor(a));
        st.setValue(a, "rating", 4);
        st.setValue(a, "markers", QJsonArray{ QJsonObject{ { "time", 3.0 }, { "note", "x" }, { "kind", 1 } } });
        st.flush();
        // the folder copy exists
        QVERIFY(QFile::exists(dir.path() + "/shoot/" + ReviewStore::sidecarName()));
        // move and rename the folder: same contents, new path
        QVERIFY(QDir().rename(dir.path() + "/shoot", dir.path() + "/moved"));
        const QString b = dir.path() + "/moved/renamed.mp4";
        QVERIFY(QFile::rename(dir.path() + "/moved/take 1.mp4", b));
        QCOMPARE(st.value(b, "rating").toInt(), 4);
        QCOMPARE(st.value(b, "markers").toArray().size(), 1);
        // a different file has nothing
        const QString c = dir.path() + "/moved/other.mp4";
        writeFile(c, fakeMedia(3));
        QCOMPARE(st.value(c, "rating").toInt(), 0);

        // another computer: empty store, but the folder copy brings the review back
        QTemporaryDir other;
        st.setRoot(other.path() + "/store");
        QCOMPARE(st.value(b, "rating").toInt(), 4);

        // legacy data is imported for clips with no record
        st.setRoot(other.path() + "/store2");
        st.setSidecarEnabled(false);
        st.setLegacyLoader([](const QString &) { return QJsonObject{ { "rating", 2 } }; });
        QCOMPARE(st.value(c, "rating").toInt(), 2);
        st.setLegacyLoader(nullptr);

        // Slate rewrites a file (thumbnail): the record follows the new contents
        writeFile(c, fakeMedia(9, 310000));
        st.fileRewritten(c);
        QCOMPARE(st.value(c, "rating").toInt(), 2);
        st.setSidecarEnabled(true);
    }
    void undoStack()
    {
        UndoStack u;
        u.push({ "a", "f", "rating", 0, 3 });
        u.push({ "b", "f", "rating", 3, 3 }); // no change: ignored
        u.push({ "c", "f", "inout", QJsonArray{ -1, -1 }, QJsonArray{ 1, 2 } });
        QCOMPARE(u.size(), 2);
        UndoStack::Entry e;
        QVERIFY(u.undo(&e));
        QCOMPARE(e.label, QStringLiteral("c"));
        QVERIFY(u.canRedo());
        QVERIFY(u.redo(&e));
        u.dropKey("inout");
        QCOMPARE(u.size(), 1);
        QCOMPARE(u.undoText(), QStringLiteral("a"));
    }
    void fcpxmlRatesAndTimecode()
    {
        using namespace Fcpxml;
        QCOMPARE(rateFor(29.97), (Rate{ 1001, 30000 }));
        QCOMPARE(rateFor(23.976), (Rate{ 1001, 24000 }));
        QCOMPARE(rateFor(25.0), (Rate{ 1, 25 }));
        QCOMPARE(rateFor(59.94), (Rate{ 1001, 60000 }));
        QCOMPARE(rateFor(15.0), (Rate{ 1, 15 }));
        QCOMPARE(timeString(0, Rate{ 1, 30 }), QStringLiteral("0s"));
        QCOMPARE(timeString(90, Rate{ 1, 30 }), QStringLiteral("3s"));
        QCOMPARE(timeString(45, Rate{ 1, 30 }), QStringLiteral("3/2s"));
        QCOMPARE(timeString(1, Rate{ 1001, 30000 }), QStringLiteral("1001/30000s"));
        QCOMPARE(timeString(108000, Rate{ 1001, 30000 }), QStringLiteral("18018/5s"));
        QCOMPARE(timecodeToFrames("01:00:00:00", Rate{ 1, 25 }), qint64(90000));
        QCOMPARE(timecodeToFrames("00:01:00;02", Rate{ 1001, 30000 }), qint64(1800)); // drop frame skips ;00 and ;01
        QCOMPARE(timecodeToFrames("00:10:00;00", Rate{ 1001, 30000 }), qint64(17982));
        QCOMPARE(timecodeToFrames("junk", Rate{ 1, 25 }), qint64(-1));
    }
    void fcpxmlTimeline()
    {
        using namespace Fcpxml;
        Item a;
        a.path = "/footage/take 1 & co.mp4"; a.name = "Take 1"; a.rate = rateFor(29.97); a.duration = 60;
        a.width = 3840; a.height = 2160; a.in = 10; a.out = 20; a.startTimecode = "10:00:00:00";
        a.markers = { { 12.0, -1, 1, "Nice" }, { 30.0, -1, 2, "outside" }, { 14.0, 15.0, 0, "Range" } };
        a.keywords = { "4 stars" };
        Item b;
        b.path = "/footage/take 2.mov"; b.name = "Take 2"; b.rate = rateFor(25); b.duration = 8; b.width = 1920; b.height = 1080;
        b.in = 7.4; b.out = 100; // past the end: clamped
        Options o;
        o.rate = rateFor(30); o.gap = 1; o.title = "My <selects>";
        const Result r = build({ a, b }, o);
        QCOMPARE(r.clips, 2);
        QCOMPARE(r.markers, 2);
        // 10 s of 29.97 footage is 299 frames once snapped to whole frames, + 30 frames of gap + 18 frames (0.6 s)
        QVERIFY2(qAbs(r.duration - 347.0 / 30) < 1e-9, qPrintable(QString::number(r.duration * 30)));
        QVERIFY(r.xml.contains("<fcpxml version=\"1.9\">"));
        QVERIFY(r.xml.contains("tcStart=\"3600s\""));
        QVERIFY(r.xml.contains("My &lt;selects&gt;"));
        QVERIFY(r.xml.contains("src=\"file:///footage/take%201%20&amp;%20co.mp4\""));
        // source start = 10:00:00:00 at 30 NDF + 10 s of 29.97 frames (300 frames)
        QVERIFY(r.xml.contains(QStringLiteral("start=\"%1\"").arg(timeString(36000 * 30 + 300, Rate{ 1001, 30000 }))));
        QVERIFY(r.xml.contains("value=\"Good take: Nice\""));
        QVERIFY(!r.xml.contains("outside"));
        QVERIFY(r.xml.contains("<keyword"));
        QVERIFY(r.xml.contains(QStringLiteral("<gap name=\"Gap\" offset=\"%1\"").arg(timeString(108000 + 299, Rate{ 1, 30 }))));
        // second clip starts after 299 + 30 frames, and its in-point is a whole 25 fps frame
        QVERIFY(r.xml.contains(QStringLiteral("offset=\"%1\"").arg(timeString(108000 + 329, Rate{ 1, 30 }))));
        QVERIFY(r.xml.contains("start=\"37/5s\""));
        // well-formed
        QXmlStreamReader x(r.xml);
        int clips = 0;
        while (!x.atEnd()) { x.readNext(); if (x.isStartElement() && x.name() == QLatin1String("asset-clip")) ++clips; }
        QVERIFY2(!x.hasError(), qPrintable(x.errorString()));
        QCOMPARE(clips, 2);
        // coloured markers on the new timeline: the good take lands 2 s in
        QVERIFY(r.timelineEdl.contains("01:00:02:00"));
        QVERIFY(r.timelineEdl.contains("ResolveColorGreen"));
        // without clip markers the XML has none, but the EDL still does
        o.clipMarkers = false;
        const Result r2 = build({ a }, o);
        QVERIFY(!r2.xml.contains("<marker"));
        QVERIFY(!r2.timelineEdl.isEmpty());
    }
    void edlExport()
    {
        QVector<MarkerModel::Marker> items{ { 1, 12.0, "Good | take", 1 }, { 2, 27.5, "Line\nbreak", 0 } };
        const QString edl = MarkerModel::toEdl(items, 30, "clip.mp4", 30 * 3600);
        QVERIFY(edl.startsWith("TITLE: clip.mp4\nFCM: NON-DROP FRAME\n\n"));
        QVERIFY(edl.contains("001  001      V     C        01:00:12:00 01:00:12:01 01:00:12:00 01:00:12:01"));
        QVERIFY(edl.contains(" |C:ResolveColorGreen |M:Good / take |D:1"));
        QVERIFY(edl.contains("002  001      V     C        01:00:27:15"));
        QVERIFY(edl.contains("|M:Line break |D:1"));
        // a select keeps its length
        MarkerModel::Marker sel{ 3, 2.0, "Keep", 3, 4.5 };
        const QString edl2 = MarkerModel::toEdl({ sel }, 30, "clip.mp4", 30 * 3600);
        QVERIFY(edl2.contains("01:00:02:00 01:00:04:15 01:00:02:00 01:00:04:15"));
        QVERIFY(edl2.contains(" |C:ResolveColorBlue |M:Keep |D:75"));
    }
    void csvExport()
    {
        QVector<MarkerModel::Marker> items{ { 1, 1.5, "Say \"hi\", then go", 2 } };
        const QString csv = MarkerModel::toCsv(items, 25);
        QCOMPARE(csv, QStringLiteral("timecode,seconds,end_timecode,end_seconds,kind,note\n00:00:01:12,1.500,,,mistake,\"Say \"\"hi\"\", then go\"\n"));
        const QString csv2 = MarkerModel::toCsv({ MarkerModel::Marker{ 1, 1.0, "Sel", 3, 2.0 } }, 25);
        QVERIFY(csv2.endsWith("00:00:01:00,1.000,00:00:02:00,2.000,select,\"Sel\"\n"));
    }
    void youtubeChapters()
    {
        using M = MarkerModel::Marker;
        // too few
        auto c = MarkerModel::toYouTubeChapters({ M{ 1, 30, "Only one", 0 } }, 300);
        QVERIFY(!c.error.isEmpty());
        // valid: adds 0:00 Intro, skips one under 10 s apart and one in the last 10 s
        c = MarkerModel::toYouTubeChapters({ M{ 1, 15, "Setup", 0 }, M{ 2, 20, "Too close", 0 }, M{ 3, 75, "Desk tour", 0 }, M{ 4, 295, "End", 0 } }, 300);
        QVERIFY(c.error.isEmpty());
        QCOMPARE(c.text, QStringLiteral("0:00 Intro\n0:15 Setup\n1:15 Desk tour"));
        QCOMPARE(c.skipped, 2);
        // a marker at the very start names the first chapter
        c = MarkerModel::toYouTubeChapters({ M{ 1, 0, "Cold open", 0 }, M{ 2, 12, "A", 0 }, M{ 3, 40, "B", 0 } }, 100);
        QCOMPARE(c.text.section('\n', 0, 0), QStringLiteral("0:00 Cold open"));
        QCOMPARE(c.count, 3);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"

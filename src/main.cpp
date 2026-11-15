#include "appcontroller.h"
#include "mpvobject.h"
#include "media.h"
#include "transcript.h"
#include "updater.h"
#include "downloader.h"
#include "icons.h"
#include "visuals.h"
#include <QTest>

#include <QFont>
#include <QFontDatabase>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QClipboard>
#include <QImage>
#include <QColor>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QProcess>
#include <clocale>
#include <cstdio>
#include <functional>

// ---------------------------------------------------------------------------
// --selftest: drives the real UI and engine through the main review workflow,
// prints PASS/FAIL for each check, saves a screenshot, and exits non-zero on failure.
// ---------------------------------------------------------------------------
class SelfTest : public QObject
{
public:
    SelfTest(QQuickWindow *w, MpvObject *mpv, AppController *app, QString file, QString shot)
        : m_win(w), m_mpv(mpv), m_app(app), m_file(std::move(file)), m_shot(std::move(shot)) {}

    void start()
    {
        if (!m_shot.isEmpty()) QFile::remove(m_shot + QStringLiteral(".log"));
        step("open file", [this] {
            QMetaObject::invokeMethod(m_win, "openMedia", Q_ARG(QVariant, QVariant(m_file)));
        }, [this] { return m_mpv->loaded() && m_mpv->duration() > 0 && m_mpv->fps() > 0; });
        step("pause", [this] { m_mpv->setPaused(true); }, [this] { return m_mpv->paused(); });
        step("update version compare", [] {}, [] {
            return Updater::isNewer(QStringLiteral("1.1.12"), QStringLiteral("1.1.9")) && !Updater::isNewer(QStringLiteral("v1.1.9"), QStringLiteral("1.1.9"))
                && Updater::isNewer(QStringLiteral("v1.2.0"), QStringLiteral("1.1.40")) && !Updater::isNewer(QStringLiteral("latest"), QStringLiteral("1.1.3"));
        });
#ifdef Q_OS_WIN
        step("update check reaches GitHub", [] { Updater::instance()->check(false); }, [] {
            const QString st = Updater::instance()->state();
            if (st == QLatin1String("current") || st == QLatin1String("available")) {
                std::printf("      latest release: %s (%s)\n", qPrintable(Updater::instance()->latestVersion()), qPrintable(st));
                return true;
            }
            return false;
        }, 20000);
#endif
#ifdef Q_OS_WIN
        step("bundled tools present (ffmpeg, ffprobe)", [] {}, [] {
            std::printf("      whisper-cli bundled: %s\n", Transcript::instance()->available() ? "yes" : "no");
            return !Media::toolPath(QStringLiteral("ffmpeg")).isEmpty() && !Media::toolPath(QStringLiteral("ffprobe")).isEmpty();
        });
#endif
        step("downloader: format plans", [] {}, [] {
            const auto mkv = Downloader::plan(QStringLiteral("video"), QStringLiteral("mkv"), 0, false);
            const auto mp4 = Downloader::plan(QStringLiteral("video"), QStringLiteral("mp4"), 1080, false);
            const auto mov = Downloader::plan(QStringLiteral("video"), QStringLiteral("mov"), 0, false);
            const auto edit = Downloader::plan(QStringLiteral("video"), QStringLiteral("mp4"), 0, true);
            const auto mp3 = Downloader::plan(QStringLiteral("audio"), QStringLiteral("mp3"), 0, false);
            const auto ogg = Downloader::plan(QStringLiteral("audio"), QStringLiteral("ogg"), 0, false);
            return mkv.args.value(1).startsWith(QLatin1String("bv*+ba/")) && mkv.convert.isEmpty() && mkv.finalExt == QLatin1String("mkv")
                && mp4.args.value(1).startsWith(QLatin1String("bv*[height<=1080]+ba[acodec^=mp4a]/")) && mp4.convert.isEmpty()
                && mov.convert == QLatin1String("mov") && edit.convert == QLatin1String("mp4-edit") && edit.finalExt == QLatin1String("mp4")
                && mp3.args.contains(QStringLiteral("-x")) && mp3.args.contains(QStringLiteral("320K")) && mp3.finalExt == QLatin1String("mp3")
                && ogg.args.contains(QStringLiteral("vorbis")) && ogg.finalExt == QLatin1String("ogg");
        });
        step("downloader: reads video info and progress", [] {}, [] {
            const QByteArray j = R"({"title":"Test","channel":"Chan","duration":61,"extractor_key":"Youtube",
                "thumbnails":[{"url":"https://i.ytimg.com/vi/x/hq.webp","preference":1},{"url":"https://i.ytimg.com/vi/x/maxres.jpg?a=1","preference":0,"width":1280}],
                "formats":[{"format_id":"sb0","vcodec":"none","acodec":"none","ext":"mhtml"},
                {"format_id":"251","vcodec":"none","acodec":"opus","abr":135.2,"filesize":1000000},
                {"format_id":"140","vcodec":"none","acodec":"mp4a.40.2","abr":129.5,"filesize":990000},
                {"format_id":"137","vcodec":"avc1.640028","acodec":"none","height":1080,"width":1920,"fps":30,"filesize":5000000},
                {"format_id":"248","vcodec":"vp9","acodec":"none","height":1080,"width":1920,"fps":30,"filesize":4000000},
                {"format_id":"315","vcodec":"vp9","acodec":"none","height":2160,"width":3840,"fps":60,"dynamic_range":"SDR","filesize":90000000},
                {"format_id":"18","vcodec":"avc1.42001E","acodec":"mp4a.40.2","height":360,"width":640,"fps":30}]})";
            QString err;
            QHash<QString, double> sizes;
            const QVariantMap i = Downloader::parseInfo(j, &err, &sizes);
            const QVariantList q = i.value(QStringLiteral("qualities")).toList();
            Downloader::Progress pr;
            const bool p1 = Downloader::parseProgress(QStringLiteral("SLATE|downloading|315|4500000|NA|90000000|5242880.0|17"), &pr);
            QString perr;
            Downloader::parseInfo(R"({"_type":"playlist","entries":[]})", &perr);
            return err.isEmpty() && q.size() == 3 && q[0].toMap().value(QStringLiteral("label")) == QLatin1String("2160p60")
                && q[0].toMap().value(QStringLiteral("tag")) == QLatin1String("4K") && q[1].toMap().value(QStringLiteral("codec")) == QLatin1String("VP9")
                && i.value(QStringLiteral("bestAudio")) == QLatin1String("Opus 135 kbps")
                && i.value(QStringLiteral("thumbnail")).toString().endsWith(QLatin1String("maxres.jpg?a=1")) && sizes.value(QStringLiteral("315")) == 90000000
                && p1 && pr.formatId == QLatin1String("315") && pr.total == 90000000 && pr.done == 4500000 && pr.eta == 17
                && !perr.isEmpty()
                && Downloader::friendlyError(QStringLiteral("ERROR: [youtube] abc: Private video. Sign in")).contains(QLatin1String("private"))
                && Downloader::friendlyError(QString()).isEmpty()
                && Downloader::friendlyError(QStringLiteral("ERROR: Postprocessing: Conversion failed!")).contains(QLatin1String("Conversion failed"));
        });
        if (qEnvironmentVariableIsSet("SLATE_TEST_DOWNLOAD")) {
            // Real downloads through yt-dlp: best quality MP4, an MP3, and a converted MOV.
            static QString dlDir = QDir::tempPath() + QStringLiteral("/slate-dl-test");
            QDir(dlDir).removeRecursively();
            auto *d = Downloader::instance();
            softStep("download: look up the video", [this, d] {
                m_app->setSetting(QStringLiteral("dl/openWhenDone"), false); // keep the clip under test open
                d->setFolder(dlDir);
                d->lookUp(qEnvironmentVariable("SLATE_TEST_DOWNLOAD"));
            }, [this, d] {
                if (d->state() != QLatin1String("ready")) return false;
                const QVariantMap i = d->info();
                say(QStringLiteral("      \"%1\": best %2 + %3, %4 resolutions").arg(i.value(QStringLiteral("title")).toString(), i.value(QStringLiteral("bestVideo")).toString(),
                        i.value(QStringLiteral("bestAudio")).toString()).arg(i.value(QStringLiteral("qualities")).toList().size()));
                return true;
            }, 240000);
            softStep("download: best quality MP4", [d] { d->start(QStringLiteral("video"), QStringLiteral("mp4"), 0, false); }, [this, d] {
                if (d->state() != QLatin1String("done")) return false;
                int h = 0;
                const QString s = streamsOf(d->outputFile(), &h);
                const int best = d->info().value(QStringLiteral("qualities")).toList().value(0).toMap().value(QStringLiteral("height")).toInt();
                const QString label = d->info().value(QStringLiteral("qualities")).toList().value(0).toMap().value(QStringLiteral("label")).toString();
                say(QStringLiteral("      %1: %2 (%3 MB); best on offer %4").arg(QFileInfo(d->outputFile()).fileName(), s)
                        .arg(QFileInfo(d->outputFile()).size() / 1e6, 0, 'f', 1).arg(label));
                // The saved name keeps the whole title, emoji included.
                const QString title = d->info().value(QStringLiteral("title")).toString();
                const bool named = title.contains(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")))
                    || QFileInfo(d->outputFile()).completeBaseName() == title.simplified();
                return d->outputFile().endsWith(QLatin1String(".mp4")) && h > 0 && s.contains(QLatin1String("audio")) && named
                    && (best == 0 || label.startsWith(QString::number(h)));
            }, 600000);
            softStep("download: MP3 audio", [d] { d->start(QStringLiteral("audio"), QStringLiteral("mp3"), 0, false); }, [this, d] {
                if (d->state() != QLatin1String("done")) return false;
                const QString s = streamsOf(d->outputFile());
                say(QStringLiteral("      %1: %2").arg(QFileInfo(d->outputFile()).fileName(), s));
                return d->outputFile().endsWith(QLatin1String(".mp3")) && s.contains(QLatin1String("mp3 audio"));
            }, 300000);
            softStep("download: MOV for editing (smallest size, converted)", [d] {
                const QVariantList q = d->info().value(QStringLiteral("qualities")).toList();
                d->start(QStringLiteral("video"), QStringLiteral("mov"), q.isEmpty() ? 0 : q.last().toMap().value(QStringLiteral("height")).toInt(), false);
            }, [this, d] {
                if (d->state() != QLatin1String("done")) return false;
                const QString s = streamsOf(d->outputFile());
                say(QStringLiteral("      %1: %2").arg(QFileInfo(d->outputFile()).fileName(), s));
                return d->outputFile().endsWith(QLatin1String(".mov")) && s.startsWith(QLatin1String("h264")) && s.contains(QLatin1String("aac audio"));
            }, 600000);
            if (qEnvironmentVariableIsSet("SLATE_TEST_ALL_FORMATS")) {
                const QList<QStringList> all{ { "video", "mkv" }, { "video", "webm" }, { "video", "avi" }, { "video", "mp4", "edit" },
                                              { "audio", "m4a" }, { "audio", "wav" }, { "audio", "flac" }, { "audio", "opus" }, { "audio", "ogg" } };
                for (const QStringList &c : all) {
                    softStep(QStringLiteral("download: %1 %2%3").arg(c[0], c[1], c.size() > 2 ? QStringLiteral(" for editing") : QString()),
                             [d, c] { d->start(c[0], c[1], 0, c.size() > 2); }, [this, d, c] {
                        if (d->state() != QLatin1String("done")) return false;
                        const QString s = streamsOf(d->outputFile());
                        say(QStringLiteral("      %1: %2").arg(QFileInfo(d->outputFile()).fileName(), s));
                        return d->outputFile().endsWith(QLatin1Char('.') + c[1]) && s.contains(QLatin1String("audio"))
                            && (c[0] == QLatin1String("audio") ? !s.contains(QLatin1Char('x')) : s.contains(QLatin1Char('x')))
                            && (c.size() < 3 || s.startsWith(QLatin1String("h264")));
                    }, 600000);
                }
            }
            step("download: settings restored", [this, d] { d->resetFolder(); d->reset(); m_app->setSetting(QStringLiteral("dl/openWhenDone"), true); },
                 [d] { return d->state() == QLatin1String("idle"); });
        }
        step("audio waveform analysed", [] {}, [this] {
            return Media::instance()->waveformReady() && Media::instance()->peaks().size() > m_mpv->duration() * Media::kBinsPerSecond * 0.9;
        }, 30000);
        step("timeline thumbnails", [] {}, [] { return !Media::instance()->spriteUrl().isEmpty() && Media::instance()->spriteCount() > 0; }, 60000);
        step("stream facts and frame-rate check", [] {}, [] {
            const QVariantMap p = Media::instance()->probe();
            return p.value(QStringLiteral("width")).toInt() > 0 && p.contains(QStringLiteral("vfr")) && !p.value(QStringLiteral("vfr")).toBool();
        }, 20000);
        step("loudness measured", [] {}, [] { return Media::instance()->loudness().contains(QStringLiteral("integrated")); }, 60000);
        step("audio tracks listed", [] {}, [this] { return m_mpv->audioTracks().size() >= 1 && m_mpv->audioTrack() >= 1; });
        step("video scopes", [] {}, [this] {
            const QImage f = m_mpv->grabFrame(360);
            if (f.isNull()) return false;
            for (int mode = 0; mode < 4; ++mode)
                if (ScopeItem::render(f, mode, QSize(300, 200)).isNull()) return false;
            return true;
        });
        step("rate clip", [this] { m_app->setRating(m_file, 4); }, [this] { return m_app->rating(m_file) == 4; });
        step("window active for keyboard", [this] { m_win->raise(); m_win->requestActivate(); }, [this] { return QGuiApplication::focusWindow() == m_win; }, 3000);
        step("keyboard: M adds a marker", [this] { m_app->markers()->clear(); QTest::keyClick(m_win, Qt::Key_M); },
             [this] { return m_app->markers()->count() == 1; });
        step("keyboard: remapped shortcut", [this] {
            m_app->setSetting(QStringLiteral("keys/marker"), QStringLiteral("Shift+B"));
            m_win->setProperty("keymapRevision", m_win->property("keymapRevision").toInt() + 1);
            QTimer::singleShot(100, this, [this] { QTest::keyClick(m_win, Qt::Key_B, Qt::ShiftModifier); });
        }, [this] { return m_app->markers()->count() == 2; });
        step("restore shortcut", [this] {
            m_app->setSetting(QStringLiteral("keys/marker"), QString());
            m_win->setProperty("keymapRevision", m_win->property("keymapRevision").toInt() + 1);
            m_app->markers()->clear();
        }, [this] { return m_app->markers()->count() == 0; });
        step("settings open and close", [this] { QMetaObject::invokeMethod(m_win, "openSettings", Q_ARG(QVariant, QVariant(4))); },
             [this] { return m_win->property("settingsOpen").toBool(); });
        step("settings closed", [this] { QMetaObject::invokeMethod(m_win, "closeSettings"); },
             [this] { return !m_win->property("settingsOpen").toBool(); });
        step("download dialog opens on demand", [this] { QMetaObject::invokeMethod(m_win, "openDownloads", Q_ARG(QVariant, QVariant(QString()))); },
             [this] { return m_win->property("downloadsOpen").toBool(); });
        step("download dialog closed", [this] { QMetaObject::invokeMethod(m_win, "closeDownloads"); },
             [this] { return !m_win->property("downloadsOpen").toBool(); });
        step("every side tab builds when shown", [this] {
            for (int i = 5; i >= 0; --i) QMetaObject::invokeMethod(m_win, "showTab", Q_ARG(QVariant, QVariant(i)));
        }, [this] { return m_win->findChild<ScopeItem *>() != nullptr && m_win->property("sideTab").toInt() == 0; });
        step("lighter rendering on", [this] { m_mpv->setLightRendering(true); },
             [this] { return m_mpv->getProperty(QStringLiteral("scale")).toString() == QLatin1String("bilinear"); });
        step("lighter rendering off again", [this] { m_mpv->setLightRendering(false); },
             [this] { return m_mpv->getProperty(QStringLiteral("scale")).toString() != QLatin1String("bilinear"); });
        step("go to a typed time (Ctrl+G, 0:03, Enter)", [this] {
            QTest::keyClick(m_win, Qt::Key_G, Qt::ControlModifier);
            QTimer::singleShot(150, this, [this] { type("0:03"); QTest::keyClick(m_win, Qt::Key_Return); });
        }, [this] { return qAbs(m_mpv->position() - 3.0) < 0.02; });
        step("go to a frame (f45 at 30 fps is 1.5 s)", [this] {
            QTest::keyClick(m_win, Qt::Key_G, Qt::ControlModifier);
            QTimer::singleShot(150, this, [this] { type("f45"); QTest::keyClick(m_win, Qt::Key_Return); });
        }, [this] { return qAbs(m_mpv->position() - 1.5) < 0.02; });
        step("Shift+M marks a good take", [this] { m_app->markers()->clear(); QTest::keyClick(m_win, Qt::Key_M, Qt::ShiftModifier); },
             [this] { return m_app->markers()->count() == 1 && m_app->markers()->items().first().kind == MarkerModel::Good; });
        step("volume keys", [this] { m_vol0 = m_mpv->volume(); QTest::keyClick(m_win, Qt::Key_Down); },
             [this] { return qAbs(m_mpv->volume() - qMax(0.0, m_vol0 - 5)) < 0.5; });
        step("volume back up", [this] { QTest::keyClick(m_win, Qt::Key_Up); }, [this] { return qAbs(m_mpv->volume() - m_vol0) < 0.5 || m_vol0 > 95; });
        step("transcript parser", [] {}, [] {
            QVariantList segs;
            const QByteArray j = R"({"transcription":[{"timestamps":{"from":"00:00:00,000","to":"00:00:02,500"},"offsets":{"from":0,"to":2500},"text":" Hello there."},)"
                                 R"({"offsets":{"from":2500,"to":4000},"text":" [BLANK_AUDIO]"},{"offsets":{"from":4000,"to":6200},"text":" Second line"}]})";
            const QString err = Transcript::parseWhisperJson(j, &segs);
            return err.isEmpty() && segs.size() == 2 && segs[1].toMap().value(QStringLiteral("start")).toDouble() == 4.0
                && Transcript::toSrt(segs).startsWith(QStringLiteral("1\n00:00:00,000 --> 00:00:02,500\nHello there."));
        });
        step("exact seek to 2.0 s", [this] { m_mpv->seekTo(2.0); }, [this] { return qAbs(m_mpv->position() - 2.0) < 0.02; });
        step("frame step forward", [this] { m_f0 = m_mpv->frame(); m_mpv->stepFrame(1); }, [this] { return m_mpv->frame() == m_f0 + 1; });
        step("frame step back", [this] { m_mpv->stepFrame(-1); }, [this] { return m_mpv->frame() == m_f0; });
        step("set in point", [this] { m_app->setInPoint(m_mpv->position()); }, [this] { return m_app->inPoint() > 1.9; });
        step("seek to 4.0 s and set out", [this] { m_mpv->seekTo(4.0); }, [this] { return qAbs(m_mpv->position() - 4.0) < 0.02; });
        step("out point", [this] { m_app->setOutPoint(m_mpv->position()); }, [this] { return m_app->outPoint() > 3.9; });
        step("add markers", [this] {
            m_app->markers()->clear();
            m_app->markers()->add(1.0, 1, QStringLiteral("Good slate"));
            m_app->markers()->add(12.0, 0, QStringLiteral("B-roll here"));
            m_app->markers()->add(25.0, 2, QStringLiteral("Mistake"));
        }, [this] { return m_app->markers()->count() == 3; });
        step("export EDL + CSV", [this] {
            connect(m_app, &AppController::message, this, [this](const QString &t, const QString &p) { m_lastMsg = t; m_lastPath = p; });
            m_app->exportMarkersEdl(m_mpv->fps());
        }, [this] { return m_lastPath.endsWith(QLatin1String(".edl")) && QFileInfo::exists(m_lastPath); });
        step("export CSV", [this] { m_app->exportMarkersCsv(m_mpv->fps()); },
             [this] { return m_lastPath.endsWith(QLatin1String(".csv")) && QFileInfo::exists(m_lastPath); });
        step("lossless clip export", [this] { m_lastPath.clear(); m_app->exportClip(true); },
             [this] { return !m_app->busy() && !m_lastPath.isEmpty() && QFileInfo(m_lastPath).size() > 1000; }, 20000);
        step("frame-exact clip export", [this] { m_lastPath.clear(); m_app->exportClip(false); },
             [this] { return !m_app->busy() && m_lastPath.endsWith(QLatin1String(".mp4")) && QFileInfo(m_lastPath).size() > 1000; }, 60000);
        step("YouTube chapters to clipboard", [this] {
            QGuiApplication::clipboard()->clear();
            m_app->markers()->add(10.0, 0, QStringLiteral("Setup"));
            m_app->markers()->add(20.0, 0, QStringLiteral("Desk"));
            m_app->copyYouTubeChapters(m_mpv->duration());
        }, [this] { return QGuiApplication::clipboard()->text() == QStringLiteral("0:00 Intro\n0:10 Setup\n0:20 Desk"); });
        step("frame grab", [this] { m_grab = m_app->frameGrabPath(m_mpv->frame()); m_mpv->screenshot(m_grab); },
             [this] { return QFileInfo(m_grab).size() > 1000; });
        step("A-B loop", [this] { m_mpv->setLoop(2.0, 4.0); }, [this] { return m_mpv->loopA() == 2.0; });
        step("open command palette", [this] { QMetaObject::invokeMethod(m_win, "openPalette"); },
             [this] { return m_win->property("paletteOpen").toBool(); });
        step("close palette", [this] { QMetaObject::invokeMethod(m_win, "closePalette"); },
             [this] { return !m_win->property("paletteOpen").toBool(); });
        step("play", [this] { m_mpv->setLoop(-1, -1); m_mpv->setPaused(false); m_p0 = m_mpv->position(); },
             [this] { return !m_mpv->paused() && m_mpv->position() > m_p0 + 0.3; });
        step("audio meter reads levels", [] {}, [this] { const QVariantList l = m_mpv->audioLevels(); return !l.isEmpty() && l.first().toDouble() > -100; }, 4000);
        step("listen to left channel only", [this] { m_mpv->setListen(1); }, [this] { m_p0 = m_mpv->position(); return m_mpv->listen() == 1; });
        step("still playing with channel filter", [] {}, [this] { return m_mpv->position() > m_p0 + 0.3 && !m_mpv->audioLevels().isEmpty(); }, 4000);
        step("both channels again", [this] { m_mpv->setListen(0); }, [this] { return m_mpv->listen() == 0; });
        step("zoom picture and back", [this] { QMetaObject::invokeMethod(m_win, "setZoom", Q_ARG(QVariant, 2.0), Q_ARG(QVariant, 0.1), Q_ARG(QVariant, 0.0)); },
             [this] { const bool z = m_mpv->getProperty(QStringLiteral("video-zoom")).toDouble() > 0.9;
                      if (z) QMetaObject::invokeMethod(m_win, "setZoom", Q_ARG(QVariant, 1.0), Q_ARG(QVariant, 0.0), Q_ARG(QVariant, 0.0));
                      return z; });
        step("copy frame to clipboard", [] {}, [this] { return m_mpv->copyFrame() && !QGuiApplication::clipboard()->image().isNull(); });
        step("next file in folder", [this] {
            m_next = m_app->nextInQueue(1);
            QMetaObject::invokeMethod(m_win, "openNext", Q_ARG(QVariant, QVariant(1)));
        }, [this] { return !m_next.isEmpty() && m_app->currentFile() == m_next && m_mpv->loaded(); });
        step("back to first file (as if double-clicked in Explorer)", [this] { m_app->handOver(m_file); },
             [this] { return m_app->currentFile() == m_file && m_mpv->loaded() && m_mpv->duration() > 0; });
        step("compare two clips", [this] { QMetaObject::invokeMethod(m_win, "startCompare", Q_ARG(QVariant, QVariant(m_next))); }, [this] {
            const auto all = m_win->findChildren<MpvObject *>();
            return all.size() == 2 && all[0]->loaded() && all[1]->loaded();
        }, 10000);
        step("end comparison", [this] { QMetaObject::invokeMethod(m_win, "stopCompare"); },
             [this] { return m_win->findChildren<MpvObject *>().size() == 1; });
        step("screenshot", [this] {
            m_mpv->setPaused(true);
            m_mpv->seekTo(5.0);
        }, [this] {
            if (qAbs(m_mpv->position() - 5.0) > 0.02) return false;
            if (!m_settle.isValid()) m_settle.start();
            if (m_settle.elapsed() < 800) return false; // let the new frame reach the screen
            QImage img = m_win->grabWindow();
            // The centre of the video area must not be black: proves frames are drawn.
            const QColor c = img.pixelColor(img.width() / 3, img.height() / 2);
            if (c.name() != m_lastPixel) say(QStringLiteral("      video pixel at centre: %1").arg(m_lastPixel = c.name()));
            if (c.lightness() < 8) { if (!m_shot.isEmpty()) { img.save(m_shot + QStringLiteral(".black.png")); m_mpv->screenshot(m_shot + QStringLiteral(".decoded.png")); } return false; }
            return !m_shot.isEmpty() ? img.save(m_shot) : !img.isNull();
        });
        next();
    }

private:
    void type(const char *text) { for (const char *c = text; *c; ++c) QTest::keyClick(m_win, *c); }
    // Prints, and also appends to <screenshot>.log because Windows GUI apps have no console.
    void say(const QString &line)
    {
        std::printf("%s\n", qPrintable(line));
        std::fflush(stdout);
        if (!m_shot.isEmpty()) {
            QFile f(m_shot + QStringLiteral(".log"));
            if (f.open(QIODevice::Append | QIODevice::Text))
                f.write(line.toUtf8() + '\n');
        }
    }
    struct Step { QString name; std::function<void()> act; std::function<bool()> check; int timeout; bool soft = false; };
    void step(const QString &n, std::function<void()> a, std::function<bool()> c, int timeout = 6000)
    {
        m_steps.push_back({ n, std::move(a), std::move(c), timeout });
    }
    // A step that depends on the internet (YouTube): reported as WARN, not a failure, and it gives
    // up as soon as the downloader reports an error.
    void softStep(const QString &n, std::function<void()> a, std::function<bool()> c, int timeout)
    {
        m_steps.push_back({ n, std::move(a), std::move(c), timeout, true });
    }
    // "1920x1080 vp9 / opus" for the first video and audio stream of a file.
    static QString streamsOf(const QString &file, int *height = nullptr)
    {
        QProcess p;
        p.start(Media::toolPath(QStringLiteral("ffprobe")), { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                QStringLiteral("stream=codec_type,codec_name,width,height,avg_frame_rate"), QStringLiteral("-of"), QStringLiteral("csv=p=0"), file });
        p.waitForFinished(20000);
        QStringList parts;
        for (const QString &l : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList f = l.trimmed().split(QLatin1Char(','));
            if (f.size() >= 2 && f[1] == QLatin1String("video") && f.size() >= 4) {
                parts << QStringLiteral("%1 %2x%3 %4fps").arg(f[0], f[2], f[3], f.value(4));
                if (height) *height = qMin(f[2].toInt(), f[3].toInt());
            } else if (f.size() >= 2) parts << f[0] + QLatin1Char(' ') + f[1];
        }
        return parts.join(QStringLiteral(" + "));
    }
    void next()
    {
        if (m_i >= m_steps.size()) {
            say(QStringLiteral("SELFTEST %1 (%2 failed)").arg(m_failed ? "FAILED" : "PASSED").arg(m_failed));
            QCoreApplication::exit(m_failed ? 1 : 0);
            return;
        }
        Step &s = m_steps[m_i];
        s.act();
        m_clock.start();
        poll();
    }
    void poll()
    {
        Step &s = m_steps[m_i];
        if (s.check()) {
            say(QStringLiteral("PASS  %1  (%2 ms)").arg(s.name).arg(m_clock.elapsed()));
        } else if (s.soft && (m_clock.elapsed() > s.timeout || Downloader::instance()->state() == QLatin1String("error"))) {
            say(QStringLiteral("WARN  %1  (%2 ms, not counted as a failure) %3").arg(s.name).arg(m_clock.elapsed()).arg(Downloader::instance()->error()));
        } else if (m_clock.elapsed() > s.timeout) {
            say(QStringLiteral("FAIL  %1  [pos=%2 frame=%3 fps=%4 msg=%5]").arg(s.name).arg(m_mpv->position(), 0, 'f', 3)
                    .arg(m_mpv->frame()).arg(m_mpv->fps(), 0, 'f', 3).arg(m_lastMsg));
            ++m_failed;
        } else {
            QTimer::singleShot(50, this, [this] { poll(); });
            return;
        }
        std::fflush(stdout);
        ++m_i;
        QTimer::singleShot(150, this, [this] { next(); });
    }

    QQuickWindow *m_win; MpvObject *m_mpv; AppController *m_app; QString m_file, m_shot;
    QVector<Step> m_steps; int m_i = 0, m_failed = 0; QElapsedTimer m_clock;
    qint64 m_f0 = 0; double m_p0 = 0, m_vol0 = 100; QString m_lastMsg, m_lastPath, m_grab, m_next, m_lastPixel; QElapsedTimer m_settle;
};


// ---------------------------------------------------------------------------
// --tour <dir>: opens a file, then saves a screenshot of each main screen, for design review.
// ---------------------------------------------------------------------------
class Tour : public QObject
{
public:
    Tour(QQuickWindow *w, QString file, QString dir) : m_win(w), m_file(std::move(file)), m_dir(std::move(dir)) {}
    void start()
    {
        QDir().mkpath(m_dir);
        if (!m_file.isEmpty())
            QMetaObject::invokeMethod(m_win, "openMedia", Q_ARG(QVariant, QVariant(m_file)));
        // name, action, delay before the shot
        m_shots = {
            { QStringLiteral("01-review"), [this] { call("showTab", 0); }, 6000 },
            { QStringLiteral("01a-markers-range"), [this] {
                  auto *app = AppController::instance();
                  if (app->markers()->count() == 0) {
                      app->markers()->add(3.2, 1, QStringLiteral("Clean slate, strong start"));
                      app->markers()->add(14.5, 0, QStringLiteral("Cut the cough here"));
                      app->markers()->add(26.0, 2, QStringLiteral("Fluffed the line"));
                  }
                  app->setInPoint(8.0);
                  app->setOutPoint(21.5);
                  if (auto *mpv = m_win->findChild<MpvObject *>()) { mpv->setPaused(true); mpv->seekTo(12.0); }
                  QMetaObject::invokeMethod(m_win, "setVolume", Q_ARG(QVariant, 65));
              }, 500 },
            { QStringLiteral("01a2-context-menu"), [this] {
                  QTest::mouseClick(m_win, Qt::RightButton, Qt::NoModifier, QPoint(420, 300));
              }, 700 },
            { QStringLiteral("01b-paused-volume-hover"), [this] {
                  QTest::keyClick(m_win, Qt::Key_Escape);
                  if (auto *mpv = m_win->findChild<MpvObject *>()) mpv->setPaused(true);
                  QTest::mouseMove(m_win, QPoint(m_win->width() - 94, m_win->height() - 26));
              }, 1200 },
            { QStringLiteral("01b2-go-to"), [this] { QTest::keyClick(m_win, Qt::Key_Escape); QMetaObject::invokeMethod(m_win, "beginGoTo"); }, 600 },
            { QStringLiteral("01c-crop-guide"), [this] { QTest::keyClick(m_win, Qt::Key_Escape); QMetaObject::invokeMethod(m_win, "cycleGuide"); }, 900 },
            { QStringLiteral("02-clips"), [this] { QMetaObject::invokeMethod(m_win, "cycleGuide"); QMetaObject::invokeMethod(m_win, "cycleGuide"); QMetaObject::invokeMethod(m_win, "cycleGuide"); QTest::mouseMove(m_win, QPoint(600, 300)); call("showTab", 1);
                  if (QObject *t = m_win->findChild<QObject *>(QStringLiteral("toast"))) t->setProperty("opacity", 0.0); }, 2500 },
            { QStringLiteral("03-info"), [this] { call("showTab", 2); }, 800 },
            { QStringLiteral("04-transcript"), [this] { call("showTab", 3); }, 800 },
            { QStringLiteral("05-scopes"), [this] { if (auto *mpv = m_win->findChild<MpvObject *>()) mpv->setPaused(false); call("showTab", 4); }, 1500 },
            { QStringLiteral("05a-audio"), [this] { call("showTab", 5); }, 1500 },
            { QStringLiteral("05b-zoom"), [this] { QMetaObject::invokeMethod(m_win, "toggleZoom"); }, 900 },
            { QStringLiteral("06-palette"), [this] { QMetaObject::invokeMethod(m_win, "toggleZoom"); QMetaObject::invokeMethod(m_win, "openPalette"); }, 800 },
            { QStringLiteral("07-settings"), [this] { QMetaObject::invokeMethod(m_win, "closePalette"); call("openSettings", 0); }, 800 },
            { QStringLiteral("08-shortcuts"), [this] { call("openSettings", 4); }, 800 },
            { QStringLiteral("09-windows"), [this] { call("openSettings", 5); }, 800 },
            { QStringLiteral("09b-about-updates"), [this] { if (qEnvironmentVariableIsSet("SLATE_FAKE_UPDATE")) Updater::instance()->check(false); call("openSettings", 6); }, 800 },
            { QStringLiteral("09c-download"), [this] {
                  QMetaObject::invokeMethod(m_win, "closeSettings");
                  QMetaObject::invokeMethod(m_win, "openDownloads", Q_ARG(QVariant, QVariant(qEnvironmentVariable("SLATE_TOUR_DOWNLOAD"))));
              }, 6000 },
            { QStringLiteral("09c2-download-docs"), [this] {
                  // README screenshot: show a typical link instead of the local test address
                  if (QObject *f = m_win->findChild<QObject *>(QStringLiteral("downloadLink"))) f->setProperty("text", QStringLiteral("https://www.youtube.com/watch?v=aqz-KE-bpKQ"));
                  Downloader::instance()->setFolder(QStringLiteral("C:\\Users\\You\\Downloads\\Slate"));
              }, 500 },
            { QStringLiteral("09d-download-audio"), [this] {
                  Downloader::instance()->resetFolder();
                  if (QObject *dd = m_win->findChild<QObject *>(QStringLiteral("downloadDialog"))) dd->setProperty("kind", QStringLiteral("audio"));
              }, 800 },
            { QStringLiteral("09e-download-progress"), [this] {
                  if (QObject *dd = m_win->findChild<QObject *>(QStringLiteral("downloadDialog"))) { dd->setProperty("kind", QStringLiteral("video")); QMetaObject::invokeMethod(dd, "begin"); }
              }, 2500 },
            { QStringLiteral("09f-download-header"), [this] { QMetaObject::invokeMethod(m_win, "closeDownloads"); }, 1500 },
            { QStringLiteral("10-fullscreen"), [this] { Downloader::instance()->cancel(); QMetaObject::invokeMethod(m_win, "toggleFullscreen"); }, 1500 },
        };
        next();
    }
private:
    struct Shot { QString name; std::function<void()> act; int delay; };
    void call(const char *fn, int arg) { QMetaObject::invokeMethod(m_win, fn, Q_ARG(QVariant, QVariant(arg))); }
    void next()
    {
        if (m_i >= m_shots.size()) { QCoreApplication::exit(0); return; }
        Shot &s = m_shots[m_i];
        s.act();
        QTimer::singleShot(s.delay, this, [this] {
            const QString out = m_dir + QLatin1Char('/') + m_shots[m_i].name + QStringLiteral(".png");
            m_win->grabWindow().save(out);
            std::printf("tour: %s\n", qPrintable(out));
            ++m_i;
            next();
        });
    }
    QQuickWindow *m_win; QString m_file, m_dir; QVector<Shot> m_shots; int m_i = 0;
};

// Generated by qt_add_qml_module; registers the C++ types (MpvObject, App) under "Slate".
void qml_register_types_Slate();

int main(int argc, char *argv[])
{
    // libmpv's render API draws with OpenGL; Qt 6 defaults to Direct3D on Windows.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Slate"));
    QGuiApplication::setApplicationName(QStringLiteral("Slate"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));
    std::setlocale(LC_NUMERIC, "C");

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Slate: a media player for reviewing clips"));
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument(QStringLiteral("file"), QStringLiteral("File, folder or URL to open"));
    QCommandLineOption selftest(QStringLiteral("selftest"), QStringLiteral("Run the built-in workflow test on <file>"));
    QCommandLineOption shot(QStringLiteral("screenshot"), QStringLiteral("Save a window screenshot (after the self-test, or 2.5 s after opening) and quit"), QStringLiteral("png"));
    cli.addOption(selftest);
    cli.addOption(shot);
    QCommandLineOption newWindow(QStringLiteral("new-window"), QStringLiteral("Open a separate window even if Slate is already running"));
    cli.addOption(newWindow);
    QCommandLineOption tourOpt(QStringLiteral("tour"), QStringLiteral("Save screenshots of each screen to <dir> and quit"), QStringLiteral("dir"));
    cli.addOption(tourOpt);
    cli.process(app);
    const QStringList files = cli.positionalArguments();

    // One window: a second launch (e.g. double-clicking another file in Explorer) hands
    // its file to the running Slate and exits.
    const QString serverName = QStringLiteral("slate-player-") + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    const bool single = !cli.isSet(selftest) && !cli.isSet(shot) && !cli.isSet(newWindow) && !cli.isSet(tourOpt)
        && QSettings().value(QStringLiteral("prefs/singleWindow"), true).toBool();
    if (single) {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) {
            const QString f = files.isEmpty() ? QString() : (QFileInfo::exists(files.first()) ? QFileInfo(files.first()).absoluteFilePath() : files.first());
            probe.write(f.toUtf8() + '\n');
            probe.flush();
            probe.waitForBytesWritten(1000);
            probe.disconnectFromServer();
            return 0;
        }
    }

    {
        // Simplified artwork for small sizes (title bar, taskbar), full artwork from 48 px up.
        QIcon icon;
        icon.addFile(QStringLiteral(":/Slate/assets/slate-small-32.png"), QSize(32, 32));
        icon.addFile(QStringLiteral(":/Slate/assets/slate-256.png"), QSize(256, 256));
        icon.addFile(QStringLiteral(":/Slate/assets/slate.png"), QSize(512, 512));
        app.setWindowIcon(icon);
    }
#ifdef Q_OS_WIN
    QFont uiFont(QStringLiteral("Segoe UI Variable Text"));
    if (!QFontDatabase::families().contains(uiFont.family())) uiFont.setFamily(QStringLiteral("Segoe UI"));
#else
    QFont uiFont(QFontDatabase::families().contains(QStringLiteral("Inter")) ? QStringLiteral("Inter") : QGuiApplication::font().family());
#endif
    uiFont.setPixelSize(13);
    uiFont.setHintingPreference(QFont::PreferNoHinting);
    QGuiApplication::setFont(uiFont);

    qml_register_types_Slate();
    AppController &controller = *AppController::instance();
    qmlRegisterSingletonInstance("Slate", 1, 0, "App", &controller);
    qmlRegisterSingletonInstance("Slate", 1, 0, "Media", Media::instance());
    qmlRegisterSingletonInstance("Slate", 1, 0, "Transcript", Transcript::instance());
    qmlRegisterSingletonInstance("Slate", 1, 0, "Updater", Updater::instance());
    qmlRegisterSingletonInstance("Slate", 1, 0, "Downloader", Downloader::instance());

    QLocalServer server;
    if (single) {
        QLocalServer::removeServer(serverName); // clear a stale socket left by a crash
        server.listen(serverName);
        QObject::connect(&server, &QLocalServer::newConnection, &server, [&server, &controller] {
            while (QLocalSocket *s = server.nextPendingConnection()) {
                QObject::connect(s, &QLocalSocket::readyRead, s, [s, &controller] {
                    if (!s->canReadLine()) return;
                    const QString f = QString::fromUtf8(s->readLine()).trimmed();
                    controller.handOver(f);
                    s->disconnectFromServer();
                });
                QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Slate/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    if (cli.isSet(tourOpt)) {
        auto *t = new Tour(win, files.isEmpty() ? QString() : QFileInfo(files.first()).absoluteFilePath(), cli.value(tourOpt));
        QTimer::singleShot(300, t, [t] { t->start(); });
    } else if (cli.isSet(selftest)) {
        if (files.isEmpty()) {
            std::fprintf(stderr, "--selftest needs a media file\n");
            return 3;
        }
        auto *mpv = win->findChild<MpvObject *>();
        auto *t = new SelfTest(win, mpv, &controller, QFileInfo(files.first()).absoluteFilePath(), cli.value(shot));
        QTimer::singleShot(300, t, [t] { t->start(); });
    } else {
        if (!files.isEmpty()) {
            const QString f = QFileInfo::exists(files.first()) ? QFileInfo(files.first()).absoluteFilePath() : files.first();
            QTimer::singleShot(0, win, [win, f] { QMetaObject::invokeMethod(win, "openMedia", Q_ARG(QVariant, QVariant(f))); });
        }
        if (cli.isSet(shot)) { // smoke test: open, wait, save a screenshot, quit
            const QString out = cli.value(shot);
            QTimer::singleShot(2500, win, [win, out] {
                const bool ok = win->grabWindow().save(out);
                std::printf("screenshot %s: %s\n", ok ? "saved" : "FAILED", qPrintable(out));
                QCoreApplication::exit(ok ? 0 : 1);
            });
        }
    }
    return app.exec();
}

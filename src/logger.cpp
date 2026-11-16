#include "logger.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#endif

static QMutex g_mutex;
static QtMessageHandler g_previous = nullptr;
static char g_crashNote[1024] = {0};   // paths prepared up front: nothing is allocated while crashing
#ifdef Q_OS_WIN
static wchar_t g_dumpPath[1024] = {0};
static wchar_t g_notePath[1024] = {0};
#endif

Logger::Logger(QObject *parent) : QObject(parent) {}

Logger *Logger::instance()
{
    static Logger *l = new Logger();
    return l;
}

QString Logger::logFile() const { return m_dir + QStringLiteral("/slate.log"); }

void Logger::rotate()
{
    const QFileInfo fi(logFile());
    if (fi.exists() && fi.size() > 1024 * 1024) {
        QFile::remove(m_dir + QStringLiteral("/slate.1.log"));
        QFile::rename(logFile(), m_dir + QStringLiteral("/slate.1.log"));
    }
}

void Logger::write(const QString &level, const QString &text)
{
    if (m_dir.isEmpty()) return;
    QMutexLocker lock(&g_mutex);
    static int sinceCheck = 0;
    if (++sinceCheck >= 200) { sinceCheck = 0; rotate(); }
    QFile f(logFile());
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")) + QLatin1Char(' ') + level + QLatin1Char(' ') + text + QLatin1Char('\n');
    f.write(line.toUtf8());
}

static void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *level = "info ";
    switch (type) {
    case QtDebugMsg: level = "debug"; break;
    case QtInfoMsg: level = "info "; break;
    case QtWarningMsg: level = "WARN "; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
    }
    // debug chatter stays out of the file; everything else is kept
    if (type != QtDebugMsg) {
        QString text = msg;
        if (ctx.category && std::strcmp(ctx.category, "default") != 0) text = QStringLiteral("[%1] %2").arg(QString::fromLatin1(ctx.category), msg);
        Logger::instance()->write(QString::fromLatin1(level), text);
    }
    if (g_previous) g_previous(type, ctx, msg);
    else std::fprintf(stderr, "%s\n", qPrintable(msg));
}

#ifdef Q_OS_WIN
static LONG WINAPI onCrash(EXCEPTION_POINTERS *ep)
{
    HANDLE dump = CreateFileW(g_dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpWithIndirectlyReferencedMemory, &mei, nullptr, nullptr);
        CloseHandle(dump);
    }
    HANDLE note = CreateFileW(g_notePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (note != INVALID_HANDLE_VALUE) {
        char buf[1400];
        HMODULE mod = nullptr;
        wchar_t modName[MAX_PATH] = L"?";
        void *addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
        if (addr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod))
            GetModuleFileNameW(mod, modName, MAX_PATH);
        char modA[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, modName, -1, modA, MAX_PATH, nullptr, nullptr);
        const unsigned long code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
        const unsigned long long off = mod ? (unsigned long long)((char *)addr - (char *)mod) : (unsigned long long)addr;
        const int n = _snprintf_s(buf, sizeof buf, _TRUNCATE, "%sException 0x%08lX in %s at +0x%llX\r\n", g_crashNote, code, modA, off);
        DWORD w = 0;
        if (n > 0) WriteFile(note, buf, DWORD(n), &w, nullptr);
        CloseHandle(note);
    }
    return EXCEPTION_CONTINUE_SEARCH; // let Windows Error Reporting carry on as usual
}
#endif

static void writeNoteFromSignal(const char *what)
{
    // used for abort() and std::terminate, where the minidump filter doesn't run
#ifdef Q_OS_WIN
    HANDLE note = CreateFileW(g_notePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (note != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(note, g_crashNote, DWORD(std::strlen(g_crashNote)), &w, nullptr);
        WriteFile(note, what, DWORD(std::strlen(what)), &w, nullptr);
        CloseHandle(note);
    }
#else
    const QByteArray path = (Logger::instance()->folder() + QStringLiteral("/crash.txt")).toLocal8Bit();
    if (FILE *f = std::fopen(path.constData(), "w")) { std::fputs(g_crashNote, f); std::fputs(what, f); std::fclose(f); }
#endif
}

static void onSignal(int sig)
{
    writeNoteFromSignal(sig == SIGABRT ? "Slate stopped itself after an internal error (abort)\n" : sig == SIGSEGV ? "Memory access error (SIGSEGV)\n" : "Fatal signal\n");
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void Logger::install()
{
    m_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
    if (qEnvironmentVariableIsSet("SLATE_LOG_DIR")) m_dir = qEnvironmentVariable("SLATE_LOG_DIR");
    QDir().mkpath(m_dir);
    rotate();

    // What happened last time?
    const QString note = m_dir + QStringLiteral("/crash.txt");
    const QString running = m_dir + QStringLiteral("/running");
    if (QFileInfo::exists(note)) {
        QFile f(note);
        if (f.open(QIODevice::ReadOnly)) m_crash = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
        m_crashFile = m_dir + QStringLiteral("/crash-%1.txt").arg(stamp);
        QFile::rename(note, m_crashFile);
        if (QFileInfo::exists(m_dir + QStringLiteral("/crash.dmp")))
            QFile::rename(m_dir + QStringLiteral("/crash.dmp"), m_dir + QStringLiteral("/crash-%1.dmp").arg(stamp));
        if (m_crash.isEmpty()) m_crash = QStringLiteral("Slate closed unexpectedly.");
    } else if (QFileInfo::exists(running)) {
        QFile f(running);
        QString was;
        if (f.open(QIODevice::ReadOnly)) was = QString::fromUtf8(f.readAll()).trimmed();
        m_crash = QStringLiteral("The last session didn't close normally (it may have frozen, been ended from Task Manager, or the computer lost power).\n") + was;
    }
    // Old crash files: keep the newest five of each kind
    for (const QString &pat : { QStringLiteral("crash-*.txt"), QStringLiteral("crash-*.dmp") }) {
        const QFileInfoList old = QDir(m_dir).entryInfoList({ pat }, QDir::Files, QDir::Time);
        for (int i = 5; i < old.size(); ++i) QFile::remove(old[i].absoluteFilePath());
    }

    const QString header = QStringLiteral("Slate %1 on %2 (%3), started %4\n")
                               .arg(QCoreApplication::applicationVersion(), QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                                    QDateTime::currentDateTime().toString(Qt::ISODate));
    {
        QFile f(running);
        if (f.open(QIODevice::WriteOnly)) f.write(header.toUtf8());
    }
    qstrncpy(g_crashNote, header.toUtf8().constData(), sizeof g_crashNote);
#ifdef Q_OS_WIN
    QDir::toNativeSeparators(m_dir + QStringLiteral("/crash.dmp")).toWCharArray(g_dumpPath);
    QDir::toNativeSeparators(note).toWCharArray(g_notePath);
    SetUnhandledExceptionFilter(onCrash);
#else
    std::signal(SIGSEGV, onSignal);
    std::signal(SIGBUS, onSignal);
    std::signal(SIGFPE, onSignal);
#endif
    std::signal(SIGABRT, onSignal);
    std::set_terminate([] { writeNoteFromSignal("Unhandled C++ exception (std::terminate)\n"); std::abort(); });

    g_previous = qInstallMessageHandler(handler);
    write(QStringLiteral("===="), header.trimmed());
    if (!m_crash.isEmpty()) write(QStringLiteral("WARN "), QStringLiteral("previous session: ") + m_crash.section(QLatin1Char('\n'), 0, 0));
}

void Logger::cleanExit()
{
    write(QStringLiteral("===="), QStringLiteral("closed normally"));
    QFile::remove(m_dir + QStringLiteral("/running"));
}

void Logger::openFolder() const { QDesktopServices::openUrl(QUrl::fromLocalFile(m_dir)); }
void Logger::openLog() const { QDesktopServices::openUrl(QUrl::fromLocalFile(logFile())); }

QString Logger::recentLines(int n) const
{
    QFile f(logFile());
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const qint64 size = f.size();
    f.seek(qMax<qint64>(0, size - 64 * 1024));
    QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() > n) lines = lines.mid(lines.size() - n);
    return lines.join(QLatin1Char('\n'));
}

QString Logger::details() const
{
    QString s;
    if (!m_crash.isEmpty()) s += QStringLiteral("Crash report\n") + m_crash + QStringLiteral("\n\n");
    s += QStringLiteral("Slate %1, %2\n\nRecent log\n").arg(QCoreApplication::applicationVersion(), QSysInfo::prettyProductName());
    s += recentLines(60);
    return s;
}

void Logger::copyDetails() const { QGuiApplication::clipboard()->setText(details()); }

void Logger::dismissCrash()
{
    m_crash.clear();
    emit crashChanged();
}

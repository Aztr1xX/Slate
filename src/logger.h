#pragma once
#include <QObject>
#include <QString>
#include <QtQml/qqml.h>

// Error log and crash reports.
// Every warning and error (Slate's, Qt's and mpv's) goes to <AppData>/logs/slate.log, which is
// kept under about 1 MB with one older copy (slate.1.log). If Slate crashes, a crash note (and on
// Windows a minidump) is written next to it, and the next start offers to show or copy the report.
// A start that finds the previous session never closed (killed, frozen, power cut) says so too.
class Logger : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS // exposed to QML as the "Logs" singleton instance in main.cpp
    Q_PROPERTY(QString folder READ folder CONSTANT)
    Q_PROPERTY(QString logFile READ logFile CONSTANT)
    Q_PROPERTY(QString crashReport READ crashReport NOTIFY crashChanged) // last crash, until dismissed
    Q_PROPERTY(QString crashFile READ crashFile NOTIFY crashChanged)
public:
    static Logger *instance();
    // Call first thing in main(), after the application object exists.
    void install();
    void cleanExit(); // the session ended normally

    QString folder() const { return m_dir; }
    QString logFile() const;
    QString crashReport() const { return m_crash; }
    QString crashFile() const { return m_crashFile; }

    Q_INVOKABLE void openFolder() const;
    Q_INVOKABLE void openLog() const;
    Q_INVOKABLE QString details() const;  // crash report + the end of the log, ready to paste
    Q_INVOKABLE void copyDetails() const;
    Q_INVOKABLE void dismissCrash();
    Q_INVOKABLE QString recentLines(int n) const; // the last n lines of the log

    void write(const QString &level, const QString &text);

signals:
    void crashChanged();

private:
    explicit Logger(QObject *parent = nullptr);
    void rotate();

    QString m_dir, m_crash, m_crashFile;
};

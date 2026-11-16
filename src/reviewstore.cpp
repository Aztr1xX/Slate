#include "reviewstore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

ReviewStore &ReviewStore::instance()
{
    static ReviewStore *s = new ReviewStore();
    return *s;
}

ReviewStore::ReviewStore()
{
    setRoot(QString());
}

ReviewStore::~ReviewStore()
{
    flush();
}

void ReviewStore::setRoot(const QString &dir)
{
    flush();
    m_root = dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/review") : dir;
    m_index.clear();
    m_records.clear();
    m_indexLoaded = false;
}

QString ReviewStore::computeFingerprint(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = f.size();
    if (size <= 0)
        return {};
    constexpr qint64 chunk = 64 * 1024;
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QByteArray::number(size));
    for (qint64 at : { qint64(0), qMax<qint64>(0, size / 2 - chunk / 2), qMax<qint64>(0, size - chunk) }) {
        if (!f.seek(at))
            return {};
        h.addData(f.read(chunk));
    }
    return QString::fromLatin1(h.result().toHex().left(32));
}

void ReviewStore::loadIndex()
{
    if (m_indexLoaded)
        return;
    m_indexLoaded = true;
    QFile f(m_root + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonArray a = it.value().toArray();
        if (a.size() == 3)
            m_index.insert(it.key(), { qint64(a[0].toDouble()), qint64(a[1].toDouble()), a[2].toString() });
    }
}

QString ReviewStore::fingerprintFor(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QFileInfo fi(path);
    if (!fi.isFile())
        return {};
    loadIndex();
    const QString abs = fi.absoluteFilePath();
    const qint64 size = fi.size(), mtime = fi.lastModified().toMSecsSinceEpoch();
    auto it = m_index.constFind(abs);
    if (it != m_index.constEnd() && it->size == size && it->mtime == mtime && !it->fp.isEmpty())
        return it->fp;
    const QString fp = computeFingerprint(abs);
    if (fp.isEmpty())
        return {};
    m_index.insert(abs, { size, mtime, fp });
    m_indexDirty = true;
    scheduleFlush();
    return fp;
}

QString ReviewStore::recordFile(const QString &fp) const
{
    return m_root + QLatin1Char('/') + fp + QStringLiteral(".json");
}

bool ReviewStore::isEmptyRecord(const QJsonObject &rec)
{
    for (auto it = rec.begin(); it != rec.end(); ++it) {
        const QString k = it.key();
        if (k == QLatin1String("path") || k == QLatin1String("name") || k == QLatin1String("size"))
            continue;
        const QJsonValue v = it.value();
        if (v.isArray() && v.toArray().isEmpty()) continue;
        if (v.isObject() && v.toObject().isEmpty()) continue;
        if (v.isNull() || v.isUndefined()) continue;
        if (v.isDouble() && v.toDouble() == 0 && (k == QLatin1String("rating") || k == QLatin1String("resume"))) continue;
        return false;
    }
    return true;
}

QJsonObject ReviewStore::fromSidecar(const QString &path, const QString &fp) const
{
    const QFileInfo fi(path);
    QFile f(fi.absolutePath() + QLatin1Char('/') + sidecarName());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject clips = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("clips")).toObject();
    // Same contents first (survives renames), then same name and size.
    for (auto it = clips.begin(); it != clips.end(); ++it) {
        const QJsonObject c = it.value().toObject();
        if (c.value(QStringLiteral("fingerprint")).toString() == fp)
            return c.value(QStringLiteral("review")).toObject();
    }
    const QJsonObject byName = clips.value(fi.fileName()).toObject();
    if (!byName.isEmpty() && qint64(byName.value(QStringLiteral("size")).toDouble()) == fi.size())
        return byName.value(QStringLiteral("review")).toObject();
    return {};
}

QJsonObject ReviewStore::loadRecord(const QString &fp, const QString &path)
{
    auto it = m_records.constFind(fp);
    if (it != m_records.constEnd())
        return *it;
    QJsonObject rec;
    QFile f(recordFile(fp));
    if (f.open(QIODevice::ReadOnly)) {
        rec = QJsonDocument::fromJson(f.readAll()).object();
    } else {
        rec = fromSidecar(path, fp);
        if (rec.isEmpty() && m_legacy)
            rec = m_legacy(path);
        if (!isEmptyRecord(rec)) {
            m_records.insert(fp, rec);
            setRecord(path, rec); // keep the migrated copy under its fingerprint
        }
    }
    m_records.insert(fp, rec);
    return rec;
}

QJsonObject ReviewStore::record(const QString &path)
{
    const QString fp = fingerprintFor(path);
    return fp.isEmpty() ? QJsonObject() : loadRecord(fp, path);
}

void ReviewStore::setRecord(const QString &path, const QJsonObject &recIn)
{
    const QString fp = fingerprintFor(path);
    if (fp.isEmpty())
        return;
    const QFileInfo fi(path);
    QJsonObject rec = recIn;
    rec.insert(QStringLiteral("path"), fi.absoluteFilePath());
    rec.insert(QStringLiteral("name"), fi.fileName());
    rec.insert(QStringLiteral("size"), double(fi.size()));
    m_records.insert(fp, rec);
    QDir().mkpath(m_root);
    if (isEmptyRecord(rec)) {
        QFile::remove(recordFile(fp));
    } else {
        QSaveFile f(recordFile(fp));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(rec).toJson(QJsonDocument::Indented));
            f.commit();
        }
    }
    if (m_sidecar) {
        m_dirtyFolders.insert(fi.absolutePath());
        m_folderFiles[fi.absolutePath()].insert(fi.absoluteFilePath());
        scheduleFlush();
    }
}

QJsonValue ReviewStore::value(const QString &path, const QString &key)
{
    return record(path).value(key);
}

void ReviewStore::setValue(const QString &path, const QString &key, const QJsonValue &v)
{
    QJsonObject rec = record(path);
    if (v.isNull() || v.isUndefined())
        rec.remove(key);
    else
        rec.insert(key, v);
    setRecord(path, rec);
}

void ReviewStore::fileRewritten(const QString &path)
{
    const QFileInfo fi(path);
    const QString abs = fi.absoluteFilePath();
    loadIndex();
    const QString oldFp = m_index.value(abs).fp;
    const QJsonObject rec = oldFp.isEmpty() ? QJsonObject() : loadRecord(oldFp, abs);
    m_index.remove(abs);
    const QString newFp = fingerprintFor(abs);
    if (newFp.isEmpty() || newFp == oldFp)
        return;
    if (!rec.isEmpty()) {
        setRecord(abs, rec);
        QFile::remove(recordFile(oldFp));
        m_records.remove(oldFp);
    }
}

void ReviewStore::scheduleFlush()
{
    if (!QCoreApplication::instance())
        return;
    if (!m_timer) {
        m_timer = new QTimer();
        m_timer->setSingleShot(true);
        m_timer->setInterval(1000);
        QObject::connect(m_timer, &QTimer::timeout, [this] { flush(); });
    }
    m_timer->start();
}

static void setHidden(const QString &file, bool hidden)
{
#ifdef Q_OS_WIN
    const std::wstring w = QDir::toNativeSeparators(file).toStdWString();
    const DWORD a = GetFileAttributesW(w.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return;
    SetFileAttributesW(w.c_str(), hidden ? (a | FILE_ATTRIBUTE_HIDDEN) : (a & ~DWORD(FILE_ATTRIBUTE_HIDDEN)));
#else
    Q_UNUSED(file); Q_UNUSED(hidden);
#endif
}

void ReviewStore::writeSidecar(const QString &folder)
{
    const QString file = folder + QLatin1Char('/') + sidecarName();
    QJsonObject root;
    {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly))
            root = QJsonDocument::fromJson(f.readAll()).object();
    }
    QJsonObject clips = root.value(QStringLiteral("clips")).toObject();
    for (const QString &path : m_folderFiles.value(folder)) {
        const QFileInfo fi(path);
        const QString fp = m_index.value(path).fp;
        const QJsonObject rec = m_records.value(fp);
        if (fp.isEmpty() || !fi.isFile() || isEmptyRecord(rec)) {
            clips.remove(fi.fileName());
            continue;
        }
        QJsonObject review = rec;
        review.remove(QStringLiteral("path"));
        review.remove(QStringLiteral("resume")); // where you stopped is personal, not part of the review
        clips.insert(fi.fileName(), QJsonObject{ { "fingerprint", fp }, { "size", double(fi.size()) }, { "review", review } });
    }
    m_folderFiles.remove(folder);
    if (clips.isEmpty()) {
        if (QFile::exists(file)) { setHidden(file, false); QFile::remove(file); }
        return;
    }
    root.insert(QStringLiteral("app"), QStringLiteral("Slate"));
    root.insert(QStringLiteral("about"), QStringLiteral("Markers, selects and ratings made in Slate. Safe to delete."));
    root.insert(QStringLiteral("clips"), clips);
    if (QFile::exists(file)) setHidden(file, false); // Windows won't replace a hidden file
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return; // read-only card or no permission: the main store still has it
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (f.commit()) setHidden(file, true);
}

void ReviewStore::flush()
{
    if (m_indexDirty) {
        QDir().mkpath(m_root);
        QJsonObject o;
        for (auto it = m_index.begin(); it != m_index.end(); ++it)
            o.insert(it.key(), QJsonArray{ double(it->size), double(it->mtime), it->fp });
        QSaveFile f(m_root + QStringLiteral("/index.json"));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
            if (f.commit()) m_indexDirty = false;
        }
    }
    const QSet<QString> folders = m_dirtyFolders;
    m_dirtyFolders.clear();
    for (const QString &d : folders)
        writeSidecar(d);
}

#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <functional>

class QTimer;

// Everything Slate remembers about a clip (markers, selects, rating, resume point, thumbnail
// time, tempo), keyed by a fingerprint of the file's contents instead of its path, so a review
// survives moving or renaming the folder.
//
//   fingerprint = SHA-1 of the size and three 64 KB samples (start, middle, end)
//
// Records live in <root>/<fingerprint>.json. A path index (path, size, modified time ->
// fingerprint) means each file is only read once. Optionally a hidden ".slate-review.json" in
// the clip's folder keeps a copy, so a folder copied to another drive or computer brings its
// review along.
class ReviewStore
{
public:
    static ReviewStore &instance();
    ReviewStore();
    ~ReviewStore();

    // Where records go. Empty = <AppData>/review. Tests point this at a temp folder.
    void setRoot(const QString &dir);
    QString root() const { return m_root; }

    // Also keep a copy of each record in a hidden file in the clip's folder.
    void setSidecarEnabled(bool on) { m_sidecar = on; }
    bool sidecarEnabled() const { return m_sidecar; }
    static QString sidecarName() { return QStringLiteral(".slate-review.json"); }

    // Looks for older Slate data (path-keyed markers and settings) for a clip that has no record.
    void setLegacyLoader(std::function<QJsonObject(const QString &path)> f) { m_legacy = std::move(f); }

    static QString computeFingerprint(const QString &path); // empty when the file can't be read
    QString fingerprintFor(const QString &path);             // cached

    QJsonObject record(const QString &path);
    void setRecord(const QString &path, const QJsonObject &rec);
    QJsonValue value(const QString &path, const QString &key);
    void setValue(const QString &path, const QString &key, const QJsonValue &v); // undefined/null removes

    // After Slate rewrites a file (embedding a thumbnail), carry its record over to the new contents.
    void fileRewritten(const QString &path);

    // Writes pending index and folder copies now (also happens a second after changes).
    void flush();

    static bool isEmptyRecord(const QJsonObject &rec);

private:
    struct IndexEntry { qint64 size = 0; qint64 mtime = 0; QString fp; };
    QString recordFile(const QString &fp) const;
    QJsonObject loadRecord(const QString &fp, const QString &path);
    QJsonObject fromSidecar(const QString &path, const QString &fp) const;
    void scheduleFlush();
    void loadIndex();
    void writeSidecar(const QString &folder);

    QString m_root;
    bool m_sidecar = true;
    std::function<QJsonObject(const QString &)> m_legacy;
    QHash<QString, IndexEntry> m_index;   // by absolute path
    QHash<QString, QJsonObject> m_records; // by fingerprint
    QSet<QString> m_dirtyFolders;
    QHash<QString, QSet<QString>> m_folderFiles; // folder -> paths changed since the last sidecar write
    bool m_indexDirty = false;
    bool m_indexLoaded = false;
    QTimer *m_timer = nullptr;
};

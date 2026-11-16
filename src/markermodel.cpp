#include "markermodel.h"
#include "timecode.h"
#include "reviewstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

MarkerModel::MarkerModel(QObject *parent) : QAbstractListModel(parent) {}

int MarkerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MarkerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Marker &m = m_items.at(index.row());
    switch (role) {
    case TimeRole: return m.time;
    case NoteRole: case Qt::DisplayRole: return m.note;
    case KindRole: return m.kind;
    case IdRole: return m.id;
    case EndRole: return m.end;
    case IsRangeRole: return m.isRange();
    }
    return {};
}

QHash<int, QByteArray> MarkerModel::roleNames() const
{
    return { { TimeRole, "time" }, { NoteRole, "note" }, { KindRole, "kind" }, { IdRole, "markerId" },
             { EndRole, "end" }, { IsRangeRole, "isRange" } };
}

int MarkerModel::selectCount() const
{
    int n = 0;
    for (const Marker &m : m_items) if (m.isRange()) ++n;
    return n;
}

QString MarkerModel::kindName(int k)
{
    switch (k) {
    case Good: return QStringLiteral("Good take");
    case Mistake: return QStringLiteral("Mistake");
    case Select: return QStringLiteral("Select");
    default: return QStringLiteral("Note");
    }
}

void MarkerModel::commit(const QString &label, const QJsonArray &before)
{
    ++m_rev;
    emit changed();
    save();
    if (m_gesture == 0)
        emit edited(label, before, toJson());
}

void MarkerModel::beginGesture()
{
    if (m_gesture++ == 0)
        m_gestureBefore = toJson();
}

void MarkerModel::endGesture(const QString &label)
{
    if (m_gesture == 0) return;
    if (--m_gesture == 0 && m_gestureBefore != toJson())
        emit edited(label, m_gestureBefore, toJson());
}

int MarkerModel::insertSorted(Marker m)
{
    int row = int(std::upper_bound(m_items.begin(), m_items.end(), m.time,
                                   [](double t, const Marker &x) { return t < x.time; }) - m_items.begin());
    beginInsertRows({}, row, row);
    m_items.insert(row, m);
    endInsertRows();
    return row;
}

int MarkerModel::add(double time, int kind, const QString &note, double end)
{
    if (!std::isfinite(time) || time < 0)
        time = 0;
    const QJsonArray before = toJson();
    Marker m;
    m.id = m_nextId++;
    m.time = time;
    m.end = (std::isfinite(end) && end > time) ? end : -1;
    m.kind = qBound(0, kind, 3);
    m.note = note.isEmpty() ? (m.isRange() ? QStringLiteral("Select") : QStringLiteral("Marker")) : note;
    const int row = insertSorted(m);
    emit countChanged();
    commit(m.isRange() ? QStringLiteral("add select") : QStringLiteral("add marker"), before);
    return row;
}

int MarkerModel::addMany(const QVariantList &list, const QString &label)
{
    if (list.isEmpty()) return 0;
    const QJsonArray before = toJson();
    beginResetModel();
    for (const QVariant &v : list) {
        const QVariantMap o = v.toMap();
        Marker m;
        m.id = m_nextId++;
        m.time = qMax(0.0, o.value(QStringLiteral("time")).toDouble());
        const double e = o.value(QStringLiteral("end"), -1).toDouble();
        m.end = e > m.time ? e : -1;
        m.kind = qBound(0, o.value(QStringLiteral("kind")).toInt(), 3);
        m.note = o.value(QStringLiteral("note")).toString();
        if (m.note.isEmpty()) m.note = QStringLiteral("Marker");
        m_items.push_back(m);
    }
    std::stable_sort(m_items.begin(), m_items.end(), [](const Marker &a, const Marker &b) { return a.time < b.time; });
    endResetModel();
    emit countChanged();
    commit(label, before);
    return list.size();
}

void MarkerModel::remove(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    const QJsonArray before = toJson();
    const bool range = m_items[row].isRange();
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged();
    commit(range ? QStringLiteral("delete select") : QStringLiteral("delete marker"), before);
}

void MarkerModel::setNote(int row, const QString &note)
{
    if (row < 0 || row >= m_items.size())
        return;
    const QString t = note.trimmed();
    if (t.isEmpty() || t == m_items[row].note)
        return;
    const QJsonArray before = toJson();
    m_items[row].note = t;
    emit dataChanged(index(row), index(row), { NoteRole });
    commit(QStringLiteral("edit note"), before);
}

void MarkerModel::cycleKind(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    setKind(row, (m_items[row].kind + 1) % 4);
}

void MarkerModel::setKind(int row, int kind)
{
    if (row < 0 || row >= m_items.size() || kind < 0 || kind > 3 || m_items[row].kind == kind)
        return;
    const QJsonArray before = toJson();
    m_items[row].kind = kind;
    emit dataChanged(index(row), index(row), { KindRole });
    commit(QStringLiteral("change marker kind"), before);
}

// Keeps the list sorted after a row's time changed, moving just that row.
int MarkerModel::fixOrder(int row)
{
    int to = row;
    while (to > 0 && m_items[to - 1].time > m_items[row].time) --to;
    while (to < m_items.size() - 1 && m_items[to + 1].time < m_items[row].time) ++to;
    if (to == row) return row;
    beginMoveRows({}, row, row, {}, to > row ? to + 1 : to);
    m_items.move(row, to);
    endMoveRows();
    return to;
}

int MarkerModel::setRange(int row, double start, double end)
{
    if (row < 0 || row >= m_items.size() || !std::isfinite(start))
        return row;
    Marker &m = m_items[row];
    start = qMax(0.0, start);
    const double newEnd = (std::isfinite(end) && end > start) ? end : -1;
    if (qFuzzyCompare(m.time + 1, start + 1) && qFuzzyCompare(m.end + 2, newEnd + 2))
        return row;
    const QJsonArray before = toJson();
    m.time = start;
    m.end = newEnd;
    emit dataChanged(index(row), index(row), { TimeRole, EndRole, IsRangeRole });
    const int r = fixOrder(row);
    commit(newEnd > 0 ? QStringLiteral("move select") : QStringLiteral("move marker"), before);
    return r;
}

double MarkerModel::timeAt(int row) const { return (row >= 0 && row < m_items.size()) ? m_items.at(row).time : -1; }
double MarkerModel::endAt(int row) const { return (row >= 0 && row < m_items.size()) ? m_items.at(row).end : -1; }
int MarkerModel::kindAt(int row) const { return (row >= 0 && row < m_items.size()) ? m_items.at(row).kind : -1; }
QString MarkerModel::noteAt(int row) const { return (row >= 0 && row < m_items.size()) ? m_items.at(row).note : QString(); }
int MarkerModel::idAt(int row) const { return (row >= 0 && row < m_items.size()) ? m_items.at(row).id : -1; }

int MarkerModel::rowOfId(int id) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].id == id) return i;
    return -1;
}

int MarkerModel::rowNear(double time, double tolerance) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (std::fabs(m_items[i].time - time) <= tolerance)
            return i;
    return -1;
}

int MarkerModel::selectAt(double t) const
{
    int best = -1;
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].isRange() && t >= m_items[i].time && t < m_items[i].end)
            best = i; // the latest-starting one wins when selects overlap
    return best;
}

double MarkerModel::nextAfter(double time) const
{
    for (const Marker &m : m_items)
        if (m.time > time + 1e-3)
            return m.time;
    return -1;
}

double MarkerModel::prevBefore(double time) const
{
    for (int i = m_items.size() - 1; i >= 0; --i)
        if (m_items[i].time < time - 1e-3)
            return m_items[i].time;
    return -1;
}

QVariantList MarkerModel::selects() const
{
    QVariantList out;
    for (const Marker &m : m_items)
        if (m.isRange())
            out.append(QVariantMap{ { "start", m.time }, { "end", m.end }, { "note", m.note }, { "kind", m.kind } });
    return out;
}

void MarkerModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
    ++m_rev;
    emit countChanged();
    emit changed();
}

QJsonArray MarkerModel::toJson(const QVector<Marker> &items)
{
    QJsonArray arr;
    for (const Marker &m : items) {
        QJsonObject o{ { "time", m.time }, { "note", m.note }, { "kind", m.kind } };
        if (m.isRange()) o.insert(QStringLiteral("end"), m.end);
        arr.append(o);
    }
    return arr;
}

QVector<MarkerModel::Marker> MarkerModel::fromJson(const QJsonArray &arr)
{
    QVector<Marker> out;
    int id = 1;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Marker m;
        m.id = id++;
        m.time = qMax(0.0, o.value(QStringLiteral("time")).toDouble());
        const double e = o.value(QStringLiteral("end")).toDouble(-1);
        m.end = e > m.time ? e : -1;
        m.note = o.value(QStringLiteral("note")).toString(QStringLiteral("Marker"));
        m.kind = qBound(0, o.value(QStringLiteral("kind")).toInt(), 3);
        out.push_back(m);
    }
    std::stable_sort(out.begin(), out.end(), [](const Marker &a, const Marker &b) { return a.time < b.time; });
    return out;
}

void MarkerModel::restore(const QJsonArray &arr)
{
    beginResetModel();
    m_items = fromJson(arr);
    for (Marker &m : m_items) m.id = m_nextId++;
    endResetModel();
    ++m_rev;
    emit countChanged();
    emit changed();
    save();
}

void MarkerModel::loadFor(const QString &mediaPath)
{
    m_mediaPath = mediaPath;
    beginResetModel();
    m_items.clear();
    if (!mediaPath.isEmpty()) {
        m_items = fromJson(ReviewStore::instance().value(mediaPath, QStringLiteral("markers")).toArray());
        for (Marker &m : m_items) m.id = m_nextId++;
    }
    endResetModel();
    ++m_rev;
    emit countChanged();
    emit changed();
}

bool MarkerModel::save() const
{
    if (m_mediaPath.isEmpty() || !QFileInfo(m_mediaPath).isFile())
        return false;
    ReviewStore::instance().setValue(m_mediaPath, QStringLiteral("markers"), m_items.isEmpty() ? QJsonValue() : QJsonValue(toJson()));
    return true;
}

static QString cleanNote(QString s)
{
    s.replace(QLatin1Char('|'), QLatin1Char('/'));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return s.trimmed();
}

QString MarkerModel::toEdl(const QVector<Marker> &items, double fps, const QString &title, qint64 startOffsetFrames)
{
    // CMX3600-style marker EDL in the layout DaVinci Resolve writes and reads
    // (Timelines > Import > Timeline Markers from EDL).
    static const char *colours[] = { "ResolveColorYellow", "ResolveColorGreen", "ResolveColorRed", "ResolveColorBlue" };
    QString out;
    out += QStringLiteral("TITLE: %1\nFCM: NON-DROP FRAME\n\n").arg(cleanNote(title));
    int n = 1;
    for (const Marker &m : items) {
        const qint64 f = Timecode::frameAt(m.time, Timecode::nominalFps(fps));
        const QString in = Timecode::fromFrame(f, fps, startOffsetFrames);
        const qint64 len = m.isRange() ? qMax<qint64>(1, Timecode::frameAt(m.end, Timecode::nominalFps(fps)) - f) : 1;
        const QString outTc = Timecode::fromFrame(f + len, fps, startOffsetFrames);
        out += QStringLiteral("%1  001      V     C        %2 %3 %2 %3  \n")
                   .arg(n++, 3, 10, QLatin1Char('0')).arg(in, outTc);
        out += QStringLiteral(" |C:%1 |M:%2 |D:%3\n\n").arg(QLatin1String(colours[qBound(0, m.kind, 3)]), cleanNote(m.note)).arg(len);
    }
    return out;
}

QString MarkerModel::toCsv(const QVector<Marker> &items, double fps)
{
    static const char *kinds[] = { "note", "good", "mistake", "select" };
    auto q = [](QString s) { s.replace(QLatin1Char('"'), QStringLiteral("\"\"")); return QLatin1Char('"') + s + QLatin1Char('"'); };
    QString out = QStringLiteral("timecode,seconds,end_timecode,end_seconds,kind,note\n");
    for (const Marker &m : items)
        out += QStringLiteral("%1,%2,%3,%4,%5,%6\n").arg(Timecode::fromSeconds(m.time, fps), QString::number(m.time, 'f', 3),
                                                   m.isRange() ? Timecode::fromSeconds(m.end, fps) : QString(),
                                                   m.isRange() ? QString::number(m.end, 'f', 3) : QString(),
                                                   QLatin1String(kinds[qBound(0, m.kind, 3)]), q(m.note));
    return out;
}

MarkerModel::Chapters MarkerModel::toYouTubeChapters(const QVector<Marker> &items, double duration)
{
    // YouTube rules: first timestamp 0:00, at least three, each chapter at least 10 seconds.
    Chapters c;
    struct Ch { double t; QString title; };
    QVector<Ch> out;
    QString firstTitle = QStringLiteral("Intro");
    for (const Marker &m : items)
        if (m.time < 1.0) { firstTitle = cleanNote(m.note); break; }
    out.push_back({ 0.0, firstTitle });
    for (const Marker &m : items) {
        if (m.time < 1.0)
            continue;
        const bool longEnoughBefore = m.time - out.last().t >= 10.0;
        const bool longEnoughAfter = duration <= 0 || duration - m.time >= 10.0;
        if (longEnoughBefore && longEnoughAfter)
            out.push_back({ m.time, cleanNote(m.note) });
        else
            ++c.skipped;
    }
    c.count = out.size();
    if (c.count < 3) {
        c.error = QStringLiteral("YouTube needs at least 3 chapters, each 10 seconds or longer. Add more markers.");
        return c;
    }
    QStringList lines;
    for (const Ch &ch : out)
        lines << Timecode::clock(ch.t) + QLatin1Char(' ') + ch.title;
    c.text = lines.join(QLatin1Char('\n'));
    return c;
}

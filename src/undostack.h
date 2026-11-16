#pragma once
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QVector>

// Undo / redo history for review edits. Each step records what changed (a key such as
// "markers", "rating" or "inout"), for which file, and the value before and after.
// Applying a step is up to the owner; this only keeps the history.
class UndoStack : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
    Q_PROPERTY(QString redoText READ redoText NOTIFY changed)
public:
    struct Entry { QString label, file, key; QJsonValue before, after; };
    explicit UndoStack(QObject *parent = nullptr) : QObject(parent) {}

    void push(const Entry &e)
    {
        if (e.before == e.after) return;
        m_redo.clear();
        m_undo.push_back(e);
        while (m_undo.size() > m_limit) m_undo.removeFirst();
        emit changed();
    }
    bool undo(Entry *out)
    {
        if (m_undo.isEmpty()) return false;
        *out = m_undo.takeLast();
        m_redo.push_back(*out);
        emit changed();
        return true;
    }
    bool redo(Entry *out)
    {
        if (m_redo.isEmpty()) return false;
        *out = m_redo.takeLast();
        m_undo.push_back(*out);
        emit changed();
        return true;
    }
    // Steps that only make sense while their file is open (In / Out) are dropped when it closes.
    void dropKey(const QString &key)
    {
        auto keep = [&](QVector<Entry> &v) { v.erase(std::remove_if(v.begin(), v.end(), [&](const Entry &e) { return e.key == key; }), v.end()); };
        keep(m_undo); keep(m_redo);
        emit changed();
    }
    void clear() { m_undo.clear(); m_redo.clear(); emit changed(); }
    void setLimit(int n) { m_limit = qMax(1, n); }

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString undoText() const { return m_undo.isEmpty() ? QString() : m_undo.last().label; }
    QString redoText() const { return m_redo.isEmpty() ? QString() : m_redo.last().label; }
    int size() const { return m_undo.size(); }

signals:
    void changed();

private:
    QVector<Entry> m_undo, m_redo;
    int m_limit = 100;
};

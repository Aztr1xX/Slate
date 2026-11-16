#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QString>
#include <QVector>

// Markers and selects for the open file. A marker is a point in time; a select is a range
// (end > time). Each has a kind (note, good take, mistake, select) and a note. Kept sorted by
// start time and saved through ReviewStore, keyed by the file's contents.
//
// Every change emits edited(label, before, after) once, so the app can offer undo. A drag is
// wrapped in beginGesture() / endGesture() so it becomes a single step.
class MarkerModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectCount READ selectCount NOTIFY countChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    enum Kind { Note = 0, Good = 1, Mistake = 2, Select = 3 };
    Q_ENUM(Kind)
    enum Roles { TimeRole = Qt::UserRole + 1, NoteRole, KindRole, IdRole, EndRole, IsRangeRole };

    struct Marker {
        int id = 0;
        double time = 0;
        QString note;
        int kind = Note;
        double end = -1;   // > time for a select
        bool isRange() const { return end > time; }
    };

    explicit MarkerModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_items.size(); }
    int selectCount() const;
    int revision() const { return m_rev; }

    Q_INVOKABLE int add(double time, int kind = Note, const QString &note = QString(), double end = -1);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void setNote(int row, const QString &note);
    Q_INVOKABLE void cycleKind(int row);
    Q_INVOKABLE void setKind(int row, int kind);
    // Moves a select's edges (or a marker's time when end < 0). Returns the row it ends up at.
    Q_INVOKABLE int setRange(int row, double start, double end);
    Q_INVOKABLE double timeAt(int row) const;
    Q_INVOKABLE double endAt(int row) const;
    Q_INVOKABLE int kindAt(int row) const;
    Q_INVOKABLE QString noteAt(int row) const;
    Q_INVOKABLE int rowOfId(int id) const;
    Q_INVOKABLE int idAt(int row) const;
    Q_INVOKABLE int rowNear(double time, double tolerance) const;
    // The select that contains t, or -1.
    Q_INVOKABLE int selectAt(double t) const;
    // Time of the next / previous marker or select start, or -1 when there is none.
    Q_INVOKABLE double nextAfter(double time) const;
    Q_INVOKABLE double prevBefore(double time) const;
    // Several markers in one step: [{ time, end, kind, note }].
    Q_INVOKABLE int addMany(const QVariantList &list, const QString &label);
    // Selects as [{ start, end, note, kind }] sorted by start.
    Q_INVOKABLE QVariantList selects() const;
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture(const QString &label);
    void clear();

    const QVector<Marker> &items() const { return m_items; }

    // Persistence through ReviewStore.
    void loadFor(const QString &mediaPath);
    bool save() const;
    QString mediaPath() const { return m_mediaPath; }

    QJsonArray toJson() const { return toJson(m_items); }
    static QJsonArray toJson(const QVector<Marker> &items);
    static QVector<Marker> fromJson(const QJsonArray &arr);
    // Replace everything (undo / redo). Doesn't emit edited().
    void restore(const QJsonArray &arr);

    // Exports (pure, testable).
    static QString toEdl(const QVector<Marker> &items, double fps, const QString &title, qint64 startOffsetFrames);
    static QString toCsv(const QVector<Marker> &items, double fps);
    struct Chapters { QString text; int count = 0; int skipped = 0; QString error; };
    static Chapters toYouTubeChapters(const QVector<Marker> &items, double duration);
    static QString kindName(int kind);

signals:
    void countChanged();
    void changed();
    void edited(const QString &label, const QJsonArray &before, const QJsonArray &after);

private:
    void commit(const QString &label, const QJsonArray &before);
    int insertSorted(Marker m);
    int fixOrder(int row);
    QVector<Marker> m_items;
    QString m_mediaPath;
    int m_nextId = 1;
    int m_rev = 0;
    int m_gesture = 0;
    QJsonArray m_gestureBefore;
};

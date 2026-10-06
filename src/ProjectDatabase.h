#pragma once

#include <QHash>
#include <QPair>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

struct ObjectRecord
{
    qint64 id = 0;
    QString type;
    QString name;
    QStringList aliases;
};

struct RevisionRecord
{
    qint64 id = 0;
    QString createdAt;
    QString reason;
};

class ProjectDatabase
{
public:
    ProjectDatabase();
    ~ProjectDatabase();

    bool open(const QString &path, QString *error = nullptr);
    void close();
    bool isOpen() const;

    qint64 ensureObject(const QString &name, const QString &type = QStringLiteral("character"));
    QVector<ObjectRecord> objects() const;
    QHash<QString, qint64> objectLexicon() const;
    int mentionCount(qint64 objectId) const;

    bool replaceMentions(const QString &documentId,
                         const QVector<QPair<qint64, int>> &mentions,
                         QString *error = nullptr);

    bool addRevision(const QString &documentId,
                     const QString &text,
                     const QString &reason,
                     QString *error = nullptr);
    QVector<RevisionRecord> revisions(const QString &documentId, int limit = 100) const;
    QString revisionText(qint64 revisionId, QString *error = nullptr) const;
    void pruneRevisions(const QString &documentId, int keep = 500);

private:
    bool ensureSchema(QString *error);

    QString m_connectionName;
    QSqlDatabase m_db;
};

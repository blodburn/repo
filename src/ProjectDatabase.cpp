#include "ProjectDatabase.h"

#include <QByteArray>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QUuid>

ProjectDatabase::ProjectDatabase()
    : m_connectionName(QStringLiteral("repo-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

ProjectDatabase::~ProjectDatabase()
{
    close();
}

bool ProjectDatabase::open(const QString &path, QString *error)
{
    close();

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);

    if (!m_db.open()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));

    return ensureSchema(error);
}

void ProjectDatabase::close()
{
    if (!m_db.isValid()) return;

    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool ProjectDatabase::isOpen() const
{
    return m_db.isValid() && m_db.isOpen();
}

bool ProjectDatabase::ensureSchema(QString *error)
{
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS objects ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " type TEXT NOT NULL DEFAULT 'character',"
            " name TEXT NOT NULL UNIQUE,"
            " deleted INTEGER NOT NULL DEFAULT 0,"
            " created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS aliases ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " object_id INTEGER NOT NULL REFERENCES objects(id) ON DELETE CASCADE,"
            " alias TEXT NOT NULL UNIQUE"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS mentions ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " document_id TEXT NOT NULL,"
            " object_id INTEGER NOT NULL REFERENCES objects(id) ON DELETE CASCADE,"
            " char_offset INTEGER NOT NULL"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mentions_document ON mentions(document_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mentions_object ON mentions(object_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS revisions ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " document_id TEXT NOT NULL,"
            " created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " reason TEXT NOT NULL DEFAULT 'autosave',"
            " content BLOB NOT NULL"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_revisions_document ON revisions(document_id, id DESC)")
    };

    for (const QString &sql : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            if (error) *error = query.lastError().text();
            return false;
        }
    }
    return true;
}

qint64 ProjectDatabase::ensureObject(const QString &name, const QString &type)
{
    if (!isOpen()) return 0;

    const QString clean = name.trimmed();
    if (clean.isEmpty()) return 0;

    QSqlQuery select(m_db);
    select.prepare(QStringLiteral(
        "SELECT id FROM objects WHERE name = ? COLLATE NOCASE AND deleted = 0"));
    select.addBindValue(clean);
    if (select.exec() && select.next())
        return select.value(0).toLongLong();

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO objects(type, name) VALUES(?, ?)"));
    insert.addBindValue(type.trimmed().isEmpty() ? QStringLiteral("character") : type.trimmed());
    insert.addBindValue(clean);

    if (!insert.exec()) {
        QSqlQuery retry(m_db);
        retry.prepare(QStringLiteral(
            "SELECT id FROM objects WHERE name = ? COLLATE NOCASE AND deleted = 0"));
        retry.addBindValue(clean);
        if (retry.exec() && retry.next())
            return retry.value(0).toLongLong();
        return 0;
    }

    return insert.lastInsertId().toLongLong();
}

QVector<ObjectRecord> ProjectDatabase::objects() const
{
    QVector<ObjectRecord> result;
    if (!isOpen()) return result;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT id, type, name "
            "FROM objects WHERE deleted = 0 ORDER BY name COLLATE NOCASE"))) {
        return result;
    }

    while (query.next()) {
        ObjectRecord record;
        record.id = query.value(0).toLongLong();
        record.type = query.value(1).toString();
        record.name = query.value(2).toString();

        QSqlQuery aliases(m_db);
        aliases.prepare(QStringLiteral(
            "SELECT alias FROM aliases WHERE object_id = ? ORDER BY alias COLLATE NOCASE"));
        aliases.addBindValue(record.id);
        if (aliases.exec()) {
            while (aliases.next())
                record.aliases.push_back(aliases.value(0).toString());
        }

        result.push_back(record);
    }

    return result;
}

QHash<QString, qint64> ProjectDatabase::objectLexicon() const
{
    QHash<QString, qint64> lexicon;

    for (const ObjectRecord &object : objects()) {
        lexicon.insert(object.name, object.id);
        for (const QString &alias : object.aliases)
            lexicon.insert(alias, object.id);
    }

    return lexicon;
}

int ProjectDatabase::mentionCount(qint64 objectId) const
{
    if (!isOpen()) return 0;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM mentions WHERE object_id = ?"));
    query.addBindValue(objectId);
    if (!query.exec() || !query.next()) return 0;

    return query.value(0).toInt();
}

bool ProjectDatabase::replaceMentions(
    const QString &documentId,
    const QVector<QPair<qint64, int>> &mentions,
    QString *error)
{
    if (!isOpen()) return false;

    if (!m_db.transaction()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }

    QSqlQuery remove(m_db);
    remove.prepare(QStringLiteral("DELETE FROM mentions WHERE document_id = ?"));
    remove.addBindValue(documentId);

    if (!remove.exec()) {
        if (error) *error = remove.lastError().text();
        m_db.rollback();
        return false;
    }

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO mentions(document_id, object_id, char_offset) VALUES(?, ?, ?)"));

    for (const auto &mention : mentions) {
        insert.bindValue(0, documentId);
        insert.bindValue(1, mention.first);
        insert.bindValue(2, mention.second);

        if (!insert.exec()) {
            if (error) *error = insert.lastError().text();
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }

    return true;
}

bool ProjectDatabase::addRevision(
    const QString &documentId,
    const QString &text,
    const QString &reason,
    QString *error)
{
    if (!isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO revisions(document_id, reason, content) VALUES(?, ?, ?)"));
    query.addBindValue(documentId);
    query.addBindValue(reason);
    query.addBindValue(qCompress(text.toUtf8(), 6));

    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }

    return true;
}

QVector<RevisionRecord> ProjectDatabase::revisions(const QString &documentId, int limit) const
{
    QVector<RevisionRecord> result;
    if (!isOpen()) return result;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, created_at, reason "
        "FROM revisions WHERE document_id = ? "
        "ORDER BY id DESC LIMIT ?"));
    query.addBindValue(documentId);
    query.addBindValue(limit);

    if (!query.exec()) return result;

    while (query.next()) {
        RevisionRecord record;
        record.id = query.value(0).toLongLong();
        record.createdAt = query.value(1).toString();
        record.reason = query.value(2).toString();
        result.push_back(record);
    }

    return result;
}

QString ProjectDatabase::revisionText(qint64 revisionId, QString *error) const
{
    if (!isOpen()) return {};

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT content FROM revisions WHERE id = ?"));
    query.addBindValue(revisionId);

    if (!query.exec() || !query.next()) {
        if (error) *error = query.lastError().text();
        return {};
    }

    return QString::fromUtf8(qUncompress(query.value(0).toByteArray()));
}

void ProjectDatabase::pruneRevisions(const QString &documentId, int keep)
{
    if (!isOpen() || keep <= 0) return;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM revisions "
        "WHERE document_id = ? AND id NOT IN ("
        " SELECT id FROM revisions "
        " WHERE document_id = ? ORDER BY id DESC LIMIT ?"
        ")"));
    query.addBindValue(documentId);
    query.addBindValue(documentId);
    query.addBindValue(keep);
    query.exec();
}

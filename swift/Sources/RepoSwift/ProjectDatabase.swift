import Foundation
import SQLite3

struct StoryObject: Identifiable {
    let id: Int64
    let type: String
    let name: String
    let mentions: Int
}

final class ProjectDatabase {
    private var db: OpaquePointer?

    deinit { close() }

    func open(_ url: URL) throws {
        close()
        if sqlite3_open(url.path, &db) != SQLITE_OK {
            throw DatabaseError.message(lastError)
        }
        try execute("PRAGMA foreign_keys=ON")
        try execute("PRAGMA journal_mode=WAL")
        try execute("PRAGMA synchronous=NORMAL")
        try execute("""
        CREATE TABLE IF NOT EXISTS objects (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type TEXT NOT NULL DEFAULT 'character',
            name TEXT NOT NULL UNIQUE,
            deleted INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
            updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        )
        """)
        try execute("""
        CREATE TABLE IF NOT EXISTS mentions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            document_id TEXT NOT NULL,
            object_id INTEGER NOT NULL REFERENCES objects(id) ON DELETE CASCADE,
            char_offset INTEGER NOT NULL
        )
        """)
        try execute("CREATE INDEX IF NOT EXISTS idx_mentions_object ON mentions(object_id)")
        try execute("""
        CREATE TABLE IF NOT EXISTS revisions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            document_id TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
            reason TEXT NOT NULL DEFAULT 'autosave',
            content TEXT NOT NULL
        )
        """)
    }

    func close() {
        if let db { sqlite3_close(db) }
        db = nil
    }

    func ensureObject(name: String, type: String = "character") throws -> Int64 {
        let clean = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !clean.isEmpty else { return 0 }

        if let existing = try objectID(named: clean) {
            return existing
        }

        let sql = "INSERT INTO objects(type,name) VALUES(?,?)"
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.message(lastError)
        }
        defer { sqlite3_finalize(stmt) }

        bindText(type.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? "character" : type, to: 1, in: stmt)
        bindText(clean, to: 2, in: stmt)

        guard sqlite3_step(stmt) == SQLITE_DONE else {
            if let existing = try objectID(named: clean) { return existing }
            throw DatabaseError.message(lastError)
        }
        return sqlite3_last_insert_rowid(db)
    }

    func objects() throws -> [StoryObject] {
        let sql = """
        SELECT o.id,o.type,o.name,COUNT(m.id)
        FROM objects o
        LEFT JOIN mentions m ON m.object_id=o.id
        WHERE o.deleted=0
        GROUP BY o.id,o.type,o.name
        ORDER BY o.name COLLATE NOCASE
        """
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.message(lastError)
        }
        defer { sqlite3_finalize(stmt) }

        var result: [StoryObject] = []
        while sqlite3_step(stmt) == SQLITE_ROW {
            result.append(
                StoryObject(
                    id: sqlite3_column_int64(stmt, 0),
                    type: string(stmt, 1),
                    name: string(stmt, 2),
                    mentions: Int(sqlite3_column_int(stmt, 3))
                )
            )
        }
        return result
    }

    func lexicon() throws -> [String: Int64] {
        Dictionary(uniqueKeysWithValues: try objects().map { ($0.name, $0.id) })
    }

    func replaceMentions(documentID: String, values: [(Int64, Int)]) throws {
        try execute("BEGIN IMMEDIATE")
        do {
            try withStatement("DELETE FROM mentions WHERE document_id=?") { stmt in
                bindText(documentID, to: 1, in: stmt)
                guard sqlite3_step(stmt) == SQLITE_DONE else {
                    throw DatabaseError.message(lastError)
                }
            }

            try withStatement("INSERT INTO mentions(document_id,object_id,char_offset) VALUES(?,?,?)") { stmt in
                for (objectID, offset) in values {
                    sqlite3_reset(stmt)
                    sqlite3_clear_bindings(stmt)
                    bindText(documentID, to: 1, in: stmt)
                    sqlite3_bind_int64(stmt, 2, objectID)
                    sqlite3_bind_int64(stmt, 3, sqlite3_int64(offset))
                    guard sqlite3_step(stmt) == SQLITE_DONE else {
                        throw DatabaseError.message(lastError)
                    }
                }
            }
            try execute("COMMIT")
        } catch {
            try? execute("ROLLBACK")
            throw error
        }
    }

    func addRevision(documentID: String, text: String, reason: String) throws {
        try withStatement("INSERT INTO revisions(document_id,reason,content) VALUES(?,?,?)") { stmt in
            bindText(documentID, to: 1, in: stmt)
            bindText(reason, to: 2, in: stmt)
            bindText(text, to: 3, in: stmt)
            guard sqlite3_step(stmt) == SQLITE_DONE else {
                throw DatabaseError.message(lastError)
            }
        }
        try execute("""
        DELETE FROM revisions
        WHERE document_id='script/main.txt'
          AND id NOT IN (
            SELECT id FROM revisions
            WHERE document_id='script/main.txt'
            ORDER BY id DESC LIMIT 500
          )
        """)
    }

    private func objectID(named name: String) throws -> Int64? {
        var found: Int64?
        try withStatement("SELECT id FROM objects WHERE name=? COLLATE NOCASE AND deleted=0 LIMIT 1") { stmt in
            bindText(name, to: 1, in: stmt)
            if sqlite3_step(stmt) == SQLITE_ROW {
                found = sqlite3_column_int64(stmt, 0)
            }
        }
        return found
    }

    private func execute(_ sql: String) throws {
        var errorMessage: UnsafeMutablePointer<CChar>?
        guard sqlite3_exec(db, sql, nil, nil, &errorMessage) == SQLITE_OK else {
            let message = errorMessage.map { String(cString: $0) } ?? lastError
            sqlite3_free(errorMessage)
            throw DatabaseError.message(message)
        }
    }

    private func withStatement(_ sql: String, body: (OpaquePointer?) throws -> Void) throws {
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.message(lastError)
        }
        defer { sqlite3_finalize(stmt) }
        try body(stmt)
    }

    private func bindText(_ value: String, to index: Int32, in stmt: OpaquePointer?) {
        value.withCString { ptr in
            sqlite3_bind_text(stmt, index, ptr, -1, SQLITE_TRANSIENT)
        }
    }

    private func string(_ stmt: OpaquePointer?, _ column: Int32) -> String {
        guard let value = sqlite3_column_text(stmt, column) else { return "" }
        return String(cString: value)
    }

    private var lastError: String {
        db.map { String(cString: sqlite3_errmsg($0)) } ?? "SQLite error"
    }
}

enum DatabaseError: Error, LocalizedError {
    case message(String)
    var errorDescription: String? {
        switch self {
        case .message(let value): return value
        }
    }
}

private let SQLITE_TRANSIENT = unsafeBitCast(-1, to: sqlite3_destructor_type.self)

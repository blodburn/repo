//! SQLite-backed write-ahead edit journal. Never writes to the old "revisions"
//! schema, which differs between Windows Win32, Swift and early Rust builds.

use std::fs;
use std::path::Path;
use std::time::Duration;

use rusqlite::{params, Connection, OptionalExtension};

const DOCUMENT: &str = "script/main.txt";
const SNAPSHOT_INTERVAL: usize = 128;

#[derive(Debug, Clone)]
pub enum HistoryItem {
    Snapshot { id: i64, content: String, reason: String, timestamp: String },
    Deletion { id: i64, content: String, timestamp: String },
}

impl HistoryItem {
    pub fn label(&self) -> String {
        match self {
            Self::Snapshot { id, reason, timestamp, .. } =>
                format!("저장본 #{id} · {timestamp} · {reason}"),
            Self::Deletion { id, content, timestamp } =>
                format!("삭제 기록 #{id} · {timestamp} · {}", content.replace('\n', " ↵ ").chars().take(46).collect::<String>()),
        }
    }
}

pub struct Journal {
    db: Connection,
    edits_since_snapshot: usize,
}

impl Journal {
    pub fn open(db_file: &Path, original: &str) -> Result<(Self, String, bool), String> {
        if let Some(parent) = db_file.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let db = Connection::open(db_file).map_err(|e| e.to_string())?;
        db.busy_timeout(Duration::from_secs(2)).map_err(|e| e.to_string())?;
        db.pragma_update(None, "journal_mode", "WAL").map_err(|e| e.to_string())?;
        db.pragma_update(None, "synchronous", "FULL").map_err(|e| e.to_string())?;
        db.execute_batch(
            "CREATE TABLE IF NOT EXISTS repo_edit_journal (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                document_id TEXT NOT NULL,
                byte_offset INTEGER NOT NULL,
                deleted_text TEXT NOT NULL,
                inserted_text TEXT NOT NULL,
                kind TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE INDEX IF NOT EXISTS idx_repo_journal_document
                ON repo_edit_journal(document_id, id);
            CREATE TABLE IF NOT EXISTS repo_snapshots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                document_id TEXT NOT NULL,
                journal_id INTEGER NOT NULL DEFAULT 0,
                content TEXT NOT NULL,
                reason TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE INDEX IF NOT EXISTS idx_repo_snapshots_document
                ON repo_snapshots(document_id, id);
            CREATE TABLE IF NOT EXISTS repo_project_meta (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );"
        ).map_err(|e| e.to_string())?;

        let latest: Option<(i64, String)> = db.query_row(
            "SELECT journal_id, content FROM repo_snapshots WHERE document_id=?1 ORDER BY id DESC LIMIT 1",
            [DOCUMENT], |row| Ok((row.get(0)?, row.get(1)?))
        ).optional().map_err(|e| e.to_string())?;

        let (start_id, mut recovered) = match latest {
            Some((id, txt)) => (id, txt),
            None => {
                db.execute(
                    "INSERT INTO repo_snapshots(document_id, journal_id, content, reason) VALUES(?1,0,?2,'baseline')",
                    params![DOCUMENT, original]
                ).map_err(|e| e.to_string())?;
                (0, original.to_string())
            }
        };

        let replay_count = {
            let mut stmt = db.prepare(
                "SELECT id, byte_offset, deleted_text, inserted_text
                 FROM repo_edit_journal WHERE document_id=?1 AND id>?2 ORDER BY id ASC"
            ).map_err(|e| e.to_string())?;
            let mut rows = stmt.query(params![DOCUMENT, start_id]).map_err(|e| e.to_string())?;
            let mut count = 0_usize;
            while let Some(row) = rows.next().map_err(|e| e.to_string())? {
                let id: i64 = row.get(0).map_err(|e| e.to_string())?;
                let off: i64 = row.get(1).map_err(|e| e.to_string())?;
                let deleted: String = row.get(2).map_err(|e| e.to_string())?;
                let inserted: String = row.get(3).map_err(|e| e.to_string())?;
                let offset = usize::try_from(off).map_err(|e| e.to_string())?;
                let end = offset.checked_add(deleted.len()).ok_or("journal offset overflow")?;
                if end > recovered.len() || !recovered.is_char_boundary(offset)
                    || !recovered.is_char_boundary(end) || recovered.get(offset..end) != Some(deleted.as_str()) {
                    return Err(format!("Journal replay diverged at entry #{id}; original files were not modified"));
                }
                recovered.replace_range(offset..end, &inserted);
                count += 1;
            }
            count
        };

        let differs_from_txt = recovered != original;
        Ok((Self { db, edits_since_snapshot: replay_count }, recovered, differs_from_txt))
    }

    pub fn append_edit(&mut self, current: &str, offset: usize, removed: &str,
                       added: &str, kind: &str) -> Result<(), String> {
        let end = offset.checked_add(removed.len()).ok_or("edit offset overflow")?;
        if !current.is_char_boundary(offset) || !current.is_char_boundary(end)
            || current.get(offset..end) != Some(removed) {
            return Err("edit journal mismatch: refused to apply destructive edit".into());
        }
        self.db.execute(
            "INSERT INTO repo_edit_journal(document_id,byte_offset,deleted_text,inserted_text,kind)
             VALUES(?1,?2,?3,?4,?5)",
            params![DOCUMENT, offset as i64, removed, added, kind]
        ).map_err(|e| e.to_string())?;
        self.edits_since_snapshot += 1;
        Ok(())
    }

    pub fn maybe_snapshot(&mut self, manuscript: &str) -> Result<(), String> {
        if self.edits_since_snapshot >= SNAPSHOT_INTERVAL {
            self.snapshot(manuscript, "periodic")?;
        }
        Ok(())
    }

    pub fn snapshot(&mut self, manuscript: &str, reason: &str) -> Result<(), String> {
        let journal_id: i64 = self.db.query_row(
            "SELECT COALESCE(MAX(id),0) FROM repo_edit_journal WHERE document_id=?1",
            [DOCUMENT], |r| r.get(0)
        ).map_err(|e| e.to_string())?;
        self.db.execute(
            "INSERT INTO repo_snapshots(document_id,journal_id,content,reason) VALUES(?1,?2,?3,?4)",
            params![DOCUMENT, journal_id, manuscript, reason]
        ).map_err(|e| e.to_string())?;
        self.edits_since_snapshot = 0;
        Ok(())
    }

    pub fn history(&self, limit: usize) -> Result<Vec<HistoryItem>, String> {
        let mut out = Vec::new();
        {
            let mut stmt = self.db.prepare(
                "SELECT id, content, reason, created_at FROM repo_snapshots
                 WHERE document_id=?1 ORDER BY id DESC LIMIT ?2"
            ).map_err(|e| e.to_string())?;
            let items = stmt.query_map(params![DOCUMENT, limit as i64], |r| {
                Ok(HistoryItem::Snapshot {
                    id: r.get(0)?, content: r.get(1)?, reason: r.get(2)?, timestamp: r.get(3)?
                })
            }).map_err(|e| e.to_string())?;
            for item in items { out.push(item.map_err(|e| e.to_string())?); }
        }
        {
            let mut stmt = self.db.prepare(
                "SELECT id, deleted_text, created_at FROM repo_edit_journal
                 WHERE document_id=?1 AND length(deleted_text)>1
                 ORDER BY id DESC LIMIT ?2"
            ).map_err(|e| e.to_string())?;
            let items = stmt.query_map(params![DOCUMENT, limit as i64], |r| {
                Ok(HistoryItem::Deletion { id: r.get(0)?, content: r.get(1)?, timestamp: r.get(2)? })
            }).map_err(|e| e.to_string())?;
            for item in items { out.push(item.map_err(|e| e.to_string())?); }
        }
        Ok(out)
    }

    pub fn set_title(&mut self, title: &str) -> Result<(), String> {
        self.db.execute(
            "INSERT INTO repo_project_meta(key,value) VALUES('title',?1)
             ON CONFLICT(key) DO UPDATE SET value=excluded.value",
            [title]
        ).map_err(|e| e.to_string())?;
        Ok(())
    }

    pub fn title(&self) -> Result<Option<String>, String> {
        self.db.query_row("SELECT value FROM repo_project_meta WHERE key='title'", [], |r| r.get(0))
            .optional().map_err(|e| e.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT: AtomicU64 = AtomicU64::new(0);
    fn database_path() -> std::path::PathBuf {
        let path = std::env::temp_dir().join(format!(
            "repo-journal-test-{}-{}.sqlite", std::process::id(), NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        let _ = fs::remove_file(&path);
        path
    }

    #[test]
    fn crash_recovery_replays_unsaved_unicode_edits() {
        let path = database_path();
        {
            let (mut j, initial, _) = Journal::open(&path, "김철수가 왔다").unwrap();
            assert_eq!(initial, "김철수가 왔다");
            let at = initial.len();
            j.append_edit(&initial, at, "", " 그리고 박영희.", "type").unwrap();
        }
        let (_, text, recovered) = Journal::open(&path, "김철수가 왔다").unwrap();
        assert_eq!(text, "김철수가 왔다 그리고 박영희.");
        assert!(recovered);
        let _ = fs::remove_file(path);
    }

    #[test]
    fn deleted_sentence_is_retained_for_recovery() {
        let path = database_path();
        let text = "그날 밤 비밀이 밝혀졌다.";
        let (mut j, _, _) = Journal::open(&path, text).unwrap();
        j.append_edit(text, 0, text, "", "delete").unwrap();
        let hist = j.history(10).unwrap();
        assert!(hist.iter().any(|item|
            matches!(item, HistoryItem::Deletion { content, .. } if content == text)));
        let _ = fs::remove_file(path);
    }

    #[test]
    fn corrupt_replay_does_not_overwrite_disk() {
        let path = database_path();
        let (mut j, _, _) = Journal::open(&path, "가나다").unwrap();
        j.append_edit("가나다", 0, "가", "나", "replace").unwrap();
        assert!(j.append_edit("가나다", 0, "라", "하", "invalid").is_err());
        let (_, text, _) = Journal::open(&path, "가나다").unwrap();
        assert_eq!(text, "나나다");
        let _ = fs::remove_file(path);
    }
}

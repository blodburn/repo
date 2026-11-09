//! File persistence, atomic writing, and project schema management.

use std::fs::{self, File};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

/// Layout of a standard repo project directory.
#[derive(Debug, Clone)]
pub struct ProjectLayout {
    pub root: PathBuf,
    pub script_dir: PathBuf,
    pub main_txt: PathBuf,
    pub database: PathBuf,
    pub config_json: PathBuf,
}

impl ProjectLayout {
    /// Constructs a project layout from a root directory path.
    pub fn new<P: AsRef<Path>>(root: P) -> Self {
        let root = root.as_ref().to_path_buf();
        let script_dir = root.join("script");
        let main_txt = script_dir.join("main.txt");
        let database = root.join("project.sqlite");
        let config_json = root.join("project.json");

        Self {
            root,
            script_dir,
            main_txt,
            database,
            config_json,
        }
    }

    /// Initializes project directory structure on disk if it does not exist.
    pub fn ensure_directories(&self) -> io::Result<()> {
        fs::create_dir_all(&self.script_dir)?;
        Ok(())
    }

    /// Atomically saves the manuscript text to `script/main.txt`.
    /// Writes to a temporary file first and replaces atomically to prevent corruption.
    pub fn save_manuscript_atomic(&self, content: &str) -> io::Result<()> {
        self.ensure_directories()?;

        let tmp_path = self.script_dir.join("main.txt.tmp");
        {
            let mut file = File::create(&tmp_path)?;
            file.write_all(content.as_bytes())?;
            file.sync_all()?;
        }

        fs::rename(tmp_path, &self.main_txt)?;
        Ok(())
    }

    /// Reads manuscript text from `script/main.txt` if present.
    pub fn load_manuscript(&self) -> io::Result<String> {
        if self.main_txt.exists() {
            fs::read_to_string(&self.main_txt)
        } else {
            Ok(String::new())
        }
    }
}

/// Standard SQLite schema DDL for repo projects.
pub const SQLITE_SCHEMA_DDL: &str = r#"
CREATE TABLE IF NOT EXISTS project_meta (
    key TEXT PRIMARY KEY,
    value TEXT
);

CREATE TABLE IF NOT EXISTS objects (
    id TEXT PRIMARY KEY,
    type TEXT NOT NULL,
    canonical_name TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS aliases (
    alias TEXT PRIMARY KEY,
    object_id TEXT NOT NULL,
    FOREIGN KEY(object_id) REFERENCES objects(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS mentions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    object_id TEXT NOT NULL,
    line_index INTEGER NOT NULL,
    char_offset INTEGER NOT NULL,
    length INTEGER NOT NULL,
    scene_id TEXT,
    FOREIGN KEY(object_id) REFERENCES objects(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS revisions (
    revision_id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL DEFAULT (datetime('now')),
    content TEXT NOT NULL,
    hash TEXT NOT NULL
);
"#;

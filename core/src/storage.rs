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

        replace_existing_atomic(&tmp_path, &self.main_txt)?;
        #[cfg(unix)]
        {
            // Persist directory metadata after the rename.
            File::open(&self.script_dir)?.sync_all()?;
        }
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

#[cfg(not(windows))]
fn replace_existing_atomic(from: &Path, to: &Path) -> io::Result<()> {
    fs::rename(from, to)
}

#[cfg(windows)]
fn replace_existing_atomic(from: &Path, to: &Path) -> io::Result<()> {
    // std::fs::rename is not guaranteed to overwrite an existing Windows file.
    // MoveFileExW with REPLACE_EXISTING preserves atomic destination replacement.
    use std::os::windows::ffi::OsStrExt;

    #[link(name = "Kernel32")]
    extern "system" {
        fn MoveFileExW(existing: *const u16, new_name: *const u16, flags: u32) -> i32;
    }

    const MOVEFILE_REPLACE_EXISTING: u32 = 0x00000001;
    const MOVEFILE_WRITE_THROUGH: u32 = 0x00000008;
    let old: Vec<u16> = from.as_os_str().encode_wide().chain(std::iter::once(0)).collect();
    let new: Vec<u16> = to.as_os_str().encode_wide().chain(std::iter::once(0)).collect();
    let ok = unsafe {
        MoveFileExW(old.as_ptr(), new.as_ptr(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
    };
    if ok == 0 { Err(io::Error::last_os_error()) } else { Ok(()) }
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

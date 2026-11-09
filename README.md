# repo

A lightweight, keyboard-first desktop editor for screenplays, novels, and scenario writing.

## Stack

- Rust 2021 (Native application & `core/` shared engine)
- C++20 (Windows Win32 / Qt 6)
- Swift (macOS AppKit)
- SQLite

## Core rules

- manuscript source is ordinary UTF-8 plain text
- `@@ dialogue @@`
- `## situation/action description ##`
- `₩₩ narration ₩₩`
- `[[object]]` creates/forces an object link
- once an object exists, later plain occurrences of that name are recognized automatically
- Undo: `Cmd+Z` / `Ctrl+Z`
- Redo: `Cmd+U` / `Ctrl+U`
- keyboard-only operation is a design requirement

## Project data

```text
YourProject/
├── script/
│   └── main.txt
└── project.sqlite
```

`main.txt` is always readable without this application. SQLite stores object metadata, mentions, aliases, and persistent revision snapshots.

## Build & Run

### Rust Native Application (Recommended)

```bash
# Build binary
cargo build --release

# Run self-test suite (DOCX, HWPX, PDF, Korean UTF-8 encoding, styling Undo/Redo)
cargo run -- --self-test
cargo run -- --encoding-self-test
cargo run -- --style-self-test

# Run 3-pane interactive screenplay editor
cargo run -- [project_dir]

# Export to DOCX / HWPX / PDF / TXT
cargo run -- --export docx script/main.txt output.docx
cargo run -- --export hwpx script/main.txt output.hwpx
cargo run -- --export pdf script/main.txt output.pdf
```

### Desktop UI (Qt / CMake)

Requirements: CMake 3.21+, Qt 6.5+ (Widgets, Sql), C++20 compiler.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/<kit>
cmake --build build --config Release
```

## MVP Features

- native 3-pane UI (Objects & Scenes, Manuscript Editor, Character Details)
- plain-text manuscript with UTF-8 storage
- dialogue/description/narration syntax formatting
- explicit object creation with `[[name]]`
- automatic recognition of previously registered objects
- Korean particle (조사: 은/는/이/가/을/를/의/에/에서 등) boundary matching
- SQLite object + mention index
- atomic autosave
- persistent revision snapshots and restore
- `Cmd/Ctrl+Z` and `Cmd/Ctrl+U`
- `Esc` returns focus to the manuscript
- DOCX, HWPX, PDF and clean text export

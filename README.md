# repo

A lightweight, keyboard-first desktop editor for screenplays, novels, and scenario writing.

## Stack

- C++20
- Qt 6 Widgets
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

## Build

Requirements: CMake 3.21+, Qt 6.5+ (Widgets, Sql), C++20 compiler.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/<kit>
cmake --build build --config Release
```

## MVP

The first MVP includes:

- native three-pane UI
- plain-text manuscript
- dialogue/description/narration syntax highlighting
- explicit object creation with `[[name]]`
- automatic recognition of previously registered objects
- SQLite object + mention index
- autosave
- persistent revision snapshots and restore
- `Cmd/Ctrl+Z` and `Cmd/Ctrl+U`
- `Esc` returns focus to the manuscript

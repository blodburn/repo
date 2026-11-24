# repo

A lightweight, keyboard-first desktop editor for screenplays, novels, and scenario writing.

## Stack

- Rust 2021 — `core/` shared engine (`repo-core`) and the optional terminal build in `rust/`
- Swift / AppKit — macOS application (`swift/`, ships as `repo.app`)
- C++20 — Windows Win32 application (`windows/`, ships as a single portable EXE)
- SQLite — project metadata, object index, revision history

Both desktop applications are the release artifacts and both run on the same
`repo-core` engine through the C interface in `core/include/repo_core.h`. The
`rust/` terminal build is kept for headless use and is published separately as a
preview.

## Core rules

- manuscript source is ordinary UTF-8 plain text
- `@@ dialogue @@`
- `## situation/action description ##`
- `₩₩ narration ₩₩`
- `// author note` (never exported)
- `[[object]]` creates/forces an object link; `[[장소:서울역]]` sets its type and
  `[[철수|김철수]]` adds an alias
- once an object exists, later plain occurrences of that name are recognized
  automatically (longest match first, Korean particle boundaries respected)
- Undo: `Cmd+Z` / `Ctrl+Z`
- Redo: `Cmd+U` / `Ctrl+U`
- keyboard-only operation is a design requirement

## Architecture

```text
          ┌──────────────────────────────┐
          │ core/  repo-core (Rust)      │  syntax.rs  objects.rs  export.rs
          │ staticlib + rlib             │  storage.rs
          └───────────────┬──────────────┘
                          │ C ABI (core/include/repo_core.h)
          ┌───────────────┴───────────────┐
          │                               │
   swift/ (AppKit, repo.app)      windows/ (Win32 C++20)
   RepoCoreBridge.swift            src/RepoCore.h|cpp
```

What the engine owns, so the two GUIs cannot disagree:

- line classification and cleaned content (`repo_parse_line`)
- explicit object extraction with normalized types and aliases
  (`repo_extract_objects`)
- mention scanning with Korean particle boundaries and caller-owned object ids
  (`repo_find_mentions`)
- clean text export (`repo_export_clean`)

The GUIs keep the platform work: windows, text views, SQLite project storage,
undo/redo, document export containers (DOCX/HWPX/PDF).

## Project data

```text
YourProject/
├── script/
│   └── main.txt
└── project.sqlite
```

`main.txt` is always readable without this application. SQLite stores object metadata, mentions, aliases, and persistent revision snapshots.

## Build & Run

### macOS application (repo.app)

Requirements: macOS 13+, Xcode 15 (Swift 5.10), Rust stable toolchain.

```bash
swift/scripts/build-core.sh          # builds repo-core for arm64 + x86_64, lipo -> swift/.core/
cd swift
swift build -c release --arch arm64
./.build/arm64-apple-macosx/release/repo-swift --core-self-test
./.build/arm64-apple-macosx/release/repo-swift --export-self-test
```

### Windows application (portable EXE)

Requirements: Visual Studio 2022 (C++20), CMake 3.21+, vcpkg with
`sqlite3:x64-windows-static` and `zlib:x64-windows-static`, Rust stable.

```bash
cargo build --release -p repo-core --target x86_64-pc-windows-msvc
cmake -S windows -B build-win -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build-win --config Release
build-win/Release/repo.exe --core-self-test
```

CMake builds `repo-core` automatically when the static library is missing, so a
plain `cmake --build` bootstraps the engine as long as `cargo` is on `PATH`.

### Core engine

```bash
cargo test -p repo-core            # syntax, objects, storage, export, C ABI
cargo build --release -p repo-core # librepo_core.a / repo_core.lib
cc core/tests/c_abi_smoke.c -Icore/include -Ltarget/release -lrepo_core -o smoke && ./smoke
```

`core/tests/c_abi_smoke.c` compiles the published header as C and exercises every
symbol the GUIs call; `core/tests/header_sync_tests.rs` fails when the header and
the exported symbols drift apart.

### Terminal build (optional, preview)

```bash
cargo build --release
cargo run -- --self-test
cargo run -- --encoding-self-test
cargo run -- --style-self-test
cargo run -- [project_dir]
cargo run -- --export docx script/main.txt output.docx
```

### Legacy Qt shell

`src/` is the pre-1.0 Qt 6 shell. It is not part of a release; the native Swift
and Win32 applications replace it.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/<kit>
cmake --build build --config Release
```

## Features

- native three-pane UI (objects, manuscript, details/help)
- plain-text manuscript with UTF-8 storage
- dialogue/description/narration syntax formatting while typing
- explicit object creation with `[[name]]`, typed objects and aliases
- automatic recognition of previously registered objects
- Korean particle (조사: 은/는/이/가/을/를/의/에/에서 등) boundary matching
- SQLite object + mention index, atomic autosave
- persistent revision snapshots and restore
- `Cmd/Ctrl+Z` and `Cmd/Ctrl+U`
- `Esc` returns focus to the manuscript
- DOCX, HWPX, PDF and clean text export

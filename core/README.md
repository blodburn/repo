# repo-core

High-performance, memory-safe cross-platform core library for the `repo` desktop editor.

## Features

- **Deterministic Syntax Parsing**:
  - Scenes (`# Heading`)
  - Dialogue (`@@ line @@`)
  - Description (`## action ##`)
  - Narration (`₩₩ narration ₩₩` & `\\ narration \\`)
  - Object Registration & Tags (`[[김철수]]`, `[[장소:서울역]]`, `[[별칭|이름]]`)
  - Notes & Comments (`// note`, omitted from exports)
- **Object & Mention Engine**:
  - Persistent ID allocation (`CHAR-0001`, `LOC-0001`, `OBJ-0001`)
  - Korean particle (조사) aware boundary matching (prevents substring false-positives)
  - Longest-match-first greedy alias resolution
- **Persistence & Storage**:
  - Atomic manuscript save (`script/main.txt`) via temp files
  - SQLite schema DDL specification
- **C-ABI / FFI**:
  - Native C headers (`include/repo_core.h`) for integration with Windows C++ (Win32 / Qt) and macOS Swift (AppKit).
  - Memory safe, panic-guarded FFI functions.

## Building

```bash
cargo build --release
cargo test
```

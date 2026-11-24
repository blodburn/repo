# Swift macOS release

Native macOS Swift/AppKit application (`repo.app`), built on the shared Rust
engine in `core/`.

## Layout

- `Sources/RepoSwift/` — AppKit UI (`main.swift`, `MainWindowController.swift`,
  `DocumentExport.swift`, `ProjectDatabase.swift`)
- `Sources/RepoSwift/RepoCoreBridge.swift` — Swift wrapper over the `repo-core`
  C ABI; `ScriptParser.swift` and the exporters call it instead of carrying their
  own grammar
- `Sources/RepoCoreC/` — SwiftPM C target publishing `core/include/repo_core.h`
  as the `RepoCoreC` module (the header copy is generated, do not edit it there)
- `scripts/build-core.sh` — builds `repo-core` for arm64 and x86_64 and lipos it
  into `.core/librepo_core.a`, which `Package.swift` links

## Local build

```bash
swift/scripts/build-core.sh          # cargo build + lipo into swift/.core/
cd swift
swift build -c release --arch arm64
./.build/arm64-apple-macosx/release/repo-swift --core-self-test
./.build/arm64-apple-macosx/release/repo-swift --export-self-test
```

`release.yml` runs the same steps and publishes `repo.app` as
`repo-macOS-universal.zip`.

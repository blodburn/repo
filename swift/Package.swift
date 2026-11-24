// swift-tools-version: 5.10
import PackageDescription

// The repo-core Rust engine is shipped as a static library next to the package.
// `scripts/build-core.sh` builds it for both architectures and lipos them into
// `.core/librepo_core.a`; CI runs the same script before `swift build`.
let coreLibrary = Context.packageDirectory + "/.core/librepo_core.a"

let package = Package(
    name: "repo-swift",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .executable(name: "repo-swift", targets: ["RepoSwift"])
    ],
    targets: [
        // C interface to repo-core (core/include/repo_core.h).
        .target(
            name: "RepoCoreC",
            path: "Sources/RepoCoreC"
        ),
        .executableTarget(
            name: "RepoSwift",
            dependencies: ["RepoCoreC"],
            path: "Sources/RepoSwift",
            linkerSettings: [
                .linkedLibrary("sqlite3"),
                // Link the archive by path: it avoids depending on the order of
                // -L and -l flags that SwiftPM does not promise to preserve.
                .unsafeFlags([coreLibrary])
            ]
        )
    ]
)

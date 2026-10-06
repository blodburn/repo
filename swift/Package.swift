// swift-tools-version: 5.10
import PackageDescription

let package = Package(
    name: "repo-swift",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .executable(name: "repo-swift", targets: ["RepoSwift"])
    ],
    targets: [
        .executableTarget(
            name: "RepoSwift",
            path: "Sources/RepoSwift",
            linkerSettings: [
                .linkedLibrary("sqlite3")
            ]
        )
    ]
)

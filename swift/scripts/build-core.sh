#!/usr/bin/env bash
# Builds the repo-core Rust engine for every macOS architecture and links it into
# a single static library that the SwiftPM package links against.
#
#   swift/scripts/build-core.sh            # arm64 + x86_64, lipo into .core/
#
# CI runs this before `swift build`, and a local macOS build needs it too.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
swift_dir="$(cd "$here/.." && pwd)"
root="$(cd "$swift_dir/.." && pwd)"
out_dir="$swift_dir/.core"

mkdir -p "$out_dir"
# Keep the Swift facing header identical to the core header.
cp "$root/core/include/repo_core.h" "$swift_dir/Sources/RepoCoreC/include/repo_core.h"

archives=()
for target in aarch64-apple-darwin x86_64-apple-darwin; do
  echo "==> cargo build --release -p repo-core --target $target"
  cargo build --release --manifest-path "$root/core/Cargo.toml" --target "$target"
  archives+=("$root/target/$target/release/librepo_core.a")
done

echo "==> lipo -> .core/librepo_core.a"
lipo -create "${archives[@]}" -output "$out_dir/librepo_core.a"
lipo -info "$out_dir/librepo_core.a"

echo "repo-core static library ready: $out_dir/librepo_core.a"

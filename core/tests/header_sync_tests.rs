//! Guards the public C header against drifting away from the exported symbols.
//!
//! The macOS (Swift) and Windows (C++) clients compile `core/include/repo_core.h`
//! against `librepo_core.a`. If a symbol is exported without being declared, or
//! declared without being exported, the GUI build breaks only in CI. This test
//! compares both files directly and fails in `cargo test` instead.

const HEADER: &str = include_str!("../include/repo_core.h");
const FFI: &str = include_str!("../src/ffi.rs");

fn declared_in_header() -> Vec<String> {
    let mut names = Vec::new();
    for line in HEADER.lines() {
        let trimmed = line.trim_start();
        if trimmed.starts_with('*') || trimmed.starts_with("//") || trimmed.starts_with('#') {
            continue;
        }
        let Some(open) = trimmed.find('(') else { continue };
        let before = trimmed[..open].trim_end();
        let Some(name) = before.rsplit(|c: char| c.is_whitespace() || c == '*').next() else {
            continue;
        };
        if name.starts_with("repo_") {
            names.push(name.to_string());
        }
    }
    names.sort();
    names.dedup();
    names
}

fn exported_in_rust() -> Vec<String> {
    let mut names = Vec::new();
    for line in FFI.lines() {
        let trimmed = line.trim();
        let Some(rest) = trimmed.strip_prefix("pub extern \"C\" fn ") else { continue };
        let Some(open) = rest.find('(') else { continue };
        names.push(rest[..open].trim().to_string());
    }
    names.sort();
    names.dedup();
    names
}

#[test]
fn every_exported_symbol_is_declared_in_the_header() {
    let header = declared_in_header();
    let exported = exported_in_rust();
    assert!(!exported.is_empty(), "no exported FFI symbols were discovered");

    let missing: Vec<_> = exported
        .iter()
        .filter(|name| !header.contains(name))
        .collect();
    assert!(missing.is_empty(), "header is missing declarations for: {missing:?}");
}

#[test]
fn every_declared_symbol_is_exported() {
    let header = declared_in_header();
    let exported = exported_in_rust();

    let missing: Vec<_> = header
        .iter()
        .filter(|name| !exported.contains(name))
        .collect();
    assert!(missing.is_empty(), "library does not export: {missing:?}");
}

#[test]
fn header_states_the_current_version() {
    assert!(
        HEADER.contains("0.4.2"),
        "the C header must document the released core version"
    );
}

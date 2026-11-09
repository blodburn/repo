//! # repo_core
//!
//! High-performance, cross-platform core library for the `repo` desktop editor.
//!
//! Provides deterministic UTF-8 syntax parsing, object and alias indexing,
//! project atomic persistence, and clean document export formatting.
//!
//! Also exposes standard C-ABI symbols via the `ffi` module for Windows (C++)
//! and macOS (Swift) interoperability.

pub mod export;
pub mod ffi;
pub mod objects;
pub mod storage;
pub mod syntax;

pub use export::{export_clean_text, ExportOptions};
pub use objects::{Mention, ObjectEngine, ObjectEntity};
pub use storage::ProjectLayout;
pub use syntax::{clean_object_markup, extract_explicit_objects, parse_line, ElementKind, ExplicitObject, ParsedLine};

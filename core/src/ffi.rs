//! C-compatible Foreign Function Interface (FFI) bindings for C++ (Windows) and Swift (macOS).

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::panic::catch_unwind;
use std::ptr;

use crate::export::{export_clean_text, ExportOptions};
use crate::objects::ObjectEngine;
use crate::syntax::{clean_object_markup, parse_line, ElementKind};

/// Opaque wrapper around ObjectEngine for C consumers.
pub struct RepoEngineOpaque {
    engine: ObjectEngine,
}

/// Returns the semantic version string of repo_core.
#[no_mangle]
pub extern "C" fn repo_core_version() -> *const c_char {
    static VERSION: &[u8] = b"0.4.0\0";
    VERSION.as_ptr() as *const c_char
}

/// Allocates a new ObjectEngine instance.
#[no_mangle]
pub extern "C" fn repo_engine_new() -> *mut RepoEngineOpaque {
    let result = catch_unwind(|| {
        Box::into_raw(Box::new(RepoEngineOpaque {
            engine: ObjectEngine::new(),
        }))
    });
    result.unwrap_or(ptr::null_mut())
}

/// Frees an ObjectEngine instance previously allocated by `repo_engine_new`.
#[no_mangle]
pub extern "C" fn repo_engine_free(engine: *mut RepoEngineOpaque) {
    if !engine.is_null() {
        let _ = catch_unwind(|| unsafe {
            drop(Box::from_raw(engine));
        });
    }
}

/// Registers an object in the engine. Writes the allocated ID into `out_id_buf`.
/// Returns 0 on success, negative error code on failure.
#[no_mangle]
pub extern "C" fn repo_engine_register(
    engine: *mut RepoEngineOpaque,
    name: *const c_char,
    category: *const c_char,
    out_id_buf: *mut c_char,
    out_id_len: usize,
) -> c_int {
    if engine.is_null() || name.is_null() {
        return -1;
    }

    let result = catch_unwind(|| unsafe {
        let eng = &mut (*engine).engine;
        let c_name = CStr::from_ptr(name).to_string_lossy();
        let c_cat = if category.is_null() {
            "character"
        } else {
            CStr::from_ptr(category).to_str().unwrap_or("character")
        };

        let id = eng.register(&c_name, c_cat);

        if !out_id_buf.is_null() && out_id_len > 0 {
            let bytes = id.as_bytes();
            let copy_len = bytes.len().min(out_id_len - 1);
            ptr::copy_nonoverlapping(bytes.as_ptr(), out_id_buf as *mut u8, copy_len);
            *out_id_buf.add(copy_len) = 0;
        }

        0
    });

    result.unwrap_or(-2)
}

/// Returns the total count of registered objects in the engine.
#[no_mangle]
pub extern "C" fn repo_engine_count(engine: *const RepoEngineOpaque) -> u32 {
    if engine.is_null() {
        return 0;
    }
    let result = catch_unwind(|| unsafe {
        (*engine).engine.count() as u32
    });
    result.unwrap_or(0)
}

/// Classifies a line of manuscript text.
/// Returns enum integer:
/// 0: Empty, 1: Scene, 2: Dialogue, 3: Description, 4: Narration, 5: Note, 6: PlainText, -1: Error
#[no_mangle]
pub extern "C" fn repo_classify_line(line: *const c_char) -> c_int {
    if line.is_null() {
        return -1;
    }
    let result = catch_unwind(|| unsafe {
        let text = CStr::from_ptr(line).to_string_lossy();
        let parsed = parse_line(&text);
        match parsed.kind {
            ElementKind::Empty => 0,
            ElementKind::Scene => 1,
            ElementKind::Dialogue => 2,
            ElementKind::Description => 3,
            ElementKind::Narration => 4,
            ElementKind::Note => 5,
            ElementKind::PlainText => 6,
        }
    });
    result.unwrap_or(-1)
}

/// Cleans object markup `[[...]]` from text. Writes null-terminated UTF-8 into `out_buf`.
/// Returns number of bytes written, or -1 on error.
#[no_mangle]
pub extern "C" fn repo_clean_markup(
    input: *const c_char,
    out_buf: *mut c_char,
    out_len: usize,
) -> c_int {
    if input.is_null() || out_buf.is_null() || out_len == 0 {
        return -1;
    }

    let result = catch_unwind(|| unsafe {
        let text = CStr::from_ptr(input).to_string_lossy();
        let cleaned = clean_object_markup(&text);
        let bytes = cleaned.as_bytes();
        let copy_len = bytes.len().min(out_len - 1);
        ptr::copy_nonoverlapping(bytes.as_ptr(), out_buf as *mut u8, copy_len);
        *out_buf.add(copy_len) = 0;
        copy_len as c_int
    });

    result.unwrap_or(-1)
}

/// Exports screenplay text with syntax stripped and formatting applied.
/// Returns number of bytes written, or -1 on error.
#[no_mangle]
pub extern "C" fn repo_export_clean(
    manuscript: *const c_char,
    omit_notes: c_int,
    out_buf: *mut c_char,
    out_len: usize,
) -> c_int {
    if manuscript.is_null() || out_buf.is_null() || out_len == 0 {
        return -1;
    }

    let result = catch_unwind(|| unsafe {
        let text = CStr::from_ptr(manuscript).to_string_lossy();
        let opts = ExportOptions {
            omit_notes: omit_notes != 0,
            screenplay_spacing: false,
        };
        let cleaned = export_clean_text(&text, opts);
        let bytes = cleaned.as_bytes();
        let copy_len = bytes.len().min(out_len - 1);
        ptr::copy_nonoverlapping(bytes.as_ptr(), out_buf as *mut u8, copy_len);
        *out_buf.add(copy_len) = 0;
        copy_len as c_int
    });

    result.unwrap_or(-1)
}

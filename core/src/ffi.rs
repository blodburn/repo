//! C-compatible Foreign Function Interface (FFI) bindings for C++ (Windows) and Swift (macOS).
//!
//! # Buffer protocol
//!
//! Every function that returns text writes UTF-8 into a caller supplied buffer and
//! never allocates memory owned by the caller. The return value is the number of
//! bytes that the complete output requires **including the terminating NUL**, so a
//! caller can size a buffer with one probe call:
//!
//! ```c
//! long needed = repo_extract_objects(text, NULL, 0);   /* probe */
//! char* buffer = malloc(needed);
//! repo_extract_objects(text, buffer, needed);          /* fill  */
//! ```
//!
//! A negative return value is an error code (`REPO_FFI_*` in `repo_core.h`).
//!
//! # Record formats
//!
//! `repo_extract_objects` returns one record per explicit object, fields separated
//! by a tab and records terminated by a newline:
//!
//! ```text
//! name \t normalized_type \t alias_or_empty \n
//! ```
//!
//! `repo_find_mentions` returns one record per mention in document order:
//!
//! ```text
//! object_id \t line_index \t utf16_offset \t byte_offset \t matched_name \n
//! ```

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::panic::catch_unwind;
use std::ptr;

use crate::export::{export_clean_text, ExportOptions};
use crate::objects::ObjectEngine;
use crate::syntax::{clean_object_markup, extract_explicit_objects, parse_line, ElementKind};

/// `REPO_OK` — call succeeded.
pub const REPO_OK: c_int = 0;
/// `REPO_ERR_NULL` — a required argument was NULL or a buffer length was zero.
pub const REPO_ERR_NULL: c_int = -1;
/// `REPO_ERR_PANIC` — the Rust side caught a panic while serving the call.
pub const REPO_ERR_PANIC: c_int = -2;
/// `REPO_ERR_UTF8` — an input string was not valid UTF-8.
pub const REPO_ERR_UTF8: c_int = -3;

/// Opaque wrapper around ObjectEngine for C consumers.
pub struct RepoEngineOpaque {
    engine: ObjectEngine,
}

/// Returns the semantic version string of repo_core. The pointer is static and
/// must not be freed.
#[no_mangle]
pub extern "C" fn repo_core_version() -> *const c_char {
    static VERSION: &[u8] = b"0.4.2\0";
    VERSION.as_ptr() as *const c_char
}

/// Returns the integer ABI revision of this header. Increment whenever an
/// existing signature or record format changes in an incompatible way.
#[no_mangle]
pub extern "C" fn repo_core_abi_version() -> u32 {
    1
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
        return REPO_ERR_NULL;
    }

    let result = catch_unwind(|| unsafe {
        let c_name = match CStr::from_ptr(name).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };
        let c_cat = if category.is_null() {
            "character".to_string()
        } else {
            match CStr::from_ptr(category).to_str() {
                Ok(value) => value.to_string(),
                Err(_) => return REPO_ERR_UTF8,
            }
        };

        let eng = &mut (*engine).engine;
        let id = eng.register(c_name, &c_cat);
        copy_text(&id, out_id_buf, out_id_len);
        REPO_OK
    });

    result.unwrap_or(REPO_ERR_PANIC)
}

/// Restores an object together with the identifier owned by the caller
/// (typically the GUI project database row id).
///
/// The identifier is opaque to the core: it is stored verbatim and returned in
/// `repo_find_mentions` records. Returns 1 when the object was added, 0 when the
/// identifier or the name was already registered, and a negative error otherwise.
#[no_mangle]
pub extern "C" fn repo_engine_register_with_id(
    engine: *mut RepoEngineOpaque,
    object_id: *const c_char,
    name: *const c_char,
    category: *const c_char,
) -> c_int {
    if engine.is_null() || object_id.is_null() || name.is_null() {
        return REPO_ERR_NULL;
    }

    let result = catch_unwind(|| unsafe {
        let id = match CStr::from_ptr(object_id).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };
        let object_name = match CStr::from_ptr(name).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };
        let category = if category.is_null() {
            "character"
        } else {
            match CStr::from_ptr(category).to_str() {
                Ok(value) => value,
                Err(_) => return REPO_ERR_UTF8,
            }
        };

        if (*engine).engine.register_with_id(id, object_name, category) {
            1
        } else {
            0
        }
    });

    result.unwrap_or(REPO_ERR_PANIC)
}

/// Adds an alias to an object previously registered in the engine.
/// Returns 1 when added, 0 when the alias already exists or the object is unknown.
#[no_mangle]
pub extern "C" fn repo_engine_add_alias(
    engine: *mut RepoEngineOpaque,
    object_id: *const c_char,
    alias: *const c_char,
) -> c_int {
    if engine.is_null() || object_id.is_null() || alias.is_null() {
        return REPO_ERR_NULL;
    }

    let result = catch_unwind(|| unsafe {
        let id = match CStr::from_ptr(object_id).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };
        let alias_text = match CStr::from_ptr(alias).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };
        if (*engine).engine.add_alias(id, alias_text) {
            1
        } else {
            0
        }
    });

    result.unwrap_or(REPO_ERR_PANIC)
}

/// Returns the total count of registered objects in the engine.
#[no_mangle]
pub extern "C" fn repo_engine_count(engine: *const RepoEngineOpaque) -> u32 {
    if engine.is_null() {
        return 0;
    }
    let result = catch_unwind(|| unsafe { (*engine).engine.count() as u32 });
    result.unwrap_or(0)
}

/// Classifies a line of manuscript text without returning its content.
/// Returns enum integer:
/// 0: Empty, 1: Scene, 2: Dialogue, 3: Description, 4: Narration, 5: Note,
/// 6: PlainText, negative: error.
#[no_mangle]
pub extern "C" fn repo_classify_line(line: *const c_char) -> c_int {
    if line.is_null() {
        return REPO_ERR_NULL;
    }
    let result = catch_unwind(|| unsafe {
        let text = match CStr::from_ptr(line).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };
        element_kind_code(parse_line(text).kind)
    });
    result.unwrap_or(REPO_ERR_PANIC)
}

/// Parses one manuscript line and writes its cleaned content (wrapper markers
/// stripped, `[[object]]` markup reduced to the display name) into `out_content`.
///
/// The return value is the element kind code (see `repo_classify_line`), so a
/// caller gets both classification and text from a single crossing of the FFI.
#[no_mangle]
pub extern "C" fn repo_parse_line(
    line: *const c_char,
    out_content: *mut c_char,
    out_content_len: usize,
) -> c_int {
    if line.is_null() {
        return REPO_ERR_NULL;
    }

    let result = catch_unwind(|| unsafe {
        let text = match CStr::from_ptr(line).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };
        let parsed = parse_line(text);
        copy_text(&parsed.content, out_content, out_content_len);
        element_kind_code(parsed.kind)
    });

    result.unwrap_or(REPO_ERR_PANIC)
}

/// Cleans object markup `[[...]]` from text, keeping the display name.
/// Returns the number of bytes written (excluding the NUL), or a negative error.
#[no_mangle]
pub extern "C" fn repo_clean_markup(
    input: *const c_char,
    out_buf: *mut c_char,
    out_len: usize,
) -> c_int {
    if input.is_null() || out_buf.is_null() || out_len == 0 {
        return REPO_ERR_NULL;
    }

    let result = catch_unwind(|| unsafe {
        let text = match CStr::from_ptr(input).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };
        let cleaned = clean_object_markup(text);
        copy_text(&cleaned, out_buf, out_len)
    });

    result.unwrap_or(REPO_ERR_PANIC)
}

/// Lists every explicit `[[...]]` object marker in `manuscript`.
///
/// Record format: `name \t normalized_type \t alias_or_empty \n`.
/// Returns the required buffer size including the NUL, or a negative error.
#[no_mangle]
pub extern "C" fn repo_extract_objects(
    manuscript: *const c_char,
    out_buf: *mut c_char,
    out_len: usize,
) -> c_int {
    if manuscript.is_null() {
        return REPO_ERR_NULL;
    }

    let result = catch_unwind(|| unsafe {
        let text = match CStr::from_ptr(manuscript).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };

        let mut out = String::new();
        for object in extract_explicit_objects(text) {
            out.push_str(&sanitize_field(&object.name));
            out.push('\t');
            out.push_str(&sanitize_field(&object.object_type));
            out.push('\t');
            if let Some(alias) = object.alias.as_deref() {
                out.push_str(&sanitize_field(alias));
            }
            out.push('\n');
        }
        copy_text_sized(&out, out_buf, out_len)
    });

    result.unwrap_or(REPO_ERR_PANIC)
}

/// Finds every mention of a registered object in `manuscript`.
///
/// Record format: `object_id \t line_index \t utf16_offset \t byte_offset \t matched_name \n`.
/// The `object_id` is the identifier handed to `repo_engine_register_with_id`.
/// Returns the required buffer size including the NUL, or a negative error.
#[no_mangle]
pub extern "C" fn repo_find_mentions(
    engine: *const RepoEngineOpaque,
    manuscript: *const c_char,
    out_buf: *mut c_char,
    out_len: usize,
) -> c_int {
    if engine.is_null() || manuscript.is_null() {
        return REPO_ERR_NULL;
    }

    let result = catch_unwind(|| unsafe {
        let text = match CStr::from_ptr(manuscript).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };

        let mut out = String::new();
        for mention in (*engine).engine.scan_document(text) {
            out.push_str(&sanitize_field(&mention.object_id));
            out.push('\t');
            out.push_str(&mention.line_index.to_string());
            out.push('\t');
            out.push_str(&mention.utf16_offset.to_string());
            out.push('\t');
            out.push_str(&mention.byte_offset.to_string());
            out.push('\t');
            out.push_str(&sanitize_field(&mention.matched_name));
            out.push('\n');
        }
        copy_text_sized(&out, out_buf, out_len)
    });

    result.unwrap_or(REPO_ERR_PANIC)
}

/// Exports screenplay text with syntax stripped and formatting applied.
/// Returns the number of bytes written, or -1 on error.
#[no_mangle]
pub extern "C" fn repo_export_clean(
    manuscript: *const c_char,
    omit_notes: c_int,
    out_buf: *mut c_char,
    out_len: usize,
) -> c_int {
    if manuscript.is_null() || out_buf.is_null() || out_len == 0 {
        return REPO_ERR_NULL;
    }

    let result = catch_unwind(|| unsafe {
        let text = match CStr::from_ptr(manuscript).to_str() {
            Ok(value) => value,
            Err(_) => return REPO_ERR_UTF8,
        };
        let opts = ExportOptions {
            omit_notes: omit_notes != 0,
            screenplay_spacing: false,
        };
        let cleaned = export_clean_text(text, opts);
        copy_text(&cleaned, out_buf, out_len)
    });

    result.unwrap_or(REPO_ERR_PANIC)
}

fn element_kind_code(kind: ElementKind) -> c_int {
    match kind {
        ElementKind::Empty => 0,
        ElementKind::Scene => 1,
        ElementKind::Dialogue => 2,
        ElementKind::Description => 3,
        ElementKind::Narration => 4,
        ElementKind::Note => 5,
        ElementKind::PlainText => 6,
    }
}

/// Field values are tab/newline separated, so those characters are replaced to
/// keep records unambiguous even if an object name contains a control character.
fn sanitize_field(value: &str) -> String {
    value
        .chars()
        .map(|c| if c == '\t' || c == '\n' || c == '\r' { ' ' } else { c })
        .collect()
}

/// Copies `value` into a caller buffer. Returns bytes written excluding the NUL,
/// or a negative error when the buffer is unusable.
fn copy_text(value: &str, out_buf: *mut c_char, out_len: usize) -> c_int {
    if out_buf.is_null() || out_len == 0 {
        return REPO_ERR_NULL;
    }
    sized_copy(value, out_buf, out_len) as c_int
}

/// Copies `value` and reports the size the full output needs (NUL included).
fn copy_text_sized(value: &str, out_buf: *mut c_char, out_len: usize) -> c_int {
    if out_buf.is_null() || out_len == 0 {
        return (value.len() + 1) as c_int;
    }
    let required = value.len() + 1;
    sized_copy(value, out_buf, out_len);
    required as c_int
}

fn sized_copy(value: &str, out_buf: *mut c_char, out_len: usize) -> usize {
    let bytes = value.as_bytes();
    let copy_len = bytes.len().min(out_len - 1);
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), out_buf as *mut u8, copy_len);
        *out_buf.add(copy_len) = 0;
    }
    copy_len
}

/// Convenience helper for tests and embedders: returns the C string version of a
/// Rust string, panicking only on interior NUL which callers never produce.
#[allow(dead_code)]
pub(crate) fn c_string(value: &str) -> CString {
    CString::new(value).unwrap_or_else(|_| CString::new("").expect("empty C string"))
}

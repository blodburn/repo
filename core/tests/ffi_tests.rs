//! ABI level tests for the C interface used by the macOS and Windows GUIs.
//!
//! These tests call the exported `extern "C"` symbols exactly as the Swift and
//! C++ clients do, including the two step buffer protocol, so a mismatch between
//! the header contract and the Rust implementation fails here rather than inside
//! a release build.

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use repo_core::ffi::{
    repo_classify_line, repo_clean_markup, repo_core_abi_version, repo_core_version,
    repo_engine_add_alias, repo_engine_count, repo_engine_free, repo_engine_new,
    repo_engine_register, repo_engine_register_with_id, repo_export_clean, repo_extract_objects,
    repo_find_mentions, repo_parse_line, REPO_ERR_NULL, REPO_OK,
};

fn c(value: &str) -> CString {
    CString::new(value).expect("test strings contain no interior NUL")
}

/// Runs a "required size" style call twice, exactly like the client wrappers.
fn probe_and_fill(
    call: impl Fn(*mut c_char, usize) -> i32,
) -> (i32, String) {
    let required = call(std::ptr::null_mut(), 0);
    assert!(required > 0, "probe must return the required size");
    let mut buffer = vec![0u8; required as usize];
    let written = call(buffer.as_mut_ptr() as *mut c_char, buffer.len());
    assert_eq!(written, required);
    let text = unsafe { CStr::from_ptr(buffer.as_ptr() as *const c_char) }
        .to_str()
        .expect("output is UTF-8")
        .to_string();
    (written, text)
}

#[test]
fn version_and_abi_are_reported() {
    let version = unsafe { CStr::from_ptr(repo_core_version()) }.to_str().unwrap();
    assert_eq!(version, env!("CARGO_PKG_VERSION"));
    assert_eq!(version, "0.4.2");
    assert_eq!(repo_core_abi_version(), 1);
}

#[test]
fn classify_and_parse_line_agree() {
    let cases = [
        ("", 0, ""),
        ("# INT. 병원 복도 - 밤", 1, "INT. 병원 복도 - 밤"),
        ("@@ 박영희는 어디 있어? @@", 2, "박영희는 어디 있어?"),
        ("## [[김철수]]가 문을 연다 ##", 3, "김철수가 문을 연다"),
        ("₩₩ 그날 밤 비가 내렸다 ₩₩", 4, "그날 밤 비가 내렸다"),
        ("// 감정선 재검토", 5, "감정선 재검토"),
        ("그냥 문장입니다", 6, "그냥 문장입니다"),
    ];

    for (line, expected_kind, expected_content) in cases {
        let owned = c(line);
        assert_eq!(repo_classify_line(owned.as_ptr()), expected_kind, "line: {line}");

        let mut buffer = vec![0u8; 512];
        let kind = repo_parse_line(owned.as_ptr(), buffer.as_mut_ptr() as *mut c_char, buffer.len());
        assert_eq!(kind, expected_kind, "line: {line}");
        let content = unsafe { CStr::from_ptr(buffer.as_ptr() as *const c_char) }.to_str().unwrap();
        assert_eq!(content, expected_content, "line: {line}");
    }
}

#[test]
fn parse_line_handles_null_arguments() {
    assert_eq!(repo_classify_line(std::ptr::null()), REPO_ERR_NULL);
    let owned = c("# 장면");
    assert_eq!(
        repo_parse_line(owned.as_ptr(), std::ptr::null_mut(), 0),
        1,
        "classification is still returned when no content buffer is supplied"
    );
}

#[test]
fn clean_markup_reduces_object_syntax() {
    let input = c("[[김철수]]와 [[장소:서울역]] 그리고 [[철수|김철수]]");
    let mut buffer = vec![0u8; 256];
    let written = repo_clean_markup(input.as_ptr(), buffer.as_mut_ptr() as *mut c_char, buffer.len());
    assert!(written > 0);
    let text = unsafe { CStr::from_ptr(buffer.as_ptr() as *const c_char) }.to_str().unwrap();
    assert_eq!(text, "김철수와 서울역 그리고 김철수");
    assert_eq!(written as usize, text.len());

    assert_eq!(
        repo_clean_markup(std::ptr::null(), buffer.as_mut_ptr() as *mut c_char, buffer.len()),
        REPO_ERR_NULL
    );
}

#[test]
fn extract_objects_returns_tab_separated_records() {
    let manuscript = c(
        "# 병원 복도\n[[김철수]]가 들어온다\n[[장소:서울역]]\n[[철수|김철수]]\n",
    );
    let (_, records) = probe_and_fill(|buffer, len| {
        repo_extract_objects(manuscript.as_ptr(), buffer, len)
    });

    let rows: Vec<Vec<&str>> = records
        .lines()
        .map(|line| line.split('\t').collect())
        .collect();

    assert_eq!(rows.len(), 3);
    assert_eq!(rows[0], vec!["김철수", "character", ""]);
    assert_eq!(rows[1], vec!["서울역", "location", ""]);
    assert_eq!(rows[2], vec!["김철수", "character", "철수"]);
}

#[test]
fn engine_restores_caller_owned_ids_and_finds_mentions() {
    let engine = repo_engine_new();
    assert!(!engine.is_null());

    let id_one = c("17");
    let name_one = c("김철수");
    let id_two = c("42");
    let name_two = c("서울역");
    let character = c("character");
    let location = c("location");

    assert_eq!(
        repo_engine_register_with_id(engine, id_one.as_ptr(), name_one.as_ptr(), character.as_ptr()),
        1
    );
    assert_eq!(
        repo_engine_register_with_id(engine, id_two.as_ptr(), name_two.as_ptr(), location.as_ptr()),
        1
    );
    // Registering the same name twice is reported, not added twice.
    assert_eq!(
        repo_engine_register_with_id(engine, id_one.as_ptr(), name_one.as_ptr(), character.as_ptr()),
        0
    );
    assert_eq!(repo_engine_count(engine), 2);

    let alias = c("철수");
    assert_eq!(repo_engine_add_alias(engine, id_one.as_ptr(), alias.as_ptr()), 1);

    let manuscript = c(
        "[[김철수]]가 서울역에서 기다린다.\n\
         두 번째 줄: 철수가 도착했다. 김철수는 웃었다. 김철수산물과는 무관하다.\n",
    );

    let (_, records) = probe_and_fill(|buffer, len| {
        repo_find_mentions(engine, manuscript.as_ptr(), buffer, len)
    });

    let rows: Vec<Vec<&str>> = records
        .lines()
        .map(|line| line.split('\t').collect())
        .collect();

    // Line 0: [[김철수]] and 서울역. Line 1: 철수 (alias), 김철수는 (particle).
    // "김철수산물" must be rejected by the Korean suffix boundary rule.
    assert_eq!(rows.len(), 4, "records: {records:?}");
    assert_eq!(rows[0][0], "17");
    assert_eq!(rows[0][1], "0");
    assert_eq!(rows[0][4], "김철수");
    assert_eq!(rows[1][0], "42");
    assert_eq!(rows[1][1], "0");
    assert_eq!(rows[1][4], "서울역");

    // The alias mention maps back to the same caller owned id.
    assert_eq!(rows[2][0], "17");
    assert_eq!(rows[2][1], "1");
    assert_eq!(rows[2][4], "철수");

    assert_eq!(rows[3][0], "17");
    assert_eq!(rows[3][1], "1");
    assert_eq!(rows[3][4], "김철수");

    // UTF-16 offsets address the document the way NSTextView and RICHEDIT do.
    // The mention points at the name inside the markup, i.e. after "[[".
    let first = rows[0][2].parse::<usize>().unwrap();
    assert_eq!(first, 2);
    let line_one_start = "[[김철수]]가 서울역에서 기다린다.\n".encode_utf16().count();
    let second = rows[2][2].parse::<usize>().unwrap();
    let expected_line_one = line_one_start + "두 번째 줄: ".encode_utf16().count();
    assert_eq!(second, expected_line_one);

    let third = rows[3][2].parse::<usize>().unwrap();
    assert_eq!(
        third,
        expected_line_one + "철수가 도착했다. ".encode_utf16().count()
    );

    repo_engine_free(engine);
}

#[test]
fn engine_register_allocates_ids_and_reports_errors() {
    let engine = repo_engine_new();
    let name = c("박영희");
    let category = c("character");
    let mut buffer = vec![0u8; 64];

    assert_eq!(
        repo_engine_register(engine, name.as_ptr(), category.as_ptr(), buffer.as_mut_ptr() as *mut c_char, buffer.len()),
        REPO_OK
    );
    let id = unsafe { CStr::from_ptr(buffer.as_ptr() as *const c_char) }.to_str().unwrap();
    assert_eq!(id, "CHAR-0001");
    assert_eq!(repo_engine_count(engine), 1);

    assert_eq!(
        repo_engine_register(std::ptr::null_mut(), name.as_ptr(), category.as_ptr(), buffer.as_mut_ptr() as *mut c_char, buffer.len()),
        REPO_ERR_NULL
    );

    repo_engine_free(engine);
    // Freeing twice must not be attempted by callers; freeing NULL is a no-op.
    repo_engine_free(std::ptr::null_mut());
}

#[test]
fn find_mentions_rejects_null_engine() {
    let manuscript = c("김철수");
    let mut buffer = vec![0u8; 128];
    assert_eq!(
        repo_find_mentions(std::ptr::null(), manuscript.as_ptr(), buffer.as_mut_ptr() as *mut c_char, buffer.len()),
        REPO_ERR_NULL
    );
}

#[test]
fn export_clean_matches_document_format_rules() {
    let manuscript = c("# 장면\n@@ 대사 @@\n## 지문 ##\n₩₩ 나레이션 ₩₩\n// 메모\n");

    let mut with_notes = vec![0u8; 1024];
    let written = repo_export_clean(
        manuscript.as_ptr(),
        0,
        with_notes.as_mut_ptr() as *mut c_char,
        with_notes.len(),
    );
    assert!(written > 0);
    let text = unsafe { CStr::from_ptr(with_notes.as_ptr() as *const c_char) }.to_str().unwrap();
    assert!(text.contains("    대사"));
    assert!(text.contains("  (나레이션) 나레이션"));
    assert!(text.contains("// 메모"));

    let mut without_notes = vec![0u8; 1024];
    repo_export_clean(
        manuscript.as_ptr(),
        1,
        without_notes.as_mut_ptr() as *mut c_char,
        without_notes.len(),
    );
    let text = unsafe { CStr::from_ptr(without_notes.as_ptr() as *const c_char) }.to_str().unwrap();
    assert!(!text.contains("메모"));
}

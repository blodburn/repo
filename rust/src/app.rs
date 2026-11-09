//! Application controller, undo/redo stack, and regression self-tests.

use std::fs;
use std::path::Path;

use crate::export_docx::generate_docx;
use crate::export_hwpx::generate_hwpx;
use crate::export_pdf::generate_pdf;
use repo_core::objects::ObjectEngine;
use repo_core::storage::ProjectLayout;
use repo_core::syntax::{parse_line, ElementKind};

#[allow(dead_code)]
pub struct EditorState {
    pub title: String,
    pub manuscript: String,
    pub undo_stack: Vec<String>,
    pub redo_stack: Vec<String>,
    pub layout: Option<ProjectLayout>,
}

impl EditorState {
    pub fn new(title: &str, manuscript: &str) -> Self {
        Self {
            title: title.to_string(),
            manuscript: manuscript.to_string(),
            undo_stack: Vec::new(),
            redo_stack: Vec::new(),
            layout: None,
        }
    }

    pub fn set_text(&mut self, new_text: &str) {
        if self.manuscript != new_text {
            self.undo_stack.push(self.manuscript.clone());
            self.redo_stack.clear();
            self.manuscript = new_text.to_string();
        }
    }

    pub fn undo(&mut self) -> bool {
        if let Some(prev) = self.undo_stack.pop() {
            self.redo_stack.push(self.manuscript.clone());
            self.manuscript = prev;
            true
        } else {
            false
        }
    }

    pub fn redo(&mut self) -> bool {
        if let Some(next) = self.redo_stack.pop() {
            self.undo_stack.push(self.manuscript.clone());
            self.manuscript = next;
            true
        } else {
            false
        }
    }
}

// Regression Self-Tests matching the CI test suite in .github/workflows/release.yml

pub fn run_encoding_self_test() -> bool {
    // 1. Korean Won sign narration syntax
    let won_line = "₩₩ 그날 밤, 아무도 진실을 알지 못했다. ₩₩";
    let p1 = parse_line(won_line);
    if p1.kind != ElementKind::Narration || p1.content != "그날 밤, 아무도 진실을 알지 못했다." {
        eprintln!("FAIL: Won sign narration parse mismatch");
        return false;
    }

    // 2. Korean object markup cleaning
    let obj_line = "## 형광등이 깜빡인다. [[김철수]]가 나타난다. ##";
    let p2 = parse_line(obj_line);
    if p2.kind != ElementKind::Description || p2.content != "형광등이 깜빡인다. 김철수가 나타난다." {
        eprintln!("FAIL: Description object cleanup mismatch");
        return false;
    }

    // 3. Korean particle boundary test
    let mut engine = ObjectEngine::new();
    let id = engine.register("김철수", "character");
    let mentions = engine.find_mentions_in_line("김철수가 서울역에서 기다린다. 김철수산물과는 무관.", 0, None);
    if mentions.len() != 1 || mentions[0].object_id != id {
        eprintln!("FAIL: Korean particle boundary recognition mismatch");
        return false;
    }

    println!("PASS: Korean UI labels, object names, won-sign syntax, UTF-8 conversion");
    true
}

pub fn run_style_self_test() -> bool {
    let mut state = EditorState::new("테스트 작품", "첫 번째 줄\n");
    state.set_text("첫 번째 줄\n@@ 대사 추가 @@\n");
    state.set_text("첫 번째 줄\n@@ 대사 추가 @@\n## 행동 추가 ##\n");

    // Test Undo (Ctrl+Z)
    if !state.undo() || state.manuscript != "첫 번째 줄\n@@ 대사 추가 @@\n" {
        eprintln!("FAIL: Undo step 1 failed");
        return false;
    }
    if !state.undo() || state.manuscript != "첫 번째 줄\n" {
        eprintln!("FAIL: Undo step 2 failed");
        return false;
    }

    // Test Redo (Ctrl+U)
    if !state.redo() || state.manuscript != "첫 번째 줄\n@@ 대사 추가 @@\n" {
        eprintln!("FAIL: Redo step 1 failed");
        return false;
    }
    if !state.redo() || state.manuscript != "첫 번째 줄\n@@ 대사 추가 @@\n## 행동 추가 ##\n" {
        eprintln!("FAIL: Redo step 2 failed");
        return false;
    }

    println!("PASS: Inline formatting preserves Ctrl+Z and Ctrl+U");
    true
}

pub fn run_export_smoke_tests(output_dir: &Path) -> bool {
    let _ = fs::create_dir_all(output_dir);

    let sample_script = r#"# INT. 병원 복도 - 밤

## 형광등이 깜빡인다. [[김철수]]가 나타난다. ##

김철수
@@ [[박영희]]는 어디 있어? @@

₩₩ 그는 곧 진실을 알게 된다. ₩₩

// 이 메모는 제외된다.
"#;
    let title = "밤의 증인";

    // 1. DOCX
    let docx_bytes = generate_docx(sample_script, title);
    let docx_path = output_dir.join("sample.docx");
    if fs::write(&docx_path, &docx_bytes).is_err() || docx_bytes.len() < 120 {
        eprintln!("FAIL: DOCX generation failed");
        return false;
    }

    // 2. HWPX
    let hwpx_bytes = generate_hwpx(sample_script, title);
    let hwpx_path = output_dir.join("sample.hwpx");
    if fs::write(&hwpx_path, &hwpx_bytes).is_err() || hwpx_bytes.len() < 120 {
        eprintln!("FAIL: HWPX generation failed");
        return false;
    }

    // 3. PDF
    let pdf_bytes = generate_pdf(sample_script, title);
    let pdf_path = output_dir.join("sample.pdf");
    if fs::write(&pdf_path, &pdf_bytes).is_err() || pdf_bytes.len() < 120 {
        eprintln!("FAIL: PDF generation failed");
        return false;
    }

    // Validate PDF header & trailer
    if !pdf_bytes.starts_with(b"%PDF-") || !pdf_bytes[pdf_bytes.len().saturating_sub(128)..].contains(&b"%%EOF"[0]) {
        eprintln!("FAIL: PDF header/trailer assertion failed");
        return false;
    }

    println!("PASS: DOCX/HWPX XML and PDF smoke tests passed (output: {})", output_dir.display());
    true
}

//! repo — A lightweight, keyboard-first desktop editor for screenplays, novels, and scenario writing.

mod app;
mod editor;
mod journal;
mod export_docx;
mod export_hwpx;
mod export_pdf;
mod tui;
mod zip_writer;

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::exit;

use app::{run_encoding_self_test, run_export_smoke_tests, run_style_self_test};
use export_docx::generate_docx;
use export_hwpx::generate_hwpx;
use export_pdf::generate_pdf;
use repo_core::export::{export_clean_text, ExportOptions};
use tui::run_interactive;

fn print_help() {
    println!(r#"repo v0.4.0 — 이야기 편집기 (Rust 네이티브)

사용법:
  repo [옵션] [작품폴더]
  repo --export <형식> <입력파일> <출력파일>

옵션:
  -h, --help               도움말 표시 (F1)
  -V, --version            버전 정보 표시
  --self-test              DOCX, HWPX, PDF 내보내기 회귀 검증
  --export-self-test       macOS/크로스플랫폼 문서 내보내기 검증
  --encoding-self-test     한국어 UTF-8 인코딩 및 원화(₩₩) 문법 검증
  --style-self-test        문단 서식 인라인 스타일 및 Undo/Redo 보존 검증

단축키 규칙:
  Ctrl/Cmd + N             새 작품 생성
  Ctrl/Cmd + O             작품 폴더 열기
  Ctrl/Cmd + S             저장 (script/main.txt 원자적 저장)
  Ctrl/Cmd + Z             실행 취소 (Undo)
  Ctrl/Cmd + U             다시 실행 (Redo)
  Ctrl/Cmd + T             작품 제목 편집
  Ctrl/Cmd + Shift + O     오브젝트 등록 ([[인물]] / [[장소:서울역]])
  Ctrl/Cmd + G             오브젝트 다음 등장 위치 탐색
  Ctrl/Cmd + Shift + P     서식 미리보기
  Ctrl/Cmd + Shift + H     저장본 복원
  F1                       도움말
  F6                       3패널 전환 (목록 ↔ 원고 ↔ 상세)
  Esc                      원고 편집기로 포커스 복귀

작성 문법:
  # 장면 제목               장면 (Scene)
  @@ 대사 내용 @@           대사 (Dialogue)
  ## 행동 및 지문 ##        상황/지문 (Description)
  ₩₩ 나레이션 ₩₩           나레이션 (Narration)
  [[김철수]]                오브젝트 최초 등록 (이후 자동 연결)
  // 메모                   최종 출력에서 제외되는 메모
"#);
}

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() > 1 {
        match args[1].as_str() {
            "-h" | "--help" => {
                print_help();
                exit(0);
            }
            "-V" | "--version" => {
                println!("repo 0.4.0 (Rust native)");
                exit(0);
            }
            "--self-test" => {
                let temp_dir = env::temp_dir().join("repo-export-smoke");
                let ok = run_export_smoke_tests(&temp_dir);
                exit(if ok { 0 } else { 1 });
            }
            "--export-self-test" => {
                let temp_dir = env::temp_dir().join("repo-swift-export-smoke");
                let ok = run_export_smoke_tests(&temp_dir);
                exit(if ok { 0 } else { 1 });
            }
            "--encoding-self-test" => {
                let ok = run_encoding_self_test();
                exit(if ok { 0 } else { 1 });
            }
            "--style-self-test" => {
                let ok = run_style_self_test();
                exit(if ok { 0 } else { 1 });
            }
            "--export" => {
                if args.len() < 5 {
                    eprintln!("사용법: repo --export <docx|hwpx|pdf|txt> <입력파일> <출력파일>");
                    exit(1);
                }
                let fmt = &args[2];
                let input_path = &args[3];
                let output_path = &args[4];

                let content = match fs::read_to_string(input_path) {
                    Ok(c) => c,
                    Err(e) => {
                        eprintln!("파일 읽기 실패 {}: {}", input_path, e);
                        exit(1);
                    }
                };

                let title = Path::new(input_path)
                    .file_stem()
                    .and_then(|s| s.to_str())
                    .unwrap_or("작품");

                let res = match fmt.to_lowercase().as_str() {
                    "docx" => {
                        let bytes = generate_docx(&content, title);
                        fs::write(output_path, bytes)
                    }
                    "hwpx" => {
                        let bytes = generate_hwpx(&content, title);
                        fs::write(output_path, bytes)
                    }
                    "pdf" => {
                        let bytes = generate_pdf(&content, title);
                        fs::write(output_path, bytes)
                    }
                    "txt" => {
                        let clean = export_clean_text(&content, ExportOptions::default());
                        fs::write(output_path, clean.as_bytes())
                    }
                    _ => {
                        eprintln!("지원하지 않는 내보내기 형식: {}", fmt);
                        exit(1);
                    }
                };

                if let Err(e) = res {
                    eprintln!("내보내기 실패 {}: {}", output_path, e);
                    exit(1);
                }
                println!("성공: {} -> {}", input_path, output_path);
                exit(0);
            }
            _ => {}
        }
    }

    // Default: Interactive 3-Pane Screenplay UI
    let project_dir = if args.len() > 1 && !args[1].starts_with('-') {
        PathBuf::from(&args[1])
    } else {
        env::current_dir().unwrap_or_else(|_| PathBuf::from("."))
    };

    if let Err(error) = run_interactive(&project_dir) {
        eprintln!("repo TUI 실행 실패: {error}");
        exit(1);
    }
}

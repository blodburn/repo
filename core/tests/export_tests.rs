use repo_core::export::{export_clean_text, ExportOptions};

#[test]
fn test_clean_export_omits_notes_and_cleans_markup() {
    let script = r#"# INT. 병원 복도 - 밤

## 형광등이 깜빡인다. [[김철수]]가 나타난다. ##

김철수
@@ [[박영희]]는 어디 있어? @@

₩₩ 그날 모든 것이 시작됐다. ₩₩

// 이 메모는 출력에서 제외되어야 한다.
"#;

    let clean = export_clean_text(script, ExportOptions::default());

    assert!(!clean.contains("//"));
    assert!(!clean.contains("이 메모는 출력에서 제외되어야 한다."));
    assert!(!clean.contains("[["));
    assert!(!clean.contains("]]"));
    assert!(!clean.contains("@@"));
    assert!(!clean.contains("##"));
    assert!(!clean.contains("₩₩"));

    assert!(clean.contains("INT. 병원 복도 - 밤"));
    assert!(clean.contains("형광등이 깜빡인다. 김철수가 나타난다."));
    assert!(clean.contains("박영희는 어디 있어?"));
    assert!(clean.contains("그날 모든 것이 시작됐다."));
}

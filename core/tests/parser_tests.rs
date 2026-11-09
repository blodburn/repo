use repo_core::syntax::{clean_object_markup, extract_explicit_objects, parse_line, ElementKind};

#[test]
fn test_parse_scene() {
    let line = "# INT. 병원 복도 - 밤";
    let parsed = parse_line(line);
    assert_eq!(parsed.kind, ElementKind::Scene);
    assert_eq!(parsed.content, "INT. 병원 복도 - 밤");
}

#[test]
fn test_parse_dialogue() {
    let line = "@@ 박영희는 어디 있어? @@";
    let parsed = parse_line(line);
    assert_eq!(parsed.kind, ElementKind::Dialogue);
    assert_eq!(parsed.content, "박영희는 어디 있어?");
}

#[test]
fn test_parse_description() {
    let line = "## 형광등이 깜빡인다. [[김철수]]가 나타난다. ##";
    let parsed = parse_line(line);
    assert_eq!(parsed.kind, ElementKind::Description);
    assert_eq!(parsed.content, "형광등이 깜빡인다. 김철수가 나타난다.");
    assert_eq!(parsed.explicit_objects.len(), 1);
    assert_eq!(parsed.explicit_objects[0].name, "김철수");
    assert_eq!(parsed.explicit_objects[0].object_type, "character");
}

#[test]
fn test_parse_narration_won_sign() {
    let line = "₩₩ 그날 모든 것이 시작됐다. ₩₩";
    let parsed = parse_line(line);
    assert_eq!(parsed.kind, ElementKind::Narration);
    assert_eq!(parsed.content, "그날 모든 것이 시작됐다.");
}

#[test]
fn test_parse_narration_backslash() {
    let line = "\\\\ 어둠이 내려앉았다. \\\\";
    let parsed = parse_line(line);
    assert_eq!(parsed.kind, ElementKind::Narration);
    assert_eq!(parsed.content, "어둠이 내려앉았다.");
}

#[test]
fn test_parse_note() {
    let line = "// 이 메모는 최종 문서에서 제외한다.";
    let parsed = parse_line(line);
    assert_eq!(parsed.kind, ElementKind::Note);
    assert_eq!(parsed.content, "이 메모는 최종 문서에서 제외한다.");
}

#[test]
fn test_explicit_objects_parsing() {
    let text = "[[장소:서울역]]에서 [[철수|김철수]]와 만났다. [[사건:살인사건]]";
    let objs = extract_explicit_objects(text);
    assert_eq!(objs.len(), 3);

    assert_eq!(objs[0].name, "서울역");
    assert_eq!(objs[0].object_type, "location");

    assert_eq!(objs[1].name, "김철수");
    assert_eq!(objs[1].alias, Some("철수".to_string()));

    assert_eq!(objs[2].name, "살인사건");
    assert_eq!(objs[2].object_type, "event");
}

#[test]
fn test_clean_object_markup() {
    let text = "[[김철수]]가 [[장소:서울역]]에서 [[김 형사|김철수]]를 불렀다.";
    let cleaned = clean_object_markup(text);
    assert_eq!(cleaned, "김철수가 서울역에서 김철수를 불렀다.");
}

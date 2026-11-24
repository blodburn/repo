use repo_core::objects::ObjectEngine;

#[test]
fn test_object_registration_and_id_allocation() {
    let mut engine = ObjectEngine::new();

    let id1 = engine.register("김철수", "character");
    let id2 = engine.register("박영희", "character");
    let id3 = engine.register("서울역", "location");

    assert_eq!(id1, "CHAR-0001");
    assert_eq!(id2, "CHAR-0002");
    assert_eq!(id3, "LOC-0001");

    // Registering duplicate returns existing ID
    let dup = engine.register("김철수", "character");
    assert_eq!(dup, id1);
    assert_eq!(engine.count(), 3);
}

#[test]
fn test_alias_registration_and_lookup() {
    let mut engine = ObjectEngine::new();
    let id = engine.register("김철수", "character");

    assert!(engine.add_alias(&id, "철수"));
    assert!(engine.add_alias(&id, "김 형사"));

    assert_eq!(engine.lookup("김철수"), Some(id.as_str()));
    assert_eq!(engine.lookup("철수"), Some(id.as_str()));
    assert_eq!(engine.lookup("김 형사"), Some(id.as_str()));
    assert_eq!(engine.lookup("없는사람"), None);
}

#[test]
fn test_mention_scanning_with_korean_particles() {
    let mut engine = ObjectEngine::new();
    let char_id = engine.register("김철수", "character");
    let loc_id = engine.register("서울역", "location");

    let text = "김철수가 서울역에서 기다린다. 하지만 김철수산물과는 무관하다.";
    let mentions = engine.find_mentions_in_line(text, 0, Some("SCENE-01"));

    // Should match "김철수" in "김철수가" and "서울역" in "서울역에서"
    // But should NOT match "김철수" in "김철수산물"!
    assert_eq!(mentions.len(), 2);

    assert_eq!(mentions[0].object_id, char_id);
    assert_eq!(mentions[0].matched_name, "김철수");

    assert_eq!(mentions[1].object_id, loc_id);
    assert_eq!(mentions[1].matched_name, "서울역");
}

#[test]
fn test_greedy_longest_alias_matching() {
    let mut engine = ObjectEngine::new();
    let id = engine.register("김철수", "character");
    engine.add_alias(&id, "김 형사");

    // Also register another character named "김"
    let id_kim = engine.register("김", "character");

    let text = "김 형사가 현장에 도착했다.";
    let mentions = engine.find_mentions_in_line(text, 0, None);

    // Should greedily match "김 형사" (3 chars) instead of "김" (1 char)
    assert_eq!(mentions.len(), 1);
    assert_eq!(mentions[0].object_id, id);
    assert_eq!(mentions[0].matched_name, "김 형사");
    assert_ne!(mentions[0].object_id, id_kim);
}

#[test]
fn test_document_scan_reports_absolute_offsets() {
    let mut engine = ObjectEngine::new();
    let id = engine.register("김철수", "character");

    let text = "# 장면\n김철수가 들어온다.\n\n두 번째 줄: 김철수";
    let mentions = engine.scan_document(text);
    assert_eq!(mentions.len(), 2);

    assert_eq!(mentions[0].object_id, id);
    assert_eq!(mentions[0].line_index, 1);
    assert_eq!(mentions[0].byte_offset, "# 장면\n".len());
    assert_eq!(mentions[0].char_offset, "# 장면\n".chars().count());
    assert_eq!(
        mentions[0].utf16_offset,
        "# 장면\n".encode_utf16().count()
    );

    let expected_char = "# 장면\n김철수가 들어온다.\n\n두 번째 줄: ".chars().count();
    assert_eq!(mentions[1].line_index, 3);
    assert_eq!(mentions[1].char_offset, expected_char);

    // CRLF input must not shift the offsets of later lines.
    let crlf = engine.scan_document("가\r\n김철수가 온다\r\n");
    assert_eq!(crlf.len(), 1);
    assert_eq!(crlf[0].line_index, 1);
    assert_eq!(
        crlf[0].utf16_offset,
        "가\r\n".encode_utf16().count()
    );
}

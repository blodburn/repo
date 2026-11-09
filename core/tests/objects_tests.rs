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

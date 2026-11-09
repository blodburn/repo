use repo_core::storage::ProjectLayout;
use std::sync::atomic::{AtomicU64, Ordering};
static TEST_SEQ: AtomicU64 = AtomicU64::new(0);

#[test]
fn saving_existing_manuscript_is_atomic_and_repeatable() {
    let root = std::env::temp_dir().join(format!("repo-atomic-{}-{}",
        std::process::id(), TEST_SEQ.fetch_add(1, Ordering::Relaxed)));
    let layout = ProjectLayout::new(&root);
    layout.save_manuscript_atomic("첫 버전").unwrap();
    assert_eq!(layout.load_manuscript().unwrap(), "첫 버전");
    layout.save_manuscript_atomic("두 번째 버전\n김철수").unwrap();
    assert_eq!(layout.load_manuscript().unwrap(), "두 번째 버전\n김철수");
    assert!(!layout.script_dir.join("main.txt.tmp").exists());
    let _ = std::fs::remove_dir_all(root);
}
//! 3-Pane interactive screenplay editor UI.

use repo_core::objects::ObjectEngine;
use repo_core::syntax::{parse_line, ElementKind};

/// Active pane focus in the 3-pane layout.
#[allow(dead_code)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FocusedPane {
    LeftIndex,
    CenterEditor,
    RightDetail,
}

pub struct ScreenplayUI {
    pub title: String,
    pub manuscript: String,
    pub engine: ObjectEngine,
    pub focused_pane: FocusedPane,
    pub cursor_line: usize,
    pub selected_object_id: Option<String>,
}

impl ScreenplayUI {
    pub fn new(title: &str, manuscript: &str) -> Self {
        let mut engine = ObjectEngine::new();
        // Extract initial explicit objects
        for line in manuscript.lines() {
            let parsed = parse_line(line);
            for obj in parsed.explicit_objects {
                engine.register(&obj.name, &obj.object_type);
            }
        }

        Self {
            title: title.to_string(),
            manuscript: manuscript.to_string(),
            engine,
            focused_pane: FocusedPane::CenterEditor,
            cursor_line: 0,
            selected_object_id: None,
        }
    }

    /// Renders the complete 3-pane layout into a formatted display string.
    pub fn render_frame(&self, width: usize, height: usize) -> String {
        let mut out = String::new();
        let border = "═".repeat(width);
        let _thin_border = "─".repeat(width);

        // Header Top Bar
        out.push_str(&format!("┌{}┐\n", border));
        let title_line = format!(" repo — {}  │  저장됨  │  Focus: {:?}", self.title, self.focused_pane);
        out.push_str(&format!("│ {:<width$} │\n", title_line, width = width.saturating_sub(4)));
        out.push_str(&format!("├{}┤\n", border));

        // Calculate pane column widths
        let left_w = (width * 22) / 100;
        let right_w = (width * 26) / 100;
        let center_w = width.saturating_sub(left_w + right_w + 4);

        // Left Pane: Objects & Scenes
        let mut left_lines = Vec::new();
        left_lines.push(" [ Objects & Scenes ]".to_string());
        left_lines.push("─".repeat(left_w.saturating_sub(2)));

        let lines: Vec<&str> = self.manuscript.lines().collect();
        for (_i, &line) in lines.iter().enumerate() {
            let parsed = parse_line(line);
            if parsed.kind == ElementKind::Scene {
                left_lines.push(format!(" # {}", parsed.content));
            }
            for obj in &parsed.explicit_objects {
                let count = lines.iter().filter(|l| l.contains(&obj.name)).count();
                left_lines.push(format!(" ● {} · {}회", obj.name, count));
            }
        }

        // Center Pane: Manuscript
        let mut center_lines = Vec::new();
        center_lines.push(format!(" [ 원고: {} ]", self.title));
        center_lines.push("─".repeat(center_w.saturating_sub(2)));

        for (idx, &line) in lines.iter().enumerate() {
            let parsed = parse_line(line);
            let prefix = if idx == self.cursor_line { ">" } else { " " };
            let style_tag = match parsed.kind {
                ElementKind::Scene => "[S]",
                ElementKind::Dialogue => "  [D] ",
                ElementKind::Description => "[#] ",
                ElementKind::Narration => "  [N] ",
                ElementKind::Note => "// ",
                _ => "    ",
            };
            center_lines.push(format!("{} {:02} {}{}", prefix, idx + 1, style_tag, parsed.content));
        }

        // Right Pane: Detail
        let mut right_lines = Vec::new();
        right_lines.push(" [ Object Detail ]".to_string());
        right_lines.push("─".repeat(right_w.saturating_sub(2)));

        if let Some(ref obj_id) = self.selected_object_id {
            if let Some(entity) = self.engine.get_by_id(obj_id) {
                right_lines.push(format!(" 이름: {}", entity.name));
                right_lines.push(format!(" ID: {}", entity.id));
                right_lines.push(format!(" 분류: {}", entity.category));
                right_lines.push(format!(" 별칭: {}", entity.aliases.join(", ")));
            }
        } else {
            right_lines.push(" 선택된 오브젝트 없음".to_string());
            right_lines.push(" (Ctrl+Shift+O 로 등록)".to_string());
        }

        // Compose 3 columns
        let body_h = height.saturating_sub(6);
        for row in 0..body_h {
            let left_text = left_lines.get(row).map(|s| s.as_str()).unwrap_or("");
            let center_text = center_lines.get(row).map(|s| s.as_str()).unwrap_or("");
            let right_text = right_lines.get(row).map(|s| s.as_str()).unwrap_or("");

            out.push_str(&format!(
                "│ {:<lw$} │ {:<cw$} │ {:<rw$} │\n",
                left_text, center_text, right_text,
                lw = left_w.saturating_sub(2),
                cw = center_w.saturating_sub(2),
                rw = right_w.saturating_sub(2)
            ));
        }

        // Footer Bottom Bar
        out.push_str(&format!("├{}┤\n", border));
        let footer_line = " F1:도움말 │ F6:패널전환 │ Ctrl+Z:실행취소 │ Ctrl+U:다시실행 │ Ctrl+S:저장 │ Esc:편집기";
        out.push_str(&format!("│ {:<width$} │\n", footer_line, width = width.saturating_sub(4)));
        out.push_str(&format!("└{}┘\n", border));

        out
    }
}

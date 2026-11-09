//! Clean document export formatting routines.

use crate::syntax::{parse_line, ElementKind};

/// Options controlling export generation.
#[derive(Debug, Clone, Copy)]
pub struct ExportOptions {
    /// Whether to omit comment / note lines starting with `//`
    pub omit_notes: bool,
    /// Whether to add extra blank lines between scene headings and descriptions
    pub screenplay_spacing: bool,
}

impl Default for ExportOptions {
    fn default() -> Self {
        Self {
            omit_notes: true,
            screenplay_spacing: false,
        }
    }
}

/// Transforms raw screenplay manuscript with syntax into clean, publication-ready text.
pub fn export_clean_text(manuscript: &str, options: ExportOptions) -> String {
    let mut out = String::new();

    for line in manuscript.lines() {
        let parsed = parse_line(line);

        if options.omit_notes && parsed.kind == ElementKind::Note {
            continue;
        }

        match parsed.kind {
            ElementKind::Empty => {
                out.push('\n');
            }
            ElementKind::Scene => {
                out.push_str(&parsed.content);
                out.push('\n');
            }
            ElementKind::Dialogue => {
                // Dialogue lines are typically indented in standard screenplay format
                out.push_str("    ");
                out.push_str(&parsed.content);
                out.push('\n');
            }
            ElementKind::Description | ElementKind::PlainText => {
                out.push_str(&parsed.content);
                out.push('\n');
            }
            ElementKind::Narration => {
                out.push_str("  (나레이션) ");
                out.push_str(&parsed.content);
                out.push('\n');
            }
            ElementKind::Note => {
                // Kept only if omit_notes is false
                out.push_str("// ");
                out.push_str(&parsed.content);
                out.push('\n');
            }
        }
    }

    out
}

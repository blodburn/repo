//! Syntax parsing rules for the repo screenplay / manuscript format.

/// Types of syntactic blocks recognized in repo.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ElementKind {
    /// Scene heading, e.g. `# INT. 병원 복도 - 밤`
    Scene,
    /// Dialogue line, e.g. `@@ 박영희는 어디 있어? @@`
    Dialogue,
    /// Action or situation description, e.g. `## 형광등이 깜빡인다. ##`
    Description,
    /// Narration, e.g. `₩₩ 그날 모든 것이 시작됐다. ₩₩`
    Narration,
    /// Comment or note excluded from exports, e.g. `// 감정선 재검토`
    Note,
    /// Plain text line (regular narrative or speaker name)
    PlainText,
    /// Empty line
    Empty,
}

/// An explicit object declaration found within double brackets `[[...]]`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExplicitObject {
    /// Object name or alias (e.g. "김철수", "서울역")
    pub name: String,
    /// Normalized object category ("character", "location", "event", "item", "organization", etc.)
    pub object_type: String,
    /// Optional alias prefix or display alias if specified via `[[alias|canonical]]`
    pub alias: Option<String>,
    /// Byte offset in the source string
    pub start: usize,
    /// Byte end offset in the source string
    pub end: usize,
}

/// Result of parsing a single line of manuscript text.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParsedLine {
    /// Syntactic category of this line
    pub kind: ElementKind,
    /// The trimmed text content with syntax wrapper markers stripped
    pub content: String,
    /// Raw original line
    pub raw: String,
    /// Explicit objects registered or forced in this line `[[...]]`
    pub explicit_objects: Vec<ExplicitObject>,
}

/// Normalizes an object type string (supporting Korean and English keywords).
pub fn normalize_object_type(raw: &str) -> String {
    let trimmed = raw.trim();
    match trimmed.to_lowercase().as_str() {
        "" | "인물" | "character" | "char" => "character".to_string(),
        "장소" | "location" | "loc" => "location".to_string(),
        "사건" | "event" => "event".to_string(),
        "조직" | "organization" | "org" => "organization".to_string(),
        "물건" | "아이템" | "item" => "item".to_string(),
        _ => trimmed.to_string(),
    }
}

/// Extracts explicit object markers `[[...]]` from a text line.
///
/// Supported formats:
/// - `[[김철수]]` -> character "김철수"
/// - `[[장소:서울역]]` -> location "서울역"
/// - `[[철수|김철수]]` -> character "김철수" with alias "철수"
pub fn extract_explicit_objects(text: &str) -> Vec<ExplicitObject> {
    let mut objects = Vec::new();
    let mut idx = 0;

    while let Some(start_rel) = text[idx..].find("[[") {
        let start = idx + start_rel;
        if let Some(end_rel) = text[start + 2..].find("]]") {
            let end = start + 2 + end_rel;
            let inner = &text[start + 2..end];
            
            // Check for alias separator `|`
            let (raw_type_and_name, alias) = if let Some(pipe_pos) = inner.find('|') {
                let alias_part = inner[..pipe_pos].trim();
                let rest = inner[pipe_pos + 1..].trim();
                (rest, if alias_part.is_empty() { None } else { Some(alias_part.to_string()) })
            } else {
                (inner, None)
            };

            // Check for type prefix `:`
            let (obj_type, name) = if let Some(colon_pos) = raw_type_and_name.find(':') {
                let type_part = &raw_type_and_name[..colon_pos];
                let name_part = &raw_type_and_name[colon_pos + 1..];
                (normalize_object_type(type_part), name_part.trim().to_string())
            } else {
                ("character".to_string(), raw_type_and_name.trim().to_string())
            };

            if !name.is_empty() {
                objects.push(ExplicitObject {
                    name,
                    object_type: obj_type,
                    alias,
                    start,
                    end: end + 2,
                });
            }

            idx = end + 2;
        } else {
            break;
        }
    }

    objects
}

/// Cleans object markup from a text line, replacing `[[...]]` with its clean display name.
pub fn clean_object_markup(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut idx = 0;

    while let Some(start_rel) = text[idx..].find("[[") {
        let start = idx + start_rel;
        out.push_str(&text[idx..start]);

        if let Some(end_rel) = text[start + 2..].find("]]") {
            let end = start + 2 + end_rel;
            let inner = &text[start + 2..end];
            let clean_name = if let Some(pipe_pos) = inner.find('|') {
                inner[pipe_pos + 1..].trim()
            } else if let Some(colon_pos) = inner.find(':') {
                inner[colon_pos + 1..].trim()
            } else {
                inner.trim()
            };
            out.push_str(clean_name);
            idx = end + 2;
        } else {
            out.push_str(&text[start..]);
            return out;
        }
    }

    out.push_str(&text[idx..]);
    out
}

/// Parses a single line according to repo grammar rules.
pub fn parse_line(line: &str) -> ParsedLine {
    let trimmed = line.trim();
    let explicit_objects = extract_explicit_objects(line);

    if trimmed.is_empty() {
        return ParsedLine {
            kind: ElementKind::Empty,
            content: String::new(),
            raw: line.to_string(),
            explicit_objects,
        };
    }

    // Comment / Note
    if trimmed.starts_with("//") {
        let content = trimmed[2..].trim().to_string();
        return ParsedLine {
            kind: ElementKind::Note,
            content,
            raw: line.to_string(),
            explicit_objects,
        };
    }

    // Dialogue: @@ text @@
    if trimmed.starts_with("@@") && trimmed.ends_with("@@") && trimmed.len() >= 4 {
        let inner = &trimmed[2..trimmed.len() - 2];
        return ParsedLine {
            kind: ElementKind::Dialogue,
            content: clean_object_markup(inner.trim()),
            raw: line.to_string(),
            explicit_objects,
        };
    }

    // Description: ## text ##
    if trimmed.starts_with("##") && trimmed.ends_with("##") && trimmed.len() >= 4 {
        let inner = &trimmed[2..trimmed.len() - 2];
        return ParsedLine {
            kind: ElementKind::Description,
            content: clean_object_markup(inner.trim()),
            raw: line.to_string(),
            explicit_objects,
        };
    }

    // Narration: ₩₩ text ₩₩ or \\ text \\ (handles both U+20A9 Korean won sign and backslash)
    let is_won_narration = trimmed.starts_with("₩₩") && trimmed.ends_with("₩₩") && trimmed.len() >= 4;
    let is_slash_narration = trimmed.starts_with("\\\\") && trimmed.ends_with("\\\\") && trimmed.len() >= 4;
    if is_won_narration {
        // "₩" is 3 UTF-8 bytes (0xEA, 0x82, 0xA9), so 2 chars = 6 bytes
        let inner = &trimmed["₩₩".len()..trimmed.len() - "₩₩".len()];
        return ParsedLine {
            kind: ElementKind::Narration,
            content: clean_object_markup(inner.trim()),
            raw: line.to_string(),
            explicit_objects,
        };
    } else if is_slash_narration {
        let inner = &trimmed[2..trimmed.len() - 2];
        return ParsedLine {
            kind: ElementKind::Narration,
            content: clean_object_markup(inner.trim()),
            raw: line.to_string(),
            explicit_objects,
        };
    }

    // Scene: # Heading (not ##)
    if trimmed.starts_with('#') && !trimmed.starts_with("##") {
        let inner = trimmed[1..].trim();
        return ParsedLine {
            kind: ElementKind::Scene,
            content: clean_object_markup(inner),
            raw: line.to_string(),
            explicit_objects,
        };
    }

    // Default: PlainText
    ParsedLine {
        kind: ElementKind::PlainText,
        content: clean_object_markup(trimmed),
        raw: line.to_string(),
        explicit_objects,
    }
}

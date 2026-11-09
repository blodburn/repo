//! Object Engine for tracking characters, locations, items, and automatic name recognition.

use std::collections::HashMap;

/// An identified object entity in the project.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ObjectEntity {
    /// Persistent identifier, e.g. "CHAR-0001", "LOC-0001"
    pub id: String,
    /// Canonical display name
    pub name: String,
    /// Normalized category ("character", "location", "item", "event", "organization")
    pub category: String,
    /// Known aliases
    pub aliases: Vec<String>,
}

/// Occurrence of an object mention within text.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Mention {
    /// Associated object persistent ID
    pub object_id: String,
    /// Matched text fragment (name or alias)
    pub matched_name: String,
    /// 0-indexed line number
    pub line_index: usize,
    /// Character offset in line (UTF-16 or UTF-8 codepoint count)
    pub char_offset: usize,
    /// Byte offset in UTF-8 string
    pub byte_offset: usize,
    /// Length of match in bytes
    pub byte_len: usize,
    /// Optional Scene ID or title where mention occurred
    pub scene_id: Option<String>,
}

/// Object engine maintaining lexicon, ID allocation, and mention scanning.
#[derive(Debug, Clone, Default)]
pub struct ObjectEngine {
    /// Map of object ID -> ObjectEntity
    objects: HashMap<String, ObjectEntity>,
    /// Map of (lookup name in lowercase) -> object ID
    alias_map: HashMap<String, String>,
    /// Counters for persistent ID generation per prefix
    counter: HashMap<String, u32>,
}

impl ObjectEngine {
    /// Creates a new, empty ObjectEngine.
    pub fn new() -> Self {
        Self {
            objects: HashMap::new(),
            alias_map: HashMap::new(),
            counter: HashMap::new(),
        }
    }

    /// Allocates the next sequential ID for a category prefix (e.g. "CHAR-0001").
    fn allocate_id(&mut self, prefix: &str) -> String {
        let count = self.counter.entry(prefix.to_string()).or_insert(0);
        *count += 1;
        format!("{}-{:04}", prefix, *count)
    }

    /// Registers a new object or returns existing ID if canonical name already exists.
    pub fn register(&mut self, name: &str, category: &str) -> String {
        let clean_name = name.trim();
        let key = clean_name.to_lowercase();

        if let Some(existing_id) = self.alias_map.get(&key) {
            return existing_id.clone();
        }

        let prefix = match category.to_lowercase().as_str() {
            "location" | "loc" => "LOC",
            "item" => "OBJ",
            "event" => "EVT",
            "organization" | "org" => "ORG",
            _ => "CHAR",
        };

        let id = self.allocate_id(prefix);
        let entity = ObjectEntity {
            id: id.clone(),
            name: clean_name.to_string(),
            category: category.to_string(),
            aliases: Vec::new(),
        };

        self.objects.insert(id.clone(), entity);
        self.alias_map.insert(key, id.clone());
        id
    }

    /// Restores an object with its database-backed stable identifier.
    /// Also advances the ID counter so newly created entities never collide.
    pub fn register_with_id(&mut self, id: &str, name: &str, category: &str) -> bool {
        if id.trim().is_empty() || name.trim().is_empty() {
            return false;
        }
        let lookup = name.trim().to_lowercase();
        if self.objects.contains_key(id) || self.alias_map.contains_key(&lookup) {
            return false;
        }
        if let Some((prefix, digits)) = id.rsplit_once('-') {
            if let Ok(number) = digits.parse::<u32>() {
                let count = self.counter.entry(prefix.to_string()).or_insert(0);
                *count = (*count).max(number);
            }
        }
        self.objects.insert(id.to_string(), ObjectEntity {
            id: id.to_string(),
            name: name.trim().to_string(),
            category: category.to_string(),
            aliases: Vec::new(),
        });
        self.alias_map.insert(lookup, id.to_string());
        true
    }

    /// Adds an alias to an existing object ID.
    pub fn add_alias(&mut self, object_id: &str, alias: &str) -> bool {
        let clean_alias = alias.trim();
        if clean_alias.is_empty() {
            return false;
        }

        if let Some(entity) = self.objects.get_mut(object_id) {
            let key = clean_alias.to_lowercase();
            if !self.alias_map.contains_key(&key) {
                self.alias_map.insert(key, object_id.to_string());
                if !entity.aliases.contains(&clean_alias.to_string()) {
                    entity.aliases.push(clean_alias.to_string());
                }
                return true;
            }
        }
        false
    }

    /// Returns the number of registered objects.
    pub fn count(&self) -> usize {
        self.objects.len()
    }

    /// Gets an object entity by its persistent ID.
    pub fn get_by_id(&self, id: &str) -> Option<&ObjectEntity> {
        self.objects.get(id)
    }

    /// Looks up object ID by exact or alias name match.
    pub fn lookup(&self, name: &str) -> Option<&str> {
        let key = name.trim().to_lowercase();
        self.alias_map.get(&key).map(|s| s.as_str())
    }

    /// Checks if a character following a candidate match is a valid Korean particle (조사) or delimiter.
    fn is_valid_korean_suffix(rest: &str) -> bool {
        if rest.is_empty() {
            return true;
        }
        let first_char = rest.chars().next().unwrap();
        if first_char.is_whitespace() || first_char.is_ascii_punctuation() || ".,!?:;\"'()[]{}~-·".contains(first_char) {
            return true;
        }

        // Common Korean postposition particles (조사)
        const COMMON_PARTICLES: &[&str] = &[
            "은", "는", "이", "가", "을", "를", "의", "에", "에서", "에게", "한테",
            "로", "으로", "와", "과", "도", "만", "까지", "부터", "이라", "라는", "씨", "님", "형사", "작가", "선생", "군", "양"
        ];

        for &p in COMMON_PARTICLES {
            if rest.starts_with(p) {
                let after = &rest[p.len()..];
                if after.is_empty() {
                    return true;
                }
                if let Some(c) = after.chars().next() {
                    if c.is_whitespace() || c.is_ascii_punctuation() || ".,!?:;\"'()[]{}~-·".contains(c) {
                        return true;
                    }
                }
            }
        }

        false
    }

    /// Scans a line of text and finds all object mentions.
    /// Uses longest-match-first to avoid partial sub-matches.
    pub fn find_mentions_in_line(&self, line: &str, line_index: usize, scene_id: Option<&str>) -> Vec<Mention> {
        let mut mentions = Vec::new();
        if self.alias_map.is_empty() || line.trim().is_empty() {
            return mentions;
        }

        // Sort names by descending length for greedy matching
        let mut sorted_keys: Vec<&String> = self.alias_map.keys().collect();
        sorted_keys.sort_by(|a, b| b.chars().count().cmp(&a.chars().count()));

        let mut matched_spans: Vec<(usize, usize)> = Vec::new();

        for name_key in sorted_keys {
            let mut search_idx = 0;
            while search_idx < line.len() {
                // Case-insensitive substring search
                let hay = &line[search_idx..];
                let hay_lower = hay.to_lowercase();
                if let Some(rel_pos) = hay_lower.find(name_key) {
                    let start = search_idx + rel_pos;
                    let end = start + name_key.len();

                    // Check overlap with already accepted longer matches
                    let overlaps = matched_spans.iter().any(|&(s, e)| !(end <= s || start >= e));

                    if !overlaps {
                        // Check boundary and particle validity
                        let suffix = &line[end..];
                        if Self::is_valid_korean_suffix(suffix) {
                            let obj_id = self.alias_map.get(name_key).unwrap().clone();
                            let char_offset = line[..start].chars().count();
                            let matched_str = line[start..end].to_string();

                            mentions.push(Mention {
                                object_id: obj_id,
                                matched_name: matched_str,
                                line_index,
                                char_offset,
                                byte_offset: start,
                                byte_len: end - start,
                                scene_id: scene_id.map(|s| s.to_string()),
                            });
                            matched_spans.push((start, end));
                        }
                    }

                    search_idx = end;
                } else {
                    break;
                }
            }
        }

        // Sort mentions by character offset
        mentions.sort_by_key(|m| m.byte_offset);
        mentions
    }
}

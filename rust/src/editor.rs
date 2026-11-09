//! UTF-8 editor buffer with grapheme-safe cursor motion and journaled edits.

use unicode_segmentation::UnicodeSegmentation;
use crate::journal::Journal;

#[derive(Debug, Clone)]
struct Change {
    at: usize,
    removed: String,
    inserted: String,
}

#[derive(Debug)]
pub struct EditorBuffer {
    pub text: String,
    pub cursor: usize,  // byte offset, always a UTF-8 boundary
    undo: Vec<Change>,
    redo: Vec<Change>,
}

impl EditorBuffer {
    pub fn new(text: String) -> Self {
        Self { text, cursor: 0, undo: Vec::new(), redo: Vec::new() }
    }

    pub fn change(&mut self, journal: &mut Journal, start: usize, end: usize,
                  inserted: &str) -> Result<(), String> {
        if start > end || end > self.text.len()
            || !self.text.is_char_boundary(start) || !self.text.is_char_boundary(end) {
            return Err("invalid UTF-8 edit position".into());
        }
        let removed = self.text[start..end].to_string();
        if removed == inserted { return Ok(()); }
        journal.append_edit(&self.text, start, &removed, inserted, "edit")?;
        self.text.replace_range(start..end, inserted);
        self.cursor = start + inserted.len();
        self.undo.push(Change { at: start, removed, inserted: inserted.to_string() });
        self.redo.clear();
        journal.maybe_snapshot(&self.text)?;
        Ok(())
    }

    pub fn insert(&mut self, journal: &mut Journal, inserted: &str) -> Result<(), String> {
        self.change(journal, self.cursor, self.cursor, inserted)
    }

    pub fn backspace(&mut self, journal: &mut Journal) -> Result<(), String> {
        if self.cursor == 0 { return Ok(()); }
        let start = self.previous_grapheme();
        self.change(journal, start, self.cursor, "")
    }

    pub fn delete(&mut self, journal: &mut Journal) -> Result<(), String> {
        if self.cursor >= self.text.len() { return Ok(()); }
        let end = self.next_grapheme();
        self.change(journal, self.cursor, end, "")
    }

    pub fn undo(&mut self, journal: &mut Journal) -> Result<bool, String> {
        let change = match self.undo.last().cloned() { Some(c) => c, None => return Ok(false) };
        let end = change.at + change.inserted.len();
        if self.text.get(change.at..end) != Some(change.inserted.as_str()) {
            return Err("Undo history no longer matches manuscript".into());
        }
        journal.append_edit(&self.text, change.at, &change.inserted, &change.removed, "undo")?;
        self.text.replace_range(change.at..end, &change.removed);
        self.cursor = change.at + change.removed.len();
        self.undo.pop();
        self.redo.push(change);
        journal.maybe_snapshot(&self.text)?;
        Ok(true)
    }

    pub fn redo(&mut self, journal: &mut Journal) -> Result<bool, String> {
        let change = match self.redo.last().cloned() { Some(c) => c, None => return Ok(false) };
        let end = change.at + change.removed.len();
        if self.text.get(change.at..end) != Some(change.removed.as_str()) {
            return Err("Redo history no longer matches manuscript".into());
        }
        journal.append_edit(&self.text, change.at, &change.removed, &change.inserted, "redo")?;
        self.text.replace_range(change.at..end, &change.inserted);
        self.cursor = change.at + change.inserted.len();
        self.redo.pop();
        self.undo.push(change);
        journal.maybe_snapshot(&self.text)?;
        Ok(true)
    }

    pub fn replace_all(&mut self, journal: &mut Journal, recovered: &str) -> Result<(), String> {
        let n = self.text.len();
        self.change(journal, 0, n, recovered)
    }

    pub fn move_left(&mut self) { self.cursor = self.previous_grapheme(); }
    pub fn move_right(&mut self) { self.cursor = self.next_grapheme(); }
    pub fn move_home(&mut self) { self.cursor = self.line_start(); }
    pub fn move_end(&mut self) { self.cursor = self.line_end(); }

    pub fn previous_grapheme(&self) -> usize {
        self.text[..self.cursor].grapheme_indices(true).last()
            .map(|(i, _)| i).unwrap_or(0)
    }

    pub fn next_grapheme(&self) -> usize {
        if self.cursor >= self.text.len() { return self.text.len(); }
        let fragment = &self.text[self.cursor..];
        fragment.grapheme_indices(true).nth(1)
            .map(|(index, _)| self.cursor + index).unwrap_or(self.text.len())
    }

    pub fn line_start(&self) -> usize {
        self.text[..self.cursor].rfind('\n').map(|n| n + 1).unwrap_or(0)
    }

    pub fn line_end(&self) -> usize {
        self.text[self.cursor..].find('\n')
            .map(|n| self.cursor + n).unwrap_or(self.text.len())
    }

    pub fn line_and_column(&self) -> (usize, usize) {
        let before = &self.text[..self.cursor];
        let line = before.bytes().filter(|&c| c == b'\n').count();
        let col = before[before.rfind('\n').map(|i| i + 1).unwrap_or(0)..]
            .graphemes(true).count();
        (line, col)
    }

    pub fn move_vertical(&mut self, down: bool) {
        let (_, col) = self.line_and_column();
        let line_start = self.line_start();
        let target_start: usize;
        let target_end: usize;

        if down {
            let end = self.line_end();
            if end == self.text.len() { return; }
            target_start = end + 1;
            target_end = self.text[target_start..].find('\n')
                .map(|n| target_start + n).unwrap_or(self.text.len());
        } else {
            if line_start == 0 { return; }
            target_end = line_start - 1;
            target_start = self.text[..target_end].rfind('\n')
                .map(|n| n + 1).unwrap_or(0);
        }

        let slice = &self.text[target_start..target_end];
        let mut byte_col = slice.len();
        let count = slice.graphemes(true).count();
        if col < count {
            byte_col = slice.grapheme_indices(true).nth(col)
                .map(|(i,_)| i).unwrap_or(slice.len());
        }
        self.cursor = target_start + byte_col;
    }

    pub fn word_under_cursor(&self) -> Option<(usize, usize, String)> {
        let is_word = |c: char| c.is_alphanumeric() || c == '_';
        let text = &self.text;
        if text.is_empty() { return None; }
        let mut pos = self.cursor.min(text.len());
        if pos == text.len() || !text[pos..].chars().next().is_some_and(is_word) {
            if pos == 0 { return None; }
            let previous = text[..pos].chars().next_back()?;
            if !is_word(previous) { return None; }
            pos -= previous.len_utf8();
        }
        let mut start = pos;
        while start > 0 {
            let ch = text[..start].chars().next_back().unwrap();
            if !is_word(ch) { break; }
            start -= ch.len_utf8();
        }
        let mut end = pos;
        while end < text.len() {
            let ch = text[end..].chars().next().unwrap();
            if !is_word(ch) { break; }
            end += ch.len_utf8();
        }
        if end <= start { return None; }
        Some((start, end, text[start..end].to_owned()))
    }

    pub fn goto_next(&mut self, word: &str) -> bool {
        if word.is_empty() { return false; }
        let pos = self.text[self.cursor..].find(word)
            .map(|p| self.cursor + p)
            .or_else(|| self.text.find(word));
        if let Some(pos) = pos { self.cursor = pos; true } else { false }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::sync::atomic::{AtomicU64, Ordering};
    static TEST_SEQ: AtomicU64 = AtomicU64::new(0);

    fn journal(base: &str) -> (Journal, String) {
        // Manuscripts can contain '/' or newlines and are never valid filenames.
        let root = std::env::temp_dir().join(format!("repo-editor-test-{}-{}",
            std::process::id(), TEST_SEQ.fetch_add(1, Ordering::Relaxed)));
        fs::create_dir_all(&root).unwrap();
        let file = root.join("project.sqlite");
        let (j, text, _) = Journal::open(&file, base).unwrap();
        (j,text)
    }

    #[test]
    fn grapheme_cursor_remains_unicode_safe() {
        let (mut j, text) = journal("김철수");
        let mut e = EditorBuffer::new(text);
        e.cursor = e.text.len();
        e.backspace(&mut j).unwrap();
        assert_eq!(e.text,"김철");
        e.undo(&mut j).unwrap();
        assert_eq!(e.text,"김철수");
        e.redo(&mut j).unwrap();
        assert_eq!(e.text,"김철");
    }

    #[test]
    fn deletion_restore_keeps_correct_offset() {
        let (mut j, text) = journal("첫 줄\n둘째 줄\n");
        let mut e = EditorBuffer::new(text);
        let (start,end) = (0,"첫 줄".len());
        e.change(&mut j, start,end,"").unwrap();
        assert_eq!(e.text,"\n둘째 줄\n");
        e.undo(&mut j).unwrap();
        assert_eq!(e.text,"첫 줄\n둘째 줄\n");
    }

    #[test]
    fn vertical_cursor_navigation_works_on_korean() {
        let (_j, text)=journal("한글 둘\nABCD\n나");
        let mut e=EditorBuffer::new(text);
        e.cursor="한글".len();
        e.move_vertical(true);
        assert_eq!(e.line_and_column(),(1,2));
        e.move_vertical(true);
        assert_eq!(e.line_and_column(),(2,1));
    }
}

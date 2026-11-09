//! Real-time three-pane terminal editor. All input is keyboard-driven.
//! The terminal is restored even if a rendering or database call fails.

use std::io::{self, IsTerminal, Write};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use crossterm::{
    cursor::{Hide, MoveTo, Show},
    event::{self, DisableBracketedPaste, EnableBracketedPaste, Event, KeyCode,
        KeyEvent, KeyEventKind, KeyModifiers},
    execute, queue,
    style::Print,
    terminal::{self, Clear, ClearType, EnterAlternateScreen, LeaveAlternateScreen},
};
use repo_core::objects::ObjectEngine;
use repo_core::storage::ProjectLayout;
use repo_core::syntax::{parse_line, ElementKind};
use unicode_segmentation::UnicodeSegmentation;
use unicode_width::UnicodeWidthStr;

use crate::editor::EditorBuffer;
use crate::journal::{HistoryItem, Journal};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FocusedPane { LeftIndex, CenterEditor, RightDetail }

#[derive(Clone, Copy)]
enum PromptKind { NewProject, OpenProject, Title }
enum Mode {
    Edit,
    Help,
    Preview,
    History { items: Vec<HistoryItem>, selected: usize },
    Prompt { kind: PromptKind, input: String },
}

struct TerminalSession;
impl TerminalSession {
    fn enter() -> Result<Self, String> {
        terminal::enable_raw_mode().map_err(|e| e.to_string())?;
        if let Err(e) = execute!(io::stdout(), EnterAlternateScreen, Hide, EnableBracketedPaste) {
            let _ = terminal::disable_raw_mode();
            return Err(e.to_string());
        }
        Ok(Self)
    }
}
impl Drop for TerminalSession {
    fn drop(&mut self) {
        let _ = execute!(io::stdout(), DisableBracketedPaste, Show, LeaveAlternateScreen);
        let _ = terminal::disable_raw_mode();
    }
}

pub struct ScreenplayUI {
    pub title: String,
    editor: EditorBuffer,
    engine: ObjectEngine,
    layout: ProjectLayout,
    journal: Journal,
    focused_pane: FocusedPane,
    cursor_scroll: usize,
    left_selection: usize,
    status: String,
    mode: Mode,
    dirty: bool,
    parse_dirty: bool,
    last_typing: Instant,
    last_disk_save: Instant,
}

impl ScreenplayUI {
    pub fn open(root: &Path) -> Result<Self, String> {
        let layout = ProjectLayout::new(root);
        let from_txt = layout.load_manuscript().map_err(|e| e.to_string())?;
        let (journal, restored, recovered) = Journal::open(&layout.database, &from_txt)?;
        let title = journal.title()?.unwrap_or_else(|| {
            root.file_name().and_then(|s| s.to_str()).unwrap_or("새 작품").to_string()
        });
        let mut ui = Self {
            title,
            editor: EditorBuffer::new(restored),
            engine: ObjectEngine::new(),
            layout,
            journal,
            focused_pane: FocusedPane::CenterEditor,
            cursor_scroll: 0,
            left_selection: 0,
            status: if recovered {
                "SQLite 저널에서 미저장 편집을 복구했습니다. Ctrl+S로 TXT에 반영하세요.".into()
            } else {
                "F1 도움말 · F6 패널 전환 · Ctrl+S 저장 · Ctrl+Q 종료".into()
            },
            mode: Mode::Edit,
            dirty: recovered,
            parse_dirty: false,
            last_typing: Instant::now(),
            last_disk_save: Instant::now(),
        };
        ui.rebuild_index();
        Ok(ui)
    }

    fn rebuild_index(&mut self) {
        let mut engine = ObjectEngine::new();
        for line in self.editor.text.lines() {
            for object in parse_line(line).explicit_objects {
                let id = engine.register(&object.name, &object.object_type);
                if let Some(alias) = &object.alias { engine.add_alias(&id, alias); }
            }
        }
        self.engine = engine;
        self.parse_dirty = false;
    }

    fn objects(&self) -> Vec<(String,String,String)> {
        let mut result = Vec::new();
        for line in self.editor.text.lines() {
            let parsed = parse_line(line);
            for object in parsed.explicit_objects {
                let label = format!("● {} [{}]", object.name, object.object_type);
                if !result.iter().any(|(name,_,_)| name == &object.name) {
                    let id = self.engine.lookup(&object.name).unwrap_or("").to_string();
                    result.push((object.name, label, id));
                }
            }
        }
        result
    }

    fn save(&mut self, reason: &str) -> Result<(), String> {
        // SQLite records state first. If file saving fails, recovery can replay it.
        self.journal.snapshot(&self.editor.text, reason)?;
        self.layout.save_manuscript_atomic(&self.editor.text).map_err(|e| e.to_string())?;
        self.dirty = false;
        self.last_disk_save = Instant::now();
        self.status = "원고 및 SQLite 기록 저장 완료".into();
        Ok(())
    }

    fn change<F>(&mut self, op: F)
    where F: FnOnce(&mut EditorBuffer, &mut Journal) -> Result<(), String> {
        match op(&mut self.editor, &mut self.journal) {
            Ok(()) => {
                self.dirty = true;
                self.parse_dirty = true;
                self.last_typing = Instant::now();
                self.status = "변경 내용 SQLite 저널 기록 완료".into();
            }
            Err(e) => self.status = format!("편집 취소: {e}"),
        }
    }

    fn undo(&mut self) {
        match self.editor.undo(&mut self.journal) {
            Ok(true) => self.mark_dirty("실행 취소 · 저널 기록 완료"),
            Ok(false) => self.status = "실행 취소 이력이 없습니다".into(),
            Err(e) => self.status = format!("실행 취소 실패: {e}"),
        }
    }

    fn redo(&mut self) {
        match self.editor.redo(&mut self.journal) {
            Ok(true) => self.mark_dirty("다시 실행 · 저널 기록 완료"),
            Ok(false) => self.status = "다시 실행 이력이 없습니다".into(),
            Err(e) => self.status = format!("다시 실행 실패: {e}"),
        }
    }

    fn mark_dirty(&mut self, message: &str) {
        self.dirty = true;
        self.parse_dirty = true;
        self.last_typing = Instant::now();
        self.status = message.into();
    }

    fn register_current_object(&mut self) {
        let Some((start, end, word)) = self.editor.word_under_cursor() else {
            self.status = "오브젝트로 등록할 이름 위에 커서를 놓으세요".into();
            return;
        };
        let token = format!("[[{word}]]");
        self.change(|editor, journal| editor.change(journal, start, end, &token));
        self.rebuild_index();
        self.status = format!("등록 완료: {word} (이후 일반 이름도 자동 인식)");
    }

    fn next_object(&mut self) {
        let objects = self.objects();
        if let Some((name,_,_)) = objects.get(self.left_selection) {
            if self.editor.goto_next(name) {
                self.cursor_scroll = self.editor.line_and_column().0.saturating_sub(3);
                self.focused_pane = FocusedPane::CenterEditor;
                self.status = format!("{name} 등장 위치로 이동");
            } else {
                self.status = "해당 이름이 본문에 없습니다".into();
            }
        }
    }

    fn history_menu(&mut self) {
        match self.journal.history(60) {
            Ok(items) if !items.is_empty() =>
                self.mode = Mode::History { items, selected: 0 },
            Ok(_) => self.status = "저장/삭제 이력이 없습니다".into(),
            Err(e) => self.status = format!("복구 기록 조회 실패: {e}"),
        }
    }

    fn restore_history(&mut self, item: HistoryItem) {
        match item {
            HistoryItem::Snapshot { content, .. } => {
                self.change(|editor, journal| editor.replace_all(journal, &content));
                self.status = "이전 저장본 복원 완료 · Ctrl+Z로 다시 취소 가능".into();
            }
            HistoryItem::Deletion { content, .. } => {
                // Reinsert the deleted text at the current cursor; never silently
                // overwrite unrelated newer text.
                self.change(|editor, journal| editor.insert(journal, &content));
                self.status = "삭제 문장을 현재 커서 위치에 복원했습니다".into();
            }
        }
    }

    fn open_project(&mut self, root: PathBuf, create: bool) {
        let result: Result<ScreenplayUI, String> = (|| {
            if create {
                if root.exists() { return Err("같은 폴더가 이미 있습니다. Ctrl+O를 사용하세요".into()); }
                std::fs::create_dir_all(&root).map_err(|e| e.to_string())?;
            } else if !root.join("project.sqlite").exists() && !root.join("script/main.txt").exists() {
                return Err("프로젝트 폴더에서 project.sqlite 또는 main.txt를 찾을 수 없습니다".into());
            }
            ScreenplayUI::open(&root)
        })();
        match result {
            Ok(next) => {
                if let Err(e) = self.save("switch-project") {
                    self.status = format!("기존 프로젝트 저장 실패 · 전환 취소: {e}");
                    return;
                }
                *self = next;
            }
            Err(e) => self.status = format!("프로젝트 열기 실패: {e}"),
        }
    }

    fn handle_prompt(&mut self, key: KeyEvent, kind: PromptKind, mut input: String) {
        match key.code {
            KeyCode::Esc => self.mode = Mode::Edit,
            KeyCode::Enter => {
                self.mode = Mode::Edit;
                if matches!(kind, PromptKind::Title) {
                    match self.journal.set_title(&input) {
                        Ok(()) => { self.title = input; self.status = "제목 변경 완료".into(); }
                        Err(e) => self.status = format!("제목 저장 실패: {e}"),
                    }
                } else {
                    let expanded = if input.starts_with("~/") {
                        std::env::var_os("HOME").map(|h| PathBuf::from(h).join(&input[2..]))
                            .unwrap_or_else(|| PathBuf::from(&input))
                    } else { PathBuf::from(&input) };
                    self.open_project(expanded, matches!(kind, PromptKind::NewProject));
                }
            }
            KeyCode::Backspace => { input.pop(); self.mode = Mode::Prompt { kind, input }; }
            KeyCode::Char(c) if !key.modifiers.contains(KeyModifiers::CONTROL) => {
                input.push(c);
                self.mode = Mode::Prompt { kind, input };
            }
            _ => self.mode = Mode::Prompt { kind, input },
        }
    }

    fn handle_key(&mut self, key: KeyEvent) -> bool {
        if key.kind == KeyEventKind::Release { return true; }
        // Use Ctrl for portable terminal shortcuts. Some terminals forward the
        // macOS Command key as SUPER, but most consume it before TUI receives it.
        let command = key.modifiers.intersects(KeyModifiers::CONTROL | KeyModifiers::SUPER);
        let shift = key.modifiers.contains(KeyModifiers::SHIFT);
        let lowered = match key.code { KeyCode::Char(c) => c.to_ascii_lowercase(), _ => '\0' };

        let mode = std::mem::replace(&mut self.mode, Mode::Edit);
        match mode {
            Mode::Prompt { kind, input } => {
                self.handle_prompt(key, kind, input);
                return true;
            }
            Mode::Help | Mode::Preview => {
                self.mode = Mode::Edit;
                return true;
            }
            Mode::History { items, mut selected } => {
                match key.code {
                    KeyCode::Esc => { self.mode = Mode::Edit; }
                    KeyCode::Up => {
                        selected = selected.saturating_sub(1);
                        self.mode = Mode::History { items, selected };
                    }
                    KeyCode::Down => {
                        selected = (selected + 1).min(items.len().saturating_sub(1));
                        self.mode = Mode::History { items, selected };
                    }
                    KeyCode::Enter => {
                        if let Some(item) = items.get(selected).cloned() { self.restore_history(item); }
                    }
                    _ => self.mode = Mode::History { items, selected },
                }
                return true;
            }
            Mode::Edit => {}
        }

        if command {
            match (lowered, shift) {
                ('q', _) => { if let Err(e)=self.save("close") {self.status=e;} else {return false;} }
                ('s', _) => if let Err(e) = self.save("manual") { self.status = e; },
                ('z', _) => self.undo(),
                ('u', _) => self.redo(),
                ('n', _) => self.mode = Mode::Prompt { kind: PromptKind::NewProject, input: String::new() },
                ('o', true) => self.register_current_object(),
                ('o', false) => self.mode = Mode::Prompt { kind: PromptKind::OpenProject, input: String::new() },
                ('t', _) => self.mode = Mode::Prompt { kind: PromptKind::Title, input: self.title.clone() },
                ('h', true) => self.history_menu(),
                ('p', true) => self.mode = Mode::Preview,
                ('g', _) => self.next_object(),
                _ => {}
            }
            return true;
        }

        match key.code {
            KeyCode::F(1) => self.mode = Mode::Help,
            KeyCode::F(6) => {
                self.focused_pane = match self.focused_pane {
                    FocusedPane::LeftIndex => FocusedPane::CenterEditor,
                    FocusedPane::CenterEditor => FocusedPane::RightDetail,
                    FocusedPane::RightDetail => FocusedPane::LeftIndex,
                };
            }
            KeyCode::Esc => self.focused_pane = FocusedPane::CenterEditor,
            KeyCode::Up if self.focused_pane == FocusedPane::LeftIndex =>
                self.left_selection = self.left_selection.saturating_sub(1),
            KeyCode::Down if self.focused_pane == FocusedPane::LeftIndex =>
                self.left_selection = (self.left_selection + 1).min(self.objects().len().saturating_sub(1)),
            KeyCode::Enter if self.focused_pane == FocusedPane::LeftIndex => self.next_object(),
            _ if self.focused_pane != FocusedPane::CenterEditor => {}
            KeyCode::Left => self.editor.move_left(),
            KeyCode::Right => self.editor.move_right(),
            KeyCode::Home => self.editor.move_home(),
            KeyCode::End => self.editor.move_end(),
            KeyCode::Up => self.editor.move_vertical(false),
            KeyCode::Down => self.editor.move_vertical(true),
            KeyCode::PageUp => for _ in 0..12 { self.editor.move_vertical(false); },
            KeyCode::PageDown => for _ in 0..12 { self.editor.move_vertical(true); },
            KeyCode::Backspace => self.change(|e,j| e.backspace(j)),
            KeyCode::Delete => self.change(|e,j| e.delete(j)),
            KeyCode::Enter => self.change(|e,j| e.insert(j, "\n")),
            KeyCode::Tab => self.change(|e,j| e.insert(j, "    ")),
            KeyCode::Char(c) if !key.modifiers.intersects(KeyModifiers::ALT | KeyModifiers::CONTROL) =>
                self.change(|e,j| e.insert(j, &c.to_string())),
            _ => {}
        }
        true
    }

    fn background_maintenance(&mut self) {
        if self.parse_dirty && self.last_typing.elapsed() >= Duration::from_millis(200) {
            self.rebuild_index();
        }
        if self.dirty && self.last_typing.elapsed() >= Duration::from_secs(3)
            && self.last_disk_save.elapsed() >= Duration::from_secs(3) {
            if let Err(e) = self.save("autosave") {
                self.status = format!("TXT 저장 실패 · SQLite 저널에는 보관됨: {e}");
            }
        }
    }

    fn pane_lines(&self, body_rows: usize) -> (Vec<String>, Vec<String>, Vec<String>) {
        let objects = self.objects();
        let mut left = vec!["[ 장면 · 오브젝트 ]".to_string(), "────────────────────".to_string()];
        for (i, (_,name,_)) in objects.iter().enumerate() {
            left.push(format!("{} {}", if i==self.left_selection { ">" } else { " " }, name));
        }
        for line in self.editor.text.lines().filter(|l| l.trim_start().starts_with('#') && !l.trim_start().starts_with("##")).take(12) {
            left.push(format!("  {}", line.trim()));
        }

        let (line_no, grapheme_col) = self.editor.line_and_column();
        let raw_lines: Vec<&str> = self.editor.text.split('\n').collect();
        let scroll = if line_no < self.cursor_scroll {
            line_no
        } else if line_no >= self.cursor_scroll + body_rows.saturating_sub(2) {
            line_no.saturating_sub(body_rows.saturating_sub(3))
        } else { self.cursor_scroll };

        let mut center = vec![format!("[ 원고: {} ]",self.title), "────────────────────".into()];
        for (idx, &line) in raw_lines.iter().enumerate().skip(scroll).take(body_rows.saturating_sub(2)) {
            let kind = parse_line(line).kind;
            let prefix = match kind {
                ElementKind::Scene => "# ",
                ElementKind::Dialogue => "      ",
                ElementKind::Narration => "   ",
                ElementKind::Description => "  ",
                ElementKind::Note => "//",
                ElementKind::PlainText | ElementKind::Empty => " ",
            };
            let mut content = line.to_string();
            if idx==line_no && self.focused_pane==FocusedPane::CenterEditor {
                let at = content.grapheme_indices(true).nth(grapheme_col)
                    .map(|(byte,_)| byte).unwrap_or(content.len());
                content.insert_str(at, "▏");
            }
            center.push(format!("{}{:>4} {}{}", if idx==line_no {">"} else {" "},idx+1,prefix,content));
        }

        let mut right = vec!["[ 오브젝트 정보 ]".into(), "────────────────────".into()];
        if let Some((name,_,id)) = objects.get(self.left_selection) {
            if let Some(obj) = self.engine.get_by_id(id) {
                right.push(format!("이름: {}",obj.name));
                right.push(format!("유형: {}",obj.category));
                right.push(format!("별칭: {}",obj.aliases.join(", ")));
                let mut occurrences = vec![];
                for (lineno, line) in self.editor.text.lines().enumerate() {
                    for mention in self.engine.find_mentions_in_line(line,lineno,None) {
                        if mention.object_id == *id { occurrences.push(lineno+1); }
                    }
                }
                right.push(format!("등장 {}회", occurrences.len()));
                right.push(format!("줄: {}",occurrences.iter().take(16).map(ToString::to_string).collect::<Vec<_>>().join(",")));
            } else { right.push(format!("선택: {name}")); }
        } else {
            right.push("[[김철수]] : 인물".into());
            right.push("[라벨] 자동 연결".into());
            right.push("F1: 입력 도움말".into());
        }
        (left,center,right)
    }

    fn overlay(&self, h: usize) -> Option<Vec<String>> {
        match &self.mode {
            Mode::Edit => None,
            Mode::Help => Some(vec![
                " [ F1 · 입력 및 키보드 도움말 ]".into(),
                "Esc 또는 아무 키로 돌아가기".into(),
                "F6 = 좌측 → 중앙 → 우측 패널".into(),
                "Ctrl+S 저장 / Ctrl+Q 종료".into(),
                "Ctrl+Z Undo / Ctrl+U Redo".into(),
                "Ctrl+N 새 작품 / Ctrl+O 열기".into(),
                "Ctrl+T 제목 수정 / Ctrl+G 다음 등장".into(),
                "Ctrl+Shift+O 현재 단어를 [[오브젝트]]로 지정".into(),
                "Ctrl+Shift+H 삭제 문장·저장본 복구".into(),
                "Ctrl+Shift+P 시나리오 출력 미리보기".into(),
                "# 장면 제목".into(), "@@ 대사 @@", "## 상황설명 ##",
                "₩₩ 나레이션 ₩₩", "[[김철수]] / [[장소:서울역]]",
                "Mac Terminal은 Command가 전달되지 않을 수 있습니다.".into(),
                "이 TUI에서는 Ctrl 단축키를 사용하세요.".into(),
            ].into_iter().map(str::to_string).collect()),
            Mode::Preview => {
                let mut lines=vec![" [ 시나리오 형식 미리보기 ] Esc: 돌아가기".into()];
                for line in self.editor.text.lines().take(h.saturating_sub(3)) {
                    let p=parse_line(line);
                    if p.kind==ElementKind::Note {continue;}
                    let indent=match p.kind {
                        ElementKind::Dialogue=>"      ",
                        ElementKind::Narration=>"   ",
                        ElementKind::Description=>"  ",
                        _=>"",
                    };
                    lines.push(format!("{indent}{}",p.content));
                }
                Some(lines)
            }
            Mode::History {items,selected} => {
                let mut lines=vec![" [ Ctrl+Shift+H · 기록 복구 ]".into(),
                    "↑↓ 선택 · Enter 복원 · Esc 취소".into(),
                    "삭제 기록은 현재 커서에 삽입합니다.".into()];
                let scroll=selected.saturating_sub(h.saturating_sub(5));
                for (i,item) in items.iter().enumerate().skip(scroll).take(h.saturating_sub(4)) {
                    lines.push(format!("{} {}",if i==*selected {">"} else {" "},item.label()));
                }
                Some(lines)
            }
            Mode::Prompt {kind,input} => Some(vec![
                match kind {
                    PromptKind::NewProject=>"[ 새 프로젝트 경로 ]",
                    PromptKind::OpenProject=>"[ 기존 프로젝트 경로 ]",
                    PromptKind::Title=>"[ 작품 제목 ]",
                }.to_string(),
                "Enter 확인 · Esc 취소".into(),
                format!("> {input}▏"),
            ]),
        }
    }

    pub fn render_frame(&self,width:usize,height:usize) -> String {
        if width<72 || height<12 {
            return format!("repo: 터미널을 72×12 이상으로 키워주세요. ({}×{})\r\n",width,height);
        }
        let content_width=width.saturating_sub(4);
        let left_w=(content_width*24)/100;
        let right_w=(content_width*27)/100;
        let center_w=content_width-left_w-right_w;
        let body_rows=height.saturating_sub(5);
        let (left,mut center,right)=self.pane_lines(body_rows);
        if let Some(overlay)=self.overlay(body_rows) {center=overlay;}
        let mut out=String::new();
        out.push_str(&format!("┌{}┐\r\n","─".repeat(width-2)));
        let focus=match self.focused_pane {
            FocusedPane::LeftIndex=>"INDEX",FocusedPane::CenterEditor=>"EDITOR",FocusedPane::RightDetail=>"DETAIL",
        };
        let caption=format!(" repo · {} │ {} │ {}",
            self.title,if self.dirty {"● journaled"} else {"✓ saved"},focus);
        out.push_str(&format!("│{}│\r\n",pad(&caption,width-2)));
        out.push_str(&format!("├{}┤\r\n","─".repeat(width-2)));
        for row in 0..body_rows {
            let one=left.get(row).map(String::as_str).unwrap_or("");
            let two=center.get(row).map(String::as_str).unwrap_or("");
            let three=right.get(row).map(String::as_str).unwrap_or("");
            out.push_str(&format!("│{}│{}│{}│\r\n",pad(one,left_w),pad(two,center_w),pad(three,right_w)));
        }
        out.push_str(&format!("├{}┤\r\n","─".repeat(width-2)));
        let footer=format!(" F6 패널 │ F1 도움말 │ Ctrl+S 저장 │ Ctrl+Z/U 복구 │ Ctrl+Shift+H 이력 │ {}",
            self.status);
        out.push_str(&format!("│{}│",pad(&footer,width-2)));
        out
    }

    pub fn run(&mut self) -> Result<(), String> {
        if !io::stdin().is_terminal() || !io::stdout().is_terminal() {
            return Err("대화형 TUI는 실제 터미널에서 실행해야 합니다. --help 및 --self-test는 헤드리스 실행 가능".into());
        }
        let _terminal=TerminalSession::enter()?;
        let mut stdout=io::stdout();
        loop {
            self.background_maintenance();
            let (w,h)=terminal::size().map_err(|e| e.to_string())?;
            queue!(stdout,MoveTo(0,0),Clear(ClearType::All),
                Print(self.render_frame(w as usize,h as usize)))
                .map_err(|e| e.to_string())?;
            stdout.flush().map_err(|e| e.to_string())?;
            if event::poll(Duration::from_millis(180)).map_err(|e| e.to_string())? {
                match event::read().map_err(|e| e.to_string())? {
                    Event::Key(key) => if !self.handle_key(key) {break;},
                    Event::Paste(content) if self.focused_pane==FocusedPane::CenterEditor =>
                        self.change(|e,j| e.insert(j,&content)),
                    Event::Resize(_,_) => {},
                    _ => {},
                }
            }
        }
        Ok(())
    }
}

// Unicode display width is not the same as byte/char count for Korean text.
fn pad(input:&str,width:usize)->String {
    let mut result=String::new();
    let mut used=0;
    for g in input.graphemes(true) {
        let w=UnicodeWidthStr::width(g);
        if used+w>width {break;}
        result.push_str(g);
        used+=w;
    }
    result.push_str(&" ".repeat(width-used));
    result
}

pub fn run_interactive(root:&Path)->Result<(),String>{
    ScreenplayUI::open(root)?.run()
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn korean_padding_uses_display_width() {
        assert_eq!(UnicodeWidthStr::width(pad("김철수",8).as_str()),8);
        assert_eq!(pad("김철수",4),"김철");
    }

    #[test]
    fn f6_focus_cycle_without_terminal() {
        // Focus state can be tested with a small in-memory editor and SQLite.
        let base=std::env::temp_dir().join(format!("repo-tui-f6-test-{}", std::process::id()));
        let _=std::fs::remove_dir_all(&base);
        std::fs::create_dir_all(&base).unwrap();
        let mut ui=ScreenplayUI::open(&base).unwrap();
        let key=KeyEvent::new(KeyCode::F(6),KeyModifiers::NONE);
        assert_eq!(ui.focused_pane,FocusedPane::CenterEditor);
        ui.handle_key(key);
        assert_eq!(ui.focused_pane,FocusedPane::RightDetail);
        ui.handle_key(key);
        assert_eq!(ui.focused_pane,FocusedPane::LeftIndex);
        ui.handle_key(key);
        assert_eq!(ui.focused_pane,FocusedPane::CenterEditor);
        drop(ui);
        let _=std::fs::remove_dir_all(base);
    }
}

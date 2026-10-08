import AppKit
import Foundation

final class KeyboardWindow: NSWindow {
    weak var editor: NSTextView?

    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 {
            makeFirstResponder(editor)
            return
        }
        super.keyDown(with: event)
    }
}

final class MainWindowController: NSWindowController, NSTextViewDelegate, NSTableViewDataSource, NSTableViewDelegate {
    private let database = ProjectDatabase()

    private let tableView = NSTableView()
    private let editor = NSTextView()
    private let infoView = NSTextView()
    private let projectLabel = NSTextField(labelWithString: "No project")
    private let statusLabel = NSTextField(labelWithString: "Ready")
    private let titleField = NSTextField(string: "")
    private var previewWindow: NSWindow?
    private static let guide = """
    repo — 빠른 시작

    ⌘N: 새 작품  /  ⌘O: 기존 작품 열기
    상단 제목 입력 후 원고 작성

    # 병원 복도 - 밤    장면 제목
    ## 문이 열린다 ##    상황 설명
    @@ 여기는 어디야? @@    대사
    ₩₩ 그날 밤... ₩₩    나레이션
    [[김철수]]          오브젝트 최초 등록
    [[장소:서울역]]     장소 등록

    이후 일반 '김철수' 입력도 자동으로 연결됩니다.

    ⌘Z Undo / ⌘U Redo
    ⌘⇧O 오브젝트 등록  /  ⌘G 다음 등장
    ⌘⇧P 서식 미리보기  /  ⌘⇧H 이전 저장본
    ⌘S 저장  /  ⌘T 제목 편집
    메뉴 File > Export: DOCX / PDF / HWPX(실험)

    script/main.txt = UTF-8 원고
    project.sqlite = 오브젝트와 복구 기록
    """

    private var objects: [StoryObject] = []
    private var projectURL: URL?
    private var autosaveTimer: Timer?
    private var parseTimer: Timer?
    private var loading = false
    private var lastRevisionText = ""

    init() {
        let window = KeyboardWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1440, height: 900),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "repo 0.3.0"
        window.minSize = NSSize(width: 900, height: 600)
        super.init(window: window)

        buildUI(in: window)
        window.editor = editor
        window.center()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func buildUI(in window: NSWindow) {
        guard let content = window.contentView else { return }

        let top = NSStackView()
        top.orientation = .horizontal
        top.alignment = .centerY
        top.distribution = .fill
        top.spacing = 8
        top.translatesAutoresizingMaskIntoConstraints = false
        projectLabel.lineBreakMode = .byTruncatingMiddle
        projectLabel.stringValue = "프로젝트가 없습니다. ⌘N 새 작품 / ⌘O 열기 / Help 사용법"
        titleField.placeholderString = "작품 제목"
        titleField.isEnabled = false
        titleField.lineBreakMode = .byTruncatingTail
        titleField.widthAnchor.constraint(equalToConstant: 240).isActive = true
        top.addArrangedSubview(NSTextField(labelWithString: "제목"))
        top.addArrangedSubview(titleField)
        top.addArrangedSubview(projectLabel)
        top.addArrangedSubview(NSView())
        top.addArrangedSubview(statusLabel)

        let split = NSSplitView()
        split.isVertical = true
        split.dividerStyle = .thin
        split.translatesAutoresizingMaskIntoConstraints = false

        let objectPane = NSScrollView()
        objectPane.hasVerticalScroller = true
        objectPane.borderType = .bezelBorder

        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("objects"))
        column.title = "Objects"
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.delegate = self
        tableView.dataSource = self
        tableView.usesAlternatingRowBackgroundColors = true
        tableView.focusRingType = .none
        tableView.target = self
        tableView.doubleAction = #selector(showSelectedObject)
        objectPane.documentView = tableView

        let editorScroll = NSScrollView()
        editorScroll.hasVerticalScroller = true
        editorScroll.hasHorizontalScroller = false
        editorScroll.autohidesScrollers = true
        editorScroll.borderType = .bezelBorder
        editorScroll.documentView = editor

        editor.isEditable = false
        editor.string = "⌘N: 새 프로젝트\n⌘O: 기존 프로젝트 열기\n\n우측 도움말에서 문법을 확인하세요."
        editor.isRichText = false
        editor.isAutomaticQuoteSubstitutionEnabled = false
        editor.isAutomaticDashSubstitutionEnabled = false
        editor.isAutomaticSpellingCorrectionEnabled = false
        editor.allowsUndo = true
        editor.delegate = self
        editor.font = NSFont.monospacedSystemFont(ofSize: 14, weight: .regular)
        editor.textContainerInset = NSSize(width: 18, height: 18)
        editor.textContainer?.widthTracksTextView = true
        editor.isHorizontallyResizable = false
        editor.isVerticallyResizable = true
        editor.autoresizingMask = [.width]

        let infoScroll = NSScrollView()
        infoScroll.hasVerticalScroller = true
        infoScroll.borderType = .bezelBorder
        infoScroll.documentView = infoView

        infoView.isRichText = false
        infoView.isEditable = false
        infoView.string = Self.guide
        infoView.font = NSFont.monospacedSystemFont(ofSize: 13, weight: .regular)
        infoView.textContainerInset = NSSize(width: 12, height: 12)

        split.addArrangedSubview(objectPane)
        split.addArrangedSubview(editorScroll)
        split.addArrangedSubview(infoScroll)

        objectPane.widthAnchor.constraint(greaterThanOrEqualToConstant: 180).isActive = true
        infoScroll.widthAnchor.constraint(greaterThanOrEqualToConstant: 220).isActive = true

        content.addSubview(top)
        content.addSubview(split)

        NSLayoutConstraint.activate([
            top.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 10),
            top.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -10),
            top.topAnchor.constraint(equalTo: content.topAnchor, constant: 7),
            top.heightAnchor.constraint(equalToConstant: 24),

            split.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            split.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            split.topAnchor.constraint(equalTo: top.bottomAnchor, constant: 7),
            split.bottomAnchor.constraint(equalTo: content.bottomAnchor)
        ])

        split.setPosition(220, ofDividerAt: 0)
        split.setPosition(1110, ofDividerAt: 1)
    }

    func newProject() {
        let panel = NSOpenPanel()
        panel.title = "Choose parent folder"
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = true

        guard panel.runModal() == .OK, let parent = panel.url else { return }

        let alert = NSAlert()
        alert.messageText = "New Project"
        alert.informativeText = "Project folder name"
        let field = NSTextField(string: "NewProject")
        field.frame = NSRect(x: 0, y: 0, width: 280, height: 24)
        alert.accessoryView = field
        alert.addButton(withTitle: "Create")
        alert.addButton(withTitle: "Cancel")

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let name = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty else { return }

        let project = parent.appendingPathComponent(name, isDirectory: true)
        if FileManager.default.fileExists(atPath: project.path) {
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = "프로젝트 폴더가 이미 있습니다"
            alert.informativeText = "기존 프로젝트를 열려면 ⌘O를 사용하세요."
            alert.runModal()
            return
        }
        openProject(at: project, create: true)
    }

    func chooseProject() {
        let panel = NSOpenPanel()
        panel.title = "Open repo project"
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = true

        guard panel.runModal() == .OK, let url = panel.url else { return }
        openProject(at: url, create: false)
    }

    func saveNow(reason: String = "manual") {
        guard let projectURL, !loading else { return }

        do {
            let scriptFolder = projectURL.appendingPathComponent("script", isDirectory: true)
            try FileManager.default.createDirectory(at: scriptFolder, withIntermediateDirectories: true)

            let script = scriptFolder.appendingPathComponent("main.txt")
            let text = editor.string
            try database.setTitle(titleField.stringValue)
            window?.title = (titleField.stringValue.isEmpty ? "repo" : titleField.stringValue) + " — repo 0.3.0"
            guard let data = text.data(using: .utf8) else { throw CocoaError(.fileWriteInapplicableStringEncoding) }
            try data.write(to: script, options: .atomic)

            if text != lastRevisionText {
                try database.addRevision(documentID: "script/main.txt", text: text, reason: reason)
                lastRevisionText = text
            }
            status("Saved")
        } catch {
            status("Save failed: \(error.localizedDescription)")
        }
    }

    func redo() {
        editor.undoManager?.redo()
    }

    func undo() {
        editor.undoManager?.undo()
    }

    func createObjectFromSelection() {
        var selected = (editor.string as NSString).substring(with: editor.selectedRange())
            .trimmingCharacters(in: .whitespacesAndNewlines)

        if selected.isEmpty {
            let caret = editor.selectedRange().location
            let ns = editor.string as NSString
            if caret < ns.length {
                let wordRange = ns.rangeOfCharacter(from: .whitespacesAndNewlines, options: [], range: NSRange(location: caret, length: ns.length - caret))
                let startSearch = ns.rangeOfCharacter(from: .whitespacesAndNewlines, options: .backwards, range: NSRange(location: 0, length: caret))
                let start = startSearch.location == NSNotFound ? 0 : startSearch.location + 1
                let end = wordRange.location == NSNotFound ? ns.length : wordRange.location
                if end > start {
                    selected = ns.substring(with: NSRange(location: start, length: end - start))
                }
            }
        }

        selected = selected.replacingOccurrences(of: "[[", with: "")
            .replacingOccurrences(of: "]]", with: "")
            .trimmingCharacters(in: .whitespacesAndNewlines)

        guard !selected.isEmpty else {
            status("Select or place the cursor on an object name")
            return
        }

        do {
            _ = try database.ensureObject(name: selected)
            parseDocument()
            status("Object registered: \(selected)")
        } catch {
            status("Object error: \(error.localizedDescription)")
        }
    }

    func textDidChange(_ notification: Notification) {
        guard !loading else { return }

        autosaveTimer?.invalidate()
        autosaveTimer = Timer.scheduledTimer(withTimeInterval: 0.8, repeats: false) { [weak self] _ in
            self?.saveNow(reason: "autosave")
        }

        parseTimer?.invalidate()
        parseTimer = Timer.scheduledTimer(withTimeInterval: 0.18, repeats: false) { [weak self] _ in
            self?.parseDocument()
        }
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        objects.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let id = NSUserInterfaceItemIdentifier("ObjectCell")
        let cell = tableView.makeView(withIdentifier: id, owner: self) as? NSTableCellView ?? {
            let view = NSTableCellView()
            view.identifier = id
            let label = NSTextField(labelWithString: "")
            label.translatesAutoresizingMaskIntoConstraints = false
            label.lineBreakMode = .byTruncatingTail
            view.textField = label
            view.addSubview(label)
            NSLayoutConstraint.activate([
                label.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 6),
                label.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -6),
                label.centerYAnchor.constraint(equalTo: view.centerYAnchor)
            ])
            return view
        }()

        let object = objects[row]
        cell.textField?.stringValue = "\(object.name)  [\(object.type)]  \(object.mentions)"
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        showSelectedObject()
    }

    @objc private func showSelectedObject() {
        let row = tableView.selectedRow
        guard row >= 0, row < objects.count else {
            infoView.string = Self.guide
            return
        }

        let object = objects[row]
        let ns = editor.string as NSString
        var search = NSRange(location: 0, length: ns.length)
        var lines: [String] = []
        while search.length > 0 && lines.count < 25 {
            let range = ns.range(of: object.name, options: [], range: search)
            if range.location == NSNotFound { break }
            let line = ns.substring(to: range.location).filter { $0 == "\n" }.count + 1
            let lineRange = ns.lineRange(for: range)
            let excerpt = ns.substring(with: lineRange).trimmingCharacters(in: .whitespacesAndNewlines)
            lines.append("줄 \(line): \(excerpt.prefix(65))")
            let next = range.location + max(1, range.length)
            if next >= ns.length { break }
            search = NSRange(location: next, length: ns.length - next)
        }
        infoView.string = """
        오브젝트: \(object.name)
        유형: \(object.type)
        등장 횟수: \(object.mentions)

        등장 위치
        --------------------
        \(lines.joined(separator: "\n"))

        ⌘G: 다음 등장 위치로 이동
        """
    }

    private func openProject(at url: URL, create: Bool) {
        saveNow(reason: "switch-project")
        do {
            if !create && !FileManager.default.fileExists(atPath: url.appendingPathComponent("project.sqlite").path) {
                status("올바른 프로젝트 폴더를 선택하세요")
                return
            }
            if create {
                try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
            }

            let scriptFolder = url.appendingPathComponent("script", isDirectory: true)
            try FileManager.default.createDirectory(at: scriptFolder, withIntermediateDirectories: true)

            try database.open(url.appendingPathComponent("project.sqlite"))
            projectURL = url
            projectLabel.stringValue = url.path
            titleField.isEnabled = true
            titleField.stringValue = try database.title() ?? url.lastPathComponent
            editor.isEditable = true

            loading = true
            let script = scriptFolder.appendingPathComponent("main.txt")
            if FileManager.default.fileExists(atPath: script.path) {
                editor.string = try String(contentsOf: script, encoding: .utf8)
            } else {
                editor.string = """
                # 첫 장면

                ## 상황 설명을 입력하세요. ##

                [[김철수]]
                @@ 대사를 입력하세요. @@

                ₩₩ 나레이션을 입력하세요. ₩₩
                """
            }
            loading = false

            parseDocument()
            lastRevisionText = editor.string
            window?.title = titleField.stringValue + " — repo 0.3.0"
            saveNow(reason: "open")
            window?.makeFirstResponder(editor)
            status("Project loaded")
        } catch {
            loading = false
            status("Open failed: \(error.localizedDescription)")
        }
    }

    private func parseDocument() {
        guard projectURL != nil else { return }

        do {
            let text = editor.string
            for explicit in ScriptParser.explicitObjects(in: text) {
                _ = try database.ensureObject(name: explicit.name, type: explicit.type)
            }

            let lexicon = try database.lexicon()
            try database.replaceMentions(
                documentID: "script/main.txt",
                values: ScriptParser.mentions(in: text, lexicon: lexicon)
            )
            objects = try database.objects()
            let selectedObjectID = tableView.selectedRow >= 0 && tableView.selectedRow < objects.count
                ? objects[tableView.selectedRow].id : nil
            tableView.reloadData()
            if let selectedObjectID, let row = objects.firstIndex(where: { $0.id == selectedObjectID }) {
                tableView.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
            }
            applyTemporaryHighlighting(lexicon: lexicon)
            showSelectedObject()
        } catch {
            status("Parse error: \(error.localizedDescription)")
        }
    }

    private func applyTemporaryHighlighting(lexicon: [String: Int64]) {
        guard let layoutManager = editor.layoutManager else { return }
        let full = NSRange(location: 0, length: (editor.string as NSString).length)
        layoutManager.removeTemporaryAttribute(.foregroundColor, forCharacterRange: full)
        layoutManager.removeTemporaryAttribute(.backgroundColor, forCharacterRange: full)
        layoutManager.removeTemporaryAttribute(.underlineStyle, forCharacterRange: full)

        let ns = editor.string as NSString

        let rules: [(String, NSColor)] = [
            (#"@@.*?@@"#, .systemBlue),
            (#"##.*?##"#, .systemOrange),
            (#"₩₩.*?₩₩"#, .systemPurple),
            (#"[[.*?]]"#, .systemTeal)
        ]

        for (pattern, color) in rules {
            guard let regex = try? NSRegularExpression(pattern: pattern) else { continue }
            regex.enumerateMatches(in: editor.string, range: full) { match, _, _ in
                guard let range = match?.range else { return }
                layoutManager.addTemporaryAttribute(.foregroundColor, value: color, forCharacterRange: range)
            }
        }

        for name in lexicon.keys where !name.isEmpty {
            var search = NSRange(location: 0, length: ns.length)
            while search.length > 0 {
                let found = ns.range(of: name, range: search)
                if found.location == NSNotFound { break }
                layoutManager.addTemporaryAttribute(.underlineStyle, value: NSUnderlineStyle.single.rawValue, forCharacterRange: found)
                layoutManager.addTemporaryAttribute(.backgroundColor, value: NSColor.controlAccentColor.withAlphaComponent(0.08), forCharacterRange: found)
                let next = found.location + max(1, found.length)
                if next >= ns.length { break }
                search = NSRange(location: next, length: ns.length - next)
            }
        }
    }


    func showHelp() {
        infoView.string = Self.guide
        if projectURL == nil {
            let alert = NSAlert()
            alert.messageText = "repo 사용법"
            alert.informativeText = Self.guide
            alert.runModal()
        }
    }

    func focusTitle() {
        guard projectURL != nil else { return }
        window?.makeFirstResponder(titleField)
        titleField.selectText(nil)
    }

    func nextObjectMention() {
        let row = tableView.selectedRow
        guard row >= 0 && row < objects.count else { return }
        let name = objects[row].name
        let ns = editor.string as NSString
        let current = editor.selectedRange()
        var range = NSRange(location: min(current.location + current.length, ns.length),
                            length: max(0, ns.length - current.location - current.length))
        var found = ns.range(of: name, range: range)
        if found.location == NSNotFound {
            range = NSRange(location: 0, length: ns.length)
            found = ns.range(of: name, range: range)
        }
        if found.location != NSNotFound {
            editor.setSelectedRange(found)
            editor.scrollRangeToVisible(found)
            window?.makeFirstResponder(editor)
        }
    }

    func restoreEarlierRevision() {
        guard projectURL != nil else { return }
        saveNow(reason: "before-restore")
        do {
            guard let text = try database.previousRevisionText() else {
                status("이전 저장본이 없습니다")
                return
            }
            let alert = NSAlert()
            alert.messageText = "이전 원고 복원"
            alert.informativeText = "현재 원고는 복구 기록으로 보관됩니다. 이전 저장본으로 바꾸시겠습니까?"
            alert.addButton(withTitle: "복원")
            alert.addButton(withTitle: "취소")
            guard alert.runModal() == .alertFirstButtonReturn else { return }
            loading = true
            editor.string = text
            loading = false
            parseDocument()
            saveNow(reason: "restore")
            window?.makeFirstResponder(editor)
        } catch {
            loading = false
            status("복원 실패: \(error.localizedDescription)")
        }
    }

    func showPreview() {
        guard projectURL != nil else { return }
        let paragraphs = ScriptDocument.paragraphs(editor.string,
            title: titleField.stringValue, names: Set(objects.filter { $0.type == "character" }.map(\.name)))
        if previewWindow == nil {
            let panel = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 820, height: 800),
                                 styleMask: [.titled, .closable, .resizable],
                                 backing: .buffered, defer: false)
            panel.isReleasedWhenClosed = false
            panel.minSize = NSSize(width: 550, height: 450)
            panel.title = "repo — 시나리오 미리보기"
            panel.center()
            let scroll = NSScrollView(frame: panel.contentView!.bounds)
            scroll.autoresizingMask = [.width, .height]
            scroll.hasVerticalScroller = true
            let preview = NSTextView(frame: scroll.bounds)
            preview.isEditable = false
            preview.isRichText = true
            preview.textContainerInset = NSSize(width: 38, height: 35)
            preview.textContainer?.widthTracksTextView = true
            preview.isHorizontallyResizable = false
            preview.autoresizingMask = [.width]
            scroll.documentView = preview
            panel.contentView?.addSubview(scroll)
            previewWindow = panel
        }
        if let scroll = previewWindow?.contentView?.subviews.first as? NSScrollView,
           let view = scroll.documentView as? NSTextView {
            view.textStorage?.setAttributedString(ScriptDocument.formattedText(paragraphs))
        }
        previewWindow?.makeKeyAndOrderFront(nil)
    }

    func exportDocument(extension fileExtension: String) {
        guard projectURL != nil else { return }
        let panel = NSSavePanel()
        panel.title = "문서 내보내기"
        panel.allowedFileTypes = [fileExtension]
        panel.nameFieldStringValue = (titleField.stringValue.isEmpty ? "script" : titleField.stringValue) + "." + fileExtension
        guard panel.runModal() == .OK, let url = panel.url else { return }
        let paragraphs = ScriptDocument.paragraphs(editor.string,
            title: titleField.stringValue, names: Set(objects.filter { $0.type == "character" }.map(\.name)))
        do {
            switch fileExtension {
            case "pdf": try ScriptDocument.exportPDF(to: url, paragraphs: paragraphs)
            case "docx": try ScriptDocument.exportDOCX(to: url, paragraphs: paragraphs)
            case "hwpx": try ScriptDocument.exportHWPX(to: url, paragraphs: paragraphs, title: titleField.stringValue)
            default: return
            }
            status("내보내기 완료: \(url.path)")
            if fileExtension == "hwpx" {
                let warning = NSAlert()
                warning.messageText = "HWPX 실험 기능"
                warning.informativeText = "한컴오피스에서 파일 열림과 레이아웃을 확인해주세요."
                warning.runModal()
            }
        } catch {
            status("내보내기 실패: \(error.localizedDescription)")
        }
    }

    private func status(_ text: String) {
        statusLabel.stringValue = text
    }
}

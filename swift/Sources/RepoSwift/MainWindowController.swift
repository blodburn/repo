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
        window.title = "repo Swift 0.1"
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

        openProject(at: parent.appendingPathComponent(name, isDirectory: true), create: true)
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
            try text.data(using: .utf8)?.write(to: script, options: .atomic)

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
            infoView.string = ""
            return
        }

        let object = objects[row]
        infoView.string = """
        Name
        \(object.name)

        Type
        \(object.type)

        Mentions
        \(object.mentions)
        """
    }

    private func openProject(at url: URL, create: Bool) {
        do {
            if create {
                try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
            }

            let scriptFolder = url.appendingPathComponent("script", isDirectory: true)
            try FileManager.default.createDirectory(at: scriptFolder, withIntermediateDirectories: true)

            try database.open(url.appendingPathComponent("project.sqlite"))
            projectURL = url
            projectLabel.stringValue = url.path

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
            tableView.reloadData()
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

    private func status(_ text: String) {
        statusLabel.stringValue = text
    }
}

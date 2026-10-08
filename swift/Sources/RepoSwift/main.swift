import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var mainWindowController: MainWindowController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        let controller = MainWindowController()
        mainWindowController = controller
        buildMenus(for: controller)
        controller.showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    private func buildMenus(for controller: MainWindowController) {
        let mainMenu = NSMenu()

        let appItem = NSMenuItem()
        mainMenu.addItem(appItem)
        let appMenu = NSMenu()
        appItem.submenu = appMenu
        appMenu.addItem(withTitle: "About repo Swift", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Quit repo Swift", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")

        let fileItem = NSMenuItem()
        mainMenu.addItem(fileItem)
        let fileMenu = NSMenu(title: "File")
        fileItem.submenu = fileMenu

        let new = NSMenuItem(title: "New Project", action: #selector(MenuActions.newProject(_:)), keyEquivalent: "n")
        new.target = MenuActions.shared
        fileMenu.addItem(new)

        let open = NSMenuItem(title: "Open Project", action: #selector(MenuActions.openProject(_:)), keyEquivalent: "o")
        open.target = MenuActions.shared
        fileMenu.addItem(open)

        let save = NSMenuItem(title: "Save", action: #selector(MenuActions.save(_:)), keyEquivalent: "s")
        save.target = MenuActions.shared
        fileMenu.addItem(save)

        fileMenu.addItem(.separator())
        let exportItem = NSMenuItem(title: "Export", action: nil, keyEquivalent: "")
        let exportMenu = NSMenu(title: "Export")
        for (title, ext) in [("Word (.docx)", "docx"), ("PDF (.pdf)", "pdf"), ("Hancom (.hwpx) — experimental", "hwpx")] {
            let item = NSMenuItem(title: title, action: #selector(MenuActions.export(_:)), keyEquivalent: "")
            item.representedObject = ext
            item.target = MenuActions.shared
            exportMenu.addItem(item)
        }
        fileMenu.addItem(exportItem)
        fileMenu.setSubmenu(exportMenu, for: exportItem)

        let editItem = NSMenuItem()
        mainMenu.addItem(editItem)
        let editMenu = NSMenu(title: "Edit")
        editItem.submenu = editMenu

        let undo = NSMenuItem(title: "Undo", action: #selector(MenuActions.undo(_:)), keyEquivalent: "z")
        undo.target = MenuActions.shared
        editMenu.addItem(undo)

        let redo = NSMenuItem(title: "Redo", action: #selector(MenuActions.redo(_:)), keyEquivalent: "u")
        redo.target = MenuActions.shared
        editMenu.addItem(redo)

        editMenu.addItem(.separator())

        let createObject = NSMenuItem(title: "Create Object from Selection", action: #selector(MenuActions.createObject(_:)), keyEquivalent: "o")
        createObject.keyEquivalentModifierMask = [.command, .shift]
        createObject.target = MenuActions.shared
        editMenu.addItem(createObject)

        editMenu.addItem(.separator())
        func addEdit(_ title: String, _ key: String,
                     _ modifiers: NSEvent.ModifierFlags = [.command],
                     _ action: Selector) {
            let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
            item.keyEquivalentModifierMask = modifiers
            item.target = MenuActions.shared
            editMenu.addItem(item)
        }
        addEdit("Edit Project Title", "t", [.command], #selector(MenuActions.editTitle(_:)))
        addEdit("Next Object Mention", "g", [.command], #selector(MenuActions.nextMention(_:)))
        addEdit("Screenplay Preview", "p", [.command, .shift], #selector(MenuActions.preview(_:)))
        addEdit("Restore Previous Saved Version", "h", [.command, .shift], #selector(MenuActions.restore(_:)))

        let helpItem = NSMenuItem()
        mainMenu.addItem(helpItem)
        let helpMenu = NSMenu(title: "Help")
        helpItem.submenu = helpMenu
        let guide = NSMenuItem(title: "Getting Started", action: #selector(MenuActions.help(_:)), keyEquivalent: "?")
        guide.keyEquivalentModifierMask = [.command, .shift]
        guide.target = MenuActions.shared
        helpMenu.addItem(guide)

        MenuActions.shared.controller = controller
        NSApp.mainMenu = mainMenu
    }
}

final class MenuActions: NSObject {
    static let shared = MenuActions()
    weak var controller: MainWindowController?

    @objc func newProject(_ sender: Any?) { controller?.newProject() }
    @objc func openProject(_ sender: Any?) { controller?.chooseProject() }
    @objc func save(_ sender: Any?) { controller?.saveNow() }
    @objc func undo(_ sender: Any?) { controller?.undo() }
    @objc func redo(_ sender: Any?) { controller?.redo() }
    @objc func createObject(_ sender: Any?) { controller?.createObjectFromSelection() }
    @objc func editTitle(_ sender: Any?) { controller?.focusTitle() }
    @objc func nextMention(_ sender: Any?) { controller?.nextObjectMention() }
    @objc func preview(_ sender: Any?) { controller?.showPreview() }
    @objc func restore(_ sender: Any?) { controller?.restoreEarlierRevision() }
    @objc func help(_ sender: Any?) { controller?.showHelp() }
    @objc func export(_ sender: NSMenuItem) {
        if let ext = sender.representedObject as? String { controller?.exportDocument(extension: ext) }
    }
}

let application = NSApplication.shared
let delegate = AppDelegate()
application.delegate = delegate
application.setActivationPolicy(.regular)
application.run()

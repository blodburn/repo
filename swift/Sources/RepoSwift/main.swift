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
}

let application = NSApplication.shared
let delegate = AppDelegate()
application.delegate = delegate
application.setActivationPolicy(.regular)
application.run()

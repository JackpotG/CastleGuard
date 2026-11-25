import AppKit
import WebKit

// The native shell owns only the window and its local service. Editing remains in config-editor.
final class PlannerApplication: NSObject, NSApplicationDelegate, NSWindowDelegate, WKUIDelegate, WKNavigationDelegate {
    private var window: NSWindow!
    private var webView: WKWebView!
    private var service: Process?
    private var startupTimer: Timer?
    private var sessionDirectory: URL!
    private var readyFile: URL!
    private var serviceURL: URL?
    private var deadline = Date()
    private var isStopping = false
    private var isCheckingTermination = false
    private var logHandle: FileHandle?

    func applicationDidFinishLaunching(_ notification: Notification) {
        installMenus()
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1360, height: 900),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = "游戏策划配置 · CastleGuard"
        window.minSize = NSSize(width: 1050, height: 700)
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = .nonPersistent()
        webView = WKWebView(frame: window.contentView!.bounds, configuration: configuration)
        webView.autoresizingMask = [.width, .height]
        webView.uiDelegate = self
        webView.navigationDelegate = self
        window.contentView!.addSubview(webView)
        webView.loadHTMLString("<html lang='zh-CN'><body style='font:16px -apple-system;background:#f3f6f0;color:#41624c;padding:80px'>正在打开游戏策划工作台…</body></html>", baseURL: nil)
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        startService()
    }

    private func installMenus() {
        let menu = NSMenu()
        let appItem = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "关于游戏策划配置", action: #selector(showAbout), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "隐藏游戏策划配置", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "退出游戏策划配置", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu
        menu.addItem(appItem)
        let file = NSMenu(title: "文件")
        file.addItem(withTitle: "工作台", action: #selector(goHome), keyEquivalent: "0")
        file.addItem(withTitle: "保存当前配置", action: #selector(save), keyEquivalent: "s")
        let fileItem = NSMenuItem(title: "文件", action: nil, keyEquivalent: "")
        fileItem.submenu = file
        menu.addItem(fileItem)
        let edit = NSMenu(title: "编辑")
        for (title, selector, key) in [("撤销", "undo:", "z"), ("剪切", "cut:", "x"),
                                       ("复制", "copy:", "c"), ("粘贴", "paste:", "v"), ("全选", "selectAll:", "a")] {
            edit.addItem(withTitle: title, action: Selector(selector), keyEquivalent: key)
        }
        let editItem = NSMenuItem(title: "编辑", action: nil, keyEquivalent: "")
        editItem.submenu = edit
        menu.addItem(editItem)
        NSApp.mainMenu = menu
    }

    @objc private func goHome() { webView.evaluateJavaScript("window.PlannerApp?.navigate('home')", completionHandler: nil) }
    @objc private func save() { webView.evaluateJavaScript("window.PlannerApp?.save()", completionHandler: nil) }
    @objc private func showAbout() {
        let alert = NSAlert()
        alert.messageText = "游戏策划配置"
        alert.informativeText = "CastleGuard\n关卡、技能树与基础数值的统一工作台。\n修改完成后，使用各工具的“保存配置”。"
        alert.addButton(withTitle: "好")
        alert.runModal()
    }

    private func startService() {
        guard let root = Bundle.main.object(forInfoDictionaryKey: "PlannerProjectRoot") as? String,
              let python = Bundle.main.object(forInfoDictionaryKey: "PlannerPython") as? String else {
            showServiceError("应用信息不完整，请重新生成应用。", canRetry: false)
            return
        }
        let script = URL(fileURLWithPath: root).appendingPathComponent("tools/config-editor/server.py")
        guard FileManager.default.fileExists(atPath: script.path), FileManager.default.isExecutableFile(atPath: python) else {
            showServiceError("找不到项目或 Python，请在项目目录重新生成应用。", canRetry: false)
            return
        }
        do {
            if sessionDirectory == nil {
                sessionDirectory = FileManager.default.temporaryDirectory.appendingPathComponent("castleguard-planner-\(UUID().uuidString)")
                try FileManager.default.createDirectory(at: sessionDirectory, withIntermediateDirectories: true)
                readyFile = sessionDirectory.appendingPathComponent("ready.json")
                let log = sessionDirectory.appendingPathComponent("service.log")
                FileManager.default.createFile(atPath: log.path, contents: nil)
                logHandle = try FileHandle(forWritingTo: log)
            }
            try? FileManager.default.removeItem(at: readyFile)
            let process = Process()
            process.executableURL = URL(fileURLWithPath: python)
            // Reconnection keeps the existing origin and web view, preserving in-memory drafts.
            process.arguments = [script.path, "--port", String(serviceURL?.port ?? 0), "--ready-file", readyFile.path]
            process.currentDirectoryURL = URL(fileURLWithPath: root)
            var environment = ProcessInfo.processInfo.environment
            environment["PYTHONUNBUFFERED"] = "1"
            environment["PYTHONDONTWRITEBYTECODE"] = "1"
            process.environment = environment
            process.standardOutput = logHandle
            process.standardError = logHandle
            process.terminationHandler = { [weak self] ended in
                DispatchQueue.main.async {
                    guard let self = self, !self.isStopping, self.service === ended else { return }
                    self.startupTimer?.invalidate()
                    NSLog("[PlannerApp] service exited status=%d", ended.terminationStatus)
                    self.showServiceError("本地配置服务已停止。可以重新连接，窗口中的未保存修改会保留。", canRetry: true)
                }
            }
            service = process
            try process.run()
            deadline = Date().addingTimeInterval(15)
            startupTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.checkReady() }
        } catch {
            NSLog("[PlannerApp] startup failed: %@", error.localizedDescription)
            showServiceError("无法启动本地配置服务。\n\(error.localizedDescription)", canRetry: false)
        }
    }

    private func checkReady() {
        if let bytes = try? Data(contentsOf: readyFile),
           let value = try? JSONSerialization.jsonObject(with: bytes) as? [String: Int],
           let port = value["port"], value["pid"] == Int(service?.processIdentifier ?? -1) {
            startupTimer?.invalidate()
            startupTimer = nil
            let address = URL(string: "http://127.0.0.1:\(port)")!
            let isFirstConnection = serviceURL == nil
            serviceURL = address
            if isFirstConnection { webView.load(URLRequest(url: address.appendingPathComponent("planner"))) }
            NSLog("[PlannerApp] service ready port=%d", port)
        } else if Date() > deadline {
            startupTimer?.invalidate()
            service?.terminationHandler = nil
            service?.terminate()
            showServiceError("启动时间过长，请关闭后重新打开应用。", canRetry: false)
        }
    }

    private func showServiceError(_ message: String, canRetry: Bool) {
        let alert = NSAlert()
        alert.messageText = "配置服务暂时不可用"
        alert.informativeText = message
        if canRetry { alert.addButton(withTitle: "重新连接") }
        alert.addButton(withTitle: "关闭提示")
        if alert.runModal() == .alertFirstButtonReturn && canRetry { startService() }
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        NSApp.terminate(nil)
        return false
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if isCheckingTermination { return .terminateCancel }
        isCheckingTermination = true
        // Blur first so an in-progress field edit reaches the editor's change handler before checking.
        webView.evaluateJavaScript("document.activeElement?.blur(); window.PlannerApp?.hasUnsavedChanges() ?? false") { [weak self] value, error in
            guard let self = self else { sender.reply(toApplicationShouldTerminate: false); return }
            var shouldExit = true
            if (value as? Bool) == true || error != nil && self.serviceURL != nil {
                let alert = NSAlert()
                alert.messageText = "还有未保存的配置修改"
                alert.informativeText = "继续编辑并保存，或放弃本次未保存修改后退出。"
                alert.addButton(withTitle: "继续编辑")
                alert.addButton(withTitle: "放弃修改并退出")
                shouldExit = alert.runModal() == .alertSecondButtonReturn
            }
            self.isCheckingTermination = false
            sender.reply(toApplicationShouldTerminate: shouldExit)
        }
        return .terminateLater
    }

    func applicationWillTerminate(_ notification: Notification) {
        isStopping = true
        startupTimer?.invalidate()
        service?.terminationHandler = nil
        if service?.isRunning == true { service?.terminate(); service?.waitUntilExit() }
        try? logHandle?.close()
        if let directory = sessionDirectory { try? FileManager.default.removeItem(at: directory) }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        window.makeKeyAndOrderFront(nil)
        return true
    }

    func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard let url = navigationAction.request.url else { decisionHandler(.cancel); return }
        decisionHandler(url.scheme == "about" || url.scheme == "http" && url.host == "127.0.0.1" && url.port == serviceURL?.port ? .allow : .cancel)
    }

    func webView(_ webView: WKWebView, runJavaScriptConfirmPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping (Bool) -> Void) {
        let alert = NSAlert()
        alert.messageText = message
        alert.addButton(withTitle: "取消")
        alert.addButton(withTitle: "确定")
        completionHandler(alert.runModal() == .alertSecondButtonReturn)
    }

    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        NSLog("[PlannerApp] web content process terminated")
        showServiceError("配置页面意外停止，请重新打开应用。", canRetry: false)
    }
}

let application = NSApplication.shared
application.setActivationPolicy(.regular)
let delegate = PlannerApplication()
application.delegate = delegate
application.run()

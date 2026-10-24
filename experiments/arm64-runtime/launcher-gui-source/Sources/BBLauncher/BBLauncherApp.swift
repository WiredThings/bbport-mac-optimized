// SPDX-License-Identifier: GPL-2.0-or-later
// bbport launcher for macOS: picks the game folder, edits bbport.ini and the start-up
// environment, and runs tools/macos/run.sh (the Mac counterpart of launcher/bbport_launcher.py).
import AppKit
import SwiftUI

@main
struct BBLauncherApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var model = LauncherModel.shared

    var body: some Scene {
        Window("Blood Echoes", id: "main") {
            ContentView()
                .id(model.prefs.uiLanguage)
                .environment(\.locale, L10n.locale)
                .environmentObject(model)
        }
        .defaultSize(width: 680, height: 860)
        .commands {
            CommandGroup(replacing: .appInfo) {
                Button(L10n.key("About Blood Echoes")) {
                    NSApplication.shared.orderFrontStandardAboutPanel(options: AppBuild.current.aboutOptions)
                }
            }
            CommandGroup(replacing: .help) {
                HelpCommands()
                    .environmentObject(model)
            }
        }

        Window(L10n.key("Game Log"), id: "log") {
            LogView()
                .environment(\.locale, L10n.locale)
                .environmentObject(model)
        }
        .defaultSize(width: 900, height: 560)

        Window(L10n.key("Save Manager"), id: "saves") {
            SaveManagerView().environmentObject(model).environment(\.locale, L10n.locale)
        }.defaultSize(width: 740, height: 560)

        Settings {
            LauncherSettingsView()
                .environment(\.locale, L10n.locale)
                .environmentObject(model)
        }
    }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Also come to the front when started as a bare executable (swift run).
        NSApplication.shared.setActivationPolicy(.regular)
        NSApplication.shared.activate()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    func applicationWillTerminate(_ notification: Notification) {
        LauncherModel.shared.prepareForQuit()
    }
}

/// Help menu: the user guide, downloads, problem reports and the last log.
struct HelpCommands: View {
    @EnvironmentObject var model: LauncherModel
    var body: some View {
        Button(L10n.key("Blood Echoes User Guide")) { Links.open(Links.userGuide) }
        Button(L10n.key("Downloads and Release Notes")) { Links.open(Links.releases) }
        Button(L10n.key("Report a Problem…")) { Links.open(Links.issues) }
        Divider()
        Button(L10n.key("Show the Last Run's Log")) { LauncherModel.shared.revealLastLog() }
        Button(L10n.key("Show the Data Folder")) { LauncherModel.shared.reveal(LauncherModel.shared.dataURL) }
    }
}

/// Settings (⌘,): which game engine runs (the release app's own, or a checkout).
struct LauncherSettingsView: View {
    @EnvironmentObject var model: LauncherModel

    var body: some View {
        Form {
            Section {
                if model.engineURL != nil {
                    LabeledContent(L10n.key("Built into this app")) {
                        Text(model.appVersion.isEmpty ? L10n.key("Yes") : L10n.format("Version %@", AppBuild.current.displayVersion))
                    }
                } else {
                    LabeledContent(L10n.key("Embedded at build")) {
                        Text(model.embeddedRepoPath ?? L10n.key("None"))
                            .textSelection(.enabled)
                            .truncationMode(.middle)
                    }
                }
                TextField(text: $model.prefs.repoOverride,
                          prompt: Text(model.engineURL != nil ? L10n.key("Use the built-in engine") : L10n.key("Use the embedded path"))) {
                    Text(L10n.key("Checkout"))
                }
                HStack {
                    Button(L10n.key("Choose…")) { model.chooseRepository() }
                    Button(model.engineURL != nil ? L10n.key("Use the Built-in Engine") : L10n.key("Use the Embedded Path")) {
                        model.prefs.repoOverride = ""
                    }
                    .disabled(model.prefs.repoOverride.isEmpty)
                }
                CheckLabel(check: model.repoCheck)
                LabeledContent(L10n.key("Data folder")) {
                    Text(model.dataURL.path)
                        .textSelection(.enabled)
                        .truncationMode(.middle)
                }
            } header: {
                Text(L10n.key("Game engine"))
            } footer: {
                Text(model.engineURL != nil
                     ? L10n.key("This app carries its own engine; settings, saves and logs go to the data folder. To use an optimized source checkout instead, choose its folder.")
                     : L10n.key("The optimized source checkout with its native build in out/. The app starts the prebuilt native renderer without recompiling."))
                    .foregroundStyle(.secondary)
            }

        }
        .formStyle(.grouped)
        .frame(width: 580, height: 400)
    }
}

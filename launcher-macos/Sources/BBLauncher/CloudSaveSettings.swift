// SPDX-License-Identifier: GPL-2.0-or-later
import AppKit
import SwiftUI

extension LauncherModel {
    func chooseCloudSaveFolder() {
        guard !isRunning, !saveBusy else { return }
        let panel = NSOpenPanel()
        panel.message = L10n.key("Choose a folder in iCloud Drive")
        panel.canChooseDirectories = true; panel.canChooseFiles = false; panel.canCreateDirectories = true
        let drive = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Mobile Documents/com~apple~CloudDocs")
        panel.directoryURL = drive
        let source = userFolderURL
        let finish: (NSApplication.ModalResponse) -> Void = { [weak self] response in
            guard let self, response == .OK, let selected = panel.url, self.userFolderURL == source else { return }
            let folder = selected.resolvingSymlinksInPath().standardizedFileURL
            let cloud = drive.resolvingSymlinksInPath().standardizedFileURL
            let ubiquitous = (try? folder.resourceValues(forKeys: [.isUbiquitousItemKey]).isUbiquitousItem) == true
            guard ubiquitous || folder.path == cloud.path || folder.path.hasPrefix(cloud.path + "/") else {
                self.cloudSaveStatus = L10n.key("Choose a folder in iCloud Drive"); return
            }
            let root = FileManager.default.fileExists(atPath: folder.appendingPathComponent("format.txt").path) ? folder : folder.appendingPathComponent("Blood Echoes Saves")
            self.prefs.cloudSaveFolder = root.path
            self.prefs.cloudSaves = true
            self.cloudSaveConflicts = []
            self.synchronizeCloudSaves()
        }
        if let window = NSApp.windows.first(where: { $0.identifier?.rawValue == "saves" && $0.isVisible }) {
            panel.beginSheetModal(for: window, completionHandler: finish)
        } else { panel.begin(completionHandler: finish) }
    }

    func setCloudSaves(_ enabled: Bool) {
        guard !isRunning, !saveBusy else { return }
        if enabled && prefs.cloudSaveFolder.isEmpty { chooseCloudSaveFolder(); return }
        prefs.cloudSaves = enabled
        if enabled { synchronizeCloudSaves() }
        else { cloudSaveStatus = ""; cloudSaveConflicts = [] }
    }

    func synchronizeCloudSaves(choice: CloudSaveChoice? = nil, then: (() -> Void)? = nil) {
        guard prefs.cloudSaves, !prefs.cloudSaveFolder.isEmpty else { then?(); return }
        let store = saveStore
        let cloud = CloudSaves(store: store, root: URL(fileURLWithPath: prefs.cloudSaveFolder, isDirectory: true))
        var outcome: Result<CloudSaveResult, Error>?
        cloudSaveStatus = L10n.key("Checking iCloud saves…")
        runSaveOperation({ outcome = Result { try store.locked { try cloud.synchronize(choice: choice) } } }) { [weak self] in
            guard let self, let outcome else { return }
            switch outcome {
            case .success(let result):
                self.cloudSaveStatus = result.message; self.cloudSaveConflicts = result.conflicts
                if result.imported { self.useOfflineAfterRestore() }
            case .failure(let error): self.cloudSaveStatus = error.localizedDescription
            }
            // Offline/cloud availability never prevents playing the untouched
            // local save. An unresolved conflict is visible in Save Manager.
            then?()
        }
    }
}

struct CloudSaveSettings: View {
    @EnvironmentObject var model: LauncherModel
    private var available: Bool { !model.isRunning && !model.saveBusy && !model.saveRefreshing }

    var body: some View {
        GroupBox(L10n.key("iCloud Drive save sync")) {
            VStack(alignment: .leading, spacing: 10) {
                Toggle(L10n.key("Sync normal saves"), isOn: Binding(get: { model.prefs.cloudSaves }, set: { model.setCloudSaves($0) }))
                    .disabled(!available)
                Text(L10n.key("Only normal saves sync, before play and after the game exits. Quick checkpoints and backups stay on this Mac."))
                    .font(.caption).foregroundStyle(.secondary)
                if !model.prefs.cloudSaveFolder.isEmpty {
                    Text(model.prefs.cloudSaveFolder).font(.caption).foregroundStyle(.secondary)
                        .lineLimit(1).truncationMode(.middle).textSelection(.enabled)
                }
                HStack {
                    Button(L10n.key("Choose iCloud folder…")) { model.chooseCloudSaveFolder() }.disabled(!available)
                    Button(L10n.key("Sync now")) { model.synchronizeCloudSaves() }.disabled(!available || !model.prefs.cloudSaves)
                }
                if !model.cloudSaveStatus.isEmpty { Text(model.cloudSaveStatus).font(.caption).textSelection(.enabled) }
                if !model.cloudSaveConflicts.isEmpty {
                    Text(L10n.key("Both progress versions are preserved. Choose one to continue; the other devices are not overwritten."))
                        .font(.caption).foregroundStyle(.secondary)
                    ForEach(model.cloudSaveConflicts, id: \.identity) { version in
                        HStack {
                            VStack(alignment: .leading) {
                                Text(version.deviceName)
                                Text(version.created, format: .dateTime.year().month().day().hour().minute()).font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer()
                            Button(L10n.key("Use this progress")) {
                                model.synchronizeCloudSaves(choice: CloudSaveChoice(device: version.id, digest: version.digest))
                            }.disabled(!available)
                        }
                    }
                }
            }.frame(maxWidth: .infinity, alignment: .leading).padding(8)
        }
    }
}

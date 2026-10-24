// SPDX-License-Identifier: GPL-2.0-or-later
import AppKit
import CryptoKit
import SwiftUI

extension LauncherModel {
    var saveStore: SaveStore {
        let key = SHA256.hash(data: Data(userFolderURL.standardizedFileURL.path.utf8)).prefix(8).map { String(format: "%02x", $0) }.joined()
        return SaveStore(user: userFolderURL, directory: dataURL.appendingPathComponent("save-backups/" + key))
    }

    func runSaveOperation(_ operation: @escaping () throws -> Void, then: (() -> Void)? = nil) {
        guard !isRunning, !saveBusy else { return }
        saveRefreshID = UUID(); saveRefreshing = false
        saveBusy = true; saveStatus = L10n.key("Working with saves…")
        let store = saveStore
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let result = Result { try operation() }
            // Retention failures never turn a committed backup/restore into a
            // reported failure or cancel a game launch. Preserve and show them.
            let cleanup: Result<SaveCleanup, Error>?
            if case .success = result { cleanup = Result { try store.locked { try store.pruneHistory() } } }
            else { cleanup = nil }
            DispatchQueue.main.async {
                guard let self else { return }
                self.saveBusy = false
                switch result {
                case .success:
                    self.saveStatus = L10n.key("Saves verified")
                    if let cleanup {
                        switch cleanup {
                        case .success(let value):
                            self.saveCleanupStatus = value.items == 0 ? "" : L10n.format("Reclaimed %@ of old automatic saves", ByteCountFormatter.string(fromByteCount: value.bytes, countStyle: .file))
                        case .failure(let error): self.saveCleanupStatus = error.localizedDescription
                        }
                    }
                    self.refreshSaveBackups()
                    then?()
                case .failure(let error): self.saveStatus = error.localizedDescription
                }
            }
        }
    }

    func cleanSaveHistory() { runSaveOperation({}) }

    func keepCheckpoint(_ checkpoint: QuickCheckpoint, _ keep: Bool) {
        let store = saveStore
        runSaveOperation { try store.locked { try store.setKept(keep, checkpoint: checkpoint) } }
    }

    func keepBackup(_ backup: SaveBackup, _ keep: Bool) {
        let store = saveStore
        runSaveOperation { try store.locked { try store.setKept(keep, backup: backup) } }
    }

    func refreshSaveBackups() {
        guard !saveBusy else { return }
        let store = saveStore, token = UUID()
        saveRefreshID = token; saveRefreshing = true
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let live = Result { try store.currentGameSave() }
            let quick = Result { try store.checkpoints() }
            let backups = Result { try store.backups() }
            DispatchQueue.main.async {
                guard let self, self.saveRefreshID == token, self.userFolderURL == store.user else { return }
                self.saveRefreshing = false
                var errors = [String]()
                switch live { case .success(let value): self.currentGameSave = value
                case .failure(let error): self.currentGameSave = nil; errors.append(error.localizedDescription) }
                switch quick { case .success(let value): self.quickCheckpoints = value
                case .failure(let error): self.quickCheckpoints = []; errors.append(error.localizedDescription) }
                switch backups { case .success(let value): self.saveBackups = value
                case .failure(let error): self.saveBackups = []; errors.append(error.localizedDescription) }
                self.saveReadError = errors.first ?? ""
            }
        }
    }

    func setOnline(_ online: Bool) {
        guard !isRunning, !saveBusy, online != prefs.online else { return }
        let store = saveStore, legacy = dataURL.appendingPathComponent("online-user")
        runSaveOperation({ try store.prepareMode(online: online, legacy: legacy) }) { [weak self] in
            self?.prefs.online = online
            if online { self?.onlineController.load() } else { self?.onlineController.cancel() }
        }
    }

    func makeSaveBackup() {
        let store = saveStore
        runSaveOperation { try store.locked { _ = try store.backup(label: "Manual backup", kinds: ["savedata", "quicksaves"]) } }
    }

    func backupCurrentGameSave() {
        let store = saveStore
        runSaveOperation { try store.locked { try store.backupCurrentGameSave() } }
    }

    func backupQuickCheckpoint(_ checkpoint: QuickCheckpoint) {
        let store = saveStore
        runSaveOperation { try store.locked { try store.backupCheckpoint(checkpoint) } }
    }

    func showCheckpoint(_ checkpoint: QuickCheckpoint) {
        do { openSaveFolder(try saveStore.checkpointFolder(checkpoint)) }
        catch { saveStatus = error.localizedDescription }
    }

    func showBackup(_ record: SaveBackup) {
        do { openSaveFolder(try saveStore.backupFolder(record)) }
        catch { saveStatus = error.localizedDescription }
    }

    func openSaveFolder(_ folder: URL) {
        // General reveal() creates missing directories for engine/mod settings.
        // Save browsing must never recreate a deleted/stale checkpoint directory.
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: folder.path, isDirectory: &isDirectory), isDirectory.boolValue else {
            saveStatus = L10n.key("Save folder not found. Refresh and try again."); return
        }
        NSWorkspace.shared.open(folder)
    }

    func synchronizeOfflineRestore() {
        if prefs.online && saveStore.hasOfflineRestoreIntent { useOfflineAfterRestore() }
    }

    func useOfflineAfterRestore() {
        if prefs.online { prefs.online = false; onlineController.cancel() }
    }

    func makeCheckpoint() {
        let store = saveStore
        runSaveOperation { try store.locked { try store.seedCheckpoint() } }
    }

    func restoreBackup(_ record: SaveBackup, kind: String) {
        guard !isRunning, !saveBusy else { return }
        let alert = NSAlert()
        alert.messageText = L10n.key("Restore this backup?")
        alert.informativeText = L10n.key("Restoring switches to offline play and backs up current saves. Restore whole saves, not individual character files.")
        alert.addButton(withTitle: L10n.key("Restore")); alert.addButton(withTitle: L10n.key("Cancel"))
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let store = saveStore
        runSaveOperation({ try store.locked { try store.restore(record, kind: kind) } }) { [weak self] in self?.useOfflineAfterRestore() }
    }

    func restoreQuickCheckpoint(_ checkpoint: QuickCheckpoint) {
        guard !isRunning, !saveBusy else { return }
        let alert = NSAlert(); alert.messageText = L10n.key("Restore this backup?")
        alert.informativeText = L10n.key("Restoring switches to offline play and backs up current saves. Restore whole saves, not individual character files.")
        alert.addButton(withTitle: L10n.key("Restore")); alert.addButton(withTitle: L10n.key("Cancel"))
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let store = saveStore
        runSaveOperation({ try store.locked { try store.restoreCheckpoint(checkpoint) } }) { [weak self] in self?.useOfflineAfterRestore() }
    }

    func importGameSave() {
        let panel = NSOpenPanel()
        panel.message = L10n.key("Choose a savedata folder")
        panel.canChooseDirectories = true; panel.canChooseFiles = false
        guard panel.runModal() == .OK, let selected = panel.url else { return }
        guard selected.lastPathComponent == "savedata" else { saveStatus = L10n.key("Choose a savedata folder"); return }
        let store = saveStore
        runSaveOperation { try store.locked { _ = try store.backup(label: "Imported save", source: selected.deletingLastPathComponent()) } }
    }

    func exportBackup(_ record: SaveBackup) {
        let store = saveStore
        exportSave { parent in try store.locked { try store.exportBackup(record, to: parent) } }
    }

    func exportCurrentGameSave() {
        let store = saveStore
        exportSave { parent in try store.locked { try store.exportCurrentGameSave(to: parent) } }
    }

    func exportQuickCheckpoint(_ checkpoint: QuickCheckpoint) {
        let store = saveStore
        exportSave { parent in try store.locked { try store.exportCheckpoint(checkpoint, to: parent) } }
    }

    private func exportSave(_ operation: @escaping (URL) throws -> URL) {
        guard !isRunning, !saveBusy else { return }
        let panel = NSOpenPanel(); panel.message = L10n.key("Choose an export folder")
        panel.canChooseDirectories = true; panel.canChooseFiles = false; panel.canCreateDirectories = true
        let source = userFolderURL
        let finish: (NSApplication.ModalResponse) -> Void = { [weak self] response in
            guard let self, response == .OK, let parent = panel.url else { return }
            guard self.userFolderURL == source else {
                self.saveStatus = L10n.key("Saves changed. Refresh and try again."); return
            }
            var exported: URL?
            self.runSaveOperation({ exported = try operation(parent) }) { [weak self] in
                if let exported { self?.saveStatus = L10n.format("Exported to %@", exported.path) }
            }
        }
        // A sheet is scoped to this scene and does not block the accessibility
        // action/main run loop while the user selects a destination.
        if let window = NSApp.windows.first(where: { $0.identifier?.rawValue == "saves" && $0.isVisible }) {
            panel.beginSheetModal(for: window, completionHandler: finish)
        } else { panel.begin(completionHandler: finish) }
    }
}

struct SaveManagerView: View {
    @EnvironmentObject var model: LauncherModel
    @State private var page = "current"

    private var canChange: Bool { !model.isRunning && !model.saveBusy && !model.saveRefreshing }
    private var current: [QuickCheckpoint] { model.quickCheckpoints.filter(\.current) }
    private var history: [QuickCheckpoint] { model.quickCheckpoints.filter { !$0.current } }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(L10n.key("Online and offline share the same game saves."))
            Picker(L10n.key("Save view"), selection: $page) {
                Text(L10n.key("Current saves")).tag("current")
                Text(L10n.key("Save history")).tag("history")
            }.pickerStyle(.segmented)
            if page == "current" {
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        GroupBox(L10n.key("Current game saves")) {
                            VStack(alignment: .leading, spacing: 10) {
                                if let save = model.currentGameSave {
                                    HStack {
                                        Text(L10n.format("Files: %@", String(save.files)))
                                        Text(size(save.bytes)).foregroundStyle(.secondary)
                                        Spacer()
                                        if let modified = save.modified {
                                            Text(L10n.key("Modified")).font(.caption).foregroundStyle(.secondary)
                                            date(modified)
                                        }
                                    }
                                    path(save.folder)
                                    HStack {
                                        Button(L10n.key("Open folder")) { model.openSaveFolder(save.folder) }
                                        Button(L10n.key("Back up")) { model.backupCurrentGameSave() }.disabled(!canChange)
                                        Button(L10n.key("Export…")) { model.exportCurrentGameSave() }.disabled(!canChange)
                                    }
                                } else {
                                    Text(L10n.key("No game saves yet.")).foregroundStyle(.secondary)
                                }
                            }.frame(maxWidth: .infinity, alignment: .leading).padding(8)
                        }
                        GroupBox(L10n.key("Current quick checkpoints")) {
                            VStack(alignment: .leading, spacing: 12) {
                                if current.isEmpty {
                                    Text(L10n.key("No active checkpoint.")).foregroundStyle(.secondary)
                                }
                                ForEach(current, id: \.identity) { checkpoint in checkpointRow(checkpoint) }
                                Button(L10n.key("Create checkpoint from current save")) { model.makeCheckpoint() }
                                    .disabled(!canChange || model.currentGameSave == nil)
                            }
                            .frame(maxWidth: .infinity, alignment: .leading).padding(8)
                        }
                        CloudSaveSettings().environmentObject(model)
                    }
                }
            } else {
                Text(L10n.key("Restoring backs up current saves and switches to offline play."))
                    .font(.caption).foregroundStyle(.secondary)
                HStack {
                    Text(L10n.key("Keep 3 old checkpoints, 3 pre-quickload copies and 5 automatic archives. Manual and kept backups stay."))
                        .font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button(L10n.key("Clean up now")) { model.cleanSaveHistory() }.disabled(!canChange)
                }
                List {
                    Section(L10n.key("Archive backups")) {
                        if model.saveBackups.isEmpty { Text(L10n.key("No archived backups.")).foregroundStyle(.secondary) }
                        ForEach(model.saveBackups) { backup in
                            VStack(alignment: .leading, spacing: 8) {
                                HStack {
                                    Text(L10n.key(backup.label)).fontWeight(.medium)
                                    Spacer()
                                    date(backup.created)
                                }
                                Text(backup.kinds.map { L10n.key($0 == "savedata" ? "Game saves" : "Quick checkpoints") }.joined(separator: " · ") + " · " + size(backup.bytes))
                                    .font(.caption).foregroundStyle(.secondary)
                                HStack {
                                    Button(L10n.key("Open folder")) { model.showBackup(backup) }
                                    Button(L10n.key("Export…")) { model.exportBackup(backup) }.disabled(!canChange)
                                    Menu(L10n.key("Restore…")) {
                                        ForEach(backup.kinds, id: \.self) { kind in
                                            Button(L10n.key(kind == "savedata" ? "Game saves" : "Quick checkpoints")) { model.restoreBackup(backup, kind: kind) }
                                        }
                                    }.disabled(!canChange).fixedSize()
                                    Spacer()
                                    if SaveStore.automatic(backup) {
                                        Toggle(L10n.key("Keep"), isOn: Binding(get: { backup.kept == true }, set: { model.keepBackup(backup, $0) }))
                                            .toggleStyle(.checkbox).disabled(!canChange)
                                    }
                                }
                            }.padding(.vertical, 6)
                        }
                    }
                    Section(L10n.key("Previous checkpoints and pre-restore backups")) {
                        if history.isEmpty { Text(L10n.key("No checkpoint history.")).foregroundStyle(.secondary) }
                        ForEach(history, id: \.identity) { checkpoint in checkpointRow(checkpoint).padding(.vertical, 6) }
                    }
                }.listStyle(.inset)
            }
            if model.isRunning { Text(L10n.key("Close the game to back up, export or restore saves.")).font(.caption).foregroundStyle(.secondary) }
            HStack {
                Button(L10n.key("Back up all saves")) { model.makeSaveBackup() }.disabled(!canChange || (model.currentGameSave == nil && model.quickCheckpoints.isEmpty))
                Button(L10n.key("Import…")) { model.importGameSave() }.disabled(!canChange)
                Button(L10n.key("Show backups")) { model.openSaveFolder(model.saveStore.directory) }
                Spacer()
                if model.saveBusy || model.saveRefreshing { ProgressView().controlSize(.small) }
                Button(L10n.key("Refresh")) { model.refreshSaveBackups() }.disabled(model.saveBusy || model.saveRefreshing)
            }
            Text(model.saveReadError.isEmpty ? model.saveStatus : model.saveReadError)
                .font(.caption).foregroundStyle(model.saveReadError.isEmpty ? Color.secondary : Color.red)
                .textSelection(.enabled).lineLimit(2)
            if !model.saveCleanupStatus.isEmpty { Text(model.saveCleanupStatus).font(.caption).foregroundStyle(.secondary).lineLimit(2) }
        }
        .padding(20).frame(minWidth: 740, minHeight: 540)
        .onAppear { model.refreshSaveBackups() }
        .onChange(of: model.userFolderURL) { model.refreshSaveBackups() }
        .onChange(of: model.isRunning) { model.refreshSaveBackups() }
        .navigationTitle(L10n.key("Save Manager"))
    }

    private func size(_ bytes: Int64) -> String { ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file) }

    private func date(_ value: Date) -> some View {
        Text(value, format: .dateTime.year().month().day().hour().minute().second()).font(.caption).foregroundStyle(.secondary)
    }

    private func path(_ folder: URL) -> some View {
        Text(folder.path).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
            .lineLimit(1).truncationMode(.middle).help(folder.path)
    }

    private func checkpointRow(_ checkpoint: QuickCheckpoint) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(checkpoint.title).fontWeight(.medium)
                Text(size(checkpoint.bytes)).foregroundStyle(.secondary)
                if checkpoint.beforeRestore { Text(L10n.key("Before quickload")).font(.caption).foregroundStyle(.secondary) }
                Spacer()
                date(checkpoint.created)
            }
            if let folder = try? model.saveStore.checkpointFolder(checkpoint) { path(folder) }
            HStack {
                Button(L10n.key("Open folder")) { model.showCheckpoint(checkpoint) }
                Button(L10n.key("Back up")) { model.backupQuickCheckpoint(checkpoint) }.disabled(!canChange)
                Button(L10n.key("Export…")) { model.exportQuickCheckpoint(checkpoint) }.disabled(!canChange)
                Button(L10n.key("Restore…")) { model.restoreQuickCheckpoint(checkpoint) }.disabled(!canChange)
                Spacer()
                Toggle(L10n.key("Keep"), isOn: Binding(get: { checkpoint.kept }, set: { model.keepCheckpoint(checkpoint, $0) }))
                    .toggleStyle(.checkbox).disabled(!canChange)
            }
        }
    }
}

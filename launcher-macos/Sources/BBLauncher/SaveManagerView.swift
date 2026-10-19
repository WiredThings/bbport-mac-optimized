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
        saveBusy = true; saveStatus = L10n.key("Working with saves…")
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let result = Result { try operation() }
            DispatchQueue.main.async {
                guard let self else { return }
                self.saveBusy = false
                switch result {
                case .success:
                    self.saveStatus = L10n.key("Saves verified")
                    self.refreshSaveBackups()
                    then?()
                case .failure(let error): self.saveStatus = error.localizedDescription
                }
            }
        }
    }

    func refreshSaveBackups() {
        do { saveBackups = try saveStore.backups(); quickCheckpoints = try saveStore.checkpoints() }
        catch { saveStatus = error.localizedDescription }
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

    func makeOfflineCheckpoint() {
        guard !prefs.online else { return }
        let store = saveStore
        runSaveOperation { try store.locked { try store.seedCheckpoint() } }
    }

    func restoreBackup(_ record: SaveBackup, kind: String) {
        guard !isRunning, !saveBusy, !prefs.online || kind != "quicksaves" else { return }
        let alert = NSAlert()
        alert.messageText = L10n.key("Restore this backup?")
        alert.informativeText = L10n.key("Current saves will be backed up before replacement. Restore whole saves, not individual character files.")
        alert.addButton(withTitle: L10n.key("Restore")); alert.addButton(withTitle: L10n.key("Cancel"))
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let store = saveStore, online = prefs.online
        runSaveOperation { try store.locked { try store.restore(record, kind: kind, online: online) } }
    }

    func restoreQuickCheckpoint(_ checkpoint: QuickCheckpoint) {
        guard !isRunning, !saveBusy, !prefs.online else { return }
        let alert = NSAlert(); alert.messageText = L10n.key("Restore this backup?")
        alert.informativeText = L10n.key("Current saves will be backed up before replacement. Restore whole saves, not individual character files.")
        alert.addButton(withTitle: L10n.key("Restore")); alert.addButton(withTitle: L10n.key("Cancel"))
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let store = saveStore
        runSaveOperation { try store.locked { try store.restoreCheckpoint(checkpoint) } }
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
        let panel = NSOpenPanel(); panel.message = L10n.key("Choose an export folder")
        panel.canChooseDirectories = true; panel.canChooseFiles = false; panel.canCreateDirectories = true
        guard panel.runModal() == .OK, let parent = panel.url else { return }
        let store = saveStore
        runSaveOperation {
            let payload = try store.verify(record)
            let target = parent.appendingPathComponent("Blood-Echoes-Save-" + record.id)
            try FileManager.default.copyItem(at: payload, to: target)
            guard try SaveStore.inventory(target).files == record.files else { throw OnlineError(message: L10n.key("Backup verification failed; current saves were not changed.")) }
        }
    }
}

struct SaveManagerView: View {
    @EnvironmentObject var model: LauncherModel
    @State private var kind = "savedata"
    @State private var selection: SaveBackup.ID?

    private var selected: SaveBackup? { model.saveBackups.first { $0.id == selection } }
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(L10n.key("Online and offline share the same game saves."))
            Text(model.userFolderURL.path).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
            HStack {
                Button(L10n.key("Back up current saves")) { model.makeSaveBackup() }
                Button(L10n.key("Import…")) { model.importGameSave() }
                Button(L10n.key("Show save folder")) { model.reveal(model.userFolderURL) }
                Spacer()
                if model.saveBusy { ProgressView().controlSize(.small) }
            }
            Picker(L10n.key("Save type"), selection: $kind) {
                Text(L10n.key("Game saves")).tag("savedata")
                Text(L10n.key("Quick checkpoints")).tag("quicksaves")
            }.pickerStyle(.segmented)
            if kind == "quicksaves" {
                Text(L10n.key("Checkpoints contain game saves plus restore metadata. Archived online checkpoints are kept here; F3 is offline only."))
                    .font(.caption).foregroundStyle(.secondary)
                Button(L10n.key("Create checkpoint from current save")) { model.makeOfflineCheckpoint() }.disabled(model.prefs.online)
            }
            if kind == "quicksaves", !model.quickCheckpoints.isEmpty {
                ScrollView {
                    VStack(alignment: .leading, spacing: 8) {
                        ForEach(model.quickCheckpoints) { checkpoint in
                            HStack {
                                Text(checkpoint.created, format: .dateTime.year().month().day().hour().minute().second())
                                if checkpoint.current { Text(L10n.key("Current checkpoint")).foregroundStyle(.secondary) }
                                Spacer()
                                Button(L10n.key("Restore…")) { model.restoreQuickCheckpoint(checkpoint) }.disabled(model.prefs.online)
                            }
                        }
                    }
                }.frame(maxHeight: 150)
            }
            List(model.saveBackups.filter { $0.kinds.contains(kind) }, selection: $selection) { backup in
                HStack {
                    VStack(alignment: .leading) {
                        Text(L10n.key(backup.label))
                        Text(backup.created, format: .dateTime.year().month().day().hour().minute().second())
                            .font(.caption).foregroundStyle(.secondary)
                    }
                    Spacer()
                    Text(ByteCountFormatter.string(fromByteCount: backup.bytes, countStyle: .file)).foregroundStyle(.secondary)
                }.padding(.vertical, 3)
            }
            HStack {
                Button(L10n.key("Restore…")) { if let selected { model.restoreBackup(selected, kind: kind) } }
                    .disabled(selected == nil || model.prefs.online && kind == "quicksaves")
                Button(L10n.key("Export…")) { if let selected { model.exportBackup(selected) } }.disabled(selected == nil)
                Button(L10n.key("Show backups")) { model.reveal(model.saveStore.directory) }
                Spacer()
                Button(L10n.key("Refresh")) { model.refreshSaveBackups() }
            }
            Text(model.saveStatus).font(.caption).textSelection(.enabled)
        }
        .padding(20).frame(minWidth: 700, minHeight: 500)
        .disabled(model.isRunning || model.saveBusy)
        .onAppear { model.refreshSaveBackups() }
        .onChange(of: kind) { _ in selection = nil }
        .navigationTitle(L10n.key("Save Manager"))
    }
}

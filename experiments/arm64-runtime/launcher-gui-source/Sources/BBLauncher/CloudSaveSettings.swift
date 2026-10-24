// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import SwiftUI

extension LauncherModel {
    var cloudSaveRoot: URL {
        // QA uses a fake home under its isolated data root, never real Drive.
        let qaHome = qaApplicationSupportURL(
            bundleIdentifier: Bundle.main.bundleIdentifier,
            previewPath: Bundle.main.object(forInfoDictionaryKey: "BBPreviewDataPath") as? String)
        return CloudSaves.automaticRoot(home: qaHome ?? FileManager.default.homeDirectoryForCurrentUser)
    }

    func setCloudSaves(_ enabled: Bool) {
        guard !isRunning, !saveBusy else { return }
        prefs.cloudSaves = enabled
        if enabled { synchronizeCloudSaves() }
        else { cloudSaveStatus = ""; cloudSaveConflicts = [] }
    }

    func synchronizeCloudSaves(choice: CloudSaveChoice? = nil, then: (() -> Void)? = nil) {
        guard prefs.cloudSaves else { then?(); return }
        let store = saveStore
        let cloud = CloudSaves(store: store, root: cloudSaveRoot)
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
                Text("iCloud Drive/Blood Echoes Saves").font(.caption).foregroundStyle(.secondary)
                    .textSelection(.enabled)
                Button(L10n.key("Sync now")) { model.synchronizeCloudSaves() }
                    .disabled(!available || !model.prefs.cloudSaves)
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

// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct MetalOptimizationSection: View {
    @EnvironmentObject var model: LauncherModel
    @State private var showAdopted = false
    @State private var showExperiments = true

    var body: some View {
        Section {
            Text(L10n.key("Applies on the next game launch. Resolution, effects and cache capacities are not changed by these defaults."))
                .font(.caption).foregroundStyle(.secondary)
            DisclosureGroup(L10n.key("Adopted optimizations (default on)"), isExpanded: $showAdopted) {
                Text(L10n.key("Some options were accepted as a bundle; individual FPS gains are not established."))
                    .font(.caption).foregroundStyle(.secondary)
                ForEach(MetalOptimizations.adopted) { option in row(option) }
            }
            DisclosureGroup(L10n.key("Experimental optimizations (default off)"), isExpanded: $showExperiments) {
                Text(L10n.key("Experimental does not mean faster. Read each result and cost before enabling an option."))
                    .font(.caption).foregroundStyle(.secondary)
                ForEach(MetalOptimizations.optionalExperiments) { option in row(option) }
            }
            Button(L10n.key("Restore recommended optimization defaults")) {
                model.prefs.metalOptimizations = MetalOptimizations.defaults
            }
        } header: {
            Text(L10n.key("Metal optimizations"))
        } footer: {
            Text(L10n.key("These controls target the bundled Metal engine and override matching developer environment entries. Quicksave, saves and game-quality settings are retained."))
                .foregroundStyle(.secondary)
        }
    }

    private func row(_ option: MetalOptimization) -> some View {
        let available = MetalOptimizations.dependenciesMet(option, prefs: model.prefs)
        return Toggle(isOn: Binding(
            get: { MetalOptimizations.enabled(option, prefs: model.prefs) },
            set: { model.prefs.metalOptimizations[option.id] = $0 }
        )) {
            HStack {
                Text(L10n.key(option.title))
                Spacer()
                Text(L10n.key(option.evidence.rawValue))
                    .font(.caption)
                    .foregroundStyle(option.evidence.experimental ? Color.orange : Color.secondary)
            }
            Text(L10n.key(option.note)).font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            if !available {
                Text(L10n.format("Requires: %@", option.requires.compactMap { id in
                    MetalOptimizations.all.first { $0.id == id }.map { L10n.key($0.title) }
                }.joined(separator: ", ")))
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .disabled(!available)
        .help(option.id + "\n" + L10n.key(option.note))
        .accessibilityIdentifier(option.id)
    }
}

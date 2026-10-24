// SPDX-License-Identifier: GPL-2.0-or-later
import AppKit
import SwiftUI

struct ShortcutKey: Equatable {
    let code: Int // SDL physical scancode, shared by native GPU and Rosetta input process.
    let label: String
    static let save = ShortcutKey(code: 59, label: "F2")
    static let load = ShortcutKey(code: 60, label: "F3")
    static let menu = ShortcutKey(code: 58, label: "F1")

    // macOS virtual key -> SDL scancode. Physical codes keep the captured key working across
    // keyboard layouts; printable labels come from the actual event, not a US-layout guess.
    static let macKeys: [UInt16: ShortcutKey] = {
        var keys: [UInt16: ShortcutKey] = [:]
        func add(_ mac: UInt16, _ code: Int, _ label: String) { keys[mac] = ShortcutKey(code: code, label: label) }
        let letters: [(UInt16, Int, String)] = [
            (0,4,"A"),(1,22,"S"),(2,7,"D"),(3,9,"F"),(4,11,"H"),(5,10,"G"),
            (6,29,"Z"),(7,27,"X"),(8,6,"C"),(9,25,"V"),(10,100,"§"),(11,5,"B"),
            (12,20,"Q"),(13,26,"W"),(14,8,"E"),(15,21,"R"),(16,28,"Y"),(17,23,"T"),
            (18,30,"1"),(19,31,"2"),(20,32,"3"),(21,33,"4"),(22,35,"6"),(23,34,"5"),
            (24,46,"="),(25,38,"9"),(26,36,"7"),(27,45,"-"),(28,37,"8"),(29,39,"0"),
            (30,48,"]"),(31,18,"O"),(32,24,"U"),(33,47,"["),(34,12,"I"),(35,19,"P"),
            (36,40,"Return"),(37,15,"L"),(38,13,"J"),(39,52,"'"),(40,14,"K"),(41,51,";"),
            (42,49,"\\"),(43,54,","),(44,56,"/"),(45,17,"N"),(46,16,"M"),(47,55,"."),
            (48,43,"Tab"),(49,44,"Space"),(50,53,"`"),(51,42,"Backspace"),(53,41,"Escape"),
            (65,99,"Keypad ."),(67,85,"Keypad *"),(69,87,"Keypad +"),(75,84,"Keypad /"),
            (76,88,"Keypad Enter"),(78,86,"Keypad -"),(81,103,"Keypad ="),
            (82,98,"Keypad 0"),(83,89,"Keypad 1"),(84,90,"Keypad 2"),(85,91,"Keypad 3"),
            (86,92,"Keypad 4"),(87,93,"Keypad 5"),(88,94,"Keypad 6"),(89,95,"Keypad 7"),
            (91,96,"Keypad 8"),(92,97,"Keypad 9"),
            (114,73,"Insert"),(115,74,"Home"),(116,75,"PageUp"),(117,76,"Delete"),
            (119,77,"End"),(121,78,"PageDown"),(123,80,"Left"),(124,79,"Right"),
            (125,81,"Down"),(126,82,"Up"),
        ]
        for (mac, code, label) in letters { add(mac, code, label) }
        let functions: [UInt16] = [122,120,99,118,96,97,98,100,101,109,103,111,105,107,113,106,64,79,80,90]
        for (index, mac) in functions.enumerated() { add(mac, index < 12 ? 58 + index : 104 + index - 12, "F\(index + 1)") }
        return keys
    }()

    static func from(event: NSEvent) -> ShortcutKey? {
        guard let key = macKeys[event.keyCode] else { return nil }
        if key.code < 40 || (45...56).contains(key.code) || key.code == 100,
           let text = event.charactersIgnoringModifiers, text.count == 1,
           text.unicodeScalars.allSatisfy({ !CharacterSet.controlCharacters.contains($0) }) {
            return ShortcutKey(code: key.code, label: text.uppercased())
        }
        return key
    }

    static func valid(_ code: Int) -> Bool { macKeys.values.contains { $0.code == code } }
    static func name(_ code: Int) -> String {
        if code == 225 { return "Left Shift" }
        return macKeys.values.first { $0.code == code }?.label ?? "Key \(code)"
    }
    static func code(named name: String) -> Int? {
        let name = name.trimmingCharacters(in: .whitespaces).lowercased()
        if name == "left shift" { return 225 }
        return macKeys.values.first { $0.label.lowercased() == name }?.code
    }
    var reservedAction: String? {
        switch code {
        case 41: return L10n.key("Close the menu / cancel text entry")
        case 66: return L10n.key("Start / stop input recording (developer shortcut)")
        default: return nil
        }
    }
}

enum GameShortcut: String, Identifiable {
    case save, load, menu
    var id: String { rawValue }
    var title: String {
        switch self {
        case .save: return L10n.key("Quicksave")
        case .load: return L10n.key("Quickload")
        case .menu: return L10n.key("Configuration menu")
        }
    }
}

struct KeyboardMap {
    struct Input: Identifiable {
        let id: String
        let title: String
        var keys: [Int]
    }
    let prefs: LauncherPrefs
    var inputs: [Input]

    init(ini: [String: String], prefs: LauncherPrefs) {
        self.prefs = prefs
        let defaults: [(String, String, [Int])] = [
            ("move_up",L10n.key("Move forward"),[26]),("move_down",L10n.key("Move backward"),[22]),
            ("move_left",L10n.key("Move left"),[4]),("move_right",L10n.key("Move right"),[7]),
            ("look_up",L10n.key("Camera up"),[82]),("look_down",L10n.key("Camera down"),[81]),
            ("look_left",L10n.key("Camera left"),[80]),("look_right",L10n.key("Camera right"),[79]),
            ("cross",L10n.key("Cross / interact"),[44]),("circle",L10n.key("Circle / dodge"),[225]),
            ("square",L10n.key("Square / use item"),[8]),("triangle",L10n.key("Triangle / heal"),[20]),
            ("l1",L10n.key("L1 / transform weapon"),[30]),("r1",L10n.key("R1 / attack"),[32]),
            ("l2",L10n.key("L2 / firearm"),[21]),("r2",L10n.key("R2 / strong attack"),[9]),
            ("l3","L3",[29]),("r3",L10n.key("R3 / lock on"),[6]),
            ("options",L10n.key("Options / game menu"),[40]),("touchpad",L10n.key("Touchpad left"),[43]),
            ("touchpad_right",L10n.key("Touchpad right"),[42]),
            ("up",L10n.key("D-pad up"),[12]),("down",L10n.key("D-pad down"),[14]),
            ("left",L10n.key("D-pad left"),[13]),("right",L10n.key("D-pad right"),[15]),
        ]
        inputs = defaults.map { id, title, keys in
            let configured = ini["key.\(id)"].map { $0.split(separator: ",").compactMap { ShortcutKey.code(named: String($0)) } }
            return Input(id: id, title: title, keys: configured ?? keys)
        }
    }

    func conflict(for key: ShortcutKey, assigning action: GameShortcut) -> String? {
        if let reserved = key.reservedAction { return L10n.format("%@ is already mapped to %@. Choose another key.", String(describing: key.label), String(describing: reserved)) }
        let assignments: [(GameShortcut, ShortcutKey)] = [(.menu, prefs.menuKey), (.save, prefs.quicksaveKey), (.load, prefs.quickloadKey)]
        if let (owner, _) = assignments.first(where: { $0.0 != action && $0.1.code == key.code }) {
            return L10n.format("%@ is already mapped to %@. Choose another key.", String(describing: key.label), String(describing: owner.title))
        }
        // Retain existing S/L choices; newly assigned shortcuts must not steal gameplay keys.
        if assignments.contains(where: { $0.0 == action && $0.1.code == key.code }) { return nil }
        if let input = inputs.first(where: { $0.keys.contains(key.code) }) {
            return L10n.format("%@ is already mapped to %@. Choose another key.", String(describing: key.label), String(describing: input.title))
        }
        return nil
    }


}

struct ShortcutRecorder: View {
    let action: GameShortcut
    let map: KeyboardMap
    let assign: (ShortcutKey) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var monitor: Any?
    @State private var message: String?

    var body: some View {
        VStack(spacing: 16) {
            Text(L10n.format("Remap %@", String(describing: action.title.lowercased()))).font(.title2.bold())
            Image(systemName: "keyboard").font(.system(size: 36)).foregroundStyle(.secondary)
            Text(L10n.key("Press the key you want to use.")).font(.headline)
            Text(L10n.key("Use a single key. Hold Fn if your keyboard needs it for F-keys. Escape cancels."))
                .foregroundStyle(.secondary).multilineTextAlignment(.center)
            if let message { Text(message).foregroundStyle(.orange).multilineTextAlignment(.center) }
            Button(L10n.key("Cancel")) { dismiss() }.keyboardShortcut(.cancelAction)
        }
        .padding(28)
        .frame(width: 390)
        .onAppear {
            monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
                if event.keyCode == 53 { dismiss(); return nil }
                guard !event.isARepeat else { return nil }
                let modifiers = event.modifierFlags.intersection([.command, .control, .option, .shift])
                guard modifiers.isEmpty else { message = L10n.key("Use a single key without Command, Control, Option or Shift."); return nil }
                guard let key = ShortcutKey.from(event: event) else { message = L10n.key("That key is unavailable. Try a letter, number or function key."); return nil }
                if let conflict = map.conflict(for: key, assigning: action) { message = conflict; return nil }
                assign(key)
                dismiss()
                return nil
            }
        }
        .onDisappear { if let monitor { NSEvent.removeMonitor(monitor) }; monitor = nil }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didResignActiveNotification)) { _ in dismiss() }
    }
}

struct KeyLegend: View {
    @EnvironmentObject var model: LauncherModel
    @State private var recording: GameShortcut?

    var body: some View {
        Section {
            shortcut(.menu, key: model.prefs.menuKey, active: true)
            shortcut(.save, key: model.prefs.quicksaveKey, active: model.prefs.quicksave)
            shortcut(.load, key: model.prefs.quickloadKey, active: model.prefs.quicksave)
        } header: {
            Text(L10n.key("In-game shortcuts"))
        } footer: {
            Text(L10n.key("Click a key to remap it. Changes apply next time you press Play."))
                .foregroundStyle(.secondary)
        }
        .sheet(item: $recording) { action in
            ShortcutRecorder(action: action, map: model.keyboardMap) { key in
                switch action {
                case .menu: model.prefs.menuKey = key
                case .save: model.prefs.quicksaveKey = key
                case .load: model.prefs.quickloadKey = key
                }
            }
        }
    }

    private func shortcut(_ action: GameShortcut, key: ShortcutKey, active: Bool) -> some View {
        LabeledContent {
            HStack(spacing: 8) {
                if !active { Text(L10n.key("Disabled")).font(.caption).foregroundStyle(.secondary) }
                Button(L10n.format("%@  ·  Remap", String(describing: key.label))) { recording = action }
                    .accessibilityLabel(L10n.format("Remap %@", String(describing: action.title)))
                    .accessibilityValue(key.label)
            }
        } label: {
            Text(action.title)
        }
    }
}

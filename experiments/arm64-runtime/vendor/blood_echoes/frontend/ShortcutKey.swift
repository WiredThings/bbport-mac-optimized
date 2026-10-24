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


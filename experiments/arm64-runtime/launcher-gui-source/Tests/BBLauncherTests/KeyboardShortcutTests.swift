import XCTest
@testable import BBLauncher

final class KeyboardShortcutTests: XCTestCase {
    func testFreshDefaultsAreF1F2F3() {
        let prefs = LauncherPrefs()
        let env = OptimizedProfile(prefs: prefs, game: GameSettings()).environment
        XCTAssertEqual(prefs.menuKey.label, "F1")
        XCTAssertEqual(prefs.quicksaveKey.label, "F2")
        XCTAssertEqual(prefs.quickloadKey.label, "F3")
        XCTAssertEqual(env["BB_MENU_SCANCODE"], "58")
        XCTAssertEqual(env["BB_QUICKSAVE_SCANCODE"], "59")
        XCTAssertEqual(env["BB_QUICKLOAD_SCANCODE"], "60")
    }

    func testReservedAndGameplayKeysExplainTheirConflict() {
        let map = KeyboardMap(ini: [:], prefs: LauncherPrefs())
        XCTAssertTrue(map.conflict(for: ShortcutKey(code: 58, label: "F1"), assigning: .save)!.contains(L10n.key("Configuration menu")))
        XCTAssertTrue(map.conflict(for: ShortcutKey(code: 26, label: "W"), assigning: .save)!.contains(L10n.key("Move forward")))
        XCTAssertTrue(map.conflict(for: .load, assigning: .save)!.contains(L10n.key("Quickload")))
        XCTAssertNil(map.conflict(for: ShortcutKey(code: 59, label: "F2"), assigning: .save))
        XCTAssertNil(map.conflict(for: .save, assigning: .save)) // retain the current assignment
    }

    func testCustomGameBindingsAreChecked() {
        let map = KeyboardMap(ini: ["key.cross": "F4", "key.move_up": ""], prefs: LauncherPrefs())
        XCTAssertTrue(map.conflict(for: ShortcutKey(code: 61, label: "F4"), assigning: .load)!.contains(L10n.key("Cross / interact")))
        XCTAssertNil(map.conflict(for: ShortcutKey(code: 26, label: "W"), assigning: .load))
    }

    func testMovingConfigurationFreesItsOldKeyAndReservesItsNewKey() {
        var prefs = LauncherPrefs()
        prefs.menuKey = ShortcutKey(code: 61, label: "F4")
        var map = KeyboardMap(ini: [:], prefs: prefs)
        XCTAssertNil(map.conflict(for: .menu, assigning: .save)) // F1 is free
        XCTAssertTrue(map.conflict(for: prefs.menuKey, assigning: .save)!.contains(L10n.key("Configuration menu")))
        prefs.quicksaveKey = .menu
        map = KeyboardMap(ini: [:], prefs: prefs)
        XCTAssertTrue(map.conflict(for: .menu, assigning: .menu)!.contains(L10n.key("Quicksave")))
        XCTAssertTrue(map.conflict(for: .load, assigning: .menu)!.contains(L10n.key("Quickload")))
        XCTAssertTrue(map.conflict(for: ShortcutKey(code: 66, label: "F9"), assigning: .menu)!.contains(L10n.key("Start / stop input recording (developer shortcut)")))
        XCTAssertNil(map.conflict(for: ShortcutKey(code: 73, label: "Insert"), assigning: .menu))
    }

    func testKeysPersistAndReachNativeRuntime() throws {
        let suite = "bbport.keys.tests.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        var prefs = LauncherPrefs()
        prefs.quicksaveKey = ShortcutKey(code: 59, label: "F2")
        prefs.quickloadKey = ShortcutKey(code: 60, label: "F3")
        prefs.menuKey = ShortcutKey(code: 61, label: "F4")
        prefs.save(to: defaults)
        let saved = LauncherPrefs.load(from: defaults)
        XCTAssertEqual(saved, prefs)
        let env = OptimizedProfile(prefs: saved, game: GameSettings()).environment
        XCTAssertEqual(env["BB_QUICKSAVE_SCANCODE"], "59")
        XCTAssertEqual(env["BB_QUICKLOAD_SCANCODE"], "60")
        XCTAssertEqual(env["BB_MENU_SCANCODE"], "61")
        defaults.set(["code": 61, "label": "F4"], forKey: "quicksaveKey")
        XCTAssertEqual(LauncherPrefs.load(from: defaults).quicksaveKey, .save)
    }

    func testMacPhysicalFunctionKeysMatchSDL() {
        XCTAssertEqual(ShortcutKey.macKeys[122]?.code, 58) // F1
        XCTAssertEqual(ShortcutKey.macKeys[120]?.code, 59) // F2
        XCTAssertEqual(ShortcutKey.macKeys[99]?.code, 60) // F3
        XCTAssertEqual(ShortcutKey.macKeys[105]?.code, 104) // F13
        XCTAssertEqual(ShortcutKey.macKeys[1], ShortcutKey(code: 22, label: "S"))
        XCTAssertEqual(ShortcutKey.macKeys[37], ShortcutKey(code: 15, label: "L"))
    }
}

import XCTest
@testable import BBLauncher

final class OnlineServiceTests: XCTestCase {
    func testTHDRoutesPreserveOriginalGamePorts() {
        XCTAssertEqual(HuntersDream.environment["BB_THD_HTTP_ORIGIN"], "https://thehuntersdream.com:20443")
        XCTAssertEqual(HuntersDream.environment["BB_THD_HTTP_ROUTE"], "https://thehuntersdream.com")
        XCTAssertEqual(HuntersDream.environment["BB_THD_SERVER"], "https://thehuntersdream.com:18671")
    }
    func testExistingTHDKeychainIdentityIsPreserved() {
        XCTAssertEqual(OnlineAccountStore(bundleIdentifier: "io.github.bbport.optimized").service,
                       "io.github.bbport.optimized.huntersdream")
    }
    func testRetiredCustomServerPreferenceIsIgnored() throws {
        let suite = "bbport.retired-server.tests.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let baseline = LauncherPrefs.load(from: defaults)
        defaults.set(Data(#"{"custom":true,"auth":"https://retired.example.test"}"#.utf8), forKey: "onlineServer")
        XCTAssertEqual(LauncherPrefs.load(from: defaults), baseline)
        baseline.save(to: defaults)
        XCTAssertEqual(LauncherPrefs.load(from: defaults), baseline)
    }
}

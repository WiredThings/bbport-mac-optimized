import XCTest
@testable import BBLauncher

final class OnlineServerTests: XCTestCase {
    func testCustomServersKeepAccountsSeparateAndForwardAllEndpoints() throws {
        var server = OnlineServer(); server.custom = true
        server.auth = "https://account.example.test"; server.game = "https://game.example.test:20443"
        server.session = "https://session.example.test:18671"; server.stun = "stun.example.test:3478"
        let validated = try server.validated()
        XCTAssertEqual(validated.environment["BB_THD_HTTP_ORIGIN"], server.game)
        XCTAssertEqual(validated.environment["BB_THD_STUN"], server.stun)
        XCTAssertNotEqual(OnlineAccountStore(server: server).service, OnlineAccountStore().service)
        XCTAssertNil(HuntersDream.trustedVerificationURL("https://thehuntersdream.com/link", base: URL(string: server.auth)!))
        XCTAssertNotNil(HuntersDream.trustedVerificationURL("https://account.example.test/link", base: URL(string: server.auth)!))
        server.session = "https://other.example.test"
        XCTAssertNotEqual(validated.accountSuffix, server.accountSuffix)
    }

    func testBuiltinLegacyPortsAndCustomImplicitHTTPSPortStayDistinct() throws {
        let preset = OnlineServer.thd.environment
        XCTAssertEqual(preset["BB_THD_HTTP_ORIGIN"], "https://thehuntersdream.com:20443")
        XCTAssertEqual(preset["BB_THD_HTTP_ROUTE"], "https://thehuntersdream.com")
        var custom = OnlineServer(); custom.custom = true; custom.game = "https://game.example.test"
        XCTAssertEqual(try custom.validated().environment["BB_THD_HTTP_ROUTE"], "https://game.example.test:443")
    }

    func testMalformedEndpointsAndPortsAreRejected() {
        for value in ["http://example.test", "https://user:password@example.test", "https://example.test/path", "https://example.test?q=secret", "https://example.test:0"] {
            var server = OnlineServer(); server.custom = true; server.game = value
            XCTAssertThrowsError(try server.validated())
        }
        var server = OnlineServer(); server.custom = true; server.stun = "example.test:99999"
        XCTAssertThrowsError(try server.validated())
    }
}

import XCTest
@testable import BBLauncher

final class AppBuildTests: XCTestCase {
    let commit = "72f014b38c04daf9a1df1045008f57389d762fca"
    func testAboutUsesSourceCommitInsteadOfNumericBundleBuild() {
        let build = AppBuild(info: ["CFBundleShortVersionString": "0.2.40", "CFBundleVersion": "0240", "BBSourceCommit": commit])
        XCTAssertEqual(build.displayVersion, "0.2.40 (72f014b)")
        XCTAssertEqual(build.aboutOptions[.version] as? String, "72f014b")
        XCTAssertEqual(build.aboutOptions[.applicationVersion] as? String, "0.2.40")
    }
    func testGitlessPackageManifestSuppliesProvenance() throws {
        let data = try JSONSerialization.data(withJSONObject: ["source_commit": commit])
        XCTAssertEqual(AppBuild(info: ["CFBundleShortVersionString": "0.2.40"], manifest: data).revision, commit)
    }
    func testLegacyManifestChoosesProjectRatherThanDependency() throws {
        let data = try JSONSerialization.data(withJSONObject: [["component": "metal", "revision": String(repeating: "a", count: 40)], ["component": "blood-echoes", "revision": commit]])
        XCTAssertEqual(AppBuild(info: [:], manifest: data).revision, commit)
    }
    func testUnknownRevisionNeverMasqueradesAsBuildNumber() {
        let build = AppBuild(info: ["CFBundleShortVersionString": "0.2.40", "CFBundleVersion": "0240", "BBSourceCommit": "unknown"])
        XCTAssertNil(build.revision)
        XCTAssertEqual(build.displayVersion, "0.2.40")
        XCTAssertEqual(build.aboutOptions[.version] as? String, "")
    }
}

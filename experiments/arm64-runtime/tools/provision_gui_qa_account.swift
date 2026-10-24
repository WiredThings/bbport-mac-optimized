// Private test fixture provisioning only. The original GUI Keychain/account code is unchanged.
import Foundation
import Security
import Darwin
func stop(_ status: Int32) -> Never { print("QA credential fixture refused status=\(status)"); exit(1) }
guard CommandLine.arguments.count == 2, isatty(STDIN_FILENO) == 0 else { stop(-1) }
let raw = URL(fileURLWithPath: CommandLine.arguments[1]).standardizedFileURL
let app = raw.resolvingSymlinksInPath()
let root = "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/apps/"
guard app.path == raw.path, app.path.hasPrefix(root), app.pathExtension == "app",
      let info = NSDictionary(contentsOf: app.appendingPathComponent("Contents/Info.plist")),
      let identifier = info["CFBundleIdentifier"] as? String,
      identifier.hasPrefix("io.github.bbport.optimized.options-qa.arm64.gui.") else { stop(-2) }
var code: SecStaticCode?
let created = SecStaticCodeCreateWithPath(app as CFURL, [], &code)
guard created == errSecSuccess, let code,
      SecStaticCodeCheckValidity(code, [], nil) == errSecSuccess else { stop(-3) }
let data = FileHandle.standardInput.readDataToEndOfFile()
guard data.count <= 32 * 1024,
      let account = try? JSONSerialization.jsonObject(with: data) as? [String: String],
      Set(account.keys) == Set(["name", "token"]),
      let name = account["name"], !name.isEmpty, name.utf8.count <= 256,
      let token = account["token"], !token.isEmpty, token.utf8.count <= 16384 else { stop(-4) }
var trusted: SecTrustedApplication?
let trustedStatus = SecTrustedApplicationCreateFromPath(app.path, &trusted)
guard trustedStatus == errSecSuccess, let trusted else { stop(trustedStatus) }
var access: SecAccess?
let accessStatus = SecAccessCreate("Blood Echoes isolated GUI QA" as CFString, [trusted] as CFArray, &access)
guard accessStatus == errSecSuccess, let access else { stop(accessStatus) }
// Add only. Existing QA items and the formal account are never changed or enumerated.
let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
    kSecAttrService as String: identifier + ".huntersdream",
    kSecAttrAccount as String: "device", kSecAttrAccess as String: access,
    kSecValueData as String: data]
let status = SecItemAdd(query as CFDictionary, nil)
guard status == errSecSuccess else { stop(status) }
print("QA credential fixture installed; access limited to the exact signed QA application; values not logged")

// Read only the isolated QA item. UI requires explicit --authorize-private-qa.
// Never enumerate items or read/write the formal application account.
import Foundation
import Security
import LocalAuthentication
let arguments = Set(CommandLine.arguments.dropFirst())
guard arguments.isSubset(of: ["--authorize-private-qa", "--export-to-parent-pipe"]) else { exit(2) }
let authorize = arguments.contains("--authorize-private-qa")
let context = LAContext()
context.interactionNotAllowed = !authorize
SecKeychainSetUserInteractionAllowed(authorize)
let query: [String: Any] = [
    kSecClass as String: kSecClassGenericPassword,
    kSecAttrService as String: "io.github.bbport.optimized.options-qa.online.huntersdream",
    kSecAttrAccount as String: "device",
    kSecMatchLimit as String: kSecMatchLimitOne,
    kSecReturnData as String: true,
    kSecUseAuthenticationContext as String: context
]
var value: CFTypeRef?
let status = SecItemCopyMatching(query as CFDictionary, &value)
if status != errSecSuccess {
    print("{\"available\":false,\"status\":\(status),\"prompt_allowed\":\(authorize)}")
    exit(0)
}
guard let data = value as? Data,
      let account = try? JSONSerialization.jsonObject(with: data) as? [String: String],
      let token = account["token"], !token.isEmpty,
      let name = account["name"], !name.isEmpty else {
    print("{\"available\":false,\"invalid_account\":true}")
    exit(0)
}
// Secret output is an explicit separate mode for a parent runner's captured pipe.
// Normal probes never print the account name or token.
if CommandLine.arguments.dropFirst().contains("--export-to-parent-pipe") {
    guard isatty(STDOUT_FILENO) == 0 else { exit(2) }
    FileHandle.standardOutput.write(data)
} else {
    print("{\"available\":true,\"valid_account_shape\":true,\"prompt_allowed\":\(authorize)}")
}

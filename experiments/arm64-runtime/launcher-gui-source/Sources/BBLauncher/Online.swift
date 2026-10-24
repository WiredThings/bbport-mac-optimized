// SPDX-License-Identifier: GPL-2.0-or-later
// The Hunter's Dream device flow, as used by bbhost/bbport. Tokens stay in Keychain.
import AppKit
import Foundation
import Security

struct OnlineAccount: Codable, Equatable {
    let name: String
    let token: String
}

struct OnlineError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

struct OnlineAccountStore {
    let service: String
    init(bundleIdentifier: String = Bundle.main.bundleIdentifier ?? "io.github.bbport.optimized") {
        service = bundleIdentifier + ".huntersdream"
    }
    private var query: [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: service, kSecAttrAccount as String: "device"]
    }
    func load() -> OnlineAccount? {
        var q = query
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var value: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &value) == errSecSuccess,
              let data = value as? Data else { return nil }
        return try? JSONDecoder().decode(OnlineAccount.self, from: data)
    }
    func save(_ account: OnlineAccount) throws {
        let data = try JSONEncoder().encode(account)
        let status = SecItemUpdate(query as CFDictionary, [kSecValueData as String: data] as CFDictionary)
        if status == errSecItemNotFound {
            var q = query
            q[kSecValueData as String] = data
            q[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            let added = SecItemAdd(q as CFDictionary, nil)
            guard added == errSecSuccess else { throw OnlineError(message: L10n.key("Could not save sign-in in Keychain.")) }
        } else if status != errSecSuccess {
            throw OnlineError(message: L10n.key("Could not save sign-in in Keychain."))
        }
    }
    func remove() throws {
        let status = SecItemDelete(query as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw OnlineError(message: L10n.key("Could not remove sign-in from Keychain."))
        }
    }
}

enum HuntersDream {
    static let base = URL(string: "https://thehuntersdream.com")!
    static let game = URL(string: "https://thehuntersdream.com:20443")!
    // A portless host override preserves the game's original PS4 service ports.
    static let environment = [
        "BB_THD_AUTH": base.absoluteString,
        "BB_THD_HTTP_ORIGIN": game.absoluteString,
        "BB_THD_HTTP_ROUTE": base.absoluteString,
        "BB_THD_SERVER": "https://thehuntersdream.com:18671",
        "BB_THD_STUN": "", "BB_THD_RULESET": "vanilla", "BB_P2P_PORT": "3658"
    ]
    static func call(_ path: String, body: [String: String], base: URL = base) async throws -> [String: Any] {
        var request = URLRequest(url: base.appendingPathComponent(path))
        request.httpMethod = "POST"
        request.timeoutInterval = 15
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONSerialization.data(withJSONObject: body)
        let (data, _) = try await URLSession.shared.data(for: request)
        guard data.count <= 1024 * 1024,
              let reply = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let kind = reply["ResKind"] as? Int else {
            throw OnlineError(message: L10n.key("Unexpected reply from the account service."))
        }
        guard kind == 0 else {
            throw OnlineError(message: reply["Message"] as? String ?? L10n.key("Sign-in was refused."))
        }
        return reply
    }
    static func trustedVerificationURL(_ text: String?, base: URL = base) -> URL? {
        guard let url = URL(string: text ?? base.appendingPathComponent("link").absoluteString),
              url.scheme == "https", url.host?.lowercased() == base.host?.lowercased(),
              url.user == nil, url.password == nil, (url.port ?? 443) == (base.port ?? 443) else { return nil }
        return url
    }
}

@MainActor
final class OnlineController: ObservableObject {
    @Published private(set) var account: OnlineAccount?
    @Published private(set) var busy = false
    @Published private(set) var code = ""
    @Published private(set) var status = ""
    private var task: Task<Void, Never>?
    private var operation = UUID()
    private var loaded = false
    private let store = OnlineAccountStore()

    func copyCode() {
        guard !code.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(code, forType: .string)
        status = L10n.key("Code copied")
    }

    func load() {
        guard !loaded else { return }
        loaded = true
        account = store.load()
    }
    func cancel() { operation = UUID(); task?.cancel(); task = nil; busy = false; code = "" }
    func signOut() {
        cancel()
        do { try store.remove(); account = nil; status = "" }
        catch { status = error.localizedDescription }
    }
    func signIn() {
        guard !busy else { return }
        load()
        busy = true; status = ""; code = ""
        let current = UUID(); operation = current
        task = Task {
            defer { if operation == current { busy = false; code = "" } }
            do {
                let base = HuntersDream.base
                let start = try await HuntersDream.call("auth/device/start", body: ["DeviceName": "Blood Echoes on macOS"], base: base)
                guard let device = start["DeviceCode"] as? String, !device.isEmpty,
                      let userCode = start["UserCode"] as? String, !userCode.isEmpty,
                      let page = HuntersDream.trustedVerificationURL(start["VerificationUri"] as? String, base: base) else {
                    throw OnlineError(message: L10n.key("Unexpected reply from the account service."))
                }
                try Task.checkCancellation()
                code = userCode
                NSWorkspace.shared.open(page)
                var interval = min(max(start["Interval"] as? Int ?? 5, 1), 30)
                let deadline = Date().addingTimeInterval(Double(min(start["ExpiresIn"] as? Int ?? 600, 900)))
                while Date() < deadline {
                    try await Task.sleep(nanoseconds: UInt64(interval) * 1_000_000_000)
                    let poll = try await HuntersDream.call("auth/device/poll", body: ["DeviceCode": device], base: base)
                    try Task.checkCancellation()
                    switch poll["Status"] as? String {
                    case "approved":
                        guard let name = poll["OnlineId"] as? String, !name.isEmpty,
                              let token = poll["Token"] as? String, !token.isEmpty else {
                            throw OnlineError(message: L10n.key("Unexpected reply from the account service."))
                        }
                        let signedIn = OnlineAccount(name: name, token: token)
                        try store.save(signedIn)
                        account = signedIn
                        return
                    case "slow_down": interval = min(interval + 5, 60)
                    case "denied": throw OnlineError(message: L10n.key("Sign-in was refused."))
                    case "expired": throw OnlineError(message: L10n.key("The sign-in code expired. Try again."))
                    default: continue
                    }
                }
                throw OnlineError(message: L10n.key("The sign-in code expired. Try again."))
            } catch is CancellationError { }
            catch { if operation == current { status = error.localizedDescription } }
        }
    }
    func checkConnection() {
        guard !busy else { return }
        busy = true; status = ""
        let current = UUID(); operation = current
        task = Task {
            defer { if operation == current { busy = false } }
            do {
                var request = URLRequest(url: HuntersDream.game.appendingPathComponent("bb-eu/ss.info"))
                request.timeoutInterval = 10
                request.cachePolicy = .reloadIgnoringLocalCacheData
                let (data, reply) = try await URLSession.shared.data(for: request)
                guard (reply as? HTTPURLResponse)?.statusCode == 200, !data.isEmpty else {
                    throw OnlineError(message: L10n.key("The game service is not reachable."))
                }
                try Task.checkCancellation()
                status = L10n.key("Game service reachable. In-game login is checked after launch.")
            } catch is CancellationError { }
            catch { if operation == current { status = error.localizedDescription } }
        }
    }
}

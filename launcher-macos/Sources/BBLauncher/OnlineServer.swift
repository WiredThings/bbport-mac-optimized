// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import CryptoKit

struct OnlineServer: Codable, Equatable {
    var custom = false
    var auth = "https://thehuntersdream.com"
    var game = "https://thehuntersdream.com"
    var session = "https://thehuntersdream.com:18671"
    var stun = ""
    var ruleset = "vanilla"
    var p2pPort = 3658

    static let thd = OnlineServer()
    var effective: OnlineServer { custom ? self : .thd }
    var identity: String {
        let c = effective
        return [c.auth, c.game, c.session, c.ruleset].map { $0.trimmingCharacters(in: .whitespacesAndNewlines).trimmingCharacters(in: CharacterSet(charactersIn: "/")) }.joined(separator: "\n")
    }
    var accountSuffix: String {
        custom ? ".server-" + SHA256.hash(data: Data(identity.utf8)).prefix(12).map { String(format: "%02x", $0) }.joined() : ".huntersdream"
    }
    static func origin(_ text: String) throws -> URL {
        guard let url = URL(string: text), url.scheme == "https", let host = url.host, !host.isEmpty,
              url.user == nil, url.password == nil, url.query == nil, url.fragment == nil,
              url.path.isEmpty || url.path == "/", url.port == nil || (1...65535).contains(url.port!) else {
            throw OnlineError(message: L10n.key("Use HTTPS service origins, without a path, password or query."))
        }
        return url
    }
    func validated() throws -> OnlineServer {
        var c = effective
        for key in [\OnlineServer.auth, \.game, \.session] {
            c[keyPath: key] = try Self.origin(c[keyPath: key].trimmingCharacters(in: .whitespacesAndNewlines)).absoluteString.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        }
        guard (1...65535).contains(c.p2pPort), !c.ruleset.isEmpty, c.ruleset.utf8.count <= 64,
              !c.ruleset.contains(where: { $0.isWhitespace || $0.isNewline }),
              c.stun.isEmpty || c.stun == "off" || c.stun.range(of: "^[A-Za-z0-9.-]+(:[0-9]{1,5})?$", options: .regularExpression) != nil else {
            throw OnlineError(message: L10n.key("Check the ruleset, STUN host and UDP port."))
        }
        if let port = c.stun.split(separator: ":").dropFirst().first, !(1...65535).contains(Int(port) ?? 0) {
            throw OnlineError(message: L10n.key("Check the ruleset, STUN host and UDP port."))
        }
        return c
    }
    var environment: [String: String] {
        let c = effective
        return ["BB_THD_AUTH": c.auth, "BB_THD_HTTP_ORIGIN": c.game, "BB_THD_SERVER": c.session,
                "BB_THD_STUN": c.stun, "BB_THD_RULESET": c.ruleset, "BB_P2P_PORT": String(c.p2pPort)]
    }
}

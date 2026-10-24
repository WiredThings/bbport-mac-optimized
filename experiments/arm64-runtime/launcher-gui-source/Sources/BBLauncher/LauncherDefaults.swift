// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
enum LauncherDefaults {
 static let current: UserDefaults = {
  guard let id=Bundle.main.bundleIdentifier, id.hasPrefix("io.github.bbport.optimized.options-qa.") else { return .standard }
  guard let path=Bundle.main.object(forInfoDictionaryKey:"BBPreviewPreferencesPath") as? String,
        path.hasPrefix("/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/"),
        path.hasSuffix(".plist"),
        URL(fileURLWithPath:path).standardizedFileURL.resolvingSymlinksInPath().path==path,
        let result=UserDefaults(suiteName:path) else { fatalError("QA preferences must use the explicit Shared plist") }
  return result
 }()
}

import Foundation
// The unchanged launcher policy decides the language and patch source. No UserDefaults access.
extension Bundle { static var module: Bundle { .main } }
struct Input: Decodable { let language: String; let communityChinese: Bool; let chineseSimplifiedFolder: String; let chineseTraditionalFolder: String }
@main struct Export {
 static func main() throws {
  let input=try JSONDecoder().decode(Input.self,from:Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1])))
  var prefs=LauncherPrefs();prefs.language=input.language;prefs.communityChinese=input.communityChinese;prefs.chineseSimplifiedFolder=input.chineseSimplifiedFolder;prefs.chineseTraditionalFolder=input.chineseTraditionalFolder
  let env=OptimizedProfile(prefs:prefs,game:GameSettings()).environment.filter { ["BB_LANGUAGE","BB_CHINESE_SOURCE","BB_CHINESE_LANGUAGE","BB_CHINESE_TRANSLATION_DIR"].contains($0.key) }
  FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:env,options:[.sortedKeys,.prettyPrinted]))
 }
}

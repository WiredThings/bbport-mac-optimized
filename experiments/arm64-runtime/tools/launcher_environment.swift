import Foundation
// Only the two fields consumed by the unchanged shared optimization policy.
struct LauncherPrefs {var renderer="metal";var metalOptimizations:[String:Bool]=[:]}
@main struct Export {
 static func main() throws {
  let raw=try Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1]))
  let prefs=LauncherPrefs(metalOptimizations:try JSONDecoder().decode([String:Bool].self,from:raw))
  let env=MetalOptimizations.environment(prefs:prefs,metalFXActive:false)
  let out=try JSONSerialization.data(withJSONObject:env,options:[.sortedKeys])
  FileHandle.standardOutput.write(out)
 }
}

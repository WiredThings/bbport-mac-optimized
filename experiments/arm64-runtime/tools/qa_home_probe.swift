import Foundation
let d=UserDefaults(suiteName:CommandLine.arguments[1])!
print(NSHomeDirectory())
print(d.string(forKey:"probe") ?? "missing")
if CommandLine.arguments.count>2 {d.set("Shared-only-updated",forKey:"probe");print(d.synchronize())}

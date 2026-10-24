import XCTest
@testable import BBLauncher
final class CPUComponentTests: XCTestCase {
 func testArchitectureHeaderAndMalformedCases() throws {
  let root=URL(fileURLWithPath:"/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/state/cpu-ui-unit-"+UUID().uuidString)
  try FileManager.default.createDirectory(at:root,withIntermediateDirectories:true);defer{try? FileManager.default.removeItem(at:root)}
  let file=root.appendingPathComponent("component")
  func write(_ type:UInt32,_ length:Int=32) throws {var data=Data(repeating:0,count:length);if length>=8{data.replaceSubrange(0..<8,with:[0xcf,0xfa,0xed,0xfe,UInt8(type&255),UInt8((type>>8)&255),UInt8((type>>16)&255),UInt8((type>>24)&255)])};try data.write(to:file)}
  try write(0x0100000c);XCTAssertTrue(cpuComponentIsNativeARM64(file))
  XCTAssertNil(nativeCPUComponentIssue(file,renderer:"metal",remoteMetal:true))
  XCTAssertNotNil(nativeCPUComponentIssue(file,renderer:"vulkan",remoteMetal:true))
  XCTAssertNotNil(nativeCPUComponentIssue(file,renderer:"metal",remoteMetal:false))

  try write(0x01000007);XCTAssertFalse(cpuComponentIsNativeARM64(file))
  XCTAssertNil(nativeCPUComponentIssue(file,renderer:"metal",remoteMetal:false))
  XCTAssertNil(nativeCPUComponentIssue(file,renderer:"vulkan",remoteMetal:false))

  for n in [0,4,8,31]{try write(0x0100000c,n);XCTAssertFalse(cpuComponentIsNativeARM64(file))}
  XCTAssertFalse(cpuComponentIsNativeARM64(root.appendingPathComponent("missing")))
 }
 func testNativeActualComponent() {XCTAssertTrue(cpuComponentIsNativeARM64(URL(fileURLWithPath:"/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/build/fex/Bin/bb-game-arm64")))}
 func testSharedQARootAndProductionRefusal(){
  let path="/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/state/gui-unit"
  XCTAssertEqual(qaApplicationSupportURL(bundleIdentifier:"io.github.bbport.optimized.options-qa.arm64",previewPath:path)?.path,path)
  XCTAssertNil(qaApplicationSupportURL(bundleIdentifier:"io.github.bbport.optimized",previewPath:path))
 }
}

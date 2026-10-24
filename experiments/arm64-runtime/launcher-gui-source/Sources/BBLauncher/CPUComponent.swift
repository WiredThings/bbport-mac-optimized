// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
// Architecture check only; dependency/ABI acceptance is performed separately.
func cpuComponentIsNativeARM64(_ url:URL)->Bool {
 guard let f=try? FileHandle(forReadingFrom:url) else{return false};defer{try? f.close()}
 guard let bytes=try? f.read(upToCount:32),bytes.count==32 else{return false}
 let b=[UInt8](bytes)
 func u32(_ a:Int)->UInt32{UInt32(b[a])|UInt32(b[a+1])<<8|UInt32(b[a+2])<<16|UInt32(b[a+3])<<24}
 return u32(0)==0xfeedfacf && u32(4)==0x0100000c
}

// Component capabilities only. Original preparation, saves, patches and network
// policy remain in the existing launcher. Reject unsupported combinations before
// prepareGameSaves can create a session, instead of failing inside the game.
func nativeCPUComponentIssue(_ component: URL, renderer: String, remoteMetal: Bool) -> String? {
 guard cpuComponentIsNativeARM64(component) else { return nil }
 if renderer != "metal" { return "The ARM64 CPU prototype currently requires the Metal backend." }
 if !remoteMetal { return "The ARM64 CPU prototype requires the ARM64 renderer host. Enable it in Metal optimizations." }
 return nil
}

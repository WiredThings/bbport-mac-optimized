// SPDX-License-Identifier: GPL-2.0-or-later
// Only masks/pads supplied artwork; never redraws the game symbol.
// Regenerate: swift tools/macos/make_app_icon.swift OUTPUT.iconset SOURCE.png
import AppKit

guard CommandLine.arguments.count == 3 else {
    fatalError("Usage: make_app_icon.swift OUTPUT.iconset SOURCE.png")
}
let destination = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
let sourceURL = URL(fileURLWithPath: CommandLine.arguments[2])
guard let artwork = NSImage(contentsOf: sourceURL), artwork.size.width > 0, artwork.size.height > 0 else {
    fatalError("Cannot load source artwork")
}
try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
func draw() {
    let bounds = NSRect(x: 96, y: 96, width: 832, height: 832)
    let face = NSBezierPath(roundedRect: bounds, xRadius: 184, yRadius: 184)
    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow(); shadow.shadowColor = NSColor.black.withAlphaComponent(0.3)
    shadow.shadowBlurRadius = 24; shadow.shadowOffset = NSSize(width: 0, height: -12); shadow.set()
    NSColor.black.setFill(); face.fill()
    NSGraphicsContext.restoreGraphicsState()
    NSGraphicsContext.saveGraphicsState(); face.addClip()
    NSColor.black.setFill(); bounds.fill()
    let scale = min(bounds.width / artwork.size.width, bounds.height / artwork.size.height)
    let size = NSSize(width: artwork.size.width * scale, height: artwork.size.height * scale)
    let target = NSRect(x: bounds.midX - size.width / 2, y: bounds.midY - size.height / 2,
                        width: size.width, height: size.height)
    NSGraphicsContext.current?.imageInterpolation = .high
    artwork.draw(in: target, from: .zero, operation: .sourceOver, fraction: 1)
    NSGraphicsContext.restoreGraphicsState()
}

for points in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let pixels = points * scale
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        NSGraphicsContext.saveGraphicsState()
        bitmap.bitmapData?.initialize(repeating: 0, count: bitmap.bytesPerRow * pixels)
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
        NSAffineTransform().withScale(CGFloat(pixels) / 1024)
        draw(); NSGraphicsContext.restoreGraphicsState()
        let name = "icon_\(points)x\(points)\(scale == 2 ? "@2x" : "").png"
        try bitmap.representation(using: .png, properties: [:])!.write(to: destination.appendingPathComponent(name))
    }
}

extension NSAffineTransform {
    func withScale(_ scale: CGFloat) { self.scale(by: scale); concat() }
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Original moon-and-blade vector artwork; no game imagery or downloaded assets.
// Regenerate: swift tools/macos/make_app_icon.swift /path/to/AppIcon.iconset
import AppKit

let destination = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
func color(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat, _ alpha: CGFloat = 1) -> NSColor {
    NSColor(srgbRed: red, green: green, blue: blue, alpha: alpha)
}
func draw() {
    let face = NSBezierPath(roundedRect: NSRect(x: 96, y: 96, width: 832, height: 832), xRadius: 184, yRadius: 184)
    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow(); shadow.shadowColor = color(0, 0, 0, 0.3)
    shadow.shadowBlurRadius = 24; shadow.shadowOffset = NSSize(width: 0, height: -12); shadow.set()
    color(0.07, 0.075, 0.095).setFill(); face.fill()
    NSGraphicsContext.restoreGraphicsState()
    NSGraphicsContext.saveGraphicsState(); face.addClip()
    NSGradient(colors: [color(0.045, 0.055, 0.075), color(0.13, 0.06, 0.085), color(0.38, 0.085, 0.105)])!
        .draw(in: face, angle: 75)
    let glow = NSBezierPath(ovalIn: NSRect(x: 152, y: 164, width: 720, height: 720))
    NSGradient(starting: color(0.68, 0.12, 0.14, 0.38), ending: color(0.2, 0.02, 0.04, 0))!
        .draw(in: glow, relativeCenterPosition: NSPoint(x: 0.15, y: 0.25))
    // Keep the crescent inside its outer circle, subtracting the offset inner circle.
    NSGraphicsContext.saveGraphicsState()
    let circle = NSBezierPath(ovalIn: NSRect(x: 283, y: 310, width: 458, height: 458)); circle.addClip()
    let moon = NSBezierPath(); moon.windingRule = .evenOdd
    moon.append(circle); moon.appendOval(in: NSRect(x: 394, y: 384, width: 458, height: 458))
    NSGradient(starting: color(0.97, 0.93, 0.85), ending: color(0.49, 0.54, 0.61))!.draw(in: moon, angle: 110)
    NSGraphicsContext.restoreGraphicsState()
    // A broad, legible blade and crossguard retain their shape at 16 pixels.
    let blade = NSBezierPath()
    blade.move(to: NSPoint(x: 523, y: 790)); blade.line(to: NSPoint(x: 551, y: 696))
    blade.line(to: NSPoint(x: 539, y: 356)); blade.line(to: NSPoint(x: 523, y: 308))
    blade.line(to: NSPoint(x: 507, y: 356)); blade.line(to: NSPoint(x: 495, y: 696)); blade.close()
    NSGradient(starting: color(0.43, 0.48, 0.56), ending: color(0.96, 0.96, 0.91))!.draw(in: blade, angle: 0)
    let guardPath = NSBezierPath(roundedRect: NSRect(x: 440, y: 342, width: 166, height: 25), xRadius: 10, yRadius: 10)
    color(0.8, 0.75, 0.66).setFill(); guardPath.fill()
    let grip = NSBezierPath(roundedRect: NSRect(x: 510, y: 259, width: 26, height: 86), xRadius: 9, yRadius: 9)
    color(0.55, 0.53, 0.52).setFill(); grip.fill()
    let pommel = NSBezierPath(ovalIn: NSRect(x: 501, y: 240, width: 44, height: 28)); pommel.fill()
    NSGraphicsContext.restoreGraphicsState()
    color(1, 1, 1, 0.11).setStroke(); face.lineWidth = 2; face.stroke()
}

for points in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let pixels = points * scale
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        NSGraphicsContext.saveGraphicsState()
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

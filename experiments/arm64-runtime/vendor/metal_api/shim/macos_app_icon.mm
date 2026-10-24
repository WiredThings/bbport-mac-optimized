// SPDX-License-Identifier: GPL-2.0-or-later
#import <AppKit/AppKit.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "macos_app_icon.h"

namespace BbPort {
void HideGameProcessDockIcon() {
    if (![NSThread isMainThread]) return;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

bool SetGameDockIcon() {
    static bool applied = false; // SDL calls this on its main thread, outside the render loop.
    @autoreleasepool {
        if (![NSThread isMainThread] || !NSApp) {
            std::fprintf(stderr, "Game Dock icon: Cocoa main-thread application unavailable\n");
            return false;
        }
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        if (applied) return true;
        const char* game = std::getenv("BB_GAME_DIR");
        if (!game || !*game) return false;
        NSString* directory = [NSString stringWithUTF8String:game];
        if (!directory) return false;
        NSString* path = [directory stringByAppendingPathComponent:@"sce_sys/icon0.png"];
        NSDictionary* attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
        if (!attributes || [attributes fileSize] > 16 * 1024 * 1024) {
            std::fprintf(stderr, "Game Dock icon: original game image missing or over 16 MiB\n");
            return false;
        }
        NSImage* artwork = [[NSImage alloc] initWithContentsOfFile:path];
        if (!artwork || artwork.size.width <= 0 || artwork.size.height <= 0 ||
            artwork.size.width > 4096 || artwork.size.height > 4096) {
            std::fprintf(stderr, "Game Dock icon: invalid image dimensions\n");
            return false;
        }
        NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
            pixelsWide:1024 pixelsHigh:1024 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES
            isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        if (!bitmap) return false;
        std::memset(bitmap.bitmapData, 0, bitmap.bytesPerRow * bitmap.pixelsHigh);
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
        NSRect bounds = NSMakeRect(96, 96, 832, 832);
        NSBezierPath* face = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:184 yRadius:184];
        [NSGraphicsContext saveGraphicsState];
        NSShadow* shadow = [NSShadow new];
        shadow.shadowColor = [NSColor.blackColor colorWithAlphaComponent:0.3];
        shadow.shadowBlurRadius = 24; shadow.shadowOffset = NSMakeSize(0, -12); [shadow set];
        [NSColor.blackColor setFill]; [face fill];
        [NSGraphicsContext restoreGraphicsState];
        [NSGraphicsContext saveGraphicsState]; [face addClip];
        [NSColor.blackColor setFill]; NSRectFill(bounds);
        CGFloat scale = MIN(bounds.size.width / artwork.size.width, bounds.size.height / artwork.size.height);
        NSSize size = NSMakeSize(artwork.size.width * scale, artwork.size.height * scale);
        NSRect target = NSMakeRect(NSMidX(bounds) - size.width / 2, NSMidY(bounds) - size.height / 2,
                                  size.width, size.height);
        NSGraphicsContext.currentContext.imageInterpolation = NSImageInterpolationHigh;
        [artwork drawInRect:target fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
        [NSGraphicsContext restoreGraphicsState]; [NSGraphicsContext restoreGraphicsState];
        // Keep full source pixels but avoid asking AppKit to snapshot a 1024-point,
        // 2048-pixel HDR Dock image on a Retina display.
        bitmap.size = NSMakeSize(256, 256);
        NSImage* icon = [[NSImage alloc] initWithSize:NSMakeSize(256, 256)];
        [icon addRepresentation:bitmap];
        NSApp.applicationIconImage = icon;
        applied = true;
        std::printf("Game Dock icon: pid=%d original=%s normalized=1024 inset=96 logical=256\n",
                    getpid(), path.UTF8String);
        return true;
    }
}
}

#include "app/system/SystemColors.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>

namespace SystemColors {

    State read() {
        State state;
        @autoreleasepool {
            NSColor* accent = [[NSColor controlAccentColor] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
            if (accent != nil) {
                auto byte = [](CGFloat value) {
                    return (uint32_t) std::clamp((int) std::lround(value * 255.0), 0, 255);
                };
                state.accentKnown = true;
                state.accent = 0xFF000000u | (byte(accent.redComponent) << 16) | (byte(accent.greenComponent) << 8) |
                        byte(accent.blueComponent);
            }
            NSAppearance* appearance = NSApp != nil ? NSApp.effectiveAppearance : nil;
            if (appearance != nil) {
                NSAppearanceName name =
                        [appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
                state.darkKnown = true;
                state.dark = [name isEqualToString:NSAppearanceNameDarkAqua];
            }
        }
        return state;
    }
}

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#include <string.h>
#include <stdbool.h>
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Font_cocoa (objc/font_cocoa.m)
 * LEVEL: L4 — Self-Management (OS CoreText/AppKit font shim)
 * ============================================================================
 * alpha RGBA8 through the OS text stack,
 *
 * STRUCT FIELDS: none — procedural (AppKit/CoreText font shim, no struct).
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - System_listInstalledFonts(families[][256], paths[][1024], max)
 *   - for(list)
 *   - strncpy(families[count], famC, 255)
 *   - CFRelease(url)
 *   - System_rasterColorGlyph(familyName, codepoint, pixelHeight, outRGBA, outW, outH, outXOff, outYOff, outAdvance)
 *   - CTFontGetAdvancesForGlyphs(owner, kCTFontOrientationDefault, &glyph, &adv, 1)
 *   - CGColorSpaceRelease(cs)
 *   - free(raw)
 *   - CGContextSetShouldAntialias(ctx, true)
 *   - CGContextSetTextPosition(ctx, 1.0, 1.0)
 *   - CTFontDrawGlyphs(owner, &glyph, &CGPointZero, 1, ctx)
 *   - CGContextRelease(ctx)
 *
 * Getters:
 *   - System_getFontPath(familyName)
 * ============================================================================
 */


// Lists installed font families with a resolvable source path.
// Fills up to `max` entries of families[][256] + paths[][1024]; returns count.
// Backs FontBake_bakeAllFonts / FontBake_refreshAllFonts on Apple (Font Book).
int System_listInstalledFonts(char families[][256], char paths[][1024], int max) {
    if (!families || !paths || max <= 0) return 0;
    @autoreleasepool {
        NSArray *list = [[NSFontManager sharedFontManager] availableFontFamilies];
        int count = 0;
        for (NSString *fam in list) {
            if (count >= max) break;
            // Resolve via a member's PostScript name: family names alone
            // often fail fontWithName:.
            NSString *probe = fam;
            NSArray *members = [[NSFontManager sharedFontManager] availableMembersOfFontFamily:fam];
            if (members && [members count] > 0) {
                NSArray *first = [members objectAtIndex:0];
                if ([first count] > 0 && [[first objectAtIndex:0] isKindOfClass:[NSString class]])
                    probe = [first objectAtIndex:0];
            }
            NSFont *font = [NSFont fontWithName:probe size:12.0];
            if (!font) font = [NSFont fontWithName:fam size:12.0];
            if (!font) continue;
            CTFontRef ctFont = (__bridge CTFontRef)font;
            CFURLRef url = (CFURLRef)CTFontCopyAttribute(ctFont, kCTFontURLAttribute);
            if (!url) continue;
            NSString *path = [(__bridge NSURL *)url path];
            const char *famC = [fam UTF8String];
            const char *pathC = [path UTF8String];
            if (famC && pathC && famC[0] && pathC[0]) {
                // Dedupe by family (members can repeat).
                bool seen = false;
                for (int i = 0; i < count; i++) {
                    if (strncmp(families[i], famC, 256) == 0) { seen = true; break; }
                }
                if (!seen) {
                    strncpy(families[count], famC, 255);
                    families[count][255] = '\0';
                    strncpy(paths[count], pathC, 1023);
                    paths[count][1023] = '\0';
                    count++;
                }
            }
            CFRelease(url);
        }
        return count;
    }
}

// Rasters one codepoint to straight-alpha RGBA8 through the OS text stack,
// with family -> Apple Color Emoji fallback (chat-bubble emoji). The bitmap
// is sized to the glyph's bounds at `pixelHeight` px; rows are visual-top-
// first; x/y offsets follow the SDF convention (pen-relative, y-down).
// Returns true on success (caller frees *outRGBA with free()).
bool System_rasterColorGlyph(const char* familyName, uint32_t codepoint,
                             int pixelHeight, uint8_t **outRGBA,
                             int *outW, int *outH,
                             float *outXOff, float *outYOff,
                             float *outAdvance) {
    if (!familyName || !outRGBA || !outW || !outH || pixelHeight <= 0)
        return false;
    *outRGBA = NULL; *outW = 0; *outH = 0;
    // No legitimate text needs surrogates or noncharacters (U+FDD0–FDEF,
    // U+nFFFE/F), and CoreText's cmap walk spins pathologically on some of
    // them — reject before touching the framework.
    if (codepoint < 0x20 || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
        (codepoint >= 0xFDD0 && codepoint <= 0xFDEF) ||
        ((codepoint & 0xFFFE) == 0xFFFE))
        return false;
    @autoreleasepool {
        NSString *fam = [NSString stringWithUTF8String:familyName];
        UniChar chars[2];
        int nchars = 0;
        if (codepoint < 0x10000) {
            chars[0] = (UniChar)codepoint; nchars = 1;
        } else {
            uint32_t v = codepoint - 0x10000;
            chars[0] = (UniChar)(0xD800 + (v >> 10));
            chars[1] = (UniChar)(0xDC00 + (v & 0x3FF));
            nchars = 2;
        }
        // Cascade: requested family first, then the color-emoji font.
        CTFontRef base = CTFontCreateWithName((__bridge CFStringRef)fam,
                                              (CGFloat)pixelHeight, NULL);
        CTFontRef emoji = CTFontCreateWithName(CFSTR("AppleColorEmoji"),
                                               (CGFloat)pixelHeight, NULL);
        CTFontRef candidates[3] = { base, emoji, NULL };
        CGGlyph glyph = 0;
        CTFontRef owner = NULL;
        for (int i = 0; i < 2; i++) {
            if (!candidates[i])
                continue;
            CGGlyph g = 0;
            if (CTFontGetGlyphsForCharacters(candidates[i], chars, &g, nchars) && g != 0) {
                glyph = g;
                owner = candidates[i];
                break;
            }
        }
        if (!owner) {
            if (base) CFRelease(base);
            if (emoji) CFRelease(emoji);
            return false;
        }
        CGSize adv = {0, 0};
        CTFontGetAdvancesForGlyphs(owner, kCTFontOrientationDefault, &glyph, &adv, 1);
        CGRect bounds = CTFontGetBoundingRectsForGlyphs(owner, kCTFontOrientationDefault,
                                                        &glyph, NULL, 1);
        int w = (int)ceil(CGRectGetWidth(bounds)) + 2;
        int h = (int)ceil(CGRectGetHeight(bounds)) + 2;
        if (w <= 2 || h <= 2 || w > 1024 || h > 1024) {
            if (base) CFRelease(base);
            if (emoji) CFRelease(emoji);
            return false;
        }
        size_t rowBytes = (size_t)w * 4;
        uint8_t *raw = (uint8_t*) calloc((size_t)h, rowBytes);
        if (!raw) {
            if (base) CFRelease(base);
            if (emoji) CFRelease(emoji);
            return false;
        }
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(raw, w, h, 8, rowBytes, cs,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(cs);
        if (!ctx) {
            free(raw);
            if (base) CFRelease(base);
            if (emoji) CFRelease(emoji);
            return false;
        }
        // Natural CG orientation (row 0 = bottom); flip on copy-out below.
        CGContextSetShouldAntialias(ctx, true);
        CGContextSetTextPosition(ctx, -bounds.origin.x + 1.0, -bounds.origin.y + 1.0);
        CTFontDrawGlyphs(owner, &glyph, &CGPointZero, 1, ctx);
        CGContextRelease(ctx);
        if (base) CFRelease(base);
        if (emoji) CFRelease(emoji);

        // Flip to visual-top-first + unpremultiply to straight alpha (the
        // atlas contract the color fragment branch expects).
        uint8_t *out = (uint8_t*) malloc((size_t)h * rowBytes);
        if (!out) {
            free(raw);
            return false;
        }
        for (int y = 0; y < h; y++) {
            uint8_t *src = raw + (size_t)(h - 1 - y) * rowBytes;
            uint8_t *dst = out + (size_t)y * rowBytes;
            for (int x = 0; x < w; x++) {
                uint8_t r = src[x*4+0], g = src[x*4+1], b = src[x*4+2], a = src[x*4+3];
                if (a == 0) {
                    dst[x*4+0] = dst[x*4+1] = dst[x*4+2] = dst[x*4+3] = 0;
                } else if (a == 255) {
                    dst[x*4+0] = r; dst[x*4+1] = g; dst[x*4+2] = b; dst[x*4+3] = 255;
                } else {
                    dst[x*4+0] = (uint8_t)((r * 255 + a/2) / a);
                    dst[x*4+1] = (uint8_t)((g * 255 + a/2) / a);
                    dst[x*4+2] = (uint8_t)((b * 255 + a/2) / a);
                    dst[x*4+3] = a;
                }
            }
        }
        free(raw);
        *outRGBA = out;
        *outW = w;
        *outH = h;
        if (outXOff) *outXOff = (float)(bounds.origin.x - 1.0);
        if (outYOff) *outYOff = (float)(-(bounds.origin.y + CGRectGetHeight(bounds)) - 1.0);
        if (outAdvance) *outAdvance = (float)adv.width;
        return true;
    }
}

// Finds the absolute file path to a system font by its PostScript or Family name.
// Returns a malloc'd string (must be freed by caller), or NULL if not found.
char* System_getFontPath(const char* familyName) {
    @autoreleasepool {
        NSString *name = [NSString stringWithUTF8String:familyName];
        
        // Try finding by exact name
        NSFont *font = [NSFont fontWithName:name size:12.0];
        
        if (!font) {
            // Try generic matching
            font = [NSFont systemFontOfSize:12.0];
        }
        
        if (!font) return NULL;
        
        CTFontRef ctFont = (__bridge CTFontRef)font;
        CFURLRef url = (CFURLRef)CTFontCopyAttribute(ctFont, kCTFontURLAttribute);
        if (url) {
            NSString *path = [(__bridge NSURL *)url path];
            char *result = strdup([path UTF8String]);
            CFRelease(url);
            return result;
        }
    }
    return NULL;
}

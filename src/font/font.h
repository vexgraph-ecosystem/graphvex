#ifndef FONT_FONT_H
#define FONT_FONT_H

#include <stdbool.h>
#include <stdint.h>

// font/font.h — the built-in bitmap font (the language's first text render).
//
// A minimal 8x8 ASCII bitmap font (glyphs for 0x20..0x7E, 8 bytes per glyph,
// bit 7 = leftmost pixel, row 0 = top) so the language can render TEXT — the
// characters of a Label — into an RGBA8 buffer with zero dependencies. Real
// typefaces / SDF text / ligatures are later features; this is the base that
// makes text visible. Line breaks are explicit '\n' or a wrap width; measure
// and draw share one line-breaking rule so a Label's AUTO size matches its paint.
//
// COLOR: passed packed 0xRRGGBBAA (the Strict 0xRRGGBBAA Color Law: alpha low
// byte), blended straight-alpha over the target.

// The glyph bitmap for a char (8 rows), or nullptr when out of range.
const uint8_t *Font_glyph(char c);

// Advance width in pixels (8 for a glyph, 8 for an unknown char).
int Font_advance(char c);

// Draw `text` into an RGBA8 buffer at (x, y) top-left, in packed 0xRRGGBBAA.
// Honors explicit '\n' line breaks; wrapWidth > 0 also wraps at that pixel
// width (<= 0 = no wrap). Bounded: pixels outside [0,w)x[0,h) are dropped.
// Returns the end x of the last line.
int Font_drawText(uint8_t *rgba, uint32_t width, uint32_t height,
                  const char *text, int x, int y, int wrapWidth, uint32_t color);

// Measure `text` with the same line-breaking rule as Font_drawText: explicit
// '\n' plus wrap at wrapWidth (> 0). Writes the widest line and the total
// height (lines x lineHeight). Returns the widest line advance.
int Font_measure(const char *text, int wrapWidth, int *outW, int *outH);

// Pixel height of a line of text.
int Font_lineHeight(void);

#endif // FONT_FONT_H

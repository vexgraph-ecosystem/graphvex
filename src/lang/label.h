#ifndef LANG_LABEL_H
#define LANG_LABEL_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/component.h"
#include "objects/reactive.h"

// lang/label.h — the element Label (Component + text + reactive bindings).
//
// A Label is a Component (identity + tree + graphics + children) plus a text
// string and a format. Label_setText runs the [] formatter: every [] slot
// consumes the next vararg, and a [%.2f] slot formats it. The Label STORES the
// format + the bound slots, so a slot bound to a Reactive RE-RENDERS itself when
// the reactive changes — no setText call. A plain (slotless) setText erases the
// bindings: the old reactive can no longer change the text.
//
// The type marker (LABEL_TYPE) lets the painter type-dispatch to draw the text.

typedef struct Label Label;

#define LABEL_MAX_TEXT   256
#define LABEL_MAX_SLOTS  16

// Slot kinds: a bare [] is a REACTIVE; [%d]/[%.2f]/[%s] are typed scalars.
#define LABEL_SLOT_REACTIVE 0u
#define LABEL_SLOT_INT      1u
#define LABEL_SLOT_FLOAT    2u
#define LABEL_SLOT_STRING   3u

// The Label type marker (the painter dispatches on it).
#define LABEL_TYPE 0x4C4142ULL   // "LAB"

// SLOT RECORD: one bound [] slot.
typedef struct LabelSlot {
    uint32_t   kind;      // LABEL_SLOT_*
    int64_t    i;
    double     f;
    const char *s;
    Reactive  *reactive;  // LABEL_SLOT_REACTIVE only
} LabelSlot;

typedef struct Label {
    Component component;                    // the element base
    char      format[LABEL_MAX_TEXT];       // the stored format
    char      text[LABEL_MAX_TEXT];         // the rendered text
    LabelSlot slots[LABEL_MAX_SLOTS];       // the bound slots (in order)
    uint32_t  slotCount;
    uint32_t  textColor;                    // packed 0xRRGGBBAA
    uint32_t  mnemonic;                     // the &accelerator key (0 = none)
    bool      ligatures;
    float     spacing;
} Label;

// --- Constructors (arity) ---
Label *Label_0(void);
Label *Label_1(const char *text);
Label *Label_2(const char *text, uint32_t color);

#define LABEL_CHOOSER(_0, _1, _2, NAME, ...) NAME
#define Label(...) LABEL_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Label_2, Label_1, Label_0 \
)(__VA_ARGS__)

Label *Label_zero(void);
void Label_free(Label *label);

// --- Core functions ---
// Set the text from a format with [] slots. A [] slot consumes a Reactive* (it
// re-renders on change); [%d]/[%.2f]/[%s] consume a typed scalar. A slotless
// format ERASES the previous bindings. \[ emits a literal '['.
void Label_setText(Label *label, const char *fmt, ...);

// Re-render from the stored format + slots (called by the reactive observers).
void Label_render(Label *label);

void Label_setSize(Label *label, float w, float h);
void Label_setLocation(Label *label, float x, float y);
void Label_setAnchor(Label *label, int anchor);
void Label_setPivot(Label *label, int pivot);
void Label_setOrigin(Label *label, int origin);
// --- Setters / Getters ---
void Label_setTextColor(Label *label, uint32_t color);
void Label_setMnemonic(Label *label, uint32_t key);
void Label_setLigatures(Label *label, bool ligatures);
void Label_setSpacing(Label *label, float spacing);
const char *Label_getText(const Label *label);
uint32_t Label_getTextColor(const Label *label);
uint32_t Label_getMnemonic(const Label *label);
bool Label_isValid(const Label *label);

// --- toString Law (bounded, cold-path) ---
void Label_toString(const Label *self, char *dest, size_t cap, bool *outTruncated);
void Label_toStringStruct(const Label *self, char *dest, size_t cap, bool *outTruncated);

#endif // LANG_LABEL_H

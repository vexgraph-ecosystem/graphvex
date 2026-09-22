#include "lang/label.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font/font.h"
#include "lang/size.h"
#include "lang/str.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Label
 * ============================================================================
 * The element Label — a Component plus a text string and a format. Label_setText
 * runs the [] formatter: a bare [] slot consumes a Reactive* and the Label reads
 * that reactive on every render (no setText call); a [%d]/[%.2f]/[%s] slot
 * consumes a typed scalar and formats it. The Label stores the format + the bound
 * slots, so a re-render walks the stored slots, not the varargs. A slotless
 * format erases the bindings — the old reactive can no longer change the text.
 *
 * The reactive binding is a PULL: Label_render drains each bound reactive on the
 * owner thread (coalesced fire for any external observers) and then reads the
 * live atomic value, so a writer on any thread only moves the value and the
 * owner renders it. No observer ever runs on a writer's thread.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Label (label/label.c)
 * LEVEL: L2 — Behavior (the element Label: Component + text + reactive slots)
 * ============================================================================
 * SUMMARY:
 *   Component + a format + bound slots + rendered text. setText parses the
 *   format, stores the slots, and renders; a render drains the bound reactives
 *   on the owner thread and reads their live atomic values.
 *
 * STRUCT FIELDS (Mirroring lang/label.h):
 * ----------------------------------------------------------------------------
 *   Component component;   // the element base
 *   char format[256];      // the stored format
 *   char text[256];        // the rendered text
 *   LabelSlot slots[16];   // the bound slots (in order)
 *   uint32_t slotCount;
 *   uint32_t textColor; uint32_t mnemonic; bool ligatures; float spacing;
 *   bool autoW, autoH;     // SIZE_AUTO dims: measure text on render
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   drainSlots(label)                : drain every bound reactive (owner fire)
 *   resolveAuto(label)               : measure text -> concrete AUTO dims
 *   buildText(label, fmt, args, collecting) : one format walk (collect or render)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Label_0/1/2 + the Label(...) chooser / Label_zero / Label_free
 *
 * Public Core Functions: (.h)
 *   - Label_setText(label, fmt, ...) / Label_render(label)
 *   - Label_setSize(label, w, h) (SIZE_AUTO arms measure-on-render per dim)
 *
 * Public Setters: (.h)
 *   - Label_setTextColor/Mnemonic/Ligatures/Spacing
 *
 * Public Getters: (.h)
 *   - Label_getText/TextColor/Mnemonic / Label_isAutoWidth/Height / Label_isValid
 *
 * Public toString: (.h)
 *   - Label_toString / Label_toStringStruct
 * ============================================================================
 */

// Drain every bound reactive on the owner thread: this consumes the pending
// writes (coalesced) and fires any EXTERNAL observers, then the render below
// reads the live atomic values. A writer on another thread never renders this
// label — the owner pulls.
static void drainSlots(Label *label) {
    for (uint32_t i = 0; i < (*label).slotCount; i++) {
        if ((*label).slots[i].kind == LABEL_SLOT_REACTIVE && (*label).slots[i].reactive != nullptr)
            Reactive_drain((*label).slots[i].reactive);
    }
}

// Forward: defined in the setters (needs labelPrimary); called by Label_render.
static void resolveAuto(Label *label);

// One walk of the format. collecting=true reads the varargs into the slots;
// collecting=false reads the stored slots and emits the text.
static void buildText(Label *label, const char *fmt, va_list *args, bool collecting) {
    Str out;
    Str_init(&out, (*label).text, sizeof((*label).text));
    uint32_t slot = 0;
    const char *p = fmt;
    while (*p != '\0') {
        if (*p == '\\' && *(p + 1) != '\0') {       // escape
            Str_putc(&out, *(p + 1));
            p += 2;
            continue;
        }
        if (*p == '[') {
            if (*(p + 1) == ']') {                   // bare [] -> a Reactive*
                if (collecting) {
                    Reactive *r = va_arg(*args, Reactive*);
                    if (slot < LABEL_MAX_SLOTS) {
                        (*label).slots[slot].kind = LABEL_SLOT_REACTIVE;
                        (*label).slots[slot].reactive = r;
                    }
                } else if (slot < LABEL_MAX_SLOTS) {
                    Reactive *r = (*label).slots[slot].reactive;
                    Str_printf(&out, "%llu", (unsigned long long) (r ? Reactive_get(r) : 0));
                }
                slot++;
                p += 2;
                continue;
            }
            if (*(p + 1) == '%') {                   // [%spec] -> a typed scalar
                const char *close = strchr(p + 2, ']');
                if (close != nullptr) {
                    char spec[32];
                    size_t n = (size_t) (close - (p + 1));
                    if (n >= sizeof(spec))
                        n = sizeof(spec) - 1u;
                    memcpy(spec, p + 1, n);
                    spec[n] = '\0';
                    char conv = spec[n - 1u];        // the conversion char
                    uint32_t kind = (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X')
                                        ? LABEL_SLOT_INT
                                    : (conv == 's') ? LABEL_SLOT_STRING
                                                    : LABEL_SLOT_FLOAT;
                    if (collecting) {
                        if (slot < LABEL_MAX_SLOTS) {
                            (*label).slots[slot].kind = kind;
                            if (kind == LABEL_SLOT_INT)
                                (*label).slots[slot].i = va_arg(*args, int);
                            else if (kind == LABEL_SLOT_FLOAT)
                                (*label).slots[slot].f = va_arg(*args, double);
                            else
                                (*label).slots[slot].s = va_arg(*args, const char*);
                        } else {
                            // still consume the vararg to keep alignment
                            if (kind == LABEL_SLOT_INT) (void) va_arg(*args, int);
                            else if (kind == LABEL_SLOT_FLOAT) (void) va_arg(*args, double);
                            else (void) va_arg(*args, const char*);
                        }
                    } else if (slot < LABEL_MAX_SLOTS) {
                        if (kind == LABEL_SLOT_INT)
                            Str_printf(&out, spec, (int) (*label).slots[slot].i);
                        else if (kind == LABEL_SLOT_FLOAT)
                            Str_printf(&out, spec, (*label).slots[slot].f);
                        else
                            Str_printf(&out, spec, (*label).slots[slot].s ? (*label).slots[slot].s : "");
                    }
                    slot++;
                    p = close + 1;
                    continue;
                }
            }
        }
        Str_putc(&out, *p);
        p++;
    }
    if (collecting)
        (*label).slotCount = slot;
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

static Label *labelCreate(const char *text, uint32_t color) {
    Label *label = (Label*) calloc(1, sizeof(Label));
    if (label == nullptr)
        return nullptr;
    Component_init(&(*label).component);
    (*label).component.type = LABEL_TYPE;
    GraphicsComponent gc;
    GraphicsComponent_init(&gc);
    Component_addGraphics(&(*label).component, &gc);
    (*label).textColor = color;
    (*label).autoW = false;
    (*label).autoH = false;
    if (text != nullptr) {
        size_t n = strlen(text);
        if (n >= sizeof((*label).text))
            n = sizeof((*label).text) - 1u;
        memcpy((*label).text, text, n);
        (*label).text[n] = '\0';
        // A plain label's format IS its text (no slots), so a later render
        // rebuilds the same string instead of wiping it.
        memcpy((*label).format, text, n);
        (*label).format[n] = '\0';
    }
    return label;
}

Label *Label_0(void) {
    return labelCreate("", 0xFFFFFFFFu);
}

Label *Label_1(const char *text) {
    return labelCreate(text, 0xFFFFFFFFu);
}

Label *Label_2(const char *text, uint32_t color) {
    return labelCreate(text, color);
}

Label *Label_zero(void) {
    return Label_0();
}

void Label_free(Label *label) {
    if (label == nullptr)
        return;
    // Pull model: the label registers no observers, so there is nothing to
    // unbind — the bound reactives simply stop being read.
    Component_destroy(&(*label).component);
    free(label);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

void Label_setText(Label *label, const char *fmt, ...) {
    if (label == nullptr)
        return;
    if (fmt == nullptr)
        fmt = "";
    size_t n = strlen(fmt);
    if (n >= sizeof((*label).format))
        n = sizeof((*label).format) - 1u;
    memcpy((*label).format, fmt, n);
    (*label).format[n] = '\0';

    va_list args;
    va_start(args, fmt);
    buildText(label, (*label).format, &args, true);  // collect the slots
    va_end(args);

    Label_render(label);
}

void Label_render(Label *label) {
    if (label == nullptr)
        return;
    drainSlots(label);                                   // owner-thread reactive pull
    buildText(label, (*label).format, nullptr, false);   // emit from the stored slots
    if ((*label).autoW || (*label).autoH)
        resolveAuto(label);                              // measure text -> concrete size
}

// SETTERS (PUBLIC & PRIVATE)

static GraphicsComponent *labelPrimary(Label *label) {
    return Component_graphics(&(*label).component, 0);
}

;;SETTER
// Measure the rendered text (font advances + the label's padding) and write the
// measured extent into the AUTO dims. The AUTO flags persist, so every render
// re-measures — text, font, and padding may all have moved since the last one.
static void resolveAuto(Label *label) {
    GraphicsComponent *gc = labelPrimary(label);
    if (gc == nullptr)
        return;
    float padL, padT, padR, padB;
    GraphicsComponent_getPadding(gc, &padL, &padT, &padR, &padB);
    float textW = 0.0f;
    for (const char *p = (*label).text; *p != '\0'; p++)
        textW += (float) Font_advance(*p);
    float textH = (float) Font_lineHeight();
    float w = GraphicsComponent_getWidth(gc);
    float h = GraphicsComponent_getHeight(gc);
    if ((*label).autoW)
        w = textW + padL + padR;
    if ((*label).autoH)
        h = textH + padT + padB;
    GraphicsComponent_setSize(gc, w, h);
}

void Label_setSize(Label *label, float w, float h) {
    if (label == nullptr) return;
    // AUTO arms measure-on-render for that dim (the flag persists; the gc holds
    // the last resolved concrete extent). Concrete clears the intent.
    (*label).autoW = Size_isAutoF(w);
    (*label).autoH = Size_isAutoF(h);
    GraphicsComponent *gc = labelPrimary(label);
    if (gc != nullptr) GraphicsComponent_setSize(gc, w, h);
}

;;SETTER
void Label_setLocation(Label *label, float x, float y) {
    if (label == nullptr) return;
    GraphicsComponent *gc = labelPrimary(label);
    if (gc != nullptr) GraphicsComponent_setLocation(gc, x, y);
}

;;SETTER
void Label_setAnchor(Label *label, int anchor) {
    if (label == nullptr) return;
    GraphicsComponent *gc = labelPrimary(label);
    if (gc != nullptr) GraphicsComponent_setAnchor(gc, anchor);
}

;;SETTER
void Label_setPivot(Label *label, int pivot) {
    if (label == nullptr) return;
    GraphicsComponent *gc = labelPrimary(label);
    if (gc != nullptr) GraphicsComponent_setPivot(gc, pivot);
}

;;SETTER
void Label_setOrigin(Label *label, int origin) {
    if (label == nullptr) return;
    GraphicsComponent *gc = labelPrimary(label);
    if (gc != nullptr) GraphicsComponent_setOrigin(gc, origin);
}

;;SETTER
void Label_setTextColor(Label *label, uint32_t color) {
    if (label != nullptr)
        (*label).textColor = color;
}

;;SETTER
void Label_setMnemonic(Label *label, uint32_t key) {
    if (label != nullptr)
        (*label).mnemonic = key;
}

;;SETTER
void Label_setLigatures(Label *label, bool ligatures) {
    if (label != nullptr)
        (*label).ligatures = ligatures;
}

;;SETTER
void Label_setSpacing(Label *label, float spacing) {
    if (label != nullptr)
        (*label).spacing = spacing;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
const char *Label_getText(const Label *label) {
    return label ? (*label).text : "";
}

;;GETTER
uint32_t Label_getTextColor(const Label *label) {
    return label ? (*label).textColor : 0u;
}

;;GETTER
uint32_t Label_getMnemonic(const Label *label) {
    return label ? (*label).mnemonic : 0u;
}

;;GETTER
bool Label_isAutoWidth(const Label *label) {
    return label ? (*label).autoW : false;
}

;;GETTER
bool Label_isAutoHeight(const Label *label) {
    return label ? (*label).autoH : false;
}

;;GETTER
bool Label_isValid(const Label *label) {
    return label != nullptr;
}

// --- toString Law (bounded, cold-path) ---

void Label_toString(const Label *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_put(&s, "Label(");
    Str_putQuoted(&s, (*self).text);
    Str_printf(&s, ", slots: %u)", (*self).slotCount);
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}

void Label_toStringStruct(const Label *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_put(&s, "Label { name: ");
    Str_putQuoted(&s, (*self).component.name);
    Str_put(&s, ", text: ");
    Str_putQuoted(&s, (*self).text);
    Str_put(&s, ", format: ");
    Str_putQuoted(&s, (*self).format);
    Str_printf(&s, ", slots: %u, textColor: 0x%08X, mnemonic: %u, ligatures: %s, spacing: %.2f }",
               (*self).slotCount, (*self).textColor, (*self).mnemonic,
               (*self).ligatures ? "true" : "false", (*self).spacing);
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}

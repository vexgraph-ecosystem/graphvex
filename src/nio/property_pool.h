#ifndef GRAPHICS_NIO_PROPERTY_POOL_H
#define GRAPHICS_NIO_PROPERTY_POOL_H

#include <stdint.h>

#include "ui/property.h"

// graphvex R3 — nio/property_pool.h
//
// The pool every Element's bound comes from. Backed by nio/pool (ForeignMemory
// blocks, stable addresses), so a borrowed Property* is valid for the pool's
// life and may be shared by many Elements.
typedef struct PropertyPool PropertyPool;

PropertyPool *PropertyPool_0(void);
PropertyPool *PropertyPool_default(void);   // process-global, for Element-owned bounds
void PropertyPool_destroy(PropertyPool *pool);

// A zeroed, defaulted Property (init NULL => Property_default()), or NULL on OOM.
Property *PropertyPool_alloc(PropertyPool *pool, const Property *init);
void PropertyPool_release(PropertyPool *pool, Property *property);
uint32_t PropertyPool_live(const PropertyPool *pool);

#endif // GRAPHICS_NIO_PROPERTY_POOL_H

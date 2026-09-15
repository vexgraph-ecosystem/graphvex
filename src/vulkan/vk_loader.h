#ifndef GRAPHVEX_VK_LOADER_H
#define GRAPHVEX_VK_LOADER_H

#include <stdbool.h>

// vulkan/vk_loader.h — Vulkan dylib module loader entry points.
//
// Init against graphvex's device, load a vulkan.dylib module (VkHotContext
// protocol, see vulkan/vk_context.h), shut down, resolve swapped symbols.
// Dormant: no caller exists yet, and the Vk_get* accessors the loader was
// written against predate the current Vk seam — rewire before reactivating
// (see the .c overview + ;;INCOMPLETE).

bool hot_vk_init_loader(void);
bool hot_vk_load_module(const char *path);
void hot_vk_shutdown(void);
void *hot_vk_get_symbol(const char *name);

#endif

#ifndef IO_VFS_H
#define IO_VFS_H

#include <stddef.h>
#include <stdbool.h>

// io/vfs.h — Virtual File System for the Anti Engine Hub
// Maps virtual URIs (anti:// and project://) to absolute OS paths.

void Vfs_init(void);
void Vfs_setProject(const char *projectName);

// Resolves a virtual URI into an absolute macOS file path. 
// Returns true on success.
// Example: "project://geometry/level1.anti" -> "/Users/name/anti/projects/my_game/geometry/level1.anti"
bool Vfs_resolve(const char *uri, char *outPath, size_t maxLen);

#endif

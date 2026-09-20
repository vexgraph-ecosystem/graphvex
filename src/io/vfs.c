#include "io/vfs.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Vfs
 * ============================================================================
 * Virtual File System abstraction resolving URI schemes (such as anti:// and
 * project://) into validated POSIX filesystem paths for engine asset management.
 * Manages system home and project-relative directory trees, sanitizing incoming
 * paths against directory traversal attacks. Ensures automatic directory
 * hierarchy creation upon initialization and project attachment, enabling
 * unified asset resolution across shaders, textures, audio, and scripts.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Vfs (io/vfs.c)
 * LEVEL: L2 — Behavior (virtual filesystem behavior API)
 * ============================================================================
 * Virtual File System for the Anti Engine Hub resolving URI schemes
 * to absolute filesystem paths and managing sandbox directories.
 *
 * STRUCT FIELDS: none — procedural (module-level path state only).
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Vfs_init(void)                           : Initialize VFS and ensure directories
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Vfs_resolve(uri, outPath, maxLen)        : Resolve URI to filesystem path
 *
 * Private Core Functions: (.c static)
 *   - createDirectoryTree(dir)                 : Recursively create directories
 *
 * Public Setters: (.h)
 *   - Vfs_setProject(projectName)              : Mount project directory
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - (none)
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

static char s_userHome[1024] = {0};
static char s_antiHome[1024] = {0};
static char s_currentProject[1024] = {0};

// Forward declaration: called from Vfs_init (CONSTRUCTORS) before its
// definition in the CORE FUNCTIONS section below.
static void createDirectoryTree(const char *dir);

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

void Vfs_init(void) {
    const char *home = getenv("HOME");

    // Windows compatibility: check USERPROFILE
    if (!home) {
        home = getenv("USERPROFILE");
    }

    // macOS / Linux POSIX fallback
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) {
            home = (*pw).pw_dir;
        }
    }

    if (!home) home = "."; // Fallback if OS denies home dir access

    snprintf(s_userHome, sizeof(s_userHome), "%s", home);
    snprintf(s_antiHome, sizeof(s_antiHome), "%s/anti", home);

    // Ensure core hub directories exist (~/anti/config, ~/anti/projects)
    char pathBuf[1024];
    snprintf(pathBuf, sizeof(pathBuf), "%s/config", s_antiHome);
    createDirectoryTree(pathBuf);
    snprintf(pathBuf, sizeof(pathBuf), "%s/projects", s_antiHome);
    createDirectoryTree(pathBuf);
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

// Internal recursive directory builder
static void createDirectoryTree(const char *dir) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, S_IRWXU); // Ignored if it already exists
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

bool Vfs_resolve(const char *uri, char *outPath, size_t maxLen) {
    if (!uri || !outPath || maxLen == 0)
        return false;

    if (strncmp(uri, "anti://", 7) == 0) {
        const char *rel = uri + 7;
        if (strstr(rel, ".."))
            return false;
        int n = snprintf(outPath, maxLen, "%s/%s", s_antiHome, rel);
        if (n < 0 || (size_t) n >= maxLen)
            return false;
        return true;
    }
    else if (strncmp(uri, "project://", 10) == 0) {
        if (s_currentProject[0] == '\0') {
            // Cannot resolve project paths if no project is mounted
            return false;
        }
        const char *rel = uri + 10;
        if (strstr(rel, ".."))
            return false;
        int n = snprintf(outPath, maxLen, "%s/%s", s_currentProject, rel);
        if (n < 0 || (size_t) n >= maxLen)
            return false;
        return true;
    }
    else if (uri[0] == '~') {
        // Expand tilde universally to the OS home directory
        // Example: "~/Downloads/smth.png" yields "/Users/vexgraph/Downloads/smth.png"
        int n;
        if (uri[1] == '/' || uri[1] == '\\') {
            n = snprintf(outPath, maxLen, "%s%s", s_userHome, uri + 1);
        } else if (uri[1] == '\0') {
            n = snprintf(outPath, maxLen, "%s", s_userHome);
        } else {
            // If it is something like "~otheruser/", we do not support it yet,
            // so we fallback to a raw string copy.
            n = snprintf(outPath, maxLen, "%s", uri);
        }
        if (n < 0 || (size_t) n >= maxLen)
            return false;
        return true;
    }
    else {
        // Fallback: assume it is already an absolute or valid relative OS path
        int n = snprintf(outPath, maxLen, "%s", uri);
        if (n < 0 || (size_t) n >= maxLen)
            return false;
        return true;
    }
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void Vfs_setProject(const char *projectName) {
    if (!projectName || !projectName[0]) {
        s_currentProject[0] = '\0';
        return;
    }
    // Fail closed on traversal: project names are single-level labels.
    if (strstr(projectName, "..") || strchr(projectName, '/') ||
        strchr(projectName, '\\') || projectName[0] == '/') {
        s_currentProject[0] = '\0';
        return;
    }
    int n = snprintf(s_currentProject, sizeof(s_currentProject), "%s/projects/%s", s_antiHome, projectName);
    if (n < 0 || (size_t) n >= sizeof(s_currentProject)) {
        s_currentProject[0] = '\0';
        return;
    }

    // Ensure standard project directories exist
    char pathBuf[1024];
    snprintf(pathBuf, sizeof(pathBuf), "%s/textures", s_currentProject);
    createDirectoryTree(pathBuf);
    snprintf(pathBuf, sizeof(pathBuf), "%s/geometry", s_currentProject);
    createDirectoryTree(pathBuf);
    snprintf(pathBuf, sizeof(pathBuf), "%s/audio", s_currentProject);
    createDirectoryTree(pathBuf);
    snprintf(pathBuf, sizeof(pathBuf), "%s/scripts", s_currentProject);
    createDirectoryTree(pathBuf);
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

// (none)

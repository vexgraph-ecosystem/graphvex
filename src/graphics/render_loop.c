#include "graphics/render_loop.h"

#include <stdlib.h>
#include <time.h>

// graphvex R3 — graphics/render_loop.c
// The demand-driven step. Pure scheduling: no GPU resource, no verb.

struct RenderLoop {
    Client *clients;
    int count;
    int cap;
    uint32_t targetFps;   // 0 = pure demand (no cap)
    _Atomic bool wake;
    double lastSeconds;
};

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

RenderLoop *RenderLoop_0(void) { return RenderLoop_fps(0); }

RenderLoop *RenderLoop_fps(uint32_t targetFps) {
    RenderLoop *l = calloc(1, sizeof *l);
    if (l) {
        (*l).targetFps = targetFps;
        (*l).lastSeconds = now_seconds();
    }
    return l;
}

void RenderLoop_free(RenderLoop *loop) {
    if (!loop) return;
    free((*loop).clients);
    free(loop);
}

static RenderLoop *s_default = NULL;
RenderLoop *RenderLoop_default(void) {
    if (!s_default) s_default = RenderLoop_0();
    return s_default;
}

bool RenderLoop_addClient(RenderLoop *loop, const Client *client) {
    if (!loop || !client) return false;
    for (int i = 0; i < (*loop).count; i++) {
        if ((*loop).clients[i].window == (*client).window) {
            (*loop).clients[i] = *client;
            return true;
        }
    }
    if ((*loop).count == (*loop).cap) {
        (*loop).cap = (*loop).cap ? (*loop).cap * 2 : 8;
        Client *grown = realloc((*loop).clients, (size_t)((*loop).cap) * sizeof *grown);
        if (!grown) { (*loop).cap = 0; (*loop).count = 0; return false; }
        (*loop).clients = grown;
    }
    (*loop).clients[(*loop).count++] = *client;
    return true;
}

bool RenderLoop_removeClient(RenderLoop *loop, void *window) {
    if (!loop) return false;
    for (int i = 0; i < (*loop).count; i++) {
        if ((*loop).clients[i].window == window) {
            (*loop).clients[i] = (*loop).clients[(*loop).count - 1];
            (*loop).count--;
            return true;
        }
    }
    return false;
}

Client *RenderLoop_findClient(RenderLoop *loop, const void *window) {
    if (!loop) return NULL;
    for (int i = 0; i < (*loop).count; i++)
        if ((*loop).clients[i].window == window) return &(*loop).clients[i];
    return NULL;
}

void RenderLoop_markDirty(RenderLoop *loop, void *window) {
    Client *c = RenderLoop_findClient(loop, window);
    if (c) (*c).dirty = true;
}

void RenderLoop_setContentGen(RenderLoop *loop, void *window, uint64_t gen) {
    Client *c = RenderLoop_findClient(loop, window);
    if (c) (*c).contentGen = gen;
}

void RenderLoop_notify(RenderLoop *loop) {
    if (loop) (*loop).wake = true;
}

bool RenderLoop_step(RenderLoop *loop) {
    if (!loop) return false;
    double t = now_seconds();
    double dt = t - (*loop).lastSeconds;
    (*loop).lastSeconds = t;

    bool presented = false;
    for (int i = 0; i < (*loop).count; i++) {
        Client *c = &(*loop).clients[i];
        if ((*c).frameFn) (*c).frameFn((*c).userdata, dt);
        bool demand = (bool)((*c).dirty) || ((*c).contentGen != (*c).lastContentGen);
        if (!demand) continue;
        bool ok = (*c).presentFn ? (*c).presentFn((*c).window, dt, (*c).userdata) : false;
        if (ok) {
            (*c).hasPresented = true;
            (*c).lastContentGen = (*c).contentGen;
            (*c).dirty = false;
            presented = true;
        }
    }
    (*loop).wake = false;
    return presented;
}

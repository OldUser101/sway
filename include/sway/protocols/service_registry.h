#ifndef _SWAY_SERVICE_REGISTRY_H
#define _SWAY_SERVICE_REGISTRY_H

#include "service-registry-unstable-v1-protocol.h"
#include <stdint.h>
#include <wayland-server-core.h>

struct service_registry;
struct service_claim;
struct service_lookup;

struct service_registry {
    struct wl_display *display;
    struct wl_global *global;

    struct wl_list claims;
    uint64_t next_id;
    pid_t pid;

    struct wl_signal destroy;
    struct wl_listener display_destroy;
};

struct service_claim {
    struct service_registry *registry;

    struct wl_resource *resource;

    char *name;
    char *id;

    struct wl_list link;
};

struct service_lookup {
    struct service_registry *registry;

    struct wl_resource *resource;
};

struct service_registry *service_registry_create(struct wl_display *display);

#endif // _SWAY_SERVICE_REGISTRY_H

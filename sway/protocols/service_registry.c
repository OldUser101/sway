#include "sway/protocols/service_registry.h"
#include "service-registry-unstable-v1-protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>

#define SERVICE_REGISTRY_VERSION 1

static bool
service_name_valid(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }

    return true;
}

static struct service_claim *
service_registry_find_claim(struct service_registry *registry, const char *name)
{
    struct service_claim *claim;

    wl_list_for_each(claim, &registry->claims, link)
    {
        if (strcmp(claim->name, name) == 0) {
            return claim;
        }
    }

    return NULL;
}

static char *
service_registry_allocate_id(struct service_registry *registry)
{
    uint64_t id = registry->next_id;
    int len = snprintf(NULL, 0, "%d-%llu", (int)registry->pid,
                       (unsigned long long)id);

    char *str = malloc((size_t)len + 1);
    if (!str)
        return NULL;

    snprintf(str, (size_t)len + 1, "%d-%llu", (int)registry->pid,
             (unsigned long long)id);

    registry->next_id++;
    return str;
}

static void
service_claim_destroy(struct wl_resource *resource)
{
    struct service_claim *claim = wl_resource_get_user_data(resource);
    if (!claim) {
        return;
    }

    wl_list_remove(&claim->link);

    free(claim->name);
    free(claim->id);
    free(claim);
}

static void
service_claim_destroy_request(struct wl_client *client,
                              struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct service_claim_v1_interface service_claim_v1_impl = {
    .destroy = service_claim_destroy_request,
};

static void
service_registry_claim(struct wl_client *client,
                       struct wl_resource *manager_resource, uint32_t id,
                       const char *name)
{
    struct service_registry *registry =
        wl_resource_get_user_data(manager_resource);

    uint32_t version = wl_resource_get_version(manager_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &service_claim_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    if (!service_name_valid(name)) {
        wl_resource_set_implementation(resource, &service_claim_v1_impl, NULL,
                                       NULL);

        service_claim_v1_send_failed(resource,
                                     SERVICE_CLAIM_V1_ERROR_INVALID_NAME);

        return;
    }

    if (service_registry_find_claim(registry, name)) {
        wl_resource_set_implementation(resource, &service_claim_v1_impl, NULL,
                                       NULL);

        service_claim_v1_send_failed(resource,
                                     SERVICE_CLAIM_V1_ERROR_ALREADY_CLAIMED);

        return;
    }

    struct service_claim *claim = calloc(1, sizeof(*claim));
    if (!claim) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }

    claim->registry = registry;
    claim->resource = resource;
    claim->name = strdup(name);

    if (!claim->name) {
        free(claim);
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }

    claim->id = service_registry_allocate_id(registry);
    if (!claim->id) {
        free(claim->name);
        free(claim);
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }

    wl_list_insert(&registry->claims, &claim->link);

    wl_resource_set_implementation(resource, &service_claim_v1_impl, claim,
                                   service_claim_destroy);

    service_claim_v1_send_claimed(resource, claim->id);
}

static void
service_lookup_destroy(struct wl_resource *resource)
{
    struct service_lookup *lookup = wl_resource_get_user_data(resource);

    free(lookup);
}

static void
service_lookup_destroy_request(struct wl_client *client,
                               struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct service_lookup_v1_interface service_lookup_v1_impl = {
    .destroy = service_lookup_destroy_request,
};

static void
service_registry_lookup(struct wl_client *client,
                        struct wl_resource *manager_resource, uint32_t id,
                        const char *name)
{
    struct service_registry *registry =
        wl_resource_get_user_data(manager_resource);

    uint32_t version = wl_resource_get_version(manager_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &service_lookup_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    struct service_lookup *lookup = calloc(1, sizeof(*lookup));
    if (!lookup) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }

    lookup->registry = registry;
    lookup->resource = resource;

    wl_resource_set_implementation(resource, &service_lookup_v1_impl, lookup,
                                   service_lookup_destroy);

    struct service_claim *claim = service_registry_find_claim(registry, name);
    if (claim) {
        service_lookup_v1_send_found(resource, claim->id);
    } else {
        service_lookup_v1_send_not_found(resource);
    }
}

static const struct service_registry_manager_v1_interface
    service_registry_manager_v1_impl = {
        .claim = service_registry_claim,
        .lookup = service_registry_lookup,
};

static void
service_registry_bind(struct wl_client *client, void *data, uint32_t version,
                      uint32_t id)
{
    struct service_registry *registry = data;

    struct wl_resource *resource = wl_resource_create(
        client, &service_registry_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &service_registry_manager_v1_impl,
                                   registry, NULL);
}

static void
handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct service_registry *registry =
        wl_container_of(listener, registry, display_destroy);
    wl_signal_emit_mutable(&registry->destroy, NULL);

    wl_global_destroy(registry->global);

    struct service_claim *claim, *_tmp;

    wl_list_for_each_safe(claim, _tmp, &registry->claims, link)
    {
        wl_resource_destroy(claim->resource);
    }

    free(registry);
}

struct service_registry *
service_registry_create(struct wl_display *display)
{
    struct service_registry *registry = calloc(1, sizeof(*registry));
    if (!registry) {
        return NULL;
    }

    registry->display = display;
    registry->next_id = 1;
    registry->pid = getpid();

    wl_list_init(&registry->claims);

    registry->global = wl_global_create(
        display, &service_registry_manager_v1_interface,
        SERVICE_REGISTRY_VERSION, registry, service_registry_bind);

    if (!registry->global) {
        free(registry);
        return NULL;
    }

    wl_signal_init(&registry->destroy);

    registry->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(display, &registry->display_destroy);

    return registry;
}

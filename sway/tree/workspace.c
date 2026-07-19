#include "sway/tree/workspace.h"
#include "list.h"
#include "log.h"
#include "stringop.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/node.h"
#include "sway/tree/view.h"
#include "util.h"
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

int
workspace_read_num(char *s)
{
    char *ep = NULL;
    long lnum = strtol(s, &ep, 10);
    if (ep == s || lnum < 0 || lnum > INT32_MAX) {
        return -1;
    }
    return (int)lnum;
}

static int
workspace_get_max_num()
{
    int maxn = -1;

    for (int i = 0; i < root->outputs->length; ++i) {
        struct sway_output *output = root->outputs->items[i];
        for (int j = 0; j < output->workspaces->length; ++j) {
            struct sway_workspace *ws = output->workspaces->items[j];
            if (ws->num > maxn) {
                maxn = ws->num;
            }
        }
    }

    return maxn;
}

static int
workspace_get_output_max_num(struct sway_output *output)
{
    int maxn = -1;

    for (int i = 0; i < output->workspaces->length; ++i) {
        struct sway_workspace *ws = output->workspaces->items[i];
        if (ws->num > maxn) {
            maxn = ws->num;
        }
    }

    return maxn;
}

struct workspace_config *
workspace_find_config(int ws_num)
{
    for (int i = 0; i < config->workspace_configs->length; ++i) {
        struct workspace_config *wsc = config->workspace_configs->items[i];
        if (wsc->workspace == ws_num) {
            return wsc;
        }
    }
    return NULL;
}

struct sway_output *
workspace_get_initial_output(int ws_num)
{
    // Check workspace configs for a workspace<->output pair
    struct workspace_config *wsc = workspace_find_config(ws_num);
    if (wsc) {
        for (int i = 0; i < wsc->outputs->length; i++) {
            struct sway_output *output =
                output_by_name_or_id(wsc->outputs->items[i]);
            if (output) {
                return output;
            }
        }
    }
    // Otherwise try to put it on the focused output
    struct sway_seat *seat = input_manager_current_seat();
    struct sway_node *focus = seat_get_focus_inactive(seat, &root->node);
    if (focus && focus->type == N_WORKSPACE) {
        return focus->sway_workspace->output;
    } else if (focus && focus->type == N_CONTAINER) {
        return focus->sway_container->pending.workspace->output;
    }
    // Fallback to the first output or the headless output
    return root->outputs->length ? root->outputs->items[0]
                                 : root->fallback_output;
}

struct sway_workspace *
workspace_create(struct sway_output *output, int num)
{
    if (output == NULL) {
        output = workspace_get_initial_output(num);
    }

    if (num == -1) {
        // get the next available number
        num = workspace_get_max_num() + 1;
    }

    sway_log(SWAY_DEBUG, "Adding workspace %d for output %s", num,
             output->wlr_output->name);

    struct sway_workspace *ws = calloc(1, sizeof(struct sway_workspace));
    if (!ws) {
        sway_log(SWAY_ERROR, "Unable to allocate sway_workspace");
        return NULL;
    }
    node_init(&ws->node, N_WORKSPACE, ws);

    bool failed = false;
    ws->layers.tiling = alloc_scene_tree(root->staging, &failed);
    ws->layers.fullscreen = alloc_scene_tree(root->staging, &failed);

    if (failed) {
        wlr_scene_node_destroy(&ws->layers.tiling->node);
        wlr_scene_node_destroy(&ws->layers.fullscreen->node);
        free(ws);
        return NULL;
    }

    ws->num = num;
    ws->tags = create_list();
    ws->prev_split_layout = L_NONE;
    ws->layout = output_get_default_layout(output);
    ws->floating = create_list();
    ws->tiling = create_list();
    ws->output_priority = create_list();

    ws->gaps_outer = config->gaps_outer;
    ws->gaps_inner = config->gaps_inner;

    struct workspace_config *wsc = workspace_find_config(num);
    if (wsc) {
        if (wsc->gaps_outer.top != INT_MIN) {
            ws->gaps_outer.top = wsc->gaps_outer.top;
        }
        if (wsc->gaps_outer.right != INT_MIN) {
            ws->gaps_outer.right = wsc->gaps_outer.right;
        }
        if (wsc->gaps_outer.bottom != INT_MIN) {
            ws->gaps_outer.bottom = wsc->gaps_outer.bottom;
        }
        if (wsc->gaps_outer.left != INT_MIN) {
            ws->gaps_outer.left = wsc->gaps_outer.left;
        }
        if (wsc->gaps_inner != INT_MIN) {
            ws->gaps_inner = wsc->gaps_inner;
        }

        // Add output priorities
        for (int i = 0; i < wsc->outputs->length; ++i) {
            char *name = wsc->outputs->items[i];
            if (strcmp(name, "*") != 0) {
                list_add(ws->output_priority, strdup(name));
            }
        }
    }

    // If not already added, add the output to the lowest priority
    workspace_output_add_priority(ws, output);

    output_add_workspace(output, ws);
    output_sort_workspaces(output);

    ipc_event_workspace(NULL, ws, "init");
    wl_signal_emit_mutable(&root->events.new_node, &ws->node);

    return ws;
}

void
workspace_destroy(struct sway_workspace *workspace)
{
    if (!sway_assert(
            workspace->node.destroying,
            "Tried to free workspace which wasn't marked as destroying")) {
        return;
    }
    if (!sway_assert(workspace->node.ntxnrefs == 0,
                     "Tried to free workspace "
                     "which is still referenced by transactions")) {
        return;
    }

    scene_node_disown_children(workspace->layers.tiling);
    scene_node_disown_children(workspace->layers.fullscreen);
    wlr_scene_node_destroy(&workspace->layers.tiling->node);
    wlr_scene_node_destroy(&workspace->layers.fullscreen->node);

    free(workspace->representation);
    list_free_items_and_destroy(workspace->tags);
    list_free_items_and_destroy(workspace->output_priority);
    list_free(workspace->floating);
    list_free(workspace->tiling);
    list_free(workspace->current.floating);
    list_free(workspace->current.tiling);
    free(workspace);
}

void
workspace_begin_destroy(struct sway_workspace *workspace)
{
    sway_log(SWAY_DEBUG, "Destroying workspace '%d'", workspace->num);
    ipc_event_workspace(NULL, workspace, "empty"); // intentional
    wl_signal_emit_mutable(&workspace->node.events.destroy, &workspace->node);

    if (workspace->output) {
        workspace_detach(workspace);
    }
    workspace->node.destroying = true;
    node_set_dirty(&workspace->node);
}

void
workspace_consider_destroy(struct sway_workspace *ws)
{
    if (ws->tiling->length || ws->floating->length) {
        return;
    }

    if (ws->output && output_get_active_workspace(ws->output) == ws) {
        return;
    }

    struct sway_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link)
    {
        struct sway_node *node = seat_get_focus_inactive(seat, &root->node);
        if (node == &ws->node) {
            return;
        }
    }

    workspace_begin_destroy(ws);
}

static bool
workspace_valid_on_output(const char *output_name, int ws_num)
{
    struct workspace_config *wsc = workspace_find_config(ws_num);
    struct sway_output *output = output_by_name_or_id(output_name);
    if (!output) {
        return false;
    }
    if (!wsc) {
        return true;
    }

    for (int i = 0; i < wsc->outputs->length; i++) {
        if (output_match_name_or_id(output, wsc->outputs->items[i])) {
            return true;
        }
    }

    return false;
}

static void
workspace_num_from_binding(const struct sway_binding *binding,
                           const char *output_name, int *min_order,
                           int *earliest_num)
{
    char *cmdlist = strdup(binding->command);
    char *dup = cmdlist;
    char *name = NULL;
    *earliest_num = -1;

    // workspace n
    char *cmd = argsep(&cmdlist, " ", NULL);
    if (cmdlist) {
        name = argsep(&cmdlist, ",;", NULL);
    }

    // TODO: support "move container to workspace" bindings as well

    if (strcmp("workspace", cmd) == 0 && name) {
        char *_target = strdup(name);
        _target = do_var_replacement(_target);
        strip_quotes(_target);
        sway_log(SWAY_DEBUG, "Got valid workspace command for target: '%s'",
                 _target);

        // Make sure that the command references an actual workspace
        // not a command about workspaces
        if (strcmp(_target, "next") == 0 || strcmp(_target, "prev") == 0 ||
            strcmp(_target, "next_on_output") == 0 ||
            strcmp(_target, "prev_on_output") == 0 ||
            strcmp(_target, "number") == 0 ||
            strcmp(_target, "back_and_forth") == 0 ||
            strcmp(_target, "current") == 0) {
            free(_target);
            free(dup);
            return;
        }

        int _target_num;
        if ((_target_num = workspace_read_num(_target) == -1)) {
            return;
        }

        // Make sure that the workspace doesn't already exist
        if (workspace_by_number(_target_num)) {
            free(_target);
            free(dup);
            return;
        }

        // make sure that the workspace can appear on the given
        // output
        if (!workspace_valid_on_output(output_name, _target_num)) {
            free(_target);
            free(dup);
            return;
        }

        if (binding->order < *min_order) {
            *min_order = binding->order;
            *earliest_num = _target_num;
            sway_log(SWAY_DEBUG, "Workspace: Found free num %u", _target_num);
            free(_target);
        }
    }
    free(dup);
}

int
workspace_next_num(const char *output_name)
{
    sway_log(SWAY_DEBUG,
             "Workspace: Generating new workspace name for output %s",
             output_name);
    // Scan for available workspace names by looking through output-workspace
    // assignments primarily, falling back to bindings and numbers.
    struct sway_mode *mode = config->current_mode;

    struct sway_output *output = output_by_name_or_id(output_name);
    if (!output) {
        return -1;
    }

    int order = INT_MAX;
    int target = -1;
    for (int i = 0; i < mode->keysym_bindings->length; ++i) {
        workspace_num_from_binding(mode->keysym_bindings->items[i], output_name,
                                   &order, &target);
    }
    for (int i = 0; i < mode->keycode_bindings->length; ++i) {
        workspace_num_from_binding(mode->keycode_bindings->items[i],
                                   output_name, &order, &target);
    }
    for (int i = 0; i < config->workspace_configs->length; ++i) {
        // Unlike with bindings, this does not guarantee order
        const struct workspace_config *wsc =
            config->workspace_configs->items[i];
        if (workspace_by_number(wsc->workspace)) {
            continue;
        }
        bool found = false;
        for (int j = 0; j < wsc->outputs->length; ++j) {
            if (output_match_name_or_id(output, wsc->outputs->items[j])) {
                found = true;
                target = wsc->workspace;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    if (target != -1) {
        return target;
    }

    // As a fall back, use the next available number
    return workspace_get_max_num() + 1;
}

static bool
_workspace_by_number(struct sway_workspace *ws, void *data)
{
    int *num = (int *)data;
    if (*num == ws->num) {
        return true;
    }
    return false;
}

struct sway_workspace *
workspace_by_number(int num)
{
    return root_find_workspace(_workspace_by_number, (void *)&num);
}

struct sway_workspace *
workspace_by_desc(const char *desc)
{
    struct sway_seat *seat = input_manager_current_seat();
    struct sway_workspace *current = seat_get_focused_workspace(seat);

    if (current && strcmp(desc, "prev") == 0) {
        return workspace_prev(current);
    } else if (current && strcmp(desc, "prev_on_output") == 0) {
        return workspace_output_prev(current);
    } else if (current && strcmp(desc, "next") == 0) {
        return workspace_next(current);
    } else if (current && strcmp(desc, "next_on_output") == 0) {
        return workspace_output_next(current);
    } else if (strcmp(desc, "current") == 0) {
        return current;
    } else if (strcasecmp(desc, "back_and_forth") == 0) {
        struct sway_seat *seat = input_manager_current_seat();
        if (seat->prev_workspace == -1) {
            return NULL;
        }
        return root_find_workspace(_workspace_by_number,
                                   (void *)&seat->prev_workspace);
    } else {
        int ws_num;
        if ((ws_num = workspace_read_num((char *)desc)) == -1) {
            return NULL;
        }

        return workspace_by_number(ws_num);
    }
}

struct sway_workspace *
workspace_prev(struct sway_workspace *workspace)
{
    int n = workspace->num;
    struct sway_workspace *prev = NULL, *last = NULL;

    // Find the prev numbered workspace
    int prevn = -1, lastn = -1;
    for (int i = root->outputs->length - 1; i >= 0; i--) {
        struct sway_output *output = root->outputs->items[i];
        for (int j = output->workspaces->length - 1; j >= 0; j--) {
            struct sway_workspace *ws = output->workspaces->items[j];
            int wsn = ws->num;
            if (!last || (wsn >= 0 && wsn > lastn)) {
                // The greatest numbered (or last) workspace
                last = ws;
                lastn = last->num;
            }
            if (wsn < n && (!prev || wsn > prevn)) {
                // The closest workspace before the current
                prev = ws;
                prevn = prev->num;
            }
        }
    }

    if (!prev) {
        prev = workspace;
    }
    return prev;
}

struct sway_workspace *
workspace_next(struct sway_workspace *workspace)
{
    int n = workspace->num;
    struct sway_workspace *next = NULL, *first = NULL;

    // Find the next numbered workspace
    int nextn = -1, firstn = -1;
    for (int i = 0; i < root->outputs->length; i++) {
        struct sway_output *output = root->outputs->items[i];
        for (int j = 0; j < output->workspaces->length; j++) {
            struct sway_workspace *ws = output->workspaces->items[j];
            int wsn = ws->num;
            if (!first || (wsn >= 0 && wsn < firstn)) {
                // The first (or least numbered) workspace
                first = ws;
                firstn = first->num;
            }
            if (n < wsn && (!next || wsn < nextn)) {
                // The first workspace numerically after the current
                next = ws;
                nextn = next->num;
            }
        }
    }

    if (!next) {
        // if there is no next, stay where we are
        next = workspace;
    }
    return next;
}

struct sway_workspace *
workspace_output_next(struct sway_workspace *current)
{
    int nextn = -1;
    struct sway_workspace *nextws = NULL;

    for (int i = 0; i < current->output->workspaces->length; ++i) {
        struct sway_workspace *ws = current->output->workspaces->items[i];

        if (ws->num > current->num && (nextn == -1 || ws->num < nextn)) {
            // the closest to current, working forwards
            nextn = ws->num;
            nextws = ws;
        }
    }

    if (!nextws) {
        // no next, create one with maximum id
        nextws = workspace_create(current->output, -1);
    }

    return nextws;
}

struct sway_workspace *
workspace_output_prev(struct sway_workspace *current)
{
    int prevn = -1;
    struct sway_workspace *prevws = NULL;

    for (int i = 0; i < current->output->workspaces->length; ++i) {
        struct sway_workspace *ws = current->output->workspaces->items[i];

        if (ws->num < current->num && (prevn == -1 || ws->num > prevn)) {
            // the closest to current, working backwards
            prevn = ws->num;
            prevws = ws;
        }
    }

    if (!prevws) {
        // Wrap around to maximum if no previous available
        int maxn = workspace_get_output_max_num(current->output);
        if (maxn == -1) {
            return NULL;
        }

        prevws = workspace_by_number(maxn);
    }

    return prevws;
}

struct sway_workspace *
workspace_auto_back_and_forth(struct sway_workspace *workspace)
{
    struct sway_seat *seat = input_manager_current_seat();
    struct sway_workspace *active_ws = NULL;
    struct sway_node *focus = seat_get_focus_inactive(seat, &root->node);
    if (focus && focus->type == N_WORKSPACE) {
        active_ws = focus->sway_workspace;
    } else if (focus && focus->type == N_CONTAINER) {
        active_ws = focus->sway_container->pending.workspace;
    }

    if (config->auto_back_and_forth && active_ws && active_ws == workspace &&
        seat->prev_workspace > -1) {
        struct sway_workspace *new_ws =
            workspace_by_number(seat->prev_workspace);
        workspace =
            new_ws ? new_ws : workspace_create(NULL, seat->prev_workspace);
    }
    return workspace;
}

bool
workspace_switch(struct sway_workspace *workspace)
{
    struct sway_seat *seat = input_manager_current_seat();

    sway_log(SWAY_DEBUG, "Switching to workspace %p:%d", workspace,
             workspace->num);
    struct sway_node *next = seat_get_focus_inactive(seat, &workspace->node);
    if (next == NULL) {
        next = &workspace->node;
    }
    seat_set_focus(seat, next);
    arrange_workspace(workspace);
    return true;
}

bool
workspace_is_visible(struct sway_workspace *ws)
{
    if (ws->node.destroying) {
        return false;
    }
    return output_get_active_workspace(ws->output) == ws;
}

bool
workspace_is_empty(struct sway_workspace *ws)
{
    if (ws->tiling->length) {
        return false;
    }
    // Sticky views are not considered to be part of this workspace
    for (int i = 0; i < ws->floating->length; ++i) {
        struct sway_container *floater = ws->floating->items[i];
        if (!container_is_sticky(floater)) {
            return false;
        }
    }
    return true;
}

static int
find_output(const void *id1, const void *id2)
{
    return strcmp(id1, id2);
}

static int
workspace_output_get_priority(struct sway_workspace *ws,
                              struct sway_output *output)
{
    char identifier[128];
    output_get_identifier(identifier, sizeof(identifier), output);
    int index_id = list_seq_find(ws->output_priority, find_output, identifier);
    int index_name = list_seq_find(ws->output_priority, find_output,
                                   output->wlr_output->name);
    return index_name < 0 || index_id < index_name ? index_id : index_name;
}

void
workspace_output_raise_priority(struct sway_workspace *ws,
                                struct sway_output *old_output,
                                struct sway_output *output)
{
    int old_index = workspace_output_get_priority(ws, old_output);
    if (old_index < 0) {
        return;
    }

    int new_index = workspace_output_get_priority(ws, output);
    if (new_index < 0) {
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        list_insert(ws->output_priority, old_index, strdup(identifier));
    } else if (new_index > old_index) {
        char *name = ws->output_priority->items[new_index];
        list_del(ws->output_priority, new_index);
        list_insert(ws->output_priority, old_index, name);
    }
}

void
workspace_output_add_priority(struct sway_workspace *workspace,
                              struct sway_output *output)
{
    if (workspace_output_get_priority(workspace, output) < 0) {
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        list_add(workspace->output_priority, strdup(identifier));
    }
}

struct sway_output *
workspace_output_get_highest_available(struct sway_workspace *ws)
{
    for (int i = 0; i < ws->output_priority->length; i++) {
        const char *name = ws->output_priority->items[i];
        struct sway_output *output = output_by_name_or_id(name);
        if (output) {
            return output;
        }
    }

    return NULL;
}

static bool
find_urgent_iterator(struct sway_container *con, void *data)
{
    return con->view && view_is_urgent(con->view);
}

void
workspace_detect_urgent(struct sway_workspace *workspace)
{
    bool new_urgent =
        (bool)workspace_find_container(workspace, find_urgent_iterator, NULL);

    if (workspace->urgent != new_urgent) {
        workspace->urgent = new_urgent;
        ipc_event_workspace(NULL, workspace, "urgent");
    }
}

void
workspace_for_each_container(struct sway_workspace *ws,
                             void (*f)(struct sway_container *con, void *data),
                             void *data)
{
    // Tiling
    for (int i = 0; i < ws->tiling->length; ++i) {
        struct sway_container *container = ws->tiling->items[i];
        f(container, data);
        container_for_each_child(container, f, data);
    }
    // Floating
    for (int i = 0; i < ws->floating->length; ++i) {
        struct sway_container *container = ws->floating->items[i];
        f(container, data);
        container_for_each_child(container, f, data);
    }
}

struct sway_container *
workspace_find_container(struct sway_workspace *ws,
                         bool (*test)(struct sway_container *con, void *data),
                         void *data)
{
    struct sway_container *result = NULL;
    if (ws == NULL) {
        sway_log(SWAY_ERROR, "Cannot find container with no workspace.");
        return NULL;
    }

    // Tiling
    for (int i = 0; i < ws->tiling->length; ++i) {
        struct sway_container *child = ws->tiling->items[i];
        if (test(child, data)) {
            return child;
        }
        if ((result = container_find_child(child, test, data))) {
            return result;
        }
    }
    // Floating
    for (int i = 0; i < ws->floating->length; ++i) {
        struct sway_container *child = ws->floating->items[i];
        if (test(child, data)) {
            return child;
        }
        if ((result = container_find_child(child, test, data))) {
            return result;
        }
    }
    return NULL;
}

static void
set_workspace(struct sway_container *container, void *data)
{
    container->pending.workspace = container->pending.parent->pending.workspace;
}

static void
workspace_attach_tiling(struct sway_workspace *ws, struct sway_container *con)
{
    list_add(ws->tiling, con);
    con->pending.workspace = ws;
    container_for_each_child(con, set_workspace, NULL);
    container_handle_fullscreen_reparent(con);
    workspace_update_representation(ws);
    node_set_dirty(&ws->node);
    node_set_dirty(&con->node);
}

struct sway_container *
workspace_wrap_children(struct sway_workspace *ws)
{
    struct sway_container *fs = ws->fullscreen;
    struct sway_container *middle = container_create(NULL);
    middle->pending.layout = ws->layout;
    while (ws->tiling->length) {
        struct sway_container *child = ws->tiling->items[0];
        container_detach(child);
        container_add_child(middle, child);
    }
    workspace_attach_tiling(ws, middle);
    ws->fullscreen = fs;
    return middle;
}

void
workspace_unwrap_children(struct sway_workspace *ws,
                          struct sway_container *wrap)
{
    if (!sway_assert(workspace_is_empty(ws),
                     "target workspace must be empty")) {
        return;
    }

    ws->layout = wrap->pending.layout;
    while (wrap->pending.children->length) {
        struct sway_container *child = wrap->pending.children->items[0];
        container_detach(child);
        workspace_add_tiling(ws, child);
    }
}

void
workspace_detach(struct sway_workspace *workspace)
{
    struct sway_output *output = workspace->output;
    int index = list_find(output->workspaces, workspace);
    if (index != -1) {
        list_del(output->workspaces, index);
    }
    workspace->output = NULL;

    node_set_dirty(&workspace->node);
    node_set_dirty(&output->node);
}

struct sway_container *
workspace_add_tiling(struct sway_workspace *workspace,
                     struct sway_container *con)
{
    if (con->pending.workspace) {
        struct sway_container *old_parent = con->pending.parent;
        container_detach(con);
        if (old_parent) {
            container_reap_empty(old_parent);
        }
    }
    if (config->default_layout != L_NONE) {
        con = container_split(con, config->default_layout);
    }
    list_add(workspace->tiling, con);
    con->pending.workspace = workspace;
    container_for_each_child(con, set_workspace, NULL);
    container_handle_fullscreen_reparent(con);
    workspace_update_representation(workspace);
    node_set_dirty(&workspace->node);
    node_set_dirty(&con->node);
    return con;
}

void
workspace_add_floating(struct sway_workspace *workspace,
                       struct sway_container *con)
{
    if (con->pending.workspace) {
        container_detach(con);
    }
    list_add(workspace->floating, con);
    con->pending.workspace = workspace;
    container_for_each_child(con, set_workspace, NULL);
    container_handle_fullscreen_reparent(con);
    node_set_dirty(&workspace->node);
    node_set_dirty(&con->node);
}

void
workspace_insert_tiling_direct(struct sway_workspace *workspace,
                               struct sway_container *con, int index)
{
    list_insert(workspace->tiling, index, con);
    con->pending.workspace = workspace;
    container_for_each_child(con, set_workspace, NULL);
    container_handle_fullscreen_reparent(con);
    workspace_update_representation(workspace);
    node_set_dirty(&workspace->node);
    node_set_dirty(&con->node);
}

struct sway_container *
workspace_insert_tiling(struct sway_workspace *workspace,
                        struct sway_container *con, int index)
{
    if (con->pending.workspace) {
        container_detach(con);
    }
    if (config->default_layout != L_NONE) {
        con = container_split(con, config->default_layout);
    }
    workspace_insert_tiling_direct(workspace, con, index);
    return con;
}

bool
workspace_has_single_visible_container(struct sway_workspace *ws)
{
    struct sway_seat *seat = input_manager_get_default_seat();
    struct sway_container *focus = seat_get_focus_inactive_tiling(seat, ws);
    if (focus && !focus->view) {
        focus = seat_get_focus_inactive_view(seat, &focus->node);
    }
    return (focus && focus->view && view_ancestor_is_only_visible(focus->view));
}

void
workspace_add_gaps(struct sway_workspace *ws)
{
    if (config->smart_gaps == SMART_GAPS_ON &&
        workspace_has_single_visible_container(ws)) {
        ws->current_gaps.top = 0;
        ws->current_gaps.right = 0;
        ws->current_gaps.bottom = 0;
        ws->current_gaps.left = 0;
        return;
    }

    if (config->smart_gaps == SMART_GAPS_INVERSE_OUTER &&
        !workspace_has_single_visible_container(ws)) {
        ws->current_gaps.top = 0;
        ws->current_gaps.right = 0;
        ws->current_gaps.bottom = 0;
        ws->current_gaps.left = 0;
    } else {
        ws->current_gaps = ws->gaps_outer;
    }

    // Add inner gaps and make sure we don't turn out negative
    ws->current_gaps.top = fmax(0, ws->current_gaps.top + ws->gaps_inner);
    ws->current_gaps.right = fmax(0, ws->current_gaps.right + ws->gaps_inner);
    ws->current_gaps.bottom = fmax(0, ws->current_gaps.bottom + ws->gaps_inner);
    ws->current_gaps.left = fmax(0, ws->current_gaps.left + ws->gaps_inner);

    // Now that we have the total gaps calculated we may need to clamp them in
    // case they've made the available area too small
    if (ws->width - ws->current_gaps.left - ws->current_gaps.right <
            MIN_SANE_W &&
        ws->current_gaps.left + ws->current_gaps.right > 0) {
        int total_gap = fmax(0, ws->width - MIN_SANE_W);
        double left_gap_frac =
            ((double)ws->current_gaps.left /
             ((double)ws->current_gaps.left + (double)ws->current_gaps.right));
        ws->current_gaps.left = left_gap_frac * total_gap;
        ws->current_gaps.right = total_gap - ws->current_gaps.left;
    }
    if (ws->height - ws->current_gaps.top - ws->current_gaps.bottom <
            MIN_SANE_H &&
        ws->current_gaps.top + ws->current_gaps.bottom > 0) {
        int total_gap = fmax(0, ws->height - MIN_SANE_H);
        double top_gap_frac =
            ((double)ws->current_gaps.top /
             ((double)ws->current_gaps.top + (double)ws->current_gaps.bottom));
        ws->current_gaps.top = top_gap_frac * total_gap;
        ws->current_gaps.bottom = total_gap - ws->current_gaps.top;
    }

    ws->x += ws->current_gaps.left;
    ws->y += ws->current_gaps.top;
    ws->width -= ws->current_gaps.left + ws->current_gaps.right;
    ws->height -= ws->current_gaps.top + ws->current_gaps.bottom;
}

struct sway_container *
workspace_split(struct sway_workspace *workspace,
                enum sway_container_layout layout)
{
    if (workspace->tiling->length == 0) {
        workspace->prev_split_layout = workspace->layout;
        workspace->layout = layout;
        return NULL;
    }

    enum sway_container_layout old_layout = workspace->layout;
    struct sway_container *middle = workspace_wrap_children(workspace);
    workspace->layout = layout;
    middle->pending.layout = old_layout;

    struct sway_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link)
    {
        if (seat_get_focus(seat) == &workspace->node) {
            seat_set_focus(seat, &middle->node);
        }
    }

    return middle;
}

void
workspace_update_representation(struct sway_workspace *ws)
{
    size_t len = container_build_representation(ws->layout, ws->tiling, NULL);
    free(ws->representation);
    ws->representation = calloc(len + 1, sizeof(char));
    if (!sway_assert(ws->representation, "Unable to allocate title string")) {
        return;
    }
    container_build_representation(ws->layout, ws->tiling, ws->representation);
}

void
workspace_get_box(struct sway_workspace *workspace, struct wlr_box *box)
{
    box->x = workspace->x;
    box->y = workspace->y;
    box->width = workspace->width;
    box->height = workspace->height;
}

static void
count_tiling_views(struct sway_container *con, void *data)
{
    if (con->view && !container_is_floating_or_child(con)) {
        size_t *count = data;
        *count += 1;
    }
}

size_t
workspace_num_tiling_views(struct sway_workspace *ws)
{
    size_t count = 0;
    workspace_for_each_container(ws, count_tiling_views, &count);
    return count;
}

static void
count_sticky_containers(struct sway_container *con, void *data)
{
    if (container_is_sticky(con)) {
        size_t *count = data;
        *count += 1;
    }
}

size_t
workspace_num_sticky_containers(struct sway_workspace *ws)
{
    size_t count = 0;
    workspace_for_each_container(ws, count_sticky_containers, &count);
    return count;
}

void
workspace_squash(struct sway_workspace *workspace)
{
    for (int i = 0; i < workspace->tiling->length; i++) {
        struct sway_container *child = workspace->tiling->items[i];
        i += container_squash(child);
    }
}

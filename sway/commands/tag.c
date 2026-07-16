#include "list.h"
#include "log.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include <strings.h>

typedef enum {
    SWAY_TAG_NONE = 0,
    SWAY_TAG_SET = 1,
    SWAY_TAG_CLEAR = 2,
} sway_tag_op_t;

static const char expected_syntax[] =
    "Expected 'tag workspace <desc> set|clear <tag>' or "
    "'tag workspace set|clear <tag>'";

struct cmd_results *
cmd_tag(int argc, char **argv)
{
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "tag", EXPECTED_AT_LEAST, 3))) {
        return error;
    }
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID,
            "Can't run this command while there's no outputs connected.");
    }
    if (strcasecmp(argv[0], "workspace") != 0) {
        return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
    }

    int argn = 1;
    sway_tag_op_t tag_op = SWAY_TAG_NONE;
    struct sway_workspace *workspace = NULL;

    if (strcasecmp(argv[1], "set") == 0) {
        // 'tag workspace set <tag>'
        tag_op = SWAY_TAG_SET;
        workspace = config->handler_context.workspace;
        argn++;
    } else if (strcasecmp(argv[1], "clear") == 0) {
        // 'tag workspace clear <tag>'
        tag_op = SWAY_TAG_CLEAR;
        workspace = config->handler_context.workspace;
        argn++;
    } else {
        // 'tag workspace <desc> set|clear <tag>'
        int end = argn;
        while (end < argc && (strcasecmp(argv[end], "set") != 0 &&
                              strcasecmp(argv[end], "clear") != 0)) {
            ++end;
        }
        char *desc = join_args(argv + argn, end - argn);
        workspace = workspace_by_desc(desc);
        argn = end;
    }

    if (!workspace) {
        return cmd_results_new(
            CMD_INVALID, "There is no workspace matching that description.");
    }

    if (argn >= argc) {
        return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
    }

    if (workspace && tag_op == SWAY_TAG_NONE) {
        if (strcasecmp(argv[argn], "set") == 0) {
            tag_op = SWAY_TAG_SET;
        } else {
            tag_op = SWAY_TAG_CLEAR;
        }

        argn++;
    }

    if (argn >= argc) {
        return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
    }

    char *tag = join_args(argv + argn, argc - argn);

    if (tag_op == SWAY_TAG_SET) {
        sway_log(SWAY_DEBUG, "adding tag '%s' to workspace '%d'", tag,
                 workspace->num);
    } else {
        sway_log(SWAY_DEBUG, "removing tag '%s' from workspace '%d'", tag,
                 workspace->num);
    }

    if (tag_op == SWAY_TAG_SET) {
        // check the tag isn't set already
        for (int i = 0; i < workspace->tags->length; ++i) {
            if (strcasecmp(workspace->tags->items[i], tag) == 0) {
                // tag was already set, ignore
                return cmd_results_new(CMD_SUCCESS, NULL);
            }
        }

        list_add(workspace->tags, tag);
    } else {
        // find the tag index
        int idx = -1;
        for (int i = 0; i < workspace->tags->length; ++i) {
            if (strcasecmp(workspace->tags->items[i], tag) == 0) {
                idx = i;
                break;
            }
        }

        if (idx >= 0) {
            list_del(workspace->tags, idx);
        }
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}

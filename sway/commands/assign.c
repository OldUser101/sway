#include "list.h"
#include "log.h"
#include "stringop.h"
#include "sway/commands.h"
#include "sway/criteria.h"
#include "sway/tree/workspace.h"
#include <stdio.h>
#include <string.h>

struct cmd_results *
cmd_assign(int argc, char **argv)
{
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "assign", EXPECTED_AT_LEAST, 2))) {
        return error;
    }

    // Create criteria
    char *err_str = NULL;
    struct criteria *criteria = criteria_parse(argv[0], &err_str);
    if (!criteria) {
        error = cmd_results_new(CMD_INVALID, "%s", err_str);
        free(err_str);
        return error;
    }

    --argc;
    ++argv;

    if (has_prefix(*argv, "→")) {
        if (argc < 2) {
            free(criteria);
            return cmd_results_new(CMD_INVALID, "Missing workspace");
        }
        --argc;
        ++argv;
    }

    if (strcmp(*argv, "output") == 0) {
        criteria->type = CT_ASSIGN_OUTPUT;
        --argc;
        ++argv;
    } else {
        if (strcmp(*argv, "workspace") == 0) {
            --argc;
            ++argv;
        }
        if (workspace_read_num(argv[0]) == -1) {
            return cmd_results_new(CMD_INVALID, "Invalid workspace number '%s'",
                                   argv[0]);
        }
        criteria->type = CT_ASSIGN_WORKSPACE;
    }

    criteria->target = join_args(argv, argc);

    list_add(config->criteria, criteria);
    sway_log(SWAY_DEBUG, "assign: '%s' -> '%s' added", criteria->raw,
             criteria->target);

    return cmd_results_new(CMD_SUCCESS, NULL);
}

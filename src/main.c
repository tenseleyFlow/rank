#include "config.h"
#include "options.h"
#include "line.h"
#include "output.h"
#include "plan.h"
#include "sort.h"
#include "util.h"

#include <stdlib.h>

int
main(int argc, char **argv)
{
    struct rank_options options;
    struct rank_plan plan;
    struct rank_lines lines;
    int parse_status;
    int exit_status = RANK_EXIT_SUCCESS;

    rank_options_init(&options, argv[0]);
    parse_status = rank_options_parse(&options, argc, argv);
    if (parse_status != RANK_OPTIONS_OK) {
        return parse_status;
    }

    if (options.show_help) {
        rank_options_print_help(stdout);
        return 0;
    }

    if (options.show_version) {
        rank_options_print_version(stdout);
        return 0;
    }

    plan = rank_plan_from_options(&options);
    (void)plan;

    rank_lines_init(&lines);
    if (!rank_lines_read_all(&lines, &options)) {
        rank_lines_free(&lines);
        free(options.keys);
        return RANK_EXIT_SERIOUS;
    }
    if (!rank_lines_prepare_keys(&lines, &options)) {
        rank_lines_free(&lines);
        free(options.keys);
        return RANK_EXIT_SERIOUS;
    }
    if (!rank_sort_lines(&lines, &options, &plan)) {
        exit_status = RANK_EXIT_SERIOUS;
    } else if (!rank_output_lines(&lines, &options)) {
        exit_status = RANK_EXIT_SERIOUS;
    }
    rank_lines_free(&lines);
    return exit_status;
}

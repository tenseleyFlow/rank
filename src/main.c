#include "config.h"
#include "check.h"
#include "options.h"
#include "line.h"
#include "external.h"
#include "rank_locale.h"
#include "merge.h"
#include "output.h"
#include "plan.h"
#include "sort.h"
#include "util.h"

#include <stdlib.h>

static void free_options(struct rank_options *options);

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

    if (rank_options_load_files0(&options) != RANK_OPTIONS_OK) {
        free_options(&options);
        return RANK_EXIT_SERIOUS;
    }
    if (!rank_check_options_valid(&options)) {
        free_options(&options);
        return RANK_EXIT_SERIOUS;
    }
    if (!rank_locale_init(&options)) {
        free_options(&options);
        return RANK_EXIT_SERIOUS;
    }

    plan = rank_plan_from_options(&options);
    (void)plan;

    if (options.check_mode != RANK_CHECK_NONE) {
        int status = rank_check_stream(&options);

        free_options(&options);
        return status;
    }

    if (options.merge) {
        bool ok = rank_merge_all(&options);

        free_options(&options);
        return ok ? RANK_EXIT_SUCCESS : RANK_EXIT_SERIOUS;
    }

    if (options.has_buffer_size) {
        bool ok = rank_external_sort(&options);

        free_options(&options);
        return ok ? RANK_EXIT_SUCCESS : RANK_EXIT_SERIOUS;
    }

    rank_lines_init(&lines);
    if (!rank_lines_read_all(&lines, &options)) {
        rank_lines_free(&lines);
        free_options(&options);
        return RANK_EXIT_SERIOUS;
    }
    if (!rank_lines_prepare_keys(&lines, &options)) {
        rank_lines_free(&lines);
        free_options(&options);
        return RANK_EXIT_SERIOUS;
    }
    if (!rank_sort_lines(&lines, &options, &plan)) {
        exit_status = RANK_EXIT_SERIOUS;
    } else if (!rank_output_lines(&lines, &options)) {
        exit_status = RANK_EXIT_SERIOUS;
    }
    rank_lines_free(&lines);
    free_options(&options);
    return exit_status;
}

static void
free_options(struct rank_options *options)
{
    free(options->keys);
    free(options->temporary_dirs);
    free(options->files0_buf);
    free(options->files0_names);
}

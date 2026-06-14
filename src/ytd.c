#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define FLAG_IMPLEMENTATION
#include "../thirdparty/flag.h"

#define NOB_IMPLEMENTATION
#include "../thirdparty/nob.h"

void usage(FILE *stream)
{
    fprintf(stream, "Usage: ytd [OPTIONS] [--] [ARGS]\n");
    fprintf(stream, "Options:\n");
    flag_print_options(stream);
}

int main(int argc, char **argv)
{

    bool      *help     = flag_bool("h", false, "Print this help to stdout and exit with 0");
    bool      *debug    = flag_bool("debug", false, "Print the debug logs");
    char     **download = flag_str("d", NULL, "Pass the video ID to download");
    uint64_t  *latest   = flag_uint64("l", 0, "Pass the number of latest videos for channel");

    if (!flag_parse(argc, argv)) {
        usage(stderr);
        flag_print_error(stderr);
        return 1;
    }

    if (*help) {
        usage(stdout);
        return 0;
    }

    if (!*debug) nob_set_log_handler(&nob_null_log_handler);

    if (*latest) {
        argc = flag_rest_argc();
        argv = flag_rest_argv();

        if (argc != 1) {
            fprintf(stderr, "ytd: passed %d args; must only pass 1 arg", argc);
            return 1;
        }

        Cmd cmd = {0};
        Chain chain = {0};
        if (!chain_begin(&chain)) return 1;
        {
            cmd_append(&cmd, "yt-dlp");
            cmd_append(&cmd, "--playlist-end", temp_sprintf("%" PRIu64, *latest));
            cmd_append(&cmd, "--flat-playlist");
            cmd_append(&cmd, "--extractor-args", "youtubetab:approximate_date");
            cmd_append(&cmd, "--ignore-errors");
            cmd_append(&cmd, "-j", temp_sprintf("https://youtube.com/%s", argv[0]));
            if (!chain_cmd(&chain, &cmd)) return 1;

            cmd_append(&cmd, "jq");
            cmd_append(&cmd, "-r", "select(.url | contains(\"/shorts/\") | not) | \"[\\(.upload_date)] [\\(.id)] \\(.title)\"");
            if (!chain_cmd(&chain, &cmd)) return 1;
        }
        if (!chain_end(&chain)) return 1;
        return 0;
    }

    if (*download) {
        Cmd cmd = {0};
        cmd_append(&cmd, "yt-dlp");
        cmd_append(&cmd, "-f", "bestvideo[ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4]/best");
        cmd_append(&cmd, "--merge-output-format", "mp4");
        cmd_append(&cmd, "--embed-thumbnail");
        cmd_append(&cmd, "--no-mtime");
        cmd_append(&cmd, "-o", "$HOME/Videos/%(upload_date>%Y-%m-%d)s - %(title)s.%(ext)s");
        cmd_append(&cmd, temp_sprintf("https://youtu.be/%s", *download));
        if (!cmd_run(&cmd)) return 1;
        return 0;
    }

    return 0;
}

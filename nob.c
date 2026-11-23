#include <stdbool.h>

#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"

#define BUILD_FOLDER      "build/"
#define SRC_FOLDER        "src/"

int main(int argc, char **argv)
{
    if (!mkdir_if_not_exists(BUILD_FOLDER)) return 1;

    GO_REBUILD_URSELF(argc, argv);

    shift(argv, argc);

    bool asan = false;

    Cmd cmd = {0};
    cmd_append(&cmd, "cc");
    cmd_append(&cmd, "-Wall", "-Wextra");
    if (asan) cmd_append(&cmd, "-g", "-fsanitize=address,undefined");
    cmd_append(&cmd, "-o", BUILD_FOLDER"ytd", SRC_FOLDER"ytd.c");
    cmd_append(&cmd, "-lsqlite3");

    if (!cmd_run(&cmd)) return 1;

    if (argc > 0 && strcmp(argv[0], "-run") == 0) {
        shift(argv, argc);
        Cmd cmd = {0};
        cmd_append(&cmd, BUILD_FOLDER"ytd");
        for (int i = 0; i < argc; ++i) cmd_append(&cmd, argv[i]);
        if (!cmd_run(&cmd)) return 1;
    }

    if (argc == 1 && strcmp(argv[0], "-install") == 0) {
        Cmd cmd = {0};
        const char *home = getenv("HOME");
        assert(home);
        cmd_append(&cmd, "cp", BUILD_FOLDER"ytd", temp_sprintf("%s/%s", home, ".local/bin"));
        if (!cmd_run(&cmd)) return 1;
    }

    return 0;
}

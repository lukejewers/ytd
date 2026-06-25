#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"

#include "sqlite3.h"

#define BUILD_FOLDER      "build/"
#define SRC_FOLDER        "src/"

int main(int argc, char **argv)
{
    if (!mkdir_if_not_exists(BUILD_FOLDER)) return 1;

    GO_REBUILD_URSELF(argc, argv);
    Cmd cmd = {0};
    cmd_append(&cmd, "cc");
    cmd_append(&cmd, "-Wall", "-Wextra");
    cmd_append(&cmd, "-lsqlite3");
    cmd_append(&cmd, "-o", BUILD_FOLDER"ytd", SRC_FOLDER"ytd.c");

    if (!cmd_run(&cmd)) return 1;

    shift(argv, argc); // program name
    if (argc == 1 && strcmp(argv[0], "-install") == 0) {
        Cmd cmd = {0};
        const char *home = getenv("HOME");
        assert(home);
        cmd_append(&cmd, "cp", BUILD_FOLDER"ytd", temp_sprintf("%s/%s", home, ".local/bin"));
        if (!cmd_run(&cmd)) return 1;
    }

    return 0;
}

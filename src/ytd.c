#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "sqlite3.h"
#define FLAG_IMPLEMENTATION
#include "../thirdparty/flag.h"
#define NOB_IMPLEMENTATION
#include "../thirdparty/nob.h"

sqlite3 *open_db() {
    sqlite3 *db = NULL;
    const char *db_path = "/me/db/yt.db";
    const char *home_path = getenv("HOME");
    if (!home_path) {
        fprintf(stderr, "ytd: HOME environment variable not set\n");
        return NULL;
    }
    if (sqlite3_open(temp_sprintf("%s%s", home_path, db_path), &db) != SQLITE_OK) {
        fprintf(stderr, "ytd: can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }
    return db;
}

const char *get_url_for_platform(const char *platform)
{
    if (strcmp(platform, "youtube") == 0) {
        return "https://youtu.be";
    } else if (strcmp(platform, "twitch") == 0) {
        return "https://www.twitch.tv/videos";
    }
    return NULL;
}

bool get_latest_videos(int argc, char **argv, uint64_t latest, const char *platform)
{
    if (platform && strcmp(platform, "youtube") != 0) {
        fprintf(stderr, "ytd: %s is not a valid option for latest videos\n", platform);
        return false;
    }

    argc = flag_rest_argc();
    argv = flag_rest_argv();

    if (argc != 1) {
        fprintf(stderr, "ytd: passed %d args; must only pass 1 arg", argc);
        return false;
    }

    Cmd cmd = {0};
    Chain chain = {0};
    if (!chain_begin(&chain)) return false;
    {
        cmd_append(&cmd, "yt-dlp");
        cmd_append(&cmd, "--playlist-end", temp_sprintf("%" PRIu64, latest));
        cmd_append(&cmd, "--flat-playlist");
        cmd_append(&cmd, "--extractor-args", "youtubetab:approximate_date");
        cmd_append(&cmd, "--ignore-errors");
        cmd_append(&cmd, "-j", temp_sprintf("https://youtube.com/%s", argv[0]));
        if (!chain_cmd(&chain, &cmd)) return false;

        cmd_append(&cmd, "jq");
        cmd_append(&cmd, "-r", "select(.url | contains(\"/shorts/\") | not) | \"[\\(.upload_date)] [\\(.id)] \\(.title)\"");
        if (!chain_cmd(&chain, &cmd)) return false;
    }
    if (!chain_end(&chain)) return false;
    return true;
}

bool open_video(const char *open, const char *platform)
{
    Cmd cmd = {0};
    const char *url = get_url_for_platform(platform);
    if (!url) {
        fprintf(stderr, "ytd: platform %s not supported\n", platform);
        return false;
    }
    cmd_append(&cmd, "firefox", temp_sprintf("%s/%s", url, open));
    if(!cmd_run(&cmd)) return false;
    return true;
}

bool download_video(sqlite3 *db, const char *download, const char *platform)
{
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, "select exists(select 1 from video where video_id = ?)", -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ytd: can't prepare statement: %s\n", sqlite3_errmsg(db));
        return false;
    }

    rc = sqlite3_bind_text(stmt, 1, download, -1, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ytd: can't bind to statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "ytd: can't read video_id: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }

    int video_exists = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (video_exists) {
        fprintf(stdout, "ytd: video with id %s already exists. Not downloading.\n", download);
        return true;
    }

    const char *url = get_url_for_platform(platform);
    if (!url) {
        fprintf(stderr, "ytd: platform %s not supported\n", platform);
        return false;
    }

    Cmd cmd = {0};
    cmd_append(&cmd, "yt-dlp");
    cmd_append(&cmd, "-f", "bestvideo[ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4]/best");
    cmd_append(&cmd, "--merge-output-format", "mp4");
    cmd_append(&cmd, "--embed-thumbnail");
    cmd_append(&cmd, "--no-mtime");
    cmd_append(&cmd, "-o", "$HOME/Videos/%(upload_date>%Y-%m-%d)s - %(title)s.%(ext)s");
    cmd_append(&cmd, temp_sprintf("%s/%s", url, download));
    if (!cmd_run(&cmd)) return false;

    rc = sqlite3_prepare_v2(db, "insert into video (video_id,platform) values (?,?)", -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ytd: can't prepare statement: %s\n", sqlite3_errmsg(db));
        return false;
    }

    rc = sqlite3_bind_text(stmt, 1, download, -1, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ytd: can't bind to statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }

    rc = sqlite3_bind_text(stmt, 2, platform, -1, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ytd: can't bind to statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "ytd: can't insert video_id into video: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

    return true;
}

bool apply_migrations(sqlite3 *db)
{
    const char *migrations[] = {
        // 0001
        "CREATE TABLE IF NOT EXISTS video ("
        "    id            INTEGER PRIMARY KEY,"
        "    video_id      TEXT NOT NULL,"
        "    downloaded_on TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");",
        // 0002
        "ALTER TABLE video ADD COLUMN platform TEXT;"
    };

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ytd: can't prepare user_version: %s\n", sqlite3_errmsg(db));
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "ytd: can't read user_version: %s\n", sqlite3_errmsg(db));
        return false;
    }

    int current_version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    for (int i = current_version; i < (int)ARRAY_LEN(migrations); i++) {
        sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

        char *zErrMsg = NULL;
        rc = sqlite3_exec(db, migrations[i], NULL, 0, &zErrMsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "ytd: SQL error when applying migration %04d: %s\n", i + 1, zErrMsg);
            sqlite3_free(zErrMsg);
            return false;
        }

        char *sql = sqlite3_mprintf("PRAGMA user_version = %d", i + 1);
        rc = sqlite3_exec(db, sql, NULL, 0, &zErrMsg);
        sqlite3_free(sql);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "ytd: SQL error when setting user_version %d: %s\n", i + 1, zErrMsg);
            sqlite3_free(zErrMsg);
            return false;
        }

        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    }

    return true;
}

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
    char     **platform = flag_str("p", "youtube", "Pass the platform (youtube or twitch). Get latest only supports youtube.");
    char     **download = flag_str("d", NULL, "Pass the video ID to download");
    char     **open     = flag_str("o", NULL, "Pass the video ID to open");
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

    sqlite3 *db = open_db();
    if (!db) return 1;

    if (!apply_migrations(db)) {
        sqlite3_close(db);
        return 1;
    }

    if (*latest) {
        if (!get_latest_videos(argc, argv, *latest, *platform)) {
            sqlite3_close(db);
            return 1;
        }
    } else if (*open) {
        if (!open_video(*open, *platform)) {
            sqlite3_close(db);
            return 1;
        }
    } else if (*download) {
        if (!download_video(db, *download, *platform)) {
            sqlite3_close(db);
            return 1;
        }
    }

    sqlite3_close(db);
    return 0;
}

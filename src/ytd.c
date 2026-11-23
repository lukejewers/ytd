#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sqlite3.h"
#define FLAG_IMPLEMENTATION
#include "../thirdparty/flag.h"
#define NOB_IMPLEMENTATION
#include "../thirdparty/nob.h"

typedef struct {
    char *title;
    char *uploader_id;
} VideoMetadata;

sqlite3 *open_db(void)
{
    sqlite3 *db = NULL;
    const char *db_path = "/me/db/yt.db";
    const char *home_path = getenv("HOME");
    if (!home_path) {
        fprintf(stderr, "[ytd] HOME environment variable not set\n");
        return NULL;
    }
    if (sqlite3_open(temp_sprintf("%s%s", home_path, db_path), &db) != SQLITE_OK) {
        fprintf(stderr, "[ytd] can't open database: %s\n", sqlite3_errmsg(db));
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

bool ytdlp_latest_videos(const char *uploader_id, uint64_t latest, const char *platform)
{
    if (platform && strcmp(platform, "youtube") != 0) {
        fprintf(stderr, "[ytd] %s is not a valid option for latest videos\n", platform);
        return false;
    }

    Cmd cmd = {0};
    Chain chain = {0};
    bool ok = false;
    if (!chain_begin(&chain)) goto cleanup;
    {
        cmd_append(&cmd, "yt-dlp");
        cmd_append(&cmd, "--playlist-end", temp_sprintf("%" PRIu64, latest));
        cmd_append(&cmd, "--flat-playlist");
        cmd_append(&cmd, "--extractor-args", "youtubetab:approximate_date");
        cmd_append(&cmd, "--ignore-errors");
        cmd_append(&cmd, "-j", temp_sprintf("https://youtube.com/%s", uploader_id));
        if (!chain_cmd(&chain, &cmd)) goto cleanup;

        cmd_append(&cmd, "jq");
        cmd_append(&cmd, "-r",
                   "select(.url | contains(\"/shorts/\") | not) "
                   "| select((.live_status // \"not_live\") == \"not_live\") "
                   "| \"[\\(.upload_date)] [\\(.id)] \\(.title)\"");
        if (!chain_cmd(&chain, &cmd)) goto cleanup;
    }
    if (!chain_end(&chain)) goto cleanup;
    ok = true;

 cleanup:
    free(cmd.items);
    free(chain.cmd.items);
    return ok;
}

bool open_video(const char *open, const char *platform)
{
    Cmd cmd = {0};
    const char *url = get_url_for_platform(platform);
    if (!url) {
        fprintf(stderr, "[ytd] platform %s not supported\n", platform);
        return false;
    }
    cmd_append(&cmd, "firefox", temp_sprintf("%s/%s", url, open));
    bool ok = cmd_run(&cmd);
    free(cmd.items);
    return ok;
}

bool video_exists(sqlite3 *db, const char *video_id)
{
    bool video_exists = false;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "select exists(select 1 from video where video_id = ?)", -1, &stmt, 0) != SQLITE_OK) {
        fprintf(stderr, "[ytd] can't prepare statement: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    if (sqlite3_bind_text(stmt, 1, video_id, -1, NULL) != SQLITE_OK) {
        fprintf(stderr, "[ytd] can't bind to statement: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        fprintf(stderr, "[ytd] can't read video_id: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    video_exists = sqlite3_column_int(stmt, 0);

 cleanup:
    sqlite3_finalize(stmt);
    return video_exists;
}

bool ytdlp_download(const char *platform, const char *video_id, bool audio_only, VideoMetadata *out)
{
    const char *url = get_url_for_platform(platform);
    if (!url) {
        fprintf(stderr, "[ytd] platform %s not supported\n", platform);
        return false;
    }

    char tmp_filename[] = "/tmp/outputXXXXXX";
    int fd = mkstemp(tmp_filename);
    if (fd < 0) {
        perror("[ytd] mkstemp");
        return false;
    }

    const char delimiter[] = "__delimiter__";
    size_t delimiter_len = strlen(delimiter);

    bool ok = false;
    Cmd cmd = {0};
    cmd_append(&cmd, "yt-dlp");
    if (audio_only) {
        cmd_append(&cmd, "-f", "bestaudio/best");
        cmd_append(&cmd, "-x", "--audio-format", "mp3", "--audio-quality", "0");
    } else {
        cmd_append(&cmd, "-f", "bestvideo[ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4]/best");
        cmd_append(&cmd, "--merge-output-format", "mp4");
    }
    cmd_append(&cmd, "--embed-thumbnail");
    cmd_append(&cmd, "--no-mtime");
    cmd_append(&cmd, "-o", "$HOME/Videos/%(upload_date>%Y-%m-%d)s - %(title)s.%(ext)s");
    cmd_append(&cmd, temp_sprintf("%s/%s", url, video_id));
    cmd_append(&cmd, "--print-to-file", temp_sprintf("%%(uploader_id)s%s%%(title)s", delimiter), tmp_filename);
    if (!cmd_run(&cmd)) {
        fprintf(stderr, "[ytd] yt-dlp failed\n");
        goto cleanup;
    }

    char str[4096];
    char c;
    size_t idx = 0;
    lseek(fd, 0, SEEK_SET);
    while (idx < sizeof(str) - 1 && read(fd, &c, 1) > 0) str[idx++] = c;
    str[idx] = '\0';

    const char *p = strstr(str, delimiter);
    if (!p) {
        fprintf(stderr, "[ytd] can't get video metadata\n");
        goto cleanup;
    }

    size_t before = p - str;
    size_t title_len = idx - before - delimiter_len;
    if (title_len > 0 && str[idx-1] == '\n') title_len--;

    out->title = strndup(p + delimiter_len, title_len);
    out->uploader_id = strndup(str, before);
    if (!out->title || !out->uploader_id) {
        fprintf(stderr, "[ytd] out of memory\n");
        goto cleanup;
    }
    ok = true;

 cleanup:
    close(fd);
    delete_file(tmp_filename);
    free(cmd.items);
    return ok;
}

bool download_video(sqlite3 *db, const char *video_id, const char *platform, bool audio_only)
{
    if (video_exists(db, video_id)) {
        fprintf(stdout, "[ytd] video with id %s already exists. Not downloading video again.\n", video_id);
        return true;
    }

    sqlite3_stmt *stmt = NULL;
    const char *insert_into = "insert into video (video_id,platform,title,uploader_id,media_type) values (?,?,?,?,?)";
    if (sqlite3_prepare_v2(db, insert_into, -1, &stmt, 0) != SQLITE_OK) {
        fprintf(stderr, "[ytd] can't prepare statement: %s\n", sqlite3_errmsg(db));
        return false;
    }

    bool ok = false;
    VideoMetadata metadata = {0};
    if (!ytdlp_download(platform, video_id, audio_only, &metadata)) goto cleanup;

    int rc = sqlite3_bind_text(stmt, 1, video_id, -1, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, platform, -1, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 3, metadata.title, -1, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, metadata.uploader_id, -1, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 5, audio_only ? "audio" : "video", -1, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[ytd] can't bind to statement: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "[ytd] can't insert video_id into video: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }
    ok = true;

 cleanup:
    free(metadata.title);
    free(metadata.uploader_id);
    sqlite3_finalize(stmt);
    return ok;
}

bool get_user_version(sqlite3 *db, int *version)
{
    bool ok = false;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[ytd] can't prepare user_version: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        fprintf(stderr, "[ytd] can't read user_version: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }
    *version = sqlite3_column_int(stmt, 0);
    ok = true;
 cleanup:
    sqlite3_finalize(stmt);
    return ok;
}

bool apply_migrations(sqlite3 *db)
{
    const char *migrations[] = {
        // 0001
        "CREATE TABLE IF NOT EXISTS video ("
        "    id            INTEGER PRIMARY KEY,"
        "    video_id      TEXT NOT NULL,"
        "    downloaded_on TEXT DEFAULT CURRENT_TIMESTAMP"
        ") STRICT;",
        // 0002
        "ALTER TABLE video ADD COLUMN platform TEXT;",
        // 0003
        "ALTER TABLE video ADD COLUMN title TEXT;",
        // 0004
        "ALTER TABLE video ADD COLUMN uploader_id TEXT;",
        // 0005
        "ALTER TABLE video ADD COLUMN media_type TEXT;"
    };

    int current_version;
    if (!get_user_version(db, &current_version)) return false;

    bool ok = false;
    for (int i = current_version; i < (int)ARRAY_LEN(migrations); i++) {
        if (sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "[ytd] can't begin transaction: %s\n", sqlite3_errmsg(db));
            goto out;
        }

        if (sqlite3_exec(db, migrations[i], NULL, 0, NULL) != SQLITE_OK) {
            fprintf(stderr, "[ytd] SQL error when applying migration %04d: %s\n", i + 1, sqlite3_errmsg(db));
            goto rollback;
        }

        char *sql = sqlite3_mprintf("PRAGMA user_version = %d", i + 1);
        int rc = sqlite3_exec(db, sql, NULL, 0, NULL);
        sqlite3_free(sql);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "[ytd] SQL error when setting user_version %04d: %s\n", i + 1, sqlite3_errmsg(db));
            goto rollback;
        }

        if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "[ytd] can't commit migration %04d: %s\n", i + 1, sqlite3_errmsg(db));
            goto rollback;
        }
    }
    ok = true;
    goto out;

 rollback:
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
 out:
    return ok;
}

void usage(FILE *stream)
{
    fprintf(stream, "Usage: ytd [OPTIONS] [--] [ARGS]\n");
    fprintf(stream, "Options:\n");
    flag_print_options(stream);
}

int main(int argc, char **argv)
{
    bool      *help           = flag_bool  ("h",     false,     "Print this help to stdout and exit with 0");
    bool      *debug          = flag_bool  ("debug", false,     "Print the debug logs");
    char     **platform       = flag_str   ("p",     "youtube", "Pass the platform (youtube or twitch). Get latest only supports youtube.");
    char     **download       = flag_str   ("d",     NULL,      "Pass the video ID to download");
    char     **audio          = flag_str   ("a",     NULL,      "Pass the video ID to download the audio");
    Flag_List *download_list  = flag_list  ("dl",               "Pass the list of video IDs to download");
    char     **open           = flag_str   ("o",     NULL,      "Pass the video ID to open");
    uint64_t  *latest         = flag_uint64("l",     0,         "Pass the number of latest videos for channel");

    if (!flag_parse(argc, argv)) {
        usage(stderr);
        flag_print_error(stderr);
        return EXIT_FAILURE;
    }

    if (*help) {
        usage(stdout);
        return EXIT_SUCCESS;
    }

    if (!*debug) nob_set_log_handler(&nob_null_log_handler);

    sqlite3 *db = open_db();
    if (!db) return EXIT_FAILURE;

    int status = EXIT_SUCCESS;
    if (!apply_migrations(db)) {
        status = EXIT_FAILURE;
        goto out;
    }

    if (*latest) {
        int rest_argc = flag_rest_argc();
        char **rest_argv = flag_rest_argv();
        if (rest_argc != 1) {
            fprintf(stderr, "[ytd] -l needs exactly 1 uploader id, got %d\n", rest_argc);
            status = EXIT_FAILURE;
            goto out;
        }
        if (!ytdlp_latest_videos(rest_argv[0], *latest, *platform)) {
            status = EXIT_FAILURE;
            goto out;
        }
    } else if (*open) {
        if (!open_video(*open, *platform)) {
            status = EXIT_FAILURE;
            goto out;
        }
    } else if (*download) {
        bool audio_only = false;
        if (!download_video(db, *download, *platform, audio_only)) {
            status = EXIT_FAILURE;
            goto out;
        }
    } else if (*audio) {
        bool audio_only = true;
        if (!download_video(db, *audio, *platform, audio_only)) {
            status = EXIT_FAILURE;
            goto out;
        }
    } else if (download_list->count > 0) {
        bool audio_only = false;
        for (size_t i = 0; i < download_list->count; i++) {
            if (!download_video(db, download_list->items[i], *platform, audio_only)) {
                status = EXIT_FAILURE;
                goto out;
            }
        }
    }

 out:
    sqlite3_close(db);
    return status;
}

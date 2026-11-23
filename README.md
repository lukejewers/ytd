# ytd

A small command-line wrapper around [yt-dlp](https://github.com/yt-dlp/yt-dlp) for downloading YouTube and Twitch videos. It keeps a SQLite record of what has been downloaded, so videos you already have are skipped.

## Requirements

- A C compiler and the SQLite 3 development library
- [yt-dlp](https://github.com/yt-dlp/yt-dlp)
- [jq](https://jqlang.org/) (for `-l`)
- Firefox (for `-o`)

## Quick Start

```console
$ gcc -o nob nob.c
$ ./nob && build/ytd -h
```

`./nob` rebuilds itself when `nob.c` changes, so you only need the `gcc` step once.

To install to `~/.local/bin`:

```console
$ ./nob -install
```

## Usage

```console
$ ytd -d VIDEO_ID                # download a video (mp4)
$ ytd -a VIDEO_ID                # download audio only (mp3)
$ ytd -dl ID1 -dl ID2 -dl ID3    # download several videos
$ ytd -l 10 @channel             # list a channel's 10 latest videos
$ ytd -o VIDEO_ID                # open a video in Firefox
$ ytd -p twitch -d VIDEO_ID      # use Twitch instead of YouTube
```

`-l` skips Shorts and livestreams, and only supports YouTube.

## Files

| Path | What |
|---|---|
| `~/Videos/` | Downloads, named `YYYY-MM-DD - Title.ext` |
| `~/me/db/yt.db` | Database of downloaded videos (the `~/me/db/` directory must already exist) |

The database schema is migrated automatically on startup.

## Development

To build with AddressSanitizer and UndefinedBehaviorSanitizer, set `asan = true` in `nob.c` and run `./nob`. Keep it `false` for installed builds.

To build and run in one step:

```console
$ ./nob -run -d VIDEO_ID
```

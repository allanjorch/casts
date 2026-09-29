# Podcasts

A shelf of shows. Close the window and the episode keeps playing.

Paste a feed, or bring a whole library over with an OPML export from Podcast Addict. Covers sit in a grid. Anything still unheard stays in front, including an old backlog. The player stays up for the Omarchy bar after the window is gone.

The command is `podcast`. The launcher name is Podcasts. This repo is [casts](https://github.com/allanjorch/casts).

Designed together by [Allan Kristensen](https://github.com/allanjorch) and [Grok](https://x.ai).

## Try it

**Add feed** takes one feed address. **Import OPML** takes the file Podcast Addict exports. **Refresh** asks the feeds for new episodes.

Click a cover to open the show, then click an episode to play it. On the shelf, the icons are add feed, import OPML, refresh, and mark. Mark follows the episode that is loaded. The bar has back 15 seconds, play and pause, and forward 30 seconds. The rate there cycles the speed. Space pauses and resumes. Escape steps back. Ctrl+Q closes the window and leaves the audio running.

Open Podcasts again and the same window comes back. Stop playback with nothing left loaded, and the player exits.

## Mark played

On an episode, **Mark** (or a right-click) offers:

- **Mark as played**, and **Mark as unplayed** for that episode.
- **Mark older as played** for episodes of this show published before it. The one you chose stays as it is.
- **Mark newer as played** for episodes published after it.
- **Mark all as played** for the whole show. This one asks first.

The same menu is on the bar for the episode that is loaded. Marking does not stop playback. The status line reports how many episodes changed.

An episode also counts as heard in the last few percent, so a trailing jingle does not pin the show to the front. One you have started and not finished still counts as unheard.

## Shelf

A show with any unheard episode is in the front group, ordered by its newest unheard episode. Shows you are caught up on follow, ordered by their newest episode. The badge on a cover is the unheard count.

## Build

Qt 6, with Quick, Quick Controls, Multimedia, SQL (SQLite), D-Bus, and Wayland. On Omarchy those libraries are already installed. `cmake` is not required.

```
qmake6
make -j
./podcast --self-test
make install    # ~/.local/bin/podcast and a Podcasts launcher entry
```

`make install` needs no root. The desktop entry runs `~/.local/bin/podcast`.

Colors come from `~/.local/state/omarchy/current/theme/colors.toml`, so a theme switch retints the window. Text size follows the desktop text scale.

This version streams and remembers where you stopped. The library is `~/.local/share/allanjorch/podcast/library.db`.

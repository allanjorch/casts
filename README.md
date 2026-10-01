# Podcasts

A shelf of shows. Close the window and the episode keeps playing.

Paste a feed, or bring a whole library over with an OPML export from Podcast Addict. Covers sit in a gallery or a list. Anything still unheard stays in front, including an old backlog. The player stays up for the Omarchy bar after the window is gone.

The command is `podcast`. The launcher name is Podcasts. This repo is [casts](https://github.com/allanjorch/casts).

Designed together by [Allan Kristensen](https://github.com/allanjorch) and [Grok](https://x.ai).

## Features

**Shelf.** Shows with unheard episodes sit in front, ordered by newest unheard. Caught-up shows follow. The badge is the unheard count; caught-up covers sit back. Switch gallery and list from the header. Ctrl+wheel (or Ctrl+= / Ctrl+−) zooms covers.

**Show.** Open a cover for the episode list. Play from the art on each row. Left-click a row for details. Right-click (or Mark) for played, unplayed, older, newer, or the whole show. Refresh that feed, or remove the show (history goes with it).

**Episode details.** Full description and art when the feed has them, plus date, duration, and progress. Play and Mark from the header.

**Now Playing.** Tap the bar cover or the expand control. Large cover, titles, transport, scrubber, and speed. Short windows collapse to a compact layout; the player bar stays underneath.

**Player bar.** Back 15s, play/pause, forward 30s, scrubber, volume, speed cycle, Now Playing, and Mark. Click the title for that episode’s details (Back from there returns to the shelf). Visible whenever an episode is loaded. After a full quit, the last started episode comes back paused at its saved position.

**Volume.** App-specific via Qt Multimedia on this player’s output (0–100%). Remembered in the library. Soft gain above 100% is not available.

**Theme.** Colors and type from Omarchy (`~/.local/state/omarchy/current/theme/`). A theme switch retints the window. Without Omarchy, a built-in dark palette (or light when mode asks for it) is used. Text size follows the desktop text scale.

**Navigation.** Escape, Alt+Left, Backspace (outside text fields), or the mouse Back button steps back through Now Playing, episode, and show. Alt+Right or mouse Forward restores after Back. Space pauses and resumes. Ctrl+Q closes the window and leaves the audio running.

Open Podcasts again and the same window comes back. Stop playback with nothing left loaded, and the player exits.

## Mark played

- **Mark as played** / **Mark as unplayed** for that episode.
- **Mark older as played** for episodes of this show published before it (the one you chose stays as it is).
- **Mark newer as played** for episodes published after it.
- **Mark all as played** for the whole show (asks first).

Marking does not stop playback. The status line reports how many episodes changed. An episode also counts as heard in the last few percent, so a trailing jingle does not pin the show to the front. One you have started and not finished still counts as unheard.

## Shortcuts

| Key / mouse | Action |
| --- | --- |
| Space | Play / pause |
| Escape, Alt+Left, Backspace | Back |
| Alt+Right | Forward (after Back) |
| Mouse Back / Forward | Same as Alt+Left / Alt+Right |
| Ctrl+= / Ctrl+−, Ctrl+wheel | Zoom shelf or episode list |
| Ctrl+Q | Close window (audio keeps playing) |

## Build

Qt 6 with Quick, Quick Controls, Multimedia, SQL (SQLite), Network, and D-Bus. On Omarchy those libraries are already installed. `cmake` is not required.

```
qmake6
make -j
./podcast --self-test
make install    # ~/.local/bin/podcast and a Podcasts launcher entry
```

`make install` needs no root. The desktop entry runs `~/.local/bin/podcast`.

This version streams and remembers where you stopped. The library is `~/.local/share/allanjorch/podcast/library.db`.

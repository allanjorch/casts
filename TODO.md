# Podcast app backlog

Work **one item at a time**. Do not start the next until the current one is done and checked off. Discuss item 7 before any implementation.

## Checklist

1. [x] **Refresh-all UX** — While refresh is in progress, gray out all shows that are more than “caught-up dim.” After refresh finishes, restore each show’s dim/full-lit state from played status. If refresh fails, recover a functional shelf. Animate the refresh icon while in progress; return it to the base icon on failure.
2. [ ] **Episode detail: web links** — Underline obvious web links in episode detail; open them in the system default browser.
3. [ ] **Episode detail: timestamps** — Recognize timestamps in episode detail; a click seeks the progress bar to that time and starts playback immediately, staying on the detail page.
4. [ ] **Main Mark button** — After a confirm dialog that matches the existing design language, mark all podcasts and episodes as played.
5. [ ] **Main gallery columns** — Enforce min 1 column, max 10 columns.
6. [ ] **Artwork force-reload** — Ctrl+Left-click on the refresh button forces artwork reload (per earlier decision).
7. [ ] **Later discussion (do not implement yet)** — Full keyboard-friendly app aligned with Omarchy principles. Discuss approach first; no coding until agreed.
8. [x] **Eye-toggle filter** — Independent Main vs Episodes; open = show all, closed = unplayed only; persist preferred setting.
9. [ ] **Later discussion (do not implement yet)** — Central play/pause/seek services vs current Backend/D-Bus player. Discuss approach first; no coding until agreed.

10. [ ] **Later discussion (do not implement yet)** — Consider order of top-right header actions on Main and Episodes for better UX (discuss/rearrange).

11. [ ] **Last-refresh indicator / skip auto-refresh** — Indicate when last update/refresh ran; if within ~30 minutes of last refresh, skip automatic refresh on launch (manual refresh still allowed).


12. [ ] Compact chrome — tiny window → only controls + progress.
13. [ ] Scrubber hover/drag time tooltip (shared component; bar + Player page).

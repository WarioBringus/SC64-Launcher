# SC64 Launcher: notes for Claude Code (and anyone working on it)

SC64 Launcher is a console-style front end for the SummerCart64 flashcart, made for the Analogue 3D first (it also
runs on an original N64 with an Expansion Pak). It's a fork of N64FlashcartMenu: the original menu and file browser
are still there; the launcher (Home, Library, Quick menu, Settings, themes, music, covers) sits on top of them.
It was built with Claude Code by a non-programmer directing the look and feel, so explain changes in plain language
and show the result (photos, screenshots, the performance log) rather than code.

## What it is, and isn't

- A **library launcher**: finding and starting your games, made to feel like a console OS. It is **not** a
  replacement for the Analogue 3D's own menu: no controller tests, system settings, playtime tracking, accessory
  indicators or "recently added" rows.
- The console always boots to **Home**; the Library remembers its own view.
- The SummerCart64's firmware and configuration are never touched.

## Building (Windows, native)

Build natively on Windows with **MSYS2** (MINGW64 shell). Don't use WSL or a virtual machine.

1. Install MSYS2, then in the MINGW64 shell: `pacman -S base-devel git mingw-w64-x86_64-gcc`.
2. Get the N64 toolchain: libdragon's prebuilt Windows GCC toolchain (from the libdragon releases page,
   "toolchain-continuous-prerelease"), unpacked somewhere; set `N64_INST` to that folder and put `$N64_INST/bin` on
   `PATH`.
3. Clone with MSYS2's git and fetch the submodules (`git submodule update --init --recursive`), then build and install
   the libdragon submodule into `N64_INST`: in `libdragon/`, `make -j && make install && make tools && make tools-install`.
4. In the repo: `make all -j` builds `output/sc64menu.n64` (the file that goes on the SD card).
   `make all LAUNCHER_DEV_TOOLS=1` also builds the developer tools (see Testing).

The **build number** shown in Settings > System > Menu information is the commit count since the `V0.3.6` tag, plus
the short commit hash (a `*` means `src/` had uncommitted changes). Bug reports quote it.

Working branch: `launcher` (the default branch on GitHub). Files are **CRLF** in the working tree (`.gitattributes`):
when editing by script, keep each file's line endings.

## Where things are

| Area | Files |
|---|---|
| Home (Binder, Spotlight, Cartridge, Minimal; moving favorites) | `src/menu/views/home.c` |
| Library (Grid, Carousel, Shelf; sort, filter, jump, search pages) | `src/menu/views/library_view.c` |
| Shared launcher drawing: header, footer button hints, dialogs, Quick menu, text helpers, contrast, focus ring, launch animation | `src/menu/ui_components/launcher.c`, `src/menu/launcher_ui.h` |
| Cover loading: `covers.pak` packs, cover memory, read-ahead | `src/menu/ui_components/covers.c` |
| The game list: folders, cover index (`covers.idx`), game details, title tidying, duplicates, hidden games | `src/menu/library.c` |
| Settings page (tabs and rows) | `src/menu/views/settings_editor.c`; defaults and the ini file in `src/menu/settings.c` |
| Themes (17) | `src/menu/theme.c` |
| Fonts (Inter, display sizes, `STL_FADE` for any colour) | `src/menu/fonts.c`, `assets/fonts/`, `Makefile` font rules |
| Music and sound effects | `src/menu/sound.c` |
| Loading screen, About this game, attract/wallpaper mode | `views/load_rom.c`, `views/about.c`, `views/attract.c` |
| Performance log, screenshot tour | `src/menu/perf_log.c`, `src/menu/tour.c` |
| First boot, which screen starts | `views/startup.c`, `src/menu/menu.c` |

PC tools (Python, standard library only; Windows):

| Tool | What it does |
|---|---|
| `pc-tools/coverpack/coverpack.py` | Builds `menu/cache/launcher/covers.idx` and `covers.pak` on the SD card: every cover pre-scaled to every size the launcher draws, 4:3 and 16:9 ("_w", squeezed), box spines, hi-res covers. Uses `coverimg.exe` (`coverimg.c`, build with MSYS2 gcc + the bundled libspng/miniz; the command is in its header). |
| `pc-tools/coverpack/gamemeta.py` | Game details from libretro-database (matched by ROM CRC, then name) |
| `pc-tools/coverpack/launchbox.py` | Developer tool: spines and hi-res fronts from the LaunchBox Games Database |
| `pc-tools/release/` | The user-facing kit: `get_covers.py` + `covers_window.ps1` (the "Get covers and game info" window), `music_converter.ps1`, the `.bat` launchers, `README.txt`, and `make_fresh_install.py`, which assembles the release folder |
| `pc-tools/demo/` | A demo ROM with a small card packed inside, for showing the launcher in an emulator (ares, gopher64) |
| `pc-tools/tour/tour_png.py` | Turns screenshot-tour dumps into PNGs |

## Rules that keep it fast

The N64 draws everything; frame time is the budget (60 fps = 16.7 ms).
- **Draw covers at their packed size (1:1).** Shrinking or enlarging a cover on the N64 is slow. The sizes the
  launcher asks for must exist in the packs: `BOXES`, `UP_BOXES` and the hi-res sizes in `coverpack.py` must match the
  tile sizes in `library_view.c` and `home.c` and the `LAUNCHER_*_W/H` sizes in `launcher_ui.h`.
- **Hold still frames.** When nothing moves, the launcher keeps the last frame on screen instead of redrawing
  (`launcher_frame_held`). Don't add animation that never settles.
- **Avoid full-screen blended fills.** Large rounded or see-through fills cost milliseconds; draw outlines as edges.
- **No slow maths per frame** (`powf` and friends on the N64's CPU); cache or table it.
- Covers live in a fixed block of memory (about 2.7 MB); the Library reads ahead and Home pins its own covers.
- Spotlight runs at about 50 fps; that was accepted. Don't chase 60 fps where the difference can't be seen.

## Look and feel rules

- Read from the couch: large text, button hints drawn like the real controller's buttons.
- **Text never cut off**: wrap whole words and whole detail fields ("4 players" stays together); give titles more
  lines rather than an ellipsis. Never show "???".
- Every change must look right in **all 17 themes** and at both **16:9 and 4:3** (16:9 draws squeezed for TVs set to
  stretch). Pick text and outline colours by contrast against what's behind (`launcher_standout_color`,
  `launcher_frame_color`, the button `ink_color`).
- Consistent shapes: rounded cards, the focus ring hugging the cover, no pill-shaped tab headers.
- Smooth motion: glides and fades, no hitches; the same element stays in the same place across screens.

## Testing

- **Performance log**: add `perf_log = true` under `[launcher]` in `menu/config.ini` on the SD card; it writes
  `menu/cache/launcher/perf-log.txt` (one line per second per screen: frame time, text, covers, shapes, card reads,
  plus events). Read it after any change that could cost time.
- **Screenshot tour**: build with `LAUNCHER_DEV_TOOLS=1`; Settings > System > Screenshot tour steps through every
  theme, Home style, list combination, Library layout and screen shape and dumps each frame to `menu/tour/`;
  `pc-tools/tour/tour_png.py` converts them. Good for checking contrast and cut-off text everywhere at once.
- **Demo ROM in an emulator**: `pc-tools/demo/make_demo_card.py` then `build_demo.sh`; runs in ares (with no cartridge
  the menu reads its card from `rom:/`). Handy for checking layout without hardware; not for timing.
- Real hardware (SummerCart64 + Analogue 3D or N64) is the final check.

## Releases

1. Build from a clean, committed tree (so the build number has no `*`).
2. `python -I pc-tools/release/make_fresh_install.py --out <folder> --python-zip <python embeddable zip> --fixes <game-fixes.txt> --extras <extra art folder>`
   assembles the release folder (SD card template with empty game folders, the two `.bat` tools, a portable Python,
   `README.txt`, the licence). Zip it **without** `tools/data` (that's a user's download cache).
3. Never ship ROMs, test logs, screenshots of other people's setups, or the demo ROM (it holds box art and game headers).
4. Tag `vX.Y.Z`, create the GitHub release with the zip and notes from `pc-tools/release/github/RELEASE_NOTES.md`.

## Working style that worked

- One feature per commit; build after each; say what changed and what to look at.
- Mock up visual changes (options side by side) before building them.
- Say when unsure, and verify on the real thing (log, screenshot, hardware photo) before calling something fixed.
- Vet any new dependency (source, licence, checksum or signature) before adding it.
- Keep personal details out of the repo and the commits.

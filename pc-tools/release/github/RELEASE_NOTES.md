# SC64 Launcher v1.0.2

A polish update: nicer light and dark themes, and a few visual fixes. To update, replace `sc64menu.n64` on your SD
card with the one in the download; your settings, favorites and covers stay.

## What's new

- **Midnight and Daylight are now a matched dark and light pair.** Midnight is all greys with an amber accent (no more
  blue). Daylight is its light twin: soft off-whites and greys instead of bright white, so it no longer glares on a
  TV.
- **"Now playing" has its own spot.** When a song starts, its name takes the clock's place in the top right for a few
  seconds, with "Now playing" above it, the same on every screen. It never runs over Spotlight's slanted panel, and
  long song names wrap onto two lines instead of being cut off.
- **Cartridge style:** the coloured band now starts just below the Home / Library row, leaving the header clear.
- **Fixed:** a strip of slightly different colour along the edge of Spotlight's slanted panel (and the Binder and
  Cartridge slants), in every theme.
- **Fixed:** the line under Home / Library changed colour between screens. Each theme now has one underline colour,
  everywhere.
- **Fixed (Cartridge, 4:3):** the clock and "Now playing" could disappear from the header.

**Earlier, in 1.0.1:** sound effects and music are on by default (the menu's built-in track plays until you add your
own music).

---


A console-style front end for the SummerCart64, made for the Analogue 3D.

## What's in the download

| | |
|---|---|
| `SD card\` | The launcher (`sc64menu.n64`) and empty game folders. Copy everything in it to the top level of your SD card. |
| `Get covers and game info.bat` | Opens a window: pick your SD card, and it adds box art, game details and covers for your games. |
| `Music converter.bat` | Opens a window: drop in MP3 or WAV files and it adds them to the card as background music. |
| `tools\` | What those two use. Nothing to install. |
| `README.txt` | Setup steps, controls and credits. **Start here.** |

No games are included.

## Highlights

- Home with Resume, Recently played and Favorites, in four styles: Binder, Spotlight, Cartridge, Minimal
- Library of box art: Grid, Carousel and Shelf with real box spines; sort, filter, jump, search, surprise me
- Hi-res covers that grow into the loading screen
- 17 colour themes, 16:9 and 4:3
- Background music from your own files
- One Quick menu (Start) and a tabbed Settings page
- Covers and details found from the game code inside each ROM, so ROMs from any source and region work

## Requirements

- SummerCart64 firmware 2.17.0 or newer (tested with 2.20.2)
- Analogue 3D (tested with firmware 1.5.1), or an N64 with an Expansion Pak
- Windows 10 or 11 for the PC tools, and an internet connection for the first covers run

Recommended: Analogue 3D image scaler set to **Stretch**, and the launcher's **Settings > Look > Screen** set to
**16:9**. Any combination you're comfortable with works.

## Tips

- **Getting back to the launcher from a game**: on the Analogue 3D, press the star button and choose **Reset**
  (on an N64, press Reset). You land on Home with the game under Resume. Keep **Settings > Games > Fast reboot**
  off (the default), or Reset starts the same game again.
- **Favorites**: highlight a game, press **Start**, choose **Add to favorites**.
- **Music**: add as many songs as you like. They shuffle from a random first track, with "Now playing" showing
  each one's name; pause or skip from the Quick menu.
- **Your own tabs**: every folder of games on the card becomes a Library tab.

## Known limitations

- Home is empty until you play games or add favorites.
- A ROM with a meaningless file name (like `zelda.z64`) shows that name; give it a proper title in
  `menu/game-fixes.txt`.
- ROM hacks made only for PC emulators won't run on real hardware.
- Very large libraries reload some art while you scroll far through the Library (the N64 has limited memory for
  covers).
- The PC tools are Windows only for now.

Game information comes from community databases and may be wrong in places. Corrections are welcome in the issues,
or fork it and make it your own. Not affiliated with Nintendo, Analogue or the SummerCart64 project. See the README
for the full disclaimers and credits.

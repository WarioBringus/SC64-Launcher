SC64 LAUNCHER
=============

A console-style front end for the SummerCart64, made for the Analogue 3D first (it also runs on an
original N64 with an Expansion Pak). Your games as a Home screen and a Library of box art, instead of a
file list. Based on N64FlashcartMenu.


WHAT'S IN THIS FOLDER
---------------------
  SD card\                       Copy everything in here to the root of your SD card: the launcher
                                 and empty game folders (N64 USA, N64 Europe, N64 Japan, N64 China,
                                 N64 Hacks) for you to fill.
  Get covers and game info.bat   Double-click: box art and game details for your games.
  Music converter.bat            Double-click: turn MP3/WAV files into background music.
  tools\                         What those two use. Nothing to install; leave it next to them.


SETTING UP (about 10 minutes)
-----------------------------
1. Copy everything inside the "SD card" folder to the root of your SD card
   (sc64menu.n64 must sit at the top level of the card, next to the game folders).
2. Place your games in the matching folders on the card: N64 USA, N64 Europe, N64 Japan,
   N64 China and N64 Hacks. Each folder becomes a tab in the Library; empty ones don't show.
   (You can rename them or add your own folders too.)
3. With the SD card in your PC, double-click "Get covers and game info.bat".
   It finds your card, downloads box art and game details, and builds the covers the launcher uses.
   The first run downloads about 60 MB plus about 1 MB per game; later runs only fetch what's new.
4. (Optional) Double-click "Music converter.bat", drop in MP3 or WAV files, pick your SD card and
   press "Convert and copy to SD card". Songs play in the background, in the order you choose in Settings.
5. Eject the SD card safely, put it in your SummerCart64 and turn the console on.

Added games later? Run "Get covers and game info.bat" again.


USING IT
--------
  A                Play (or Details, if Quick launch is off)
  B                Back
  Start            Quick menu: search, surprise me, this game's options, theme, Home style, music, Settings
  L / R            Home and Library
  Z (on Home)      Move a favorite
  C buttons        Library: jump between letters or groups, change tile size

Home styles: Binder, Spotlight, Cartridge, Minimal. Library layouts: Grid, Carousel, Shelf.
17 colour themes. The original menu and file browser are under Settings > Original menu.


UPDATING
--------
Replace sc64menu.n64 on the SD card with the new one. Your settings, favorites and covers stay.


REQUIREMENTS
------------
  - SummerCart64 with firmware 2.17.0 or newer (tested with 2.20.2).
  - Analogue 3D: tested with firmware 1.5.1; it should work with that version or newer.
    Or an original N64 with an Expansion Pak (art loads more slowly there).
  - Windows 10 or 11 for the PC tools.


CREDITS
-------
  N64FlashcartMenu, the menu this is built on - github.com/Polprzewodnikowy/N64FlashcartMenu (AGPL-3.0)
  libdragon, the N64 SDK it uses - libdragon.dev (Unlicense)
  Box art and metadata - N64 Flashcart Menu metadata project, github.com/n64-tools/n64-flashcart-menu-metadata
    (Unlicense)
  Game details - libretro-database, github.com/libretro/libretro-database (CC BY-SA 4.0)
  Inter typeface - Rasmus Andersson (SIL Open Font License 1.1)
  libspng and miniz, used by the cover tool (BSD-2-Clause / MIT)
  Python - python.org (PSF License)

  Extra art in tools\extras (box spines and covers the art pack doesn't have, gathered by the author):
    box spines from the LaunchBox Games Database, gamesdb.launchbox-app.com; extra covers from
    libretro-thumbnails, github.com/libretro-thumbnails

Box art belongs to the games' publishers. The main art pack is downloaded by the tool; the extras
above come with it so every game has a spine and a cover. No games are included.

Made by a motion designer with Claude Code ("vibe coded"). Free and non-profit. The source code is on
GitHub under the AGPL-3.0, like the menu it's built on.

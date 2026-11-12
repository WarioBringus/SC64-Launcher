SC64 LAUNCHER
=============

A console-style front end for the SummerCart64, made for the Analogue 3D first (it also runs on an
original N64 with an Expansion Pak). Your games as a Home screen and a Library of box art, instead of a
file list. Based on N64FlashcartMenu.


WHAT'S IN THIS FOLDER
---------------------
  SD card\                       A template: copy everything in here to the root of your SD card (the
                                 launcher, and empty game folders: N64 USA, N64 Europe, N64 Japan,
                                 N64 China, N64 Hacks). Don't put games in this folder itself.
  Get covers and game info.bat   Double-click: opens a window that adds box art and game details for
                                 the games on your SD card.
  Music converter.bat            Double-click: turn MP3/WAV files into background music.
  tools\                         What those two use. Nothing to install; leave it next to them.


SETTING UP (about 10 minutes)
-----------------------------
Everything below happens on your actual SD card, plugged into your PC. Do the steps in this order.

1. PUT THE LAUNCHER ON THE CARD
   Plug your SD card into your PC. Open the "SD card" folder here, select everything inside it,
   and copy it to the top level (root) of your SD card.
   Your card should now look like this:
       E:\                      (your SD card's drive letter)
         sc64menu.n64
         menu\
         N64 China\
         N64 Europe\
         N64 Hacks\
         N64 Japan\
         N64 USA\

2. PUT YOUR GAMES ON THE CARD
   Copy your games into the matching folders ON THE SD CARD (for example E:\N64 USA).
   Each folder becomes a tab in the Library; empty ones don't show. You can rename them or add your
   own folders too. Keep any "saves" folders you already have.

3. GET COVERS AND GAME INFO
   Leave the SD card plugged in and double-click "Get covers and game info.bat" (in this folder).
   A window opens:
     - Choose your SD card from the list (a drive with the launcher on it is marked), or click
       Browse and pick the top level of the card. The window checks it's really the card, shows how
       many games it found in each folder, and lists exactly where everything will be saved.
     - Press "Get covers and game info" and wait for "Done!" (a few minutes the first time).
   The first run copies the complete box-art library to the card (about 2,200 small images, ~60 MB),
   so games you add later already have art; then it downloads large covers for your own games only
   (about 1 MB each). Later runs only fetch what's new.

4. MUSIC (OPTIONAL)
   Double-click "Music converter.bat", drop in MP3 or WAV files, pick your SD card and press
   "Convert and copy to SD card". Songs play in the background; the order is set in Settings.

5. PLAY
   Eject the SD card safely, put it in your SummerCart64 and turn the console on.
   The first time, Home is empty: it shows the games you've played (Resume and Recently played) and
   your favorites, so it fills up as you play. Your games are all in the Library (press R).
   To add a favorite: highlight a game, press Start, and choose "Add to favorites".

ADDED OR REMOVED GAMES?
   Put the card back in your PC, open "Get covers and game info.bat" again and press the button.
   New games won't show their covers and details until you do.

Your file names don't matter. Covers and details are found from the game's own code inside the ROM, so
.z64, .v64 and .n64 files from any source and region all work, however they're named. Titles on screen
come from the file name, tidied: "Legend of Zelda, The - Ocarina of Time (USA) (Rev 2).z64" shows as
"The Legend of Zelda: Ocarina of Time". To change a title (or fix a name like "zelda.z64"), add it to
menu/game-fixes.txt on the SD card, for example:
    [zelda.z64]
    title = The Legend of Zelda: Majora's Mask
ROM hacks show their file name and borrow the original game's cover and details.

ROM HACKS: ONLY ONES MADE FOR REAL HARDWARE
   Put only ROM hacks that are made to run on a real N64 (or say they're console compatible) on
   the card. Many hacks are made for PC emulators only: they won't run on a SummerCart64 or an
   Analogue 3D, however the launcher lists them, so please don't use or report those. Check the
   hack's page for "console" or "real hardware" support before adding it.


RECOMMENDED DISPLAY SETTINGS
----------------------------
The launcher looks its best filling a widescreen TV:
  - On the Analogue 3D: set the image scaler to Stretch (instead of Integer or Integer+).
  - In the launcher: Start > Settings > Look > Screen: 16:9 (it starts at 4:3).
That's only a recommendation. Every screen is laid out for both 16:9 and 4:3, so any combination
you're comfortable with is fine: if you prefer Integer scaling, keep the launcher at 4:3.


USING IT
--------
  A                Play (turn off Quick launch in Settings to see a game's details first)
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

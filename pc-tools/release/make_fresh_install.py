"""Assemble the fresh-install folder: what a new user needs, with nothing to install.

    python -I make_fresh_install.py --out "G:/Emulators & Roms/Emulators/_FRESH INSTALL_" \
        --python-zip C:/n64/downloads/python-3.14.10/python-3.14.10-embed-amd64.zip

  SD card/                      sc64menu.n64 (the current build), game-fixes.txt, and empty game folders
                                (N64 China, N64 Europe, N64 Hacks, N64 Japan, N64 USA)
  Get covers and game info.bat  runs tools/scripts/get_covers.py with tools/python
  Music converter.bat           opens tools/scripts/music_converter.ps1
  tools/python                  the official portable Python (embeddable package)
  tools/bin                     coverimg.exe (pc-tools/coverpack/coverimg.c) and libdragon's audioconv64.exe
  tools/scripts                 get_covers.py, coverpack.py, gamemeta.py, music_converter.ps1
  README.txt, LICENSE.md
"""
import argparse, os, shutil, zipfile

GAME_FOLDERS = ('N64 China', 'N64 Europe', 'N64 Hacks', 'N64 Japan', 'N64 USA')
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--python-zip', required=True)
    ap.add_argument('--coverimg', default='C:/n64/tools/coverimg.exe')
    ap.add_argument('--audioconv', default='C:/n64/toolchain/bin/audioconv64.exe')
    ap.add_argument('--menu', default=os.path.join(REPO, 'output', 'sc64menu.n64'))
    ap.add_argument('--fixes', default=None, help='a game-fixes.txt to include on the card')
    ap.add_argument('--extras', default=None,
                    help='a folder with metadata/... art the pack lacks (spines, extra covers), copied to tools/extras')
    args = ap.parse_args()
    out = args.out
    os.makedirs(out, exist_ok=True)
    keep = os.path.join(out, 'tools', 'data')     # A previous run's downloads, if any, stay.

    for name in ('SD card', 'tools/python', 'tools/bin', 'tools/scripts', 'tools/extras'):
        path = os.path.join(out, name)
        if os.path.isdir(path):
            shutil.rmtree(path)
        os.makedirs(path)

    card = os.path.join(out, 'SD card')
    shutil.copy2(args.menu, card)
    # The game folders, ready to fill (each becomes a tab in the Library).
    for folder in GAME_FOLDERS:
        os.makedirs(os.path.join(card, folder))
    if args.fixes:
        os.makedirs(os.path.join(card, 'menu'), exist_ok=True)
        shutil.copy2(args.fixes, os.path.join(card, 'menu', 'game-fixes.txt'))

    if args.extras:
        shutil.copytree(os.path.join(args.extras, 'metadata'), os.path.join(out, 'tools', 'extras', 'metadata'))

    with zipfile.ZipFile(args.python_zip) as z:
        z.extractall(os.path.join(out, 'tools', 'python'))
    shutil.copy2(args.coverimg, os.path.join(out, 'tools', 'bin', 'coverimg.exe'))
    shutil.copy2(args.audioconv, os.path.join(out, 'tools', 'bin', 'audioconv64.exe'))
    scripts = os.path.join(out, 'tools', 'scripts')
    for src in (os.path.join(HERE, 'get_covers.py'), os.path.join(HERE, 'music_converter.ps1'),
                os.path.join(REPO, 'pc-tools', 'coverpack', 'coverpack.py'),
                os.path.join(REPO, 'pc-tools', 'coverpack', 'gamemeta.py')):
        shutil.copy2(src, scripts)

    for name in ('Get covers and game info.bat', 'Music converter.bat', 'README.txt'):
        shutil.copy2(os.path.join(HERE, name), out)
    shutil.copy2(os.path.join(REPO, 'LICENSE.md'), out)

    files = sum(len(fs) for _, _, fs in os.walk(out))
    size = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(out) for f in fs)
    print('Fresh install: %d files, %.1f MB in %s%s' % (files, size / 1e6, out,
          ' (kept tools/data from before)' if os.path.isdir(keep) else ''))


if __name__ == '__main__':
    main()

"""Demo ROM: assemble the small card that's packed inside the ROM, for showing the launcher in an emulator.

    python -I make_demo_card.py --card C:/n64/demo-card --out C:/n64/demo-rom-fs [--music "<track>.wav64"]

--card is a staging card already run through the cover tool (4:3 packs are enough for an emulator window):
    python -I ../coverpack/coverpack.py --card C:/n64/demo-card --narrow-only \
        --thumb-cache C:/n64/cache/demo-thumbs --crc-cache C:/n64/cache/demo_rom_crc.json

--out gets the ROM's own files (the repo's filesystem folder: fonts, sounds) plus the card: the cover index and
pack, game details for the covers it uses, the first 4 KB of each game (its header: enough for the launcher to
list and describe it, not to play it), a config, a history and favorites, and optionally a music track.

With no cartridge (an emulator), the launcher reads its card from rom:/, these files. They are read-only: settings
changed in the demo work until it's switched off, but aren't saved.
"""
import argparse, os, shutil

ROM_EXTENSIONS = ('.z64', '.n64', '.v64')
HEADER_BYTES = 4096

CONFIG = """[menu]
first_run = false
theme = Castle Sky

[launcher]
enabled = true
home_style = 0
widescreen = false
perf_log = false
quick_launch = false
"""
# quick_launch off: the demo holds only each game's header, so A opens the game's Details instead of trying to start it.

# Shown on Home: the last played (Resume first) and favorites. Paths relative to the card.
HISTORY = ['N64 USA/Super Mario 64.n64', 'N64 USA/Mario Kart 64.n64', 'N64 USA/Super Smash Bros..n64',
           'N64 USA/Star Fox 64.n64', 'N64 USA/Banjo-Kazooie.n64', 'N64 USA/007 - GoldenEye.n64',
           'N64 USA/Paper Mario.n64', 'N64 USA/F-Zero X.n64']
FAVORITES = ['N64 USA/The Legend of Zelda - Ocarina of Time.n64', 'N64 Hacks/DK64 QoL Swap Barrel.z64',
             'N64 USA/Kirby 64 - The Crystal Shards.n64', 'N64 USA/Mario Party 2.n64',
             'N64 USA/Wave Race 64.n64', 'N64 USA/Conker\'s Bad Fur Day.n64']


def bookkeeping(card):
    lines = []
    for group, entries in (('history', HISTORY), ('favorite', FAVORITES)):
        lines.append('[%s]' % group)
        i = 0
        for rel in entries:
            if not os.path.exists(os.path.join(card, rel)):
                print('  (not on the demo card, left out: %s)' % rel)
                continue
            lines += ['%d_primary_path = rom:/%s' % (i, rel), '%d_secondary_path = ' % i, '%d_type = 1' % i]
            i += 1
        lines.append('')
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--card', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--repo', default=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..')))
    ap.add_argument('--music', default=None, help='a .wav64 track (from the card\'s menu/music) to include')
    args = ap.parse_args()
    card, out = args.card, args.out

    if os.path.isdir(out):
        shutil.rmtree(out)
    shutil.copytree(os.path.join(args.repo, 'filesystem'), out)
    menu = os.path.join(out, 'menu')

    # The cover index and pack.
    launcher = os.path.join(menu, 'cache', 'launcher')
    os.makedirs(launcher, exist_ok=True)
    for f in ('covers.idx', 'covers.pak', 'covers.manifest'):
        shutil.copy2(os.path.join(card, 'menu', 'cache', 'launcher', f), launcher)

    # Game details for each cover's folder (and its parent: metadata/C/I/D/R and metadata/C/I/D).
    folders = set()
    for line in open(os.path.join(launcher, 'covers.idx'), encoding='utf-8'):
        if line.startswith('C\t'):
            d = os.path.dirname(line.rstrip('\n').split('\t')[1])
            folders.add(d)
            folders.add(os.path.dirname(d))
    details = 0
    for d in sorted(folders):
        for f in ('metadata.ini', 'description.txt'):
            src = os.path.join(card, 'menu', d, f)
            if os.path.isfile(src):
                os.makedirs(os.path.join(menu, d), exist_ok=True)
                shutil.copy2(src, os.path.join(menu, d, f))
                details += 1

    # Each game's header, in its own folder, under its own name.
    games = 0
    for folder in sorted(os.listdir(card)):
        src_dir = os.path.join(card, folder)
        if folder.lower() == 'menu' or not os.path.isdir(src_dir):
            continue
        for name in sorted(os.listdir(src_dir)):
            if name.lower().endswith(ROM_EXTENSIONS):
                os.makedirs(os.path.join(out, folder), exist_ok=True)
                with open(os.path.join(src_dir, name), 'rb') as f:
                    head = f.read(HEADER_BYTES)
                with open(os.path.join(out, folder, name), 'wb') as f:
                    f.write(head)
                games += 1

    for f in ('game-fixes.txt',):
        if os.path.isfile(os.path.join(card, 'menu', f)):
            shutil.copy2(os.path.join(card, 'menu', f), menu)
    with open(os.path.join(menu, 'config.ini'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(CONFIG)
    with open(os.path.join(menu, 'history.ini'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(bookkeeping(card))
    if args.music:
        os.makedirs(os.path.join(menu, 'music'), exist_ok=True)
        shutil.copy2(args.music, os.path.join(menu, 'music'))

    total = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(out) for f in fs)
    print('Demo card: %d games, %d detail files, %.1f MB in %s' % (games, details, total / 1e6, out))


if __name__ == '__main__':
    main()

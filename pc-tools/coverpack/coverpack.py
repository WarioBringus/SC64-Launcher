"""
Build the launcher's cover index and thumbnail packs on the SD card.

    python -I coverpack.py --card I: [--tool C:/n64/tools/coverimg.exe]

Writes to <card>/menu/cache/launcher/:
  covers.idx              which cover each ROM uses (so the menu needn't read ROM headers)
  covers_<W>x<H>.pak      every cover pre-scaled to each tile size (so the menu needn't decode PNGs)

Cover matching mirrors the menu (src/menu/library.c): metadata/names/<file name>.png first (an
override), then metadata/C/I/D/R, metadata/C/I/D, the old boxart folder, and another region of the
same game.
Re-run after adding ROMs or cover art. The menu falls back to its own lookup for anything missing.

Index format (UTF-8 text, sorted byte-wise so the menu can binary search):
  LCIDX1
  C<TAB><cover path relative to menu/>        one per cover; its line order is its id
  R<TAB><ROM path relative to the card><TAB><cover id or -1>
  M<TAB><ROM path><TAB><genre><TAB><developer><TAB><year><TAB><players>   (when known, see gamemeta.py)

Pack format (big-endian): "CVP1", u32 count, u32 offset[count] (0 = none), then for each cover
"CVR1", u16 width, u16 height, RGBA5551 pixels.
"""
import argparse, os, shutil, struct, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamemeta  # noqa: E402

ROM_EXTENSIONS = ('.z64', '.n64', '.v64', '.rom')
SKIPPED_FOLDERS = {'system volume information', 'menu'}
FALLBACK_REGIONS = 'EPJUADFIS'
# Must match the tile sizes in src/menu/views/library_view.c and home.c.
BOXES = [(87, 61), (133, 94), (158, 112), (59, 84), (70, 100), (81, 115), (112, 158), (74, 52)]


def is_rom(name):
    low = name.lower()
    # Flashcart menu files are not games (same rule as src/menu/library.c).
    if name.startswith('.') or low.startswith('sc64menu') or low.startswith('os64'):
        return False
    return low.endswith(ROM_EXTENSIONS)


def list_roms(card):
    """ROMs the launcher shows: top-level files of each folder under the root, plus the root."""
    roms = []
    for entry in sorted(os.listdir(card)):
        full = os.path.join(card, entry)
        if os.path.isdir(full) and not entry.startswith('.') and entry.lower() not in SKIPPED_FOLDERS:
            for f in os.listdir(full):
                if is_rom(f) and os.path.isfile(os.path.join(full, f)):
                    roms.append(entry + '/' + f)
        elif os.path.isfile(full) and is_rom(entry):
            roms.append(entry)
    return roms


def read_header(path):
    with open(path, 'rb') as f:
        h = bytearray(f.read(0x40))
    if len(h) < 0x40:
        return None, None
    if h[:2] == b'\x80\x37':
        pass
    elif h[:2] == b'\x37\x80':
        for i in range(0, 0x40, 2):
            h[i], h[i + 1] = h[i + 1], h[i]
    elif h[:2] == b'\x40\x12':
        for i in range(0, 0x40, 4):
            h[i:i + 4] = h[i:i + 4][::-1]
    else:
        return None, None
    return bytes(h[0x3B:0x3F]).decode('latin-1'), bytes(h[0x20:0x34]).decode('latin-1')


def find_cover(menu_dir, code, raw_title, title):
    def exists(rel):
        return rel if os.path.isfile(os.path.join(menu_dir, rel)) else None

    # A cover named after the game wins: it fixes prototypes and hacks that reuse another game's code.
    named = exists('metadata/names/%s.png' % title)
    if named:
        return named

    if code and code.isascii() and code.isalnum():
        if code[1] == 'E' and code[2] == 'D':
            hit = exists('metadata/homebrew/%s/boxart_front.png' % raw_title)
            if hit:
                return hit
        else:
            for base in ('metadata', 'boxart'):
                game = '%s/%s/%s/%s' % (base, code[0], code[1], code[2])
                if not os.path.isdir(os.path.join(menu_dir, game)):
                    continue
                hit = exists('%s/%s/boxart_front.png' % (game, code[3])) or exists(game + '/boxart_front.png')
                for r in FALLBACK_REGIONS:
                    if hit:
                        break
                    if r != code[3]:
                        hit = exists('%s/%s/boxart_front.png' % (game, r))
                if hit:
                    return hit
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--card', default='I:')
    ap.add_argument('--tool', default='C:/n64/tools/coverimg.exe')
    ap.add_argument('--metadata', default='C:/n64/downloads/libretro-db',
                    help='folder with the libretro-database N64 genre/developer/releaseyear/maxusers .dat files')
    ap.add_argument('--crc-cache', default='C:/n64/cache/rom_crc.json')
    args = ap.parse_args()
    card = args.card.rstrip('/\\') + '/'
    menu_dir = card + 'menu'
    out_dir = menu_dir + '/cache/launcher'
    if not os.path.isdir(menu_dir):
        sys.exit('No menu folder on %s - is the SD card in?' % card)
    os.makedirs(out_dir, exist_ok=True)

    roms = list_roms(card)
    rom_cover = {}
    for i, rom in enumerate(roms):
        code, raw_title = read_header(card + rom)
        title = os.path.splitext(os.path.basename(rom))[0]
        rom_cover[rom] = find_cover(menu_dir, code, raw_title, title)
        if (i + 1) % 100 == 0:
            print('  matched %d / %d ROMs' % (i + 1, len(roms)))

    covers = sorted({c for c in rom_cover.values() if c}, key=lambda s: s.encode('utf-8'))
    cover_id = {c: i for i, c in enumerate(covers)}
    print('%d ROMs, %d with covers, %d distinct covers' % (len(roms), sum(1 for c in rom_cover.values() if c), len(covers)))

    # Thumbnails: the image tool takes ASCII paths, so it works on a temp copy of each cover.
    blobs = {box: [None] * len(covers) for box in BOXES}
    work = tempfile.mkdtemp(prefix='coverpack-')
    failed = 0
    try:
        for i, cover in enumerate(covers):
            src = os.path.join(work, 'in.png')
            shutil.copyfile(os.path.join(menu_dir, cover), src)
            prefix = os.path.join(work, 'c')
            r = subprocess.run([args.tool, 'raw', src, prefix] + ['%dx%d' % b for b in BOXES],
                               capture_output=True, text=True)
            if r.returncode != 0:
                failed += 1
                print('  skipped %s: %s' % (cover, r.stderr.strip()))
                continue
            for box in BOXES:
                p = '%s_%dx%d.raw' % (prefix, box[0], box[1])
                with open(p, 'rb') as f:
                    blobs[box][i] = f.read()
                os.remove(p)
            if (i + 1) % 100 == 0:
                print('  scaled %d / %d covers' % (i + 1, len(covers)))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    total = 0
    for box in BOXES:
        items = blobs[box]
        table_end = 8 + 4 * len(items)
        offsets, data, pos = [], bytearray(), table_end
        for b in items:
            if b:
                offsets.append(pos)
                data += b
                pos += len(b)
            else:
                offsets.append(0)
        path = '%s/covers_%dx%d.pak' % (out_dir, box[0], box[1])
        with open(path + '.tmp', 'wb') as f:
            f.write(b'CVP1' + struct.pack('>I', len(items)))
            f.write(struct.pack('>%dI' % len(items), *offsets))
            f.write(data)
        os.replace(path + '.tmp', path)
        total += os.path.getsize(path)

    # Game details for the library's sort options.
    meta_lines = []
    if os.path.isdir(args.metadata):
        by_crc, by_name = gamemeta.load_dats(args.metadata)
        cache = gamemeta.CrcCache(args.crc_cache)
        matched = 0
        for i, rom in enumerate(sorted(roms, key=lambda s: s.encode('utf-8'))):
            d = gamemeta.match(rom, cache.get(rom, card + rom), by_crc, by_name)
            if (i + 1) % 50 == 0:
                print('  checked %d / %d ROMs for game details' % (i + 1, len(roms)))
                cache.save()
            if d:
                matched += 1
                meta_lines.append('M\t%s\t%s\t%s\t%s\t%s' % (rom, gamemeta.clean(d.get('genre')), gamemeta.clean(d.get('developer')),
                                                         gamemeta.clean(d.get('year')), gamemeta.clean(d.get('players'))))
        cache.save()
        print('Game details found for %d / %d ROMs' % (matched, len(roms)))
    else:
        print('No game details at %s, so sorting by genre/developer/year/players will be empty.' % args.metadata)

    lines = ['LCIDX1']
    lines += ['C\t' + c for c in covers]
    for rom in sorted(roms, key=lambda s: s.encode('utf-8')):
        c = rom_cover[rom]
        lines.append('R\t%s\t%d' % (rom, cover_id[c] if c else -1))
    lines += meta_lines
    with open(out_dir + '/covers.idx.tmp', 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    os.replace(out_dir + '/covers.idx.tmp', out_dir + '/covers.idx')

    print('Done: %d covers packed (%.0f MB) into %s%s' % (len(covers) - failed, total / 1e6, out_dir,
          ', %d skipped' % failed if failed else ''))


if __name__ == '__main__':
    main()

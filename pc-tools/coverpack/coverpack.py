"""
Build the launcher's cover index and thumbnail packs on the SD card.

    python -I coverpack.py --card I: [--tool C:/n64/tools/coverimg.exe]

Writes to <card>/menu/cache/launcher/:
  covers.idx              which cover each ROM uses (so the menu needn't read ROM headers)
  covers.pak              every cover pre-scaled to each tile size (so the menu needn't decode PNGs),
                          each size also squeezed to 3/4 width for the launcher's 16:9 screen (_w)
  (covers.pak 64x210[_w]) box spines for the shelf layout, under each front cover's id, 210 tall and as
                          thick as the box: the box's top edge (boxart_top.png) turned upright, else a
                          spine scan (boxart_spine.png, e.g. from the LaunchBox Games Database), else a
                          slice of the front (30 wide). covers.idx gives each one's width (W lines).

Cover matching mirrors the menu (src/menu/library.c): metadata/names/<file name>.png first (an
override), then metadata/C/I/D/R, metadata/C/I/D, the old boxart folder, and another region of the
same game.
Re-run after adding ROMs or cover art. The menu falls back to its own lookup for anything missing.

Index format (UTF-8 text, sorted byte-wise so the menu can binary search):
  LCIDX1
  C<TAB><cover path relative to menu/>        one per cover; its line order is its id
  R<TAB><ROM path relative to the card><TAB><cover id or -1>
  M<TAB><ROM path><TAB><genre><TAB><developer><TAB><year><TAB><players><TAB><publisher>   (when known, see gamemeta.py)

covers.pak (big-endian): "CVPA", u32 pack count, a directory entry per pack (u16 w, u16 h, u8 16:9,
u8 0, u16 0, u32 table offset, u32 count), then each pack's table, u32 absolute offset[count] (0 = none),
and its covers: "CVR1", u16 width, u16 height, RGBA5551 pixels. In the 16:9 packs each cover is "CVW1", u16 stored
width, u16 height, u16 the 4:3 width it stands for, u16 0, RGBA5551 pixels.
"""
import argparse, hashlib, json, os, shutil, struct, subprocess, sys, tempfile


def png_size(path):
    with open(path, 'rb') as f:
        head = f.read(24)
    return struct.unpack('>II', head[16:24]) if head[:8] == b'\x89PNG\r\n\x1a\n' else (0, 0)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamemeta  # noqa: E402

ROM_EXTENSIONS = ('.z64', '.n64', '.v64', '.rom')
SKIPPED_FOLDERS = {'system volume information', 'menu'}
FALLBACK_REGIONS = 'EPJUADFIS'
# Must match the tile sizes in src/menu/views/library_view.c and home.c.
BOXES = [(87, 61), (133, 94), (158, 112), (59, 84), (70, 100), (81, 115), (112, 158), (74, 52)]
# The carousel's and shelf's covers (LAUNCHER_SHOWCASE_W/H in src/menu/launcher_ui.h): larger than
# any cover art, so enlarged (the tool's u boxes).
UP_BOXES = [(210, 150), (150, 210)]
# Each box twice: as is, and squeezed for 16:9 (the tool's WxHw boxes, packs covers_WxH_w.pak).
PACKS = [('%dx%d' % b, '%dx%d' % b) for b in BOXES] + [('%dx%dw' % b, '%dx%d_w' % b) for b in BOXES] + \
    [('%dx%du' % b, '%dx%d' % b) for b in UP_BOXES] + [('%dx%dwu' % b, '%dx%d_w' % b) for b in UP_BOXES]
# Spines for the shelf layout (LAUNCHER_SPINE_MAX_W x LAUNCHER_SPINE_H): 210 tall, as thick as the box.
# The top-edge art and wide spine scans are turned upright; without either, a slice of the front.
SPINE_PACKS = [('64x210ru', '64x210'), ('64x210rwu', '64x210_w')]
# Home's large covers (LAUNCHER_HIRES_W/H in src/menu/launcher_ui.h): from the hi-res front under --hires
# (launchbox.py --fronts) when there is one, else the card's art enlarged.
HIRES_PACKS = [('294x210', '294x210'), ('294x210w', '294x210_w'),
               # Wallpaper mode's covers (LAUNCHER_WALL_W/H), 1:1 at about 80% of the screen's height
               ('420x300', '420x300'), ('420x300w', '420x300_w')]
SPINE_UPRIGHT = ['64x210u', '64x210wu']
SPINE_FALLBACK = ['30x210su', '30x210swu']


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


class ThumbCache:
    """Scaled covers on the PC: one file per cover holding its blob for each box, plus a JSON index."""

    def __init__(self, folder):
        self.folder = folder
        self.index_path = os.path.join(folder, 'index.json')
        try:
            self.index = json.load(open(self.index_path, encoding='utf-8'))
        except (OSError, ValueError):
            self.index = {}

    def get(self, signature, count=None):
        name = self.index.get(signature)
        if not name:
            return None
        try:
            data = open(os.path.join(self.folder, name), 'rb').read()
        except OSError:
            return None
        blobs, pos = [], 0
        for _ in range(count if count is not None else len(PACKS)):
            if pos + 4 > len(data):
                return None
            n = struct.unpack('>I', data[pos:pos + 4])[0]
            blobs.append(data[pos + 4:pos + 4 + n])
            pos += 4 + n
        return blobs

    def put(self, signature, blobs):
        os.makedirs(self.folder, exist_ok=True)
        name = hashlib.sha1(signature.encode('utf-8')).hexdigest() + '.bin'
        with open(os.path.join(self.folder, name), 'wb') as f:
            for b in blobs:
                f.write(struct.pack('>I', len(b)) + b)
        self.index[signature] = name

    def save(self):
        os.makedirs(self.folder, exist_ok=True)
        json.dump(self.index, open(self.index_path, 'w', encoding='utf-8'))

    def prune(self, keep):
        """Forget and delete every entry but these signatures (a new tool or box list leaves the old ones unused)."""
        self.index = {s: n for s, n in self.index.items() if s in keep}
        names = set(self.index.values())
        removed = 0
        for f in os.listdir(self.folder):
            if f.endswith('.bin') and f not in names:
                os.remove(os.path.join(self.folder, f))
                removed += 1
        self.save()
        return removed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--card', default='I:')
    ap.add_argument('--tool', default='C:/n64/tools/coverimg.exe')
    ap.add_argument('--metadata', default='C:/n64/downloads/libretro-db',
                    help='folder with the libretro-database N64 genre/developer/releaseyear/maxusers .dat files')
    ap.add_argument('--crc-cache', default='C:/n64/cache/rom_crc.json')
    ap.add_argument('--thumb-cache', default='C:/n64/cache/thumbs',
                    help='folder for scaled covers kept between runs')
    ap.add_argument('--hires', default='C:/n64/covers-hires',
                    help='hi-res box fronts by cover path, from launchbox.py --fronts (optional)')
    ap.add_argument('--wide-only', action='store_true',
                    help='only the 16:9 packs (about half the space); in 4:3 the menu then decodes covers itself, slowly')
    args = ap.parse_args()
    global PACKS, SPINE_PACKS, SPINE_UPRIGHT, SPINE_FALLBACK, HIRES_PACKS
    if args.wide_only:
        PACKS = [p for p in PACKS if p[1].endswith('_w')]
        HIRES_PACKS = [p for p in HIRES_PACKS if p[1].endswith('_w')]
        SPINE_PACKS, SPINE_UPRIGHT, SPINE_FALLBACK = SPINE_PACKS[1:], SPINE_UPRIGHT[1:], SPINE_FALLBACK[1:]
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

    covers = {c for c in rom_cover.values() if c}
    # Cartridge label art beside each box (for the launcher's Cover style: Cartridge).
    for c in list(covers):
        if c.endswith('/boxart_front.png'):
            cart = c[:-len('boxart_front.png')] + 'gamepak_front.png'
            if os.path.isfile(os.path.join(menu_dir, cart)):
                covers.add(cart)
    covers = sorted(covers, key=lambda s: s.encode('utf-8'))
    cover_id = {c: i for i, c in enumerate(covers)}
    print('%d ROMs, %d with covers, %d distinct covers' % (len(roms), sum(1 for c in rom_cover.values() if c), len(covers)))

    # Thumbnails: scaled covers are kept in a cache on the PC, keyed by the cover's path, size and
    # modification time, so only new or changed covers are scaled again. The key also holds a print
    # of the image tool and every box it is asked for, so a new tool or size scales everything again.
    thumb_cache = ThumbCache(args.thumb_cache)
    tool_print = hashlib.sha1(open(args.tool, 'rb').read() +
                              repr((PACKS, SPINE_PACKS, SPINE_UPRIGHT, SPINE_FALLBACK, HIRES_PACKS)).encode('utf-8')).hexdigest()[:12]
    blobs = {pack: [None] * len(covers) for pack in PACKS}
    signatures = []
    work = tempfile.mkdtemp(prefix='coverpack-')
    failed = scaled = 0
    try:
        for i, cover in enumerate(covers):
            box_art = not cover.endswith('/gamepak_front.png')    # Cartridge art keeps its see-through outline
            # Every size from the hi-res front when there is one (the same box: launchbox.py --fronts), shrunk
            # rather than enlarged from the card's small scan.
            hi = os.path.join(args.hires, cover) if args.hires and box_art else ''
            full = hi if hi and os.path.isfile(hi) else os.path.join(menu_dir, cover)
            st = os.stat(full)
            signature = '%s|%d|%d|%s' % (cover, st.st_size, int(st.st_mtime), tool_print) + ('|fill' if box_art else '') +                 ('|hi' if full == hi else '')
            signatures.append(signature)
            cached = thumb_cache.get(signature)
            if cached is None:
                # The image tool takes ASCII paths, so it works on a temp copy of each cover.
                src = os.path.join(work, 'in.png')
                shutil.copyfile(full, src)
                prefix = os.path.join(work, 'c')
                r = subprocess.run([args.tool, 'box' if box_art else 'raw', src, prefix] + [arg for arg, _ in PACKS],
                                   capture_output=True, text=True)
                if r.returncode != 0:
                    failed += 1
                    print('  skipped %s: %s' % (cover, r.stderr.strip()))
                    continue
                cached = []
                for arg, _ in PACKS:
                    p = '%s_%s.raw' % (prefix, arg)
                    with open(p, 'rb') as f:
                        cached.append(f.read())
                    os.remove(p)
                thumb_cache.put(signature, cached)
                scaled += 1
                if scaled % 100 == 0:
                    print('  scaled %d covers' % scaled)
            for pack, blob in zip(PACKS, cached):
                blobs[pack][i] = blob
    finally:
        shutil.rmtree(work, ignore_errors=True)

    # Spines, under each front cover's id (other ids, e.g. cartridge art, have none).
    spine_blobs = {name: [None] * len(covers) for _, name in SPINE_PACKS}
    spine_widths = {}
    spine_signatures = []
    work = tempfile.mkdtemp(prefix='coverpack-')
    real_spines = scanned_spines = 0
    try:
        for i, cover in enumerate(covers):
            if not cover.endswith('/boxart_front.png'):
                continue
            folder = cover[:-len('boxart_front.png')]
            has_top = os.path.isfile(os.path.join(menu_dir, folder + 'boxart_top.png'))
            has_scan = not has_top and os.path.isfile(os.path.join(menu_dir, folder + 'boxart_spine.png'))
            source = folder + ('boxart_top.png' if has_top else 'boxart_spine.png' if has_scan else 'boxart_front.png')
            if has_top:
                boxes = [arg for arg, _ in SPINE_PACKS]
            elif has_scan:
                w, h = png_size(os.path.join(menu_dir, source))
                boxes = [arg for arg, _ in SPINE_PACKS] if w > h else SPINE_UPRIGHT
            else:
                boxes = SPINE_FALLBACK
            st = os.stat(os.path.join(menu_dir, source))
            signature = 'spine|%s|%d|%d|%s' % (source, st.st_size, int(st.st_mtime), tool_print)
            spine_signatures.append(signature)
            cached = thumb_cache.get(signature, len(SPINE_PACKS))
            if cached is None:
                src = os.path.join(work, 'in.png')
                shutil.copyfile(os.path.join(menu_dir, source), src)
                prefix = os.path.join(work, 's')
                r = subprocess.run([args.tool, 'raw', src, prefix] + boxes, capture_output=True, text=True)
                if r.returncode != 0:
                    print('  no spine for %s: %s' % (cover, r.stderr.strip()))
                    continue
                cached = []
                for arg in boxes:
                    p = '%s_%s.raw' % (prefix, arg)
                    with open(p, 'rb') as f:
                        cached.append(f.read())
                    os.remove(p)
                thumb_cache.put(signature, cached)
            real_spines += has_top
            scanned_spines += has_scan
            for (_, name), blob in zip(SPINE_PACKS, cached):
                spine_blobs[name][i] = blob
            # The width it stands for on the 16:9 pack ("CVW1" header) is even, as the menu draws it.
            wide = cached[-1]
            spine_widths[i] = struct.unpack('>H', wide[8:10])[0] if wide[:4] == b'CVW1' else 0
    finally:
        shutil.rmtree(work, ignore_errors=True)
    # Home's large covers: the hi-res front when there is one (scaled down), else the card's art enlarged.
    hires_blobs = {name: [None] * len(covers) for _, name in HIRES_PACKS}
    hires_signatures = []
    from_hires = 0
    work = tempfile.mkdtemp(prefix='coverpack-')
    try:
        for i, cover in enumerate(covers):
            box_art = not cover.endswith('/gamepak_front.png')
            hi = os.path.join(args.hires, cover) if args.hires else ''
            use_hires = box_art and hi and os.path.isfile(hi)
            source = hi if use_hires else os.path.join(menu_dir, cover)
            boxes = [arg if use_hires else arg + 'u' for arg, _ in HIRES_PACKS]
            st = os.stat(source)
            signature = 'hires|%s|%s|%d|%d|%s' % (cover, 'hi' if use_hires else 'card', st.st_size, int(st.st_mtime), tool_print)
            hires_signatures.append(signature)
            cached = thumb_cache.get(signature, len(HIRES_PACKS))
            if cached is None:
                src = os.path.join(work, 'in.png')
                shutil.copyfile(source, src)
                prefix = os.path.join(work, 'h')
                r = subprocess.run([args.tool, 'box' if box_art else 'raw', src, prefix] + boxes, capture_output=True, text=True)
                if r.returncode != 0:
                    print('  no large cover for %s: %s' % (cover, r.stderr.strip()))
                    continue
                cached = []
                for arg in boxes:
                    p = '%s_%s.raw' % (prefix, arg)
                    with open(p, 'rb') as f:
                        cached.append(f.read())
                    os.remove(p)
                thumb_cache.put(signature, cached)
            from_hires += bool(use_hires)
            for (_, name), blob in zip(HIRES_PACKS, cached):
                hires_blobs[name][i] = blob
    finally:
        shutil.rmtree(work, ignore_errors=True)
    thumb_cache.save()
    if failed == 0:
        removed = thumb_cache.prune(set(signatures + spine_signatures + hires_signatures))
        if removed:
            print('Thumbnail cache: %d unused entries removed' % removed)
    print('%d covers scaled, %d reused from the cache' % (scaled, len(covers) - scaled - failed))
    print('%d large covers from hi-res art, %d enlarged from the card\'s' % (from_hires, len(hires_signatures) - from_hires))
    print('%d spines from the box top, %d from spine scans, %d made from a slice of the front' %
          (real_spines, scanned_spines, len(spine_signatures) - real_spines - scanned_spines))

    # Skip rewriting the packs (88 MB or so) when the covers are exactly the same as last time.
    manifest = hashlib.sha1('\n'.join(signatures + [name for _, name in PACKS] + ['order:shown', 'align:8', 'combined'] + spine_signatures + hires_signatures).encode('utf-8')).hexdigest()
    manifest_path = out_dir + '/covers.manifest'
    all_packs = [(name, blobs[pack]) for pack in PACKS for name in [pack[1]]] + \
        [(name, spine_blobs[name]) for _, name in SPINE_PACKS] + \
        [(name, hires_blobs[name]) for _, name in HIRES_PACKS]
    combined_path = out_dir + '/covers.pak'
    packs_present = os.path.exists(combined_path)
    old_manifest = open(manifest_path).read().strip() if os.path.exists(manifest_path) else ''

    def shown_title_key(stem):
        # As the menu sorts: " - " shows as ": ", and a leading "The " is skipped.
        title = stem.replace(' - ', ': ').lower()
        short = title[4:] if title.startswith('the ') and len(title) > 4 else title
        return (short, title)

    packs_current = packs_present and old_manifest == manifest and failed == 0

    # Store the pixels in the order the library shows games (folder, then title), box art first and
    # cartridge art after, so scrolling reads neighbouring data. Ids (the offset table) are unchanged.
    order, placed = [], set()
    shown = sorted(roms, key=lambda r: (r.rsplit('/', 1)[0].lower() if '/' in r else '',
                                        shown_title_key(os.path.splitext(os.path.basename(r))[0])))
    for cart in (False, True):
        for rom in shown:
            c = rom_cover[rom]
            if c and cart:
                c = c[:-len('boxart_front.png')] + 'gamepak_front.png' if c.endswith('/boxart_front.png') else None
            if c in cover_id and cover_id[c] not in placed:
                placed.add(cover_id[c])
                order.append(cover_id[c])
    order += [i for i in range(len(covers)) if i not in placed]

    # Every size goes into one file, covers.pak, so the menu opens one file (an open costs ~55 ms on the
    # console) rather than one per size:
    #   "CVPA", u32 pack count, then per pack: u16 w, u16 h, u8 wide, u8 0, u16 0, u32 table offset,
    #   u32 cover count; then each pack's table (u32 absolute offset per cover id, 0 = none) and covers.
    if packs_current:
        total = os.path.getsize(combined_path)
    else:
        layout, pos = [], 8 + 16 * len(all_packs)
        for name, items in all_packs:
            table_offset = pos
            pos += 4 * len(items)
            offsets, chunks = [0] * len(items), []
            for i in order:
                b = items[i]
                if b:
                    # Pixels start on an 8-byte boundary, so the cart reads whole sectors straight into
                    # the cover's memory instead of copying them one at a time.
                    header = 12 if b[:4] == b'CVW1' else 8
                    pad = (-(pos + header)) % 8
                    if pad:
                        chunks.append(bytes(pad))
                        pos += pad
                    offsets[i] = pos
                    chunks.append(b)
                    pos += len(b)
            layout.append((name, table_offset, offsets, chunks))
        with open(combined_path + '.tmp', 'wb') as f:
            f.write(b'CVPA' + struct.pack('>I', len(layout)))
            for name, table_offset, offsets, _ in layout:
                size, wide = (name[:-2], 1) if name.endswith('_w') else (name, 0)
                w, h = (int(v) for v in size.split('x'))
                f.write(struct.pack('>HHBBHII', w, h, wide, 0, 0, table_offset, len(offsets)))
            for name, table_offset, offsets, chunks in layout:
                assert f.tell() == table_offset
                f.write(struct.pack('>%dI' % len(offsets), *offsets))
                for chunk in chunks:
                    f.write(chunk)
        os.replace(combined_path + '.tmp', combined_path)
        total = os.path.getsize(combined_path)
    # The per-size packs of older runs: the menu reads covers.pak first, but stale ones would only waste space.
    for old in sorted(os.listdir(out_dir)):
        if old.startswith('covers_') and old.endswith('.pak'):
            os.remove(os.path.join(out_dir, old))
            print('Removed %s (now in covers.pak)' % old)
    if packs_current:
        print('Covers unchanged: packs left as they are.')
    else:
        with open(manifest_path, 'w') as f:
            f.write(manifest + '\n')

    # Game details for the library's sort options.
    meta_lines = []
    if os.path.isdir(args.metadata):
        by_crc, by_name = gamemeta.load_dats(args.metadata)
        cache = gamemeta.CrcCache(args.crc_cache)
        details = {}
        for i, rom in enumerate(sorted(roms, key=lambda s: s.encode('utf-8'))):
            d = gamemeta.match(rom, cache.get(rom, card + rom), by_crc, by_name)
            if (i + 1) % 50 == 0:
                print('  checked %d / %d ROMs for game details' % (i + 1, len(roms)))
                cache.save()
            if d:
                details[rom] = d
        cache.save()
        matched = len(details)
        # A ROM hack (or other unlisted ROM) that shares its cover with a listed game takes that game's details.
        by_cover = {}
        for rom, d in details.items():
            if rom_cover[rom]:
                by_cover.setdefault(rom_cover[rom], d)
        borrowed = 0
        for rom in roms:
            if rom not in details and rom_cover[rom] in by_cover:
                details[rom] = by_cover[rom_cover[rom]]
                borrowed += 1
        for rom in sorted(details, key=lambda s: s.encode('utf-8')):
            d = details[rom]
            meta_lines.append('M\t%s\t%s\t%s\t%s\t%s\t%s' % (rom, gamemeta.clean(d.get('genre')), gamemeta.clean(d.get('developer')),
                                                         gamemeta.clean(d.get('year')), gamemeta.clean(d.get('players')),
                                                         gamemeta.clean(d.get('publisher'))))
        print('Game details found for %d / %d ROMs, and %d more from the game whose cover they share' % (matched, len(roms), borrowed))
    else:
        print('No game details at %s, so sorting by genre/developer/year/players will be empty.' % args.metadata)

    lines = ['LCIDX1']
    lines += ['C\t' + c for c in covers]
    for rom in sorted(roms, key=lambda s: s.encode('utf-8')):
        c = rom_cover[rom]
        lines.append('R\t%s\t%d' % (rom, cover_id[c] if c else -1))
    lines += meta_lines
    # W<TAB>cover id<TAB>spine width: lets the shelf lay out spines before reading them.
    lines += ['W\t%d\t%d' % (i, w) for i, w in sorted(spine_widths.items()) if w]
    with open(out_dir + '/covers.idx.tmp', 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    os.replace(out_dir + '/covers.idx.tmp', out_dir + '/covers.idx')

    print('Done: %d covers packed (%.0f MB) into %s%s' % (len(covers) - failed, total / 1e6, out_dir,
          ', %d skipped' % failed if failed else ''))


if __name__ == '__main__':
    main()

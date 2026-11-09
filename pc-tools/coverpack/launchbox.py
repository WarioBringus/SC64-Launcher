"""
Check the card's library against the LaunchBox Games Database: real box spines it has for games that
lack one, and genres where it disagrees. Reports only, unless --download is given.

    python -I launchbox.py --card I: --metadata C:/n64/downloads/launchbox/Metadata.xml [--download]

Metadata.xml is in https://gamesdb.launchbox-app.com/Metadata.zip (about 105 MB), downloaded by hand.
With --download, each spine is fetched from images.launchbox-app.com into --spines, checked to be a
PNG or JPEG, converted to PNG with ffmpeg (long side at most 420) and saved beside its front cover as
boxart_spine.png (an existing file is never replaced). Run coverpack.py afterwards.

With --fronts, the hi-res box front of every box cover (its own region first) is fetched into --fronts-dir
(about 1.2 MB each; the card's art is only 158x112) and saved, long side at most 600, as a PNG under --hires
at the cover's own path (metadata/.../boxart_front.png). coverpack.py --hires builds Home's large covers from
those; the card is not changed.

Genre disagreements are only listed: put the ones to change in menu/game-fixes.txt.
ROM hacks are not in the database; they take their original's cover, spine and details anyway.
"""
import argparse, collections, os, re, struct, subprocess, sys, urllib.request
import xml.etree.ElementTree as ET

IMAGE_HOST = 'https://images.launchbox-app.com/'
SAME_REGION = {'E': ('North America', 'United States'), 'J': ('Japan',),
               'P': ('Europe', 'World', 'Australia', 'Oceania', 'France')}
# Our genre names (libretro-database) against LaunchBox's, for the comparison only.
SAME_GENRE = {'Role-playing (RPG)': {'Role-Playing'}, 'Board': {'Board Game', 'Party'}, "Beat'em Up": {"Beat 'em Up"},
              "Shoot'em Up": {'Shooter'}, 'Gambling': {'Casino'}, 'Music / Dancing': {'Music'},
              'Sports with Animals': {'Sports'}, 'Hunting and Fishing': {'Sports', 'Fishing'},
              'Lightgun Shooter': {'Shooter'}, 'Casual Game': {'Party', 'Quiz'}, 'Educational': {'Education'}}


def norm(s):
    s = re.sub(r'\([^)]*\)|\[[^\]]*\]', '', s).replace(' - ', ' ').replace('&', 'and').lower()
    s = re.sub(r'^the |, the\b', '', s)
    return re.sub(r'[^a-z0-9]', '', s)


def load_launchbox(path):
    games, alt, spines = {}, collections.defaultdict(list), collections.defaultdict(list)
    fronts = collections.defaultdict(list)
    for _, el in ET.iterparse(path, events=('end',)):
        if el.tag == 'Game':
            if el.findtext('Platform') == 'Nintendo 64':
                games[el.findtext('DatabaseID')] = (el.findtext('Name') or '', el.findtext('Genres') or '')
            el.clear()
        elif el.tag == 'GameAlternateName':
            alt[el.findtext('DatabaseID')].append(el.findtext('AlternateName') or '')
            el.clear()
        elif el.tag == 'GameImage':
            kind = el.findtext('Type')
            if kind in ('Box - Spine', 'Box - Front'):
                (spines if kind == 'Box - Spine' else fronts)[el.findtext('DatabaseID')].append(
                    (el.findtext('FileName') or '', el.findtext('Region') or ''))
            el.clear()
    by_name = {}
    for gid, (name, _) in games.items():
        by_name.setdefault(norm(name), gid)
    for gid, names in alt.items():
        for n in names:
            if gid in games:
                by_name.setdefault(norm(n), gid)
    return games, spines, fronts, by_name


def image_size(data):
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        return struct.unpack('>II', data[16:24])
    if data[:3] == b'\xff\xd8\xff':
        i = 2
        while i < len(data) - 9:
            if data[i] != 0xFF:
                i += 1
                continue
            m = data[i + 1]
            if m in (0xC0, 0xC1, 0xC2):
                h, w = struct.unpack('>HH', data[i + 5:i + 9])
                return w, h
            i += 2 if (m == 0xD8 or 0xD0 <= m <= 0xD7) else 2 + struct.unpack('>H', data[i + 2:i + 4])[0]
    return None


def fetch(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'coverpack/1.0'})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


SAME_BOX_DIFFERENCE = 27    # Mean grey difference (0-255) at 32x24, best crop; above it, another box or edition


def tiny(ffmpeg, path, w=32, h=24):
    """An image shrunk to w x h grey, for comparing."""
    r = subprocess.run([ffmpeg, '-loglevel', 'error', '-i', path, '-vf', 'scale=%d:%d:flags=area' % (w, h), '-f', 'rawvideo',
                        '-pix_fmt', 'gray', '-'], capture_output=True)
    return r.stdout if len(r.stdout) == w * h else None


def box_difference(ffmpeg, card_art, front):
    """How unlike the card's art a front is, allowing for scans cropped a little differently: the card's art at 32x24
    against every 32x24 window of the front shrunk to 32x24, 34x25 and 36x27."""
    a = tiny(ffmpeg, card_art)
    best = 255.0
    for w, h in ((32, 24), (34, 25), (36, 27)):
        b = tiny(ffmpeg, front, w, h)
        if not a or not b:
            return 255.0
        for dy in range(h - 23):
            for dx in range(w - 31):
                d = sum(abs(a[y * 32 + x] - b[(y + dy) * w + x + dx]) for y in range(24) for x in range(32)) / 768
                best = min(best, d)
    return best


def save_fronts(args, menu, front_for):
    """Fetch each hi-res front (kept in --fronts-dir) and save it, long side at most 600, under --hires, when it is
    the same box as the card's art (LaunchBox often has another region or edition: the large cover would then differ
    from the small ones)."""
    os.makedirs(args.fronts_dir, exist_ok=True)
    saved = skipped = different = 0
    for cover, f in sorted(front_for.items()):
        local = os.path.join(args.fronts_dir, f)
        if not os.path.exists(local):
            data = fetch(IMAGE_HOST + f)
            if not (data[:8] == b'\x89PNG\r\n\x1a\n' or data[:3] == b'\xff\xd8\xff'):
                print('  skipped %s: not a PNG or JPEG' % cover)
                skipped += 1
                continue
            open(local, 'wb').write(data)
        size = image_size(open(local, 'rb').read(300000))
        if not size or not (0.5 <= min(size) / max(size) <= 0.95):
            print('  skipped %s: not box-shaped %s' % (cover, size))
            skipped += 1
            continue
        dest = os.path.join(args.hires, cover)
        if not (os.path.exists(dest) and os.path.getmtime(dest) >= os.path.getmtime(local)):
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            vf = "scale='if(gt(iw,ih),min(iw,600),-2)':'if(gt(iw,ih),-2,min(ih,600))':flags=lanczos"
            r = subprocess.run([args.ffmpeg, '-loglevel', 'error', '-y', '-i', local, '-vf', vf, '-pix_fmt', 'rgb24', dest])
            if r.returncode != 0:
                skipped += 1
                continue
            saved += 1
        if box_difference(args.ffmpeg, menu + cover, dest) > SAME_BOX_DIFFERENCE:
            os.remove(dest)
            different += 1
    kept = len(front_for) - skipped - different
    print('Hi-res fronts under %s: %d kept (%d new), %d another box or edition than the card\'s (not used), %d skipped.'
          % (args.hires, kept, saved, different, skipped))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--card', default='I:')
    ap.add_argument('--metadata', required=True, help='Metadata.xml from LaunchBox Metadata.zip')
    ap.add_argument('--download', action='store_true', help='fetch the spines found and place them on the card')
    ap.add_argument('--spines', default='C:/n64/downloads/launchbox-spines', help='folder for the downloaded images')
    ap.add_argument('--ffmpeg', default='ffmpeg')
    ap.add_argument('--fronts', action='store_true', help='fetch hi-res box fronts for the large covers (see above)')
    ap.add_argument('--fronts-dir', default='C:/n64/downloads/launchbox-fronts', help='folder for the downloaded fronts')
    ap.add_argument('--hires', default='C:/n64/covers-hires', help='where the fronts are saved for coverpack --hires')
    args = ap.parse_args()
    menu = args.card.rstrip('/\\') + '/menu/'

    games, spines, fronts, by_name = load_launchbox(args.metadata)
    covers, roms, genre = [], [], {}
    for line in open(menu + 'cache/launcher/covers.idx', encoding='utf-8'):
        p = line.rstrip('\n').split('\t')
        if p[0] == 'C':
            covers.append(p[1])
        elif p[0] == 'R':
            roms.append((p[1], int(p[2])))
        elif p[0] == 'M':
            genre[p[1]] = p[2]
    # Genres already fixed by hand count as ours.
    fixed, current = {}, None
    if os.path.exists(menu + 'game-fixes.txt'):
        for line in open(menu + 'game-fixes.txt', encoding='utf-8-sig'):
            s = line.strip()
            if s.startswith('[') and s.endswith(']'):
                current = s[1:-1].strip().lower()
            elif current and not s.startswith(('#', ';')) and '=' in s:
                key, value = (t.strip() for t in s.split('=', 1))
                if key.lower() == 'genre' and value:
                    fixed[current] = value
    for rom, _ in roms:
        name = rom.rsplit('/', 1)[-1].lower()
        if name in fixed:
            genre[rom] = fixed[name]

    wanted, disagree, unmatched, front_for = {}, [], [], {}
    for rom, cid in roms:
        stem = os.path.splitext(rom.rsplit('/', 1)[-1])[0]
        latin = re.sub(r'[^\x00-\x7f]+', ' ', stem).strip()
        gid = by_name.get(norm(stem)) or by_name.get(norm(latin))
        if not gid:
            unmatched.append(rom)
            continue
        ours, theirs = genre.get(rom, ''), {g.strip() for g in games[gid][1].split(';') if g.strip()}
        if theirs and ours not in theirs and not (SAME_GENRE.get(ours, set()) & theirs) and \
                not (ours == 'Simulation' and any('Simulat' in g for g in theirs)):
            disagree.append((stem, ours or '-', games[gid][1]))
        cover = covers[cid] if cid >= 0 else ''
        if cover.endswith('/boxart_front.png') and cover not in front_for:
            parts = cover.split('/')
            region = parts[4] if len(parts) == 6 else ''
            options = fronts.get(gid, [])
            best = ([s for s in options if s[1] in SAME_REGION.get(region, ())] or
                    [s for s in options if s[1] in ('North America', 'United States')] or options)
            if best and re.fullmatch(r'(r2_)?[0-9a-f-]{36}\.(jpg|jpeg|png)', best[0][0], re.I):
                front_for[cover] = best[0][0]
        if not cover.endswith('/boxart_front.png') or cover in wanted:
            continue
        folder = cover[:-len('boxart_front.png')]
        if os.path.exists(menu + folder + 'boxart_top.png') or os.path.exists(menu + folder + 'boxart_spine.png'):
            continue
        parts = cover.split('/')
        region = parts[4] if len(parts) == 6 else ''
        options = spines.get(gid, [])
        best = ([s for s in options if s[1] in SAME_REGION.get(region, ())] or
                [s for s in options if s[1] in ('North America', 'United States')] or options)
        if best and re.fullmatch(r'(r2_)?[0-9a-f-]{36}\.(jpg|png)', best[0][0]):
            wanted[cover] = (best[0][0], best[0][1], stem)

    print('LaunchBox N64 games: %d. Your ROMs matched: %d of %d.' % (len(games), len(roms) - len(unmatched), len(roms)))
    print('\nGenres that disagree (%d) - yours, then LaunchBox:' % len(disagree))
    for stem, ours, theirs in sorted(set(disagree)):
        print('  %-50s %-20s %s' % (stem[:50], ours, theirs))
    print('\nReal spines LaunchBox has for games without one: %d' % len(wanted))
    for cover, (f, region, stem) in sorted(wanted.items(), key=lambda kv: kv[1][2].lower()):
        print('  %-50s %s' % (stem[:50], region))
    print('\nHi-res box fronts LaunchBox has for your box covers: %d' % len(front_for))
    if args.fronts:
        save_fronts(args, menu, front_for)
    if not args.download:
        print('\nNo spines downloaded. Run again with --download to fetch these %d images.' % len(wanted))
        return

    os.makedirs(args.spines, exist_ok=True)
    placed = 0
    for cover, (f, region, stem) in wanted.items():
        local = os.path.join(args.spines, f)
        if not os.path.exists(local):
            with urllib.request.urlopen(IMAGE_HOST + f, timeout=60) as r:
                data = r.read()
            open(local, 'wb').write(data)
        data = open(local, 'rb').read()
        size = image_size(data)
        if not size or not (0.12 <= min(size) / max(size) <= 0.30):
            print('  skipped %s: not a spine-shaped image' % stem)
            continue
        dest = menu + cover[:-len('boxart_front.png')] + 'boxart_spine.png'
        if os.path.exists(dest):
            continue
        vf = "scale='if(gt(iw,ih),min(iw,420),-2)':'if(gt(iw,ih),-2,min(ih,420))':flags=lanczos"
        r = subprocess.run([args.ffmpeg, '-loglevel', 'error', '-y', '-i', local, '-vf', vf, '-pix_fmt', 'rgb24', dest])
        placed += (r.returncode == 0)
    print('\nPlaced %d spines on the card. Now run coverpack.py (make-cover-pack.cmd).' % placed)


if __name__ == '__main__':
    sys.exit(main())

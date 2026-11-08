"""
Check the card's library against the LaunchBox Games Database: real box spines it has for games that
lack one, and genres where it disagrees. Reports only, unless --download is given.

    python -I launchbox.py --card I: --metadata C:/n64/downloads/launchbox/Metadata.xml [--download]

Metadata.xml is in https://gamesdb.launchbox-app.com/Metadata.zip (about 105 MB), downloaded by hand.
With --download, each spine is fetched from images.launchbox-app.com into --spines, checked to be a
PNG or JPEG, converted to PNG with ffmpeg (long side at most 420) and saved beside its front cover as
boxart_spine.png (an existing file is never replaced). Run coverpack.py afterwards.

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
    for _, el in ET.iterparse(path, events=('end',)):
        if el.tag == 'Game':
            if el.findtext('Platform') == 'Nintendo 64':
                games[el.findtext('DatabaseID')] = (el.findtext('Name') or '', el.findtext('Genres') or '')
            el.clear()
        elif el.tag == 'GameAlternateName':
            alt[el.findtext('DatabaseID')].append(el.findtext('AlternateName') or '')
            el.clear()
        elif el.tag == 'GameImage':
            if el.findtext('Type') == 'Box - Spine':
                spines[el.findtext('DatabaseID')].append((el.findtext('FileName') or '', el.findtext('Region') or ''))
            el.clear()
    by_name = {}
    for gid, (name, _) in games.items():
        by_name.setdefault(norm(name), gid)
    for gid, names in alt.items():
        for n in names:
            if gid in games:
                by_name.setdefault(norm(n), gid)
    return games, spines, by_name


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--card', default='I:')
    ap.add_argument('--metadata', required=True, help='Metadata.xml from LaunchBox Metadata.zip')
    ap.add_argument('--download', action='store_true', help='fetch the spines found and place them on the card')
    ap.add_argument('--spines', default='C:/n64/downloads/launchbox-spines', help='folder for the downloaded images')
    ap.add_argument('--ffmpeg', default='ffmpeg')
    args = ap.parse_args()
    menu = args.card.rstrip('/\\') + '/menu/'

    games, spines, by_name = load_launchbox(args.metadata)
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

    wanted, disagree, unmatched = {}, [], []
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
    if not args.download:
        print('\nNothing downloaded. Run again with --download to fetch these %d images.' % len(wanted))
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

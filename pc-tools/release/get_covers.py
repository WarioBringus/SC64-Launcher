"""
SC64 Launcher: get covers and game details for the games on your SD card.

Run it by double-clicking "Get covers and game info.bat". It needs nothing installed: the folder carries its own
Python. Run it again whenever you add games.

What it does, on the SD card you pick:
  1. Box art for every game: the card-sized art pack from the N64 Flashcart Menu metadata project
     (github.com/n64-tools/n64-flashcart-menu-metadata, public domain), into menu/metadata.
  2. Game details (genre, developer, year, players, publisher) from libretro-database
     (github.com/libretro/libretro-database, CC BY-SA 4.0).
  3. Full-size box fronts for your own games only, from the same metadata project, for Home's large covers.
  4. The launcher's cover packs (menu/cache/launcher), every cover pre-scaled so the N64 never has to.
Downloads and caches are kept in tools/data, so later runs only fetch what's new.
"""
import concurrent.futures, json, os, shutil, string, subprocess, sys, time, urllib.error, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))          # tools/scripts
TOOLS = os.path.dirname(HERE)                               # tools
DATA = os.path.join(TOOLS, 'data')
COVERIMG = os.path.join(TOOLS, 'bin', 'coverimg.exe')
COVERPACK = os.path.join(HERE, 'coverpack.py')

ART_REPO = 'n64-tools/n64-flashcart-menu-metadata'
ART_ASSET = 'release-metadata.zip'
ART_FALLBACK = 'https://github.com/%s/releases/download/v0.1.0/%s' % (ART_REPO, ART_ASSET)
FULL_SIZE = 'https://raw.githubusercontent.com/%s/main/' % ART_REPO
DETAILS = 'https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/%s/Nintendo%%20-%%20Nintendo%%2064.dat'
DETAIL_FILES = ('genre', 'developer', 'releaseyear', 'maxusers', 'publisher')
AGENT = {'User-Agent': 'SC64-Launcher-tools'}


def say(text=''):
    print(text, flush=True)


def fetch(url, timeout=60):
    return urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=timeout)


def download(url, path, label):
    """Download to path (via a temporary file, so a broken download never looks finished), with progress."""
    tmp = path + '.part'
    with fetch(url) as r, open(tmp, 'wb') as f:
        total = int(r.headers.get('Content-Length') or 0)
        done, shown = 0, -1
        while True:
            chunk = r.read(1 << 16)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            pct = done * 100 // total if total else -1
            if total and pct != shown and (total > 2e6 or pct == 100):    # Small files: just the end.
                shown = pct
                print('\r  %s: %d%% of %.0f MB' % (label, pct, total / 1e6), end='', flush=True)
    os.replace(tmp, path)
    if total:
        print()


def find_card():
    """The SD card: a drive with the launcher (sc64menu.n64) at its root. Asks when there isn't exactly one."""
    found = [d for d in string.ascii_uppercase if os.path.isfile('%s:\\sc64menu.n64' % d)]
    if len(found) == 1:
        answer = input('Your SD card looks like drive %s:. Use it? (Y/n) ' % found[0]).strip().lower()
        if answer in ('', 'y', 'yes'):
            return found[0] + ':\\'
    elif found:
        say('Drives with the launcher on them: %s' % ', '.join(d + ':' for d in found))
    else:
        say('No drive with sc64menu.n64 on it was found. (Copy the "SD card" folder\'s files to your card first.)')
    while True:
        letter = input('Type your SD card\'s drive letter (for example E), or Q to quit: ').strip().rstrip(':\\').upper()
        if letter == 'Q':
            sys.exit(0)
        if len(letter) == 1 and letter in string.ascii_uppercase and os.path.isdir(letter + ':\\'):
            return letter + ':\\'
        say('  There is no drive %s: - check the card is plugged in.' % letter)


def art_pack():
    """The latest card-sized art pack (cached by release)."""
    url, tag = ART_FALLBACK, 'v0.1.0'
    try:
        with fetch('https://api.github.com/repos/%s/releases?per_page=10' % ART_REPO, 30) as r:
            for rel in json.load(r):
                assets = [a for a in rel.get('assets', []) if a.get('name') == ART_ASSET]
                if assets:
                    url, tag = assets[0]['browser_download_url'], rel['tag_name']
                    break
    except Exception as e:
        say('  (Couldn\'t check for a newer art pack: %s. Using %s.)' % (e, tag))
    path = os.path.join(DATA, 'art-%s.zip' % tag)
    if not os.path.isfile(path):
        download(url, path, 'Box art pack %s' % tag)
    else:
        say('  Box art pack %s: already downloaded.' % tag)
    return path


def unpack_art(zip_path, card):
    """Every file under the pack's metadata folder, into the card's menu/metadata (art you added elsewhere stays)."""
    target = os.path.join(card, 'menu', 'metadata')
    written = 0
    with zipfile.ZipFile(zip_path) as z:
        names = [n for n in z.namelist() if not n.endswith('/')]
        prefixed = any('metadata/' in n for n in names)
        for name in names:
            if prefixed and 'metadata/' not in name:
                continue
            rel = name.split('metadata/', 1)[1] if prefixed else name
            if not rel or '..' in rel.split('/'):
                continue
            dest = os.path.join(target, *rel.split('/'))
            info = z.getinfo(name)
            if os.path.isfile(dest) and os.path.getsize(dest) == info.file_size:
                continue
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with z.open(info) as src, open(dest, 'wb') as out:
                shutil.copyfileobj(src, out)
            written += 1
            if written % 200 == 0:
                print('\r  Copying box art to the SD card: %d files' % written, end='', flush=True)
    say('\r  Box art on the SD card: %d new or updated files.        ' % written)


def details():
    folder = os.path.join(DATA, 'details')
    os.makedirs(folder, exist_ok=True)
    for name in DETAIL_FILES:
        path = os.path.join(folder, name + '.dat')
        if not os.path.isfile(path) or time.time() - os.path.getmtime(path) > 30 * 86400:
            download(DETAILS % name, path, 'Game details (%s)' % name)
    say('  Game details: ready.')
    return folder


def run_coverpack(card, hires):
    cmd = [sys.executable, '-I', COVERPACK, '--card', card.rstrip('\\'), '--tool', COVERIMG,
           '--metadata', os.path.join(DATA, 'details'), '--crc-cache', os.path.join(DATA, 'rom_crc.json'),
           '--thumb-cache', os.path.join(DATA, 'thumbs'), '--hires', hires]
    return subprocess.call(cmd)


def full_size_fronts(card, hires):
    """Full-size box fronts for the covers the card's games use (from the index the first pass wrote)."""
    index = os.path.join(card, 'menu', 'cache', 'launcher', 'covers.idx')
    covers = []
    for line in open(index, encoding='utf-8'):
        if line.startswith('C\t'):
            cover = line.rstrip('\n').split('\t')[1]
            if cover.startswith('metadata/') and cover.endswith('boxart_front.png'):
                if not os.path.isfile(os.path.join(hires, *cover.split('/'))):
                    covers.append(cover)
    if not covers:
        say('  Large covers: all already downloaded.')
        return

    def one(cover):
        dest = os.path.join(hires, *cover.split('/'))
        try:
            with fetch(FULL_SIZE + cover, 60) as r:
                data = r.read()
        except Exception:
            return False
        if data[:8] != b'\x89PNG\r\n\x1a\n':
            return False
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest + '.part', 'wb') as f:
            f.write(data)
        os.replace(dest + '.part', dest)
        return True

    got = 0
    with concurrent.futures.ThreadPoolExecutor(6) as pool:
        for i, ok in enumerate(pool.map(one, covers), 1):
            got += ok
            print('\r  Large covers: %d of %d' % (i, len(covers)), end='', flush=True)
    say('\r  Large covers: %d downloaded (%d not available; those use the card art enlarged).' % (got, len(covers) - got))


def main():
    os.system('')   # Lets the console show progress lines on older Windows.
    say('SC64 Launcher - get covers and game info')
    say('=' * 41)
    say()
    card = find_card()
    os.makedirs(os.path.join(card, 'menu'), exist_ok=True)
    os.makedirs(DATA, exist_ok=True)
    hires = os.path.join(DATA, 'hires')
    t0 = time.time()

    say()
    say('Step 1 of 4: box art')
    unpack_art(art_pack(), card)
    say()
    say('Step 2 of 4: game details')
    details()
    say()
    say('Step 3 of 4: matching your games to their covers')
    if run_coverpack(card, hires) != 0:
        sys.exit('\nSomething went wrong building the covers (see above).')
    say()
    say('Step 4 of 4: large covers for Home')
    full_size_fronts(card, hires)
    if run_coverpack(card, hires) != 0:
        sys.exit('\nSomething went wrong building the covers (see above).')

    say()
    say('All done in %d s. Eject the SD card safely, put it back in your SummerCart64 and turn the console on.' %
        (time.time() - t0))
    say('Added games later? Run this again; it only fetches what\'s new.')


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        say('\nStopped.')
    except urllib.error.URLError as e:
        say('\nCouldn\'t reach the internet (%s). Check your connection and run this again.' % e.reason)

"""
Game details (genre, developer, year, players, publisher) for the launcher's sort options and About page.

Data: libretro-database's N64 metadat files (genre, developer, releaseyear, maxusers, publisher), which key
games by the CRC32 of the whole ROM in big-endian (z64) byte order and also give the No-Intro name.
ROMs are matched by CRC first, then by name. CRCs are cached on the PC so later runs only read
new or changed ROMs.
"""
import array, json, os, re, zlib

# .dat file -> (key inside the file, field name used here)
FIELDS = {
    'genre': ('genre', 'genre'),
    'developer': ('developer', 'developer'),
    'releaseyear': ('releaseyear', 'year'),
    'maxusers': ('users', 'players'),
    'publisher': ('publisher', 'publisher'),
}
REGION_WORDS = {'USA': 'usa', 'Europe': 'europe', 'Japan': 'japan', 'China': 'china'}


def _norm_name(s):
    s = s.lower()
    s = re.sub(r'\(.*?\)|\[.*?\]', ' ', s)
    s = s.replace('&', ' and ').replace("'", '')
    s = re.sub(r'^the\s+|,\s*the\b', ' ', s)
    s = re.sub(r'[^a-z0-9]+', ' ', s)
    return ' '.join(s.split())


def load_dats(folder):
    """Return (by_crc, by_name): CRC -> details, normalized name -> list of (region text, details)."""
    by_crc = {}
    names = {}
    for file, (key, field) in FIELDS.items():
        path = os.path.join(folder, file + '.dat')
        if not os.path.exists(path):
            continue
        text = open(path, encoding='utf-8', errors='replace').read()
        for block in re.findall(r'game \((.*?)\n\)', text, re.S):
            crc = re.search(r'crc ([0-9A-Fa-f]{8})', block)
            # Values are quoted strings ("Racing") or bare words (users 4).
            value = re.search(r'\n\s*%s\s+(?:"(.*?)"|(\S+))' % key, block)
            name = re.search(r'comment "(.*?)"', block)
            if not crc or not value:
                continue
            d = by_crc.setdefault(crc.group(1).upper(), {})
            d[field] = (value.group(1) if value.group(1) is not None else value.group(2)).strip()
            if name:
                d['name'] = name.group(1)
    by_name = {}
    for d in by_crc.values():
        if 'name' in d:
            by_name.setdefault(_norm_name(d['name']), []).append(d)
    return by_crc, by_name


def rom_crc(path):
    """CRC32 of the ROM in big-endian (z64) byte order, whatever order the file is in."""
    data = open(path, 'rb').read()
    head = data[:4]
    if head[:2] == b'\x37\x80':           # v64: 16-bit swapped
        swapped = bytearray(len(data))
        swapped[0::2] = data[1::2]
        swapped[1::2] = data[0::2]
        data = bytes(swapped)
    elif head[:2] == b'\x40\x12':         # n64: 32-bit little-endian
        a = array.array('I', data[:len(data) // 4 * 4])
        a.byteswap()
        data = a.tobytes()
    return '%08X' % (zlib.crc32(data) & 0xFFFFFFFF)


class CrcCache:
    def __init__(self, path):
        self.path = path
        try:
            self.data = json.load(open(path, encoding='utf-8'))
        except (OSError, ValueError):
            self.data = {}

    def get(self, rel, full):
        st = os.stat(full)
        key = '%s|%d|%d' % (rel, st.st_size, int(st.st_mtime))
        if key not in self.data:
            self.data[key] = rom_crc(full)
        return self.data[key]

    def save(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        json.dump(self.data, open(self.path, 'w', encoding='utf-8'))


def match(rel, crc, by_crc, by_name):
    """Details for one ROM: exact CRC match, else the closest name (same region preferred)."""
    if crc in by_crc:
        return by_crc[crc]
    title = os.path.splitext(os.path.basename(rel))[0]
    candidates = by_name.get(_norm_name(title))
    if not candidates:
        return None
    folder = rel.split('/')[0] if '/' in rel else ''
    for word, region in REGION_WORDS.items():
        if word in folder:
            for d in candidates:
                if region in d.get('name', '').lower():
                    return d
    return candidates[0]


def clean(value):
    return (value or '').replace('\t', ' ').replace('\n', ' ').strip()

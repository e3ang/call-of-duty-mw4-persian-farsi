"""Build files/trdata2.bin from the Persian translation files.

trdata2.bin format (read by the runtime hook):
    uint32 LE  number of entries
    then for each entry:
        uint16 LE  byte length of English text (UTF-8), followed by those bytes
        uint16 LE  byte length of Persian text (UTF-8), followed by those bytes

Usage:  python tools/build_trdata.py
"""
import collections
import hashlib
import json
import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import bidi_fix  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
BIDI_DEFAULT = 'rlm'  # tested in game: punctuation lands at the end of Persian sentences
SOURCES = sorted((ROOT / 'translations').glob('mw4-fa-*.jsonl'))
OUT = ROOT / 'files' / 'trdata2.bin'

# Format codes that must survive translation unchanged.
TOKEN = re.compile(r'&&\d|%[-\d.]*[A-Za-z]|\{[^}]*\}|\^\?#x[0-9a-fA-F]+\.#x[0-9a-fA-F]+\^?|\^[0-9A-Za-z%+$]|\n')
FORBIDDEN = set('‎‏‪‫‬‭‮⁦⁧⁨⁩يك')


def load():
    rows = {}
    for path in SOURCES:
        for n, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
            if not line.strip():
                continue
            r = json.loads(line)
            i = r['index']
            if i in rows:
                sys.exit(f'{path.name}:{n}: duplicate index {i}')
            rows[i] = r
    if sorted(rows) != list(range(len(rows))):
        sys.exit('indexes are not continuous from 0')
    return [rows[i] for i in range(len(rows))]


def validate(rows):
    problems = []
    for r in rows:
        en, fa = r['en'], r['fa']
        if r['status'] == 'keep_original':
            continue
        if not fa.strip():
            problems.append((r['index'], 'empty translation'))
        elif collections.Counter(TOKEN.findall(en)) != collections.Counter(TOKEN.findall(fa)):
            problems.append((r['index'], 'format codes or line breaks differ from English'))
        elif any(c in FORBIDDEN for c in fa):
            problems.append((r['index'], 'Arabic yeh/kaf or invisible direction mark'))
    return problems


def death_screen():
    """Rows drawn with a font that has no Persian letters: force a game font that has them."""
    cfg = json.loads((ROOT / 'tools' / 'death_screen_rows.json').read_text(encoding='utf-8'))
    fonts = {i: cfg['hint_font'] for i in cfg['hints']}
    fonts.update({i: cfg['title_font'] for i in cfg['titles']})
    return fonts, cfg.get('extra', []), cfg['hint_font']


def encode(rows, bidi_mode=None):
    fonts, extra, hint_font = death_screen()
    pairs = []
    for r in rows:
        if r['status'] == 'keep_original':
            fa = r['en']
        else:
            fa = bidi_fix.fix(r['fa'], bidi_mode)
            if r['index'] in fonts:
                fa = '^F' + fonts[r['index']] + '^' + fa
        pairs.append((r['en'], fa))
    # extra English variants the game uses that are missing from the original table
    known = {en for en, _ in pairs}
    for x in extra:
        if x['en'] not in known:
            pairs.append((x['en'], '^F' + hint_font + '^' + bidi_fix.fix(rows[x['copy_of']]['fa'], bidi_mode)))
    out = bytearray(struct.pack('<I', len(pairs)))
    for en, fa in pairs:
        for text in (en, fa):
            b = text.encode('utf-8')
            if len(b) > 0xFFFF:
                sys.exit(f'text longer than 65535 bytes: {en[:60]!r}')
            out += struct.pack('<H', len(b)) + b
    return bytes(out)


def main():
    # usage: build_trdata.py [--bidi rlm|move] [--out PATH]
    args = sys.argv[1:]
    bidi_mode = args[args.index('--bidi') + 1] if '--bidi' in args else BIDI_DEFAULT
    out = pathlib.Path(args[args.index('--out') + 1]) if '--out' in args else OUT
    rows = load()
    problems = validate(rows)
    if problems:
        for p in problems[:30]:
            print('row %d: %s' % p)
        sys.exit(f'{len(problems)} problem(s); fix them before building')
    data = encode(rows, bidi_mode)
    out.parent.mkdir(exist_ok=True)
    out.write_bytes(data)
    print('punctuation fix:', bidi_mode or 'none')
    counts = collections.Counter(r['status'] for r in rows)
    print(f"wrote {out}: {struct.unpack_from('<I', data)[0]} entries, {len(data)} bytes")
    print('statuses:', dict(counts))
    print('sha256:', hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    main()

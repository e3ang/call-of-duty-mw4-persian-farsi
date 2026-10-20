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


def encode(rows, bidi_mode=None):
    out = bytearray(struct.pack('<I', len(rows)))
    for r in rows:
        fa = r['en'] if r['status'] == 'keep_original' else bidi_fix.fix(r['fa'], bidi_mode)
        for text in (r['en'], fa):
            b = text.encode('utf-8')
            if len(b) > 0xFFFF:
                sys.exit(f"row {r['index']}: text longer than 65535 bytes")
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
    print(f'wrote {out}: {len(rows)} entries, {len(data)} bytes')
    print('statuses:', dict(counts))
    print('sha256:', hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    main()

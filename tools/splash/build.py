"""Build the independent splash and add its import to the unmodified v1.3 hook.

Usage: python tools/splash/build.py --zig C:/path/to/zig.exe
Requires pefile. Never reads or changes the game installation.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
from add_dll_dependency import add_dependency

ROOT = Path(__file__).resolve().parents[2]
BASELINE_HOOK = '294479490adbc552e0098939c1b07dbf548cc337fa8cd60824680695d283dca7'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zig', required=True)
    args = parser.parse_args()
    original = subprocess.check_output(['git', 'show', 'v1.3:files/trhook.dll'], cwd=ROOT)
    assert hashlib.sha256(original).hexdigest() == BASELINE_HOOK, 'Unexpected v1.3 hook'
    with tempfile.TemporaryDirectory(prefix='mw4-splash-') as tmp:
        tmp = Path(tmp)
        dll = tmp / 'mw4splash.dll'
        artwork = ROOT/'tools/splash/artwork.bmp'
        bitmap = artwork.read_bytes()
        assert bitmap[:2] == b'BM', 'Expected BMP artwork'
        digest = hashlib.sha256(bitmap[14:]).digest()
        (tmp/'splash_digest.h').write_text('#define MW4_SPLASH_SHA256_INITIALIZER {' + ','.join(str(x) for x in digest) + '}\n')
        resource = tmp/'splash.rc'
        resource.write_text('101 BITMAP "' + artwork.as_posix() + '"\n')
        compiled_resource = tmp/'splash.res'
        subprocess.run([args.zig, 'rc', '/fo', str(compiled_resource), str(resource)], check=True)
        subprocess.run([args.zig, 'cc', '-target', 'x86_64-windows-gnu', '-shared',
                        '-O2', '-s', '-I', str(tmp), str(ROOT/'tools/splash/mw4_splash.c'),
                        str(compiled_resource), '-o', str(dll),
                        '-luser32', '-lgdi32', '-lkernel32', '-lbcrypt'], check=True)
        source = tmp / 'trhook-v13.dll'; source.write_bytes(original)
        patched = tmp / 'trhook.dll'
        print(add_dependency(source, patched, 'mw4splash.dll', 'Mw4SplashInit'))
        (ROOT/'files/mw4splash.dll').write_bytes(dll.read_bytes())
        (ROOT/'files/trhook.dll').write_bytes(patched.read_bytes())

if __name__ == '__main__':
    main()

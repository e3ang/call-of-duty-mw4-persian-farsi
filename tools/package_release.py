"""Create the consumer ZIP from an explicit allowlist; no development files."""
import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    'install.bat': 'install.bat', 'uninstall.bat': 'uninstall.bat',
    'LICENSE': 'LICENSE', 'docs/README-release.md': 'README.md',
    'tools/installer.ps1': 'installer.ps1',
    **{f'files/{name}': f'files/{name}' for name in (
        'bink2w64.dll', 'trhook.dll', 'rtlcompanion.dll', 'trdata2.bin',
        'mw4splash.dll')},
}

def content(source):
    data = (ROOT/source).read_bytes()
    if source in ('install.bat', 'uninstall.bat'):
        data = data.replace(b'tools\\installer.ps1', b'installer.ps1')
    return data

def package(destination):
    destination = Path(destination)
    for source in FILES:
        if not (ROOT/source).is_file(): raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for source, target in FILES.items():
            archive.writestr('MW4-Farsi/'+target, content(source))
    with zipfile.ZipFile(destination) as archive:
        assert archive.testzip() is None
        assert set(archive.namelist()) == {'MW4-Farsi/'+name for name in FILES.values()}
        for source, target in FILES.items():
            assert archive.read('MW4-Farsi/'+target) == content(source)
    print(f'{destination}: {destination.stat().st_size:,} bytes, {len(FILES)} files')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('destination', help='Output ZIP path')
    package(parser.parse_args().destination)

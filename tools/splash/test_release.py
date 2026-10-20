"""Isolated runtime, PE dependency, packaging and installer checks.

python tools/splash/test_release.py --host path/to/test-host.exe --work path/to/fixtures --zip path/to/package.zip
Never installs to or launches the real game.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import uuid
import zipfile
import pefile

ROOT = Path(__file__).resolve().parents[2]
hash_file = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--zip', type=Path, required=True)
    args = parser.parse_args()
    sandbox = args.work.resolve() / ('v14-fixtures-' + uuid.uuid4().hex[:8])
    sandbox.mkdir(parents=True)
    report = {'sandbox': str(sandbox), 'runtime': {}, 'installer': []}
    assert hash_file(ROOT/'files/rtlcompanion.dll') == 'c4ec5e5a46ec2b16447726d64cba2b504ff228880772b52f07c2ea9685335f4a'
    assert hash_file(ROOT/'files/trdata2.bin') == '5c5a46e8ba98cfcc0445f4d5664eeab39d02a350fa6c51cd21a74a38217c37df'
    assert struct.unpack_from('<I', (ROOT/'files/trdata2.bin').read_bytes())[0] == 39903
    before = pefile.PE(data=subprocess.check_output(['git', 'show', 'v1.3:files/trhook.dll'], cwd=ROOT))
    after = pefile.PE(str(ROOT/'files/trhook.dll'))
    for section in before.sections:
        assert before.get_data(section.VirtualAddress, section.SizeOfRawData) == after.get_data(section.VirtualAddress, section.SizeOfRawData)
    def imports(pe):
        return [(d.dll, [(s.name, s.ordinal, s.address - pe.OPTIONAL_HEADER.ImageBase) for s in d.imports]) for d in pe.DIRECTORY_ENTRY_IMPORT]
    assert imports(after)[:-1] == imports(before)
    assert imports(after)[-1][0] == b'mw4splash.dll'
    assert imports(after)[-1][1][0][0] == b'Mw4SplashInit'
    assert b'rtlcompanion.dll' in [d.dll.lower() for d in after.DIRECTORY_ENTRY_IMPORT]
    report['approved_rtl_and_data_unchanged'] = True
    report['existing_hook_sections_and_imports_unchanged'] = True

    runtime = sandbox/'runtime'; runtime.mkdir()
    for path in (ROOT/'files').iterdir(): shutil.copy2(path, runtime/path.name)
    executable = runtime/'sp26-cod.exe'; shutil.copy2(args.host, executable)
    original_host_hash = hash_file(executable)
    image = runtime/'mw4-splash.bmp'
    splash = runtime/'mw4splash.dll'; original_splash = splash.read_bytes()
    embedded = pefile.PE(data=original_splash)
    bitmap_resource = next(r for r in embedded.DIRECTORY_ENTRY_RESOURCE.entries if r.id == 2).directory.entries[0].directory.entries[0].data.struct
    original_image = (ROOT/'tools/splash/artwork.bmp').read_bytes()
    assert embedded.get_data(bitmap_resource.OffsetToData, bitmap_resource.Size) == original_image[14:]
    report['embedded_artwork_matches_approved_bitmap'] = True
    for name, mode, command in [
        ('normal', 'image', 'visible'),
        ('external_image_ignored', 'external', 'visible'),
        ('modified_resource_rejected', 'tamper', 'hidden'),
        ('other_process', 'other', 'hidden'),
    ]:
        executable = runtime/'sp26-cod.exe'
        if image.exists(): image.unlink()
        splash.write_bytes(original_splash)
        if mode == 'external': image.write_bytes(b'external replacement must be ignored')
        if mode == 'tamper':
            damaged = bytearray(original_splash)
            damaged[embedded.get_offset_from_rva(bitmap_resource.OffsetToData) + bitmap_resource.Size - 10] ^= 1
            splash.write_bytes(damaged)
        if mode == 'other':
            executable = runtime/'other-host.exe'; shutil.copy2(args.host, executable)
        result_file = runtime/'host-result.txt'
        if result_file.exists(): result_file.unlink()
        proc = subprocess.run([str(executable), command], cwd=runtime, capture_output=True, text=True, timeout=12)
        assert proc.returncode == 0, name + ': ' + proc.stdout + proc.stderr
        result = result_file.read_text()
        assert 'passed=1' in result, name + ': host did not report completion: ' + result
        assert hash_file(executable) == original_host_hash
        report['runtime'][name] = result.strip()

    with zipfile.ZipFile(args.zip) as archive:
        assert archive.testzip() is None
        assert len(archive.namelist()) == 10
        assert not any('/tools/' in name or name.endswith('.bmp') for name in archive.namelist())
        assert not any(name.endswith(('.c', '.h', '.py', '.jsonl', '.lib', '.log')) for name in archive.namelist())
        archive.extractall(sandbox/'package')
    package = sandbox/'package/MW4-Farsi'
    for name in (ROOT/'files').iterdir(): assert hash_file(package/'files'/name.name) == hash_file(name)
    env = os.environ.copy()
    env['PSModulePath'] = str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/Modules')
    def install(mode, folder, expected=0):
        proc = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            str(package/'installer.ps1'), '-Mode', mode, '-Game', str(folder), '-Quiet'],
            capture_output=True, encoding='utf-8', errors='replace', env=env, timeout=30)
        assert proc.returncode == expected, proc.stdout + proc.stderr
        return proc.stdout

    normal = sandbox/'بازی آزمایشی با فاصله/sp26'; normal.mkdir(parents=True)
    (normal/'sp26-cod.exe').write_bytes(b'not executable: installer fixture')
    (normal/'bink2w64.dll').write_bytes(b'original bink fixture')
    (normal/'trdata2.bin').write_bytes(b'previous translation fixture')
    originals = {name: hash_file(normal/name) for name in ('bink2w64.dll', 'trdata2.bin')}
    install('install', normal)
    for path in (package/'files').iterdir(): assert hash_file(normal/path.name) == hash_file(path)
    backups = {name: (hash_file(normal/(name+'.fa-backup')), (normal/(name+'.fa-backup')).stat().st_mtime_ns) for name in originals}
    for name in originals: assert backups[name][0] == originals[name]
    install('install', normal)
    for name in originals: assert backups[name] == (hash_file(normal/(name+'.fa-backup')), (normal/(name+'.fa-backup')).stat().st_mtime_ns)
    install('uninstall', normal)
    for name in originals: assert hash_file(normal/name) == originals[name]
    for name in ('trhook.dll', 'rtlcompanion.dll', 'mw4splash.dll', 'mw4-splash.bmp'): assert not (normal/name).exists()
    report['installer'].append('extract ZIP; install/reinstall in Unicode path; exact backup restoration; dependency and artwork cleanup')

    no_backup = sandbox/'no-backup/sp26'; no_backup.mkdir(parents=True)
    (no_backup/'sp26-cod.exe').write_bytes(b'fixture')
    shutil.copy2(package/'files/bink2w64.dll', no_backup/'bink2w64.dll')
    install('install', no_backup)
    text = install('uninstall', no_backup)
    assert 'mw4splash.dll were kept' in text
    for name in ('trhook.dll', 'rtlcompanion.dll', 'mw4splash.dll', 'bink2w64.dll'):
        assert hash_file(no_backup/name) == hash_file(package/'files'/name)
    assert not (no_backup/'trdata2.bin').exists() and not (no_backup/'mw4-splash.bmp').exists()
    report['installer'].append('missing original Bink backup retains complete DLL dependency chain and removes translation/artwork')

    missing = package/'files/mw4splash.dll'; missing_bytes = missing.read_bytes(); missing.unlink()
    before_install = {p.name: hash_file(p) for p in normal.iterdir()}
    install('install', normal, expected=1)
    assert before_install == {p.name: hash_file(p) for p in normal.iterdir()}
    missing.write_bytes(missing_bytes)
    report['installer'].append('incomplete package fails before changing installed files')

    running_game = sandbox/'running-game/sp26'; running_game.mkdir(parents=True)
    shutil.copy2(args.host, running_game/'sp26-cod.exe')
    (running_game/'bink2w64.dll').write_bytes(b'original fixture')
    proc = subprocess.Popen([str(running_game/'sp26-cod.exe'), 'hold'], cwd=running_game)
    try:
        snapshot = {p.name: hash_file(p) for p in running_game.iterdir()}
        text = install('install', running_game, expected=1)
        assert 'Close the game first' in text
        assert snapshot == {p.name: hash_file(p) for p in running_game.iterdir()}
    finally:
        proc.terminate(); proc.wait(timeout=5)
    report['installer'].append('running target game process blocks installation with files unchanged')
    report['passed'] = True
    (sandbox/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__': main()

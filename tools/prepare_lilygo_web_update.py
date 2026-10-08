"""Prepare, but never flash, a settings-preserving LILYGO web image.

Usage: python tools/prepare_lilygo_web_update.py <16MB-backup> <mklittlefs>
Or:    python tools/prepare_lilygo_web_update.py <1MB-web-backup> <mklittlefs> --web-only
The second form requires a separately verified partition layout and reads only
the current SystemFS backup; it never substitutes an older settings snapshot.
Requires the documented InkDeck LILYGO partition layout. Outputs remain beside
the backup (normally in .pio). Fails rather than overwriting a previous staging
directory. The original binary, NVS, app slots and SD data are never modified.
"""
from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import sys

backup = Path(sys.argv[1]).resolve()
mkfs = Path(sys.argv[2]).resolve()
image = backup.read_bytes()
web_only = sys.argv[3:] == ['--web-only']
assert not sys.argv[3:] or web_only, 'Unknown arguments'
assert len(image) == (0x100000 if web_only else 0x1000000), 'Unexpected backup length'
partitions = {}
for off in (() if web_only else range(0x8000, 0x8c00, 32)):
    magic, kind, subtype, start, size, label, flags = struct.unpack_from('<HBBII16sI', image, off)
    if magic != 0x50aa:
        break
    partitions[label.split(b'\0')[0].decode()] = (start, size)
if not web_only:
    assert partitions.get('spiffs') == (0x810000, 0x100000), partitions
    assert partitions.get('app0') == (0x10000, 0x400000), partitions
    assert partitions.get('app1') == (0x410000, 0x400000), partitions

root = backup.parent
old = root / 'web-before.bin'
assert old != backup, 'Use a distinct input backup filename'
old.write_bytes(image if web_only else image[0x810000:0x910000])
stage, verify = root / 'web-merged-files', root / 'web-verified-files'
for directory in (stage, verify):
    directory.mkdir(exist_ok=True)
    assert not any(directory.iterdir()), 'Staging directory is not empty: ' + str(directory)
args = [str(mkfs), '-b', '4096', '-p', '256', '-s', '1048576']
subprocess.run(args + ['-u', stage.name, old.name], cwd=root, check=True)

def hashes(directory):
    return {p.relative_to(directory).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()}

before = hashes(stage)
assets = ('index.html', 'script.js', 'style.css')
source = Path(__file__).resolve().parents[1] / 'data'
for name in assets:
    shutil.copyfile(source / name, stage / name)
merged = root / 'web-merged.bin'
subprocess.run(args + ['-c', stage.name, merged.name], cwd=root, check=True)
subprocess.run(args + ['-u', verify.name, merged.name], cwd=root, check=True)
after = hashes(verify)
assert set(after) == set(before) | set(assets)
for name, digest in before.items():
    if name not in assets:
        assert after[name] == digest, 'Non-web file changed: ' + name
for name in assets:
    assert after[name] == hashlib.sha256((source / name).read_bytes()).hexdigest()
print('Verified unchanged non-web files:', len(set(before) - set(assets)))
print('Prepared:', merged)
print('Original backup SHA256:', hashlib.sha256(image).hexdigest())
if not web_only:
    print('OTA records (sequence, state):', [
        (struct.unpack_from('<I', image, off)[0], struct.unpack_from('<I', image, off+24)[0])
        for off in (0xe000, 0xf000)])

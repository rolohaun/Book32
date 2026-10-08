"""Opt-in LILYGO hardware smoke test; never uses or overwrites user ROMs.

Usage: python tools/test_device_rom_upload.py http://<device-ip>
Uploads uniquely named original synthetic CGB and NES ROMs, checks
validation/duplicates, then removes only those test files via the ROM route.
Run at the home screen. No reset, game launch or firmware write is performed.
"""
import sys
import uuid
import requests
from pathlib import Path

base = sys.argv[1].rstrip('/')
session = requests.Session()
status = session.get(base + '/api/status', timeout=10).json()
assert status.get('romUpload') is True
assert status.get('romMaxBytes') == 8388608
for asset in ('index.html', 'script.js', 'style.css'):
    response = session.get(base + '/' + asset, timeout=10)
    response.raise_for_status()
    assert response.content == (Path(__file__).resolve().parents[1] / 'data' / asset).read_bytes(), asset
print('Device serves the exact updated web assets; version:', status.get('version'))
name = 'InkDeck-test-' + uuid.uuid4().hex[:12] + '.gbc'
test_names = [name, name[:-4] + '.nes']
unsupported_names = [name[:-4] + ext for ext in ('.md', '.gen', '.bin')]
def listing():
    response = session.get(base + '/api/roms', timeout=10)
    response.raise_for_status()
    return {item['name']: item['size'] for item in response.json()['roms']}
before = listing()
assert not any(n in before for n in test_names)
for invalid_name in ('../outside.gb', '/test.gbc', 'test.gb.inkdeck.sav', 'bad\\test.gb', 'test.gb\x00.extra'):
    rejected = session.delete(base + '/api/roms/delete', params={'name': invalid_name}, timeout=10)
    assert rejected.status_code == 400, (invalid_name, rejected.status_code)
assert session.delete(base + '/api/roms/delete', timeout=10).status_code == 400
assert session.delete(base + '/api/roms/delete', params={'name': name}, timeout=10).status_code == 404
assert listing() == before
rom = bytearray(32768)
rom[0x100:0x103] = b'\xc3\x50\x01'  # JP 0150
rom[0x150:0x152] = b'\x18\xfe'       # JR -2
rom[0x134:0x140] = b'INKDECK TEST'
rom[0x143] = 0xc0
rom[0x14d] = (-sum(rom[0x134:0x14d]) - 25) & 255
def upload(payload, filename=name):
    return session.post(base + '/api/roms/upload', files={'file': (filename, payload)}, timeout=45)
try:
    assert upload(b'partial ROM').status_code == 422
    invalid = bytearray(rom); invalid[0x14d] ^= 1
    assert upload(invalid).status_code == 422
    assert upload(rom, 'invalid?.gbc').status_code == 400
    for filename in unsupported_names:
        assert upload(rom, filename).status_code == 400
    assert listing() == before
    response = upload(rom)
    assert response.status_code == 201, (response.status_code, response.text)
    assert response.json()['path'] == '/roms/' + name
    assert listing()[name] == len(rom)
    assert upload(rom).status_code == 409
    assert listing()[name] == len(rom)
    print('Hardware ROM API PASS: CGB upload, exact size/path, short/header/name validation, duplicate protection')
    nes=bytearray(16400);nes[:4]=b'NES\x1a';nes[4]=1
    nes[16:19]=b'\x4c\x00\x80'
    for offset in (0x3ffa,0x3ffc,0x3ffe): nes[16+offset:18+offset]=b'\x00\x80'
    for filename in test_names[1:]:
        payload=nes
        assert upload(b'partial',filename).status_code==422
        response=upload(payload,filename)
        assert response.status_code==201,(response.status_code,response.text)
        assert listing()[filename]==len(payload)
        assert upload(payload,filename).status_code==409
    print('Hardware ROM API PASS: NES sizes, validation, duplicate protection; retired formats rejected')
finally:
    # This random name was absent before this test; never delete another file.
    for filename in test_names:
        if filename in listing():
            removed = session.delete(base + '/api/roms/delete', params={'name': filename}, timeout=10)
            removed.raise_for_status()
            assert removed.json().get('savesPreserved') is True
            assert session.delete(base + '/api/roms/delete', params={'name': filename}, timeout=10).status_code == 404
    assert listing() == before, 'ROM inventory changed unexpectedly'
    print('Only the generated test ROMs were removed; original ROM inventory unchanged')

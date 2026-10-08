const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
for (const file of ['lib/Apps/AppPaperboy/GenesisCore.c', 'lib/Apps/AppPaperboy/GenesisCore.h', 'lib/InkGenesis/library.json']) {
  assert(!fs.existsSync(path.join(root, file)), `Retired emulator still present: ${file}`);
}
const core = read('lib/Apps/AppPaperboy/ConsoleCore.c');
assert.match(core, /INK_SYSTEM_GB/);
assert.match(core, /INK_SYSTEM_NES/);
for (const file of ['lib/Apps/AppPaperboy/ConsoleCore.c', 'lib/Apps/AppPaperboy/InkBoyUi.h',
  'lib/Apps/AppPaperboy/AppPaperboy.cpp', 'data/index.html', 'data/script.js', 'docs/installer.js']) {
  assert.doesNotMatch(read(file), /INK_SYSTEM_SEGA|inkGenesis|ClownMDEmu|Genesis|Sega/, file);
}
assert.match(read('data/index.html'), /accept="\.gb,\.gbc,\.nes"/);
assert.match(read('data/index.html'), /AGPL/);
assert.match(read('data/index.html'), /Complete source and license notices/);
assert.match(read('THIRD_PARTY_NOTICES.md'), /CrankBoy/);
assert.match(read('THIRD_PARTY_NOTICES.md'), /Nofrendo/);
console.log('Ink Boy release PASS: GB/GBC and NES only; current UI and source notices agree');

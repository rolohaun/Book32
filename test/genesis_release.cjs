// Fail closed if the retired non-commercial CPU source returns to this release.
const fs=require('node:fs'),assert=require('node:assert/strict');
const port=fs.readFileSync('lib/Apps/AppPaperboy/GenesisCore.c','utf8');
assert.match(port,/ClownMDEmu_Iterate/);
assert.match(port,/inkGenesisSilentFm/);
assert(!fs.existsSync('lib/InkGenesis/src/cpus') || !fs.readdirSync('lib/InkGenesis/src/cpus',{recursive:true,withFileTypes:true}).some(e=>e.isFile()));
assert(!fs.existsSync('tools/import_gwenesis.py'));
for(const p of ['lib/InkGenesis/clownmdemu','lib/InkGenesis/clownmdemu/libraries/clown68000','lib/InkGenesis/clownmdemu/libraries/clownz80']){
    assert.match(fs.readFileSync(p+'/LICENCE.txt','utf8'),/GNU AFFERO GENERAL PUBLIC LICENSE/);
}
const app=fs.readFileSync('lib/Apps/AppPaperboy/AppPaperboy.cpp','utf8');
assert.match(app,/selected == INK_SYSTEM_SEGA \? "\.inkdeck\.clown\.sav" : "\.inkdeck\.sav"/);
assert.match(app,/inkRomSystem\(path.c_str\(\)\).*INK_SYSTEM_SEGA.*\.inkdeck\.clown\.sav/);
assert.match(fs.readFileSync('data/index.html','utf8'),/<footer[\s\S]*source code &amp; licenses/);
console.log('Genesis release PASS: ClownMDEmu core/licenses, retired CPUs absent, isolated saves, visible source link');

// Offline regression tests: selection and manifest integrity, no serial writes.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const docs = path.resolve(__dirname, '../docs');
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    textContent: '', lastChild: {textContent: ''}, disabled: false, dataset: {},
    classList: {toggle() {}, add() {}, remove() {}},
    setAttribute() {}, addEventListener() {}, style: {}, scrollIntoView() {}
  });
  return elements.get(id);
}
const cards = ['book32','sticky','lilygo'].map(id => Object.assign(element(id), {dataset: {profile: id}}));
const sandbox = {
  document: {querySelector: element, querySelectorAll: () => cards},
  navigator: {serial: {}},
  window: {isSecureContext: true, location: {href: 'https://example.test/', search: ''}, history: {replaceState() {}}},
  URL, URLSearchParams, console, setTimeout, Uint8Array,
};
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.join(docs, 'installer.js'),'utf8'),sandbox);
const expected = {book32: ['1.3.1',0x510000,16], sticky: ['1.3.1',0x810000,32], lilygo: ['1.3.1',0x810000,16]};
for (const [id,[version,fsOffset,flashMB]] of Object.entries(expected)) {
  vm.runInContext(`selectDevice('${id}')`, sandbox);
  const profile = vm.runInContext(`profiles.${id}`,sandbox);
  assert.equal(profile.version,version);
  assert.equal(element('#release-name').textContent,`InkDeck ${version}`);
  assert.equal(element('#flash-button').lastChild.textContent,` Flash InkDeck ${version}`);
  assert.equal(element('#flash-button').disabled,false);
  const manifest = JSON.parse(fs.readFileSync(path.join(docs,profile.manifest),'utf8'));
  assert.equal(manifest.version,version);
  const parts = manifest.builds[0].parts;
  assert.deepEqual(parts.map(p => p.offset),[0,0x8000,0xe000,0x10000,fsOffset]);
  for (let i=0; i<parts.length; i++) {
    const part=parts[i];
    const size=fs.statSync(path.join(docs,part.path)).size;
    assert(size>0);
    assert(part.offset+size <= (parts[i+1]?.offset ?? flashMB*1024*1024),part.path+' overlaps next partition');
  }
  assert.equal(profile.experimental===true,id==='lilygo');
}
assert(fs.readFileSync(path.join(docs,'index.html'),'utf8').includes('data-profile="lilygo"'));
console.log('PASS: all three profile selections, versions, file offsets and assets');

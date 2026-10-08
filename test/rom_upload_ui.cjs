// Capability/progress/error tests for the actual device web script.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
class Element {
  constructor() {
    this.children = []; this.files = []; this.style = {}; this.value = '';
    this.classes = new Set(['hidden']);
    this.classList = { add: x => this.classes.add(x), remove: x => this.classes.delete(x),
      toggle: (x, on) => on ? this.classes.add(x) : this.classes.delete(x) };
  }
  set innerHTML(value) { throw Error('ROM paths must not render filenames as HTML'); }
  setAttribute() {}
  replaceChildren() { this.children = []; this.textContent = ''; }
  append(...items) { this.children.push(...items); }
}
const elements = new Map();
const el = id => { if (!elements.has(id)) elements.set(id, new Element()); return elements.get(id); };
let status = { battery: 80, voltage: 4, freeSpace: 1000, totalSpace: 8000 };
let romResponse = { ok: true, data: { roms: [] } };
let requests = [], lastXhr;
let confirmed = true, prompt = '', deleteResponse = { ok: true, data: { status: 'ok', savesPreserved: true } };
let pendingDelete = null;
let lightResponse = {ok:true, data:{brightness:75,front:1,side:0}};
let sleepResponse = {sleepTimeout:10,sleepMessage:'My custom sleep message',defaultSleepMessage:'Press BOOT to wake'};
class Xhr {
  constructor() { this.upload = {}; lastXhr = this; }
  open(method, path) { assert.equal(method, 'POST'); assert.equal(path, '/api/roms/upload'); }
  send(body) { assert(body.file); }
}
const context = vm.createContext({ console, setInterval() {}, setTimeout() {}, clearTimeout() {}, AbortController,
  confirm: text => { prompt = text; return confirmed; },
  document: { getElementById: el, querySelectorAll: selector => selector === '.rom-delete' ?
    el('rom-list').children.map(row => row.children[1].children[1]) : [], querySelector: () => null,
    createElement: () => new Element(), addEventListener() {} },
  fetch: async (path, options) => {
    requests.push({ path, options });
    if (path === '/api/settings/lilygo') return {ok:lightResponse.ok,json:async()=>lightResponse.data};
    if (path === '/api/settings/sleep') return {ok:true,json:async()=>sleepResponse};
    if (path.startsWith('/api/roms/delete?')) {
      if (pendingDelete) await pendingDelete;
      if (deleteResponse.error) throw deleteResponse.error;
      return { ok: deleteResponse.ok, json: async () => deleteResponse.data };
    }
    return path === '/api/status' ? { ok: true, json: async () => status } :
      { ok: romResponse.ok, json: async () => romResponse.data };
  }, XMLHttpRequest: Xhr, FormData: class { append(key, value) { this[key] = value; } }
});
vm.runInContext(fs.readFileSync('data/script.js', 'utf8'), context);
const call = source => vm.runInContext(source, context);
(async () => {
  for (const [bytes, expected] of [[0,'0 B'], [1,'1 B'], [1023,'1023 B'],
    [1024,'1 KB'], [32768,'32 KB'], [65536,'64 KB'], [131072,'128 KB'],
    [524288,'512 KB'], [1048576,'1 MB'], [1572864,'1.5 MB'], [8388608,'8 MB'], [1073741824,'1 GB']]) {
    assert.equal(call(`formatFileSize(${bytes})`), expected);
  }
  for (const invalid of ['null', 'undefined', 'NaN', 'Infinity', '-1', '"65536"'])
    assert.equal(call(`formatFileSize(${invalid})`), 'Unknown size');
  // Dashboard keeps its existing MB/GB units.
  assert.equal(call('formatStorage(1048576)'), '1 MB');
  await call('fetchStatus()');
  assert(el('games-nav').classes.has('hidden')); assert(el('games').classes.has('hidden'));
  requests = []; call("showTab('games')"); assert.equal(requests.length, 0);
  status = { ...status, romUpload: true, romMaxBytes: 8388608 };
  await call('fetchStatus()'); assert(!el('games-nav').classes.has('hidden'));
  romResponse.data.roms = [{ name: '<img src=x onerror=alert(1)>.gbc', size: 1048576 }];
  await call('fetchRoms()');
  assert.equal(el('rom-list').children[0].children[0].textContent, romResponse.data.roms[0].name);
  assert.match(el('rom-list').children[0].children[1].children[0].textContent, /MB/);
  assert.equal(el('rom-list').children[0].children[1].children[0].title, '1,048,576 bytes');
  const deleteButton = el('rom-list').children[0].children[1].children[1];
  assert.equal(deleteButton.textContent, 'Delete');
  confirmed = false; requests = [];
  await deleteButton.onclick(); assert.equal(requests.length, 0); assert.match(prompt, /Saved games will be kept/);
  confirmed = true;
  await call('deleteRom("Game & #1.GBC")');
  assert.equal(requests[0].path, '/api/roms/delete?name=Game%20%26%20%231.GBC');
  assert.equal(requests[0].options.method, 'DELETE');
  assert.match(el('rom-status').textContent, /Saved games were kept/);
  assert.equal(el('rom-upload-button').disabled, false);
  deleteResponse = { ok: false, data: { error: 'ROM storage is busy' } };
  await call('deleteRom("test.gb")'); assert.equal(el('rom-status').textContent, 'ROM storage is busy');
  for (const error of [new TypeError('Failed to fetch'), Object.assign(new Error('Timeout'), { name: 'AbortError' })]) {
    deleteResponse = { error }; await call('deleteRom("test.gb")');
    assert.equal(el('rom-refresh-button').disabled, false);
    assert(!el('rom-status').textContent.startsWith('Deleted'));
  }
  deleteResponse = { ok: true, data: { status: 'ok' } };
  let releaseDelete;
  pendingDelete = new Promise(resolve => releaseDelete = resolve);
  const deleting = call('deleteRom("test.gb")');
  assert(el('rom-upload-button').disabled); assert(el('rom-refresh-button').disabled);
  requests = []; await call('deleteRom("another.gb")'); call('uploadRom()');
  assert.equal(requests.length, 0); assert(!lastXhr);
  releaseDelete(); await deleting; pendingDelete = null;
  el('rom-file').files = [{ name: 'test.txt', size: 32768 }];
  call('uploadRom()'); assert(!lastXhr);
  el('rom-file').files = [{ name: 'test.GBC', size: 32768 }];
  call('uploadRom()'); assert(el('rom-upload-button').disabled); assert(el('rom-file').disabled);
  const first = lastXhr; call('uploadRom()'); assert.equal(lastXhr, first);
  requests = []; await call('deleteRom("test.gb")'); assert.equal(requests.length, 0);
  first.upload.onprogress({ lengthComputable: true, loaded: 5, total: 10 });
  assert.equal(el('rom-progress').value, 50);
  first.upload.onprogress({ lengthComputable: true, loaded: 10, total: 10 });
  assert.match(el('rom-status').textContent, /Verifying/);
  first.status = 201; first.responseText = '{"path":"/roms/test.GBC"}'; first.onload();
  assert.equal(el('rom-upload-button').disabled, false);
  assert.match(el('rom-status').textContent, /Saved to \/roms\/test.GBC/);
  assert.match(el('rom-status').textContent, /Open Ink Boy/);
  call('uploadRom()'); lastXhr.status = 409; lastXhr.responseText = '{"error":"Already exists"}'; lastXhr.onload();
  assert.equal(el('rom-status').textContent, 'Already exists');
  for (const event of ['onerror', 'ontimeout', 'onabort']) {
    call('uploadRom()'); lastXhr[event](); assert.equal(el('rom-upload-button').disabled, false);
  }
  call('uploadRom()'); lastXhr.status = 200; lastXhr.responseText = '<html>error</html>'; lastXhr.onload();
  assert.match(el('rom-status').textContent, /Unexpected response/);
  for(const [name,size] of [['Test.NES',16400],['Test.md',512],['Test.GEN',4194304],['Test.bin',65536]]) {
    el('rom-file').files=[{name,size}];const previous=lastXhr;call('uploadRom()');assert.notEqual(lastXhr,previous);
    lastXhr.status=201;lastXhr.responseText=JSON.stringify({path:'/roms/'+name});lastXhr.onload();
    assert.match(el('rom-status').textContent,/Saved to/);
  }
  for(const [name,size] of [['Big.nes',2097153],['Big.md',4194305],['Empty.nes',0],['Small.gen',511],['Archive.zip',65536],['Interleaved.smd',65536]]) {
    el('rom-file').files=[{name,size}];const previous=lastXhr;call('uploadRom()');assert.equal(lastXhr,previous);
  }
  romResponse = { ok: false, data: { error: 'Insert SD card' } };
  await call('fetchRoms()'); assert.equal(el('rom-list').textContent, 'Insert SD card');
  status = { ...status, romUpload: false };
  await call('fetchStatus()'); assert(el('games-nav').classes.has('hidden'));
  // LILYGO-only settings load, successful save and server-error feedback.
  assert(el('lilygo-settings-card').classes.has('hidden'));
  requests=[]; await call('getLilygoControls()'); await call('saveLilygoControls()');
  assert.equal(requests.length,0);
  status={...status,lilygoControls:true}; await call('fetchStatus()');
  await call('getLilygoControls()');
  assert(!el('lilygo-settings-card').classes.has('hidden'));
  assert.equal(el('light-brightness-label').textContent,'75%');
  lightResponse.data={status:'ok'}; el('light-brightness').value='50';
  await call('saveLilygoControls()');
  assert.match(el('lilygo-settings-status').textContent,/saved/);
  assert.deepEqual(JSON.parse(requests.at(-1).options.body),{brightness:50,front:0,side:0});
  assert.equal(el('lilygo-settings-save').disabled,false);
  lightResponse.ok=false; await call('saveLilygoControls()');
  assert.match(el('lilygo-settings-status').textContent,/Could not save/);
  assert.equal(el('lilygo-settings-save').disabled,false);
  console.log('ROM UI tests PASS: file sizes, board gating, safe names, upload progress/errors, confirmed deletion, cancellation, busy guards and errors');
  console.log('LILYGO settings UI PASS: capability gating, brightness display, save payload, error and button recovery');
  call('getSleepSettings()'); await new Promise(setImmediate);
  assert.equal(el('sleep-message').value,'My custom sleep message');
  assert.equal(el('sleep-message').placeholder,'Press BOOT to wake');
  sleepResponse={...sleepResponse,defaultSleepMessage:'Press power to wake'};
  call('getSleepSettings()'); await new Promise(setImmediate);
  assert.equal(el('sleep-message').placeholder,'Press power to wake');
  console.log('Sleep UI PASS: firmware-provided board default and unchanged custom message');
})().catch(error => { console.error(error); process.exitCode = 1; });

// Verify the actual browser-selected release, not just unversioned aliases.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const docs = path.join(root, 'docs');
const version = '1.3.1';
const targets = {
  book32: ['seeed_xiao_esp32s3', 'firmware', 'littlefs', 0x510000, 16],
  sticky: ['seeed_reterminal_sticky', 'book32-sticky-firmware', 'book32-sticky-littlefs', 0x810000, 32],
  lilygo: ['lilygo_t5s3_pro', 'inkdeck-lilygo-firmware', 'inkdeck-lilygo-littlefs', 0x810000, 16]
};
function element() {
  const classes = new Set();
  return { textContent: '', disabled: false, hidden: true, style: {}, dataset: {},
    attrs: {}, lastChild: {textContent: ''},
    classList: {toggle(k,on){on ? classes.add(k) : classes.delete(k);}, add(k){classes.add(k);}, remove(k){classes.delete(k);}},
    setAttribute(k,v){this.attrs[k]=v;}, addEventListener(){}, scrollIntoView(){},
    querySelector(){return element();} };
}
const elements = new Map();
const cards = Object.keys(targets).map(id => Object.assign(element(), {dataset:{profile:id}}));
let overrideManifest = null;
const context = vm.createContext({URL,URLSearchParams,Uint8Array,console,setTimeout,
  navigator:{serial:{}}, window:{isSecureContext:true,location:{href:'https://example.com/Book32/',search:''},history:{replaceState(){}}},
  document:{querySelectorAll:()=>cards,querySelector(k){if(!elements.has(k))elements.set(k,element());return elements.get(k);}},
  fetch:async url => {
    const local = path.join(docs, decodeURIComponent(new URL(url).pathname.replace('/Book32/','')));
    assert(local.startsWith(docs + path.sep));
    const bytes = fs.readFileSync(local);
    return {ok:true,json:async()=>overrideManifest || JSON.parse(bytes),arrayBuffer:async()=>bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength)};
  }
});
vm.runInContext(fs.readFileSync(path.join(docs,'installer.js'),'utf8'), context);
const hash = buffer => crypto.createHash('sha256').update(buffer).digest('hex');
(async()=>{
  for (const [id,[env,app,web,fsOffset,flashMB]] of Object.entries(targets)) {
    vm.runInContext(`selectDevice('${id}')`,context);
    assert.equal(elements.get('#release-name').textContent,`InkDeck ${version}`);
    assert.equal(elements.get('#flash-button').disabled,false);
    assert.equal(cards.filter(c=>c.attrs['aria-pressed']==='true').length,1);
    const profile = vm.runInContext(`profiles.${id}`,context);
    assert.equal(profile.version,version);
    assert.equal(profile.flashSize,`${flashMB}MB`);
    const manifest = JSON.parse(fs.readFileSync(path.join(docs,profile.manifest)));
    assert.equal(manifest.version,version);
    const loaded = await vm.runInContext(`loadFirmware(profiles.${id})`,context);
    assert.deepEqual(Array.from(loaded,p=>p.address),[0,0x8000,0xe000,0x10000,fsOffset]);
    for(let i=0;i<loaded.length;i++) {
      assert(loaded[i].data.length>0);
      assert(loaded[i].address+loaded[i].data.length <= (loaded[i+1]?.address ?? flashMB*1024*1024));
    }
    const partitions = Buffer.from(loaded[1].data);
    const labels = new Map();
    for(let i=0;i+32<=partitions.length && partitions.readUInt16LE(i)===0x50aa;i+=32) {
      const label=partitions.subarray(i+12,i+28).toString().split('\0')[0];
      labels.set(label,{offset:partitions.readUInt32LE(i+4),size:partitions.readUInt32LE(i+8)});
    }
    assert.equal(labels.get('spiffs').offset,fsOffset);
    assert(loaded[4].data.length<=labels.get('spiffs').size);
    const appPartition=[...labels.values()].find(p=>p.offset===0x10000);
    assert(loaded[3].data.length<=appPartition.size);
    for (const [name,part,buildFile] of [[app,loaded[3],'firmware.bin'],[web,loaded[4],'littlefs.bin']]) {
      const expected=hash(part.data);
      assert.equal(hash(fs.readFileSync(path.join(root,'.pio','build',env,buildFile))),expected);
      assert.equal(hash(fs.readFileSync(path.join(root,`${name}.bin`))),expected);
      assert.equal(hash(fs.readFileSync(path.join(docs,'firmware',`${name}.bin`))),expected);
    }
    assert(Buffer.from(loaded[3].data).includes(Buffer.from(version+'\0')));
    assert(!Buffer.from(loaded[3].data).includes(Buffer.from('1.3.1-inkboy17')));
    const factoryName=profile.manifest.replace('-update-','-factory-');
    const factory=JSON.parse(fs.readFileSync(path.join(docs,factoryName)));
    const merged=fs.readFileSync(path.join(docs,factory.builds[0].parts[0].path));
    assert.deepEqual(merged.subarray(0x10000),Buffer.from(loaded[3].data));
    assert.equal(factory.builds[0].parts[1].offset,fsOffset);
    assert(!merged.includes(Buffer.from('Copyright Karl Stenerud.  All rights reserved.')));
    if(id==='lilygo') assert(merged.includes(Buffer.from('ClownMDEmu / Genesis')));
    else assert(!merged.includes(Buffer.from('ClownMDEmu / Genesis')));
    console.log(`${id}: ${version}, firmware/web hashes, partition limits and factory image PASS`);
  }
  overrideManifest={version:'0.0.0'};
  await assert.rejects(vm.runInContext('loadFirmware(profiles.lilygo)',context),/version mismatch/);
  console.log('Installer selection and stale-manifest rejection PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});

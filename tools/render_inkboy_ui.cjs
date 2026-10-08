// Run the actual GFX host test, then losslessly encode its grayscale render.
const fs = require('node:fs');
const zlib = require('node:zlib');
const {WASI} = require('node:wasi');
const output = process.argv[3] || '.pio/inkboy-preview';
const pgmPath = output + '.pgm';
function crc32(data) {
  let crc=0xffffffff;
  for(const b of data) { crc^=b; for(let i=0;i<8;++i) crc=(crc>>>1)^((crc&1)?0xedb88320:0); }
  return (crc^0xffffffff)>>>0;
}
function chunk(type,data) {
  const bytes=Buffer.concat([Buffer.from(type),data]), result=Buffer.alloc(bytes.length+8);
  result.writeUInt32BE(data.length,0); bytes.copy(result,4); result.writeUInt32BE(crc32(bytes),result.length-4);
  return result;
}
(async()=>{
  const fd=fs.openSync(pgmPath,'w');
  try {
    const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{},stdout:fd,returnOnExit:true});
    const module=await WebAssembly.compile(fs.readFileSync(process.argv[2]));
    const instance=await WebAssembly.instantiate(module,wasi.getImportObject());
    if(wasi.start(instance)) throw Error('UI tests failed');
  } finally { fs.closeSync(fd); }
  const pgm=fs.readFileSync(pgmPath), match=/^P5\n(\d+) (\d+)\n255\n/.exec(pgm.subarray(0,40).toString('ascii'));
  if(!match) throw Error('Bad render header');
  const width=Number(match[1]),height=Number(match[2]),pixels=pgm.subarray(match[0].length);
  if(pixels.length!==width*height) throw Error('Bad render length');
  const ihdr=Buffer.alloc(13); ihdr.writeUInt32BE(width,0); ihdr.writeUInt32BE(height,4); ihdr[8]=8;
  const scanlines=Buffer.alloc((width+1)*height);
  for(let y=0;y<height;++y) pixels.copy(scanlines,y*(width+1)+1,y*width,(y+1)*width);
  const png=Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',ihdr),chunk('IDAT',zlib.deflateSync(scanlines)),chunk('IEND',Buffer.alloc(0))]);
  fs.writeFileSync(output+'.png',png);
  console.log('Rendered '+output+'.png');
})().catch(e=>{ console.error(e);process.exitCode=1; });

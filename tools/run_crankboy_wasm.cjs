// Execute the pure synthetic-ROM suite in WASI with no filesystem preopens,
// no environment variables, no network and no device access.
const fs = require('node:fs');
const { WASI } = require('node:wasi');
(async () => {
  const wasi = new WASI({ version: 'preview1', args: [], env: {}, preopens: {}, returnOnExit: true });
  const module = await WebAssembly.compile(fs.readFileSync(process.argv[2]));
  const instance = await WebAssembly.instantiate(module, wasi.getImportObject());
  const code = wasi.start(instance);
  if (code) process.exitCode = code;
})().catch(error => { console.error(error); process.exitCode = 1; });

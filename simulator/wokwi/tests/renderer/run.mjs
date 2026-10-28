import { WASI } from 'node:wasi';
import { readFileSync } from 'node:fs';
const wasi = new WASI({ version: 'preview1', args: [], env: {}, returnOnExit: true });
const module = await WebAssembly.compile(readFileSync(process.argv[2]));
const instance = await WebAssembly.instantiate(module, wasi.getImportObject());
process.exitCode = wasi.start(instance);

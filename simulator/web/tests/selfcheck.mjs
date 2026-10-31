// Runs the actual compiled C++ modules, rather than a JavaScript behavior copy.
// Node is an optional verification tool, not a browser-preview prerequisite.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { parseArgs } from 'node:util';
import { fileURLToPath } from 'node:url';
import { WASI } from 'node:wasi';
import { performance } from 'node:perf_hooks';

const { values, positionals } = parseArgs({
  options: { 'output-dir': { type: 'string' } },
  allowPositionals: true,
});
assert(positionals.length <= 1, 'Pass at most one preview.wasm path');
const wasmPath = path.resolve(positionals[0] ||
  fileURLToPath(new URL('../.build/preview.wasm', import.meta.url)));
const outputRoot = path.resolve(values['output-dir'] || path.dirname(wasmPath));
fs.mkdirSync(outputRoot, { recursive: true });

const bytes = fs.readFileSync(wasmPath);
const manifest = JSON.parse(fs.readFileSync(path.join(path.dirname(wasmPath), 'manifest.json'), 'utf8'));
assert.equal(manifest.contract, 'tando-web-preview/v1');
assert.equal(createHash('sha256').update(bytes).digest('hex'), manifest.wasm_sha256,
  'The tested module must match its build manifest');
const module = await WebAssembly.compile(bytes);
const serialPath = path.join(outputRoot, 'selfcheck-serial.log');
const output = fs.openSync(serialPath, 'w');
let instance;
const prefs = new Map();

function text(pointer) {
  const data = new Uint8Array(instance.exports.memory.buffer);
  let end = pointer;
  while (end < data.length && data[end]) end++;
  return new TextDecoder().decode(data.subarray(pointer, end));
}

const env = {
  preview_now_us: () => performance.now() * 1000,
  preview_get_pref: (key, fallback) => prefs.get(text(key)) ?? fallback,
  preview_put_pref: (key, value) => prefs.set(text(key), value >>> 0),
  preview_clear_prefs: () => prefs.clear(),
};

async function boot(seed) {
  const wasi = new WASI({
    version: 'preview1', args: [], env: {}, preopens: {}, stdout: output, stderr: output,
  });
  instance = await WebAssembly.instantiate(module, {
    wasi_snapshot_preview1: wasi.wasiImport, env,
  });
  wasi.initialize(instance);
  instance.exports.tando_seed(seed);
  instance.exports.tando_setup();
  return instance.exports;
}

try {
  // Deterministic test seeds; the browser supplies fresh cryptographic entropy.
  const e = await boot(0x54414e44);
  assert.equal(e.tando_time(), 500);

  function advance(from, to) {
    for (let time = from; time <= to; time += 16) e.tando_step(time);
  }

  function command(letter, time) {
    e.tando_input(letter.charCodeAt(0));
    e.tando_step(time);
  }

  function pixels(side) {
    const pointer = side === 'left' ? e.tando_left() : e.tando_right();
    return new Uint16Array(e.memory.buffer, pointer, 240 * 240);
  }

  function hash(side) {
    let value = 2166136261;
    for (const pixel of pixels(side)) value = Math.imul(value ^ pixel, 16777619);
    return value >>> 0;
  }

  advance(0, 2992);
  const idleHashes = [hash('left'), hash('right')];
  assert(pixels('left').some((pixel) => pixel !== 0));
  assert(pixels('right').some((pixel) => pixel !== 0));
  assert(e.tando_frames() >= 180, 'The 16 ms host cadence must admit eye pairs');

  command('p', 3008);
  advance(3024, 9000);
  assert.equal(prefs.get('credits'), 1);
  assert(prefs.get('started'));

  command('p', 9016);
  advance(9032, 15000);
  assert.equal(prefs.get('credits'), 1, 'Repeated PET must not grant extra stage credit');

  command('s', 15024);
  advance(15040, 17000);
  command('p', 17024);
  command('1', 17040);
  advance(17056, 18000);
  assert.equal(prefs.get('credits'), 1,
    'SLEEP must block PET/FOOD and pre-window SLEEP earns no credit');

  command('s', 18016);
  advance(18032, 21000);
  assert.equal(prefs.get('credits'), 1);
  command('1', 21024);
  advance(21040, 25000);
  assert.equal(prefs.get('credits'), 2,
    'FOOD must earn one independent family credit after WAKE');

  const timingStart = performance.now();
  advance(25016, 25016 + 16 * 5000);
  const cpuMs = performance.now() - timingStart;
  assert(pixels('left').some((pixel) => pixel !== 0));

  const previousInstance = instance;
  const reboot = await boot(0x12345678);
  reboot.tando_step(0);
  assert.equal(prefs.get('credits'), 2);
  assert(fs.readFileSync(serialPath, 'utf8').includes('Progress credits: 2 / 9'),
    'A fresh module must restore stored progress');
  instance = previousInstance;

  const result = {
    wasm_bytes: bytes.length,
    imports: WebAssembly.Module.imports(module),
    exports: WebAssembly.Module.exports(module).map((entry) => entry.name),
    firmware_pairs: e.tando_frames(),
    internal_ms: e.tando_time(),
    preferences: Object.fromEntries(prefs),
    idle_hashes: idleHashes,
    final_hashes: [hash('left'), hash('right')],
    cpu_benchmark: {
      steps: 5001, elapsed_ms: cpuMs, mean_ms_per_step: cpuMs / 5001,
      scope: 'Full canonical firmware/GFX with host adapters; no GUI or ESP/SPI emulation',
    },
    checks: [
      'Tested WASM matches manifest',
      'Two nonblank display buffers',
      '16 ms host cadence admitted',
      'PET one credit',
      'Repeated PET no duplicate credit',
      'SLEEP blocks PET/FOOD and grants no early credit',
      'WAKE and FOOD gain independent credit',
      'Fresh WASM instance restores stored progress',
    ],
  };
  fs.writeFileSync(path.join(outputRoot, 'selfcheck.json'), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result, null, 2));
} finally {
  fs.closeSync(output);
}

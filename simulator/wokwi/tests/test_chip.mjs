// Runs the compiled chip with Wokwi API boundary stubs, not the cloud engine.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const binary = readFileSync(new URL('../gc9a01.chip.wasm', import.meta.url));

async function createDisplay() {
  const memory = new WebAssembly.Memory({ initial: 256 });
  const view = new DataView(memory.buffer);
  const bytes = new Uint8Array(memory.buffer);
  const frame = new Uint8Array(240 * 240 * 4);
  const pins = new Map();
  const levels = new Map();
  const watches = new Map();
  const stats = { bufferWrites: 0, spiCallbacks: 0 };
  let spi;
  let pending;
  let instance;
  const string = pointer => {
    let end = pointer;
    while (bytes[end]) end++;
    return new TextDecoder().decode(bytes.subarray(pointer, end));
  };
  function complete() {
    if (!pending) return;
    const transfer = pending;
    pending = undefined;
    if (transfer.data.length) {
      stats.spiCallbacks++;
      bytes.set(transfer.data, transfer.pointer);
      instance.exports.__indirect_function_table.get(spi.callback)(spi.user, transfer.pointer, transfer.data.length);
    }
  }
  const env = {
    memory,
    pinInit(pointer, mode) {
      const name = string(pointer);
      if (!pins.has(name)) {
        pins.set(name, pins.size + 1);
        levels.set(pins.get(name), mode === 2 ? 1 : 0);
      }
      return pins.get(name);
    },
    pinRead: pin => levels.get(pin),
    pinWatch(pin, config) {
      watches.set(pin, { user: view.getUint32(config, true), callback: view.getUint32(config + 8, true) });
      return 1;
    },
    spiInit(config) {
      spi = { user: view.getUint32(config, true), callback: view.getUint32(config + 20, true) };
      return 1;
    },
    spiStart(id, pointer, count) { assert.equal(id, 1); pending = { pointer, count, data: [] }; },
    spiStop() { complete(); pending = undefined; },
    framebufferInit(width, height) {
      view.setUint32(width, 240, true);
      view.setUint32(height, 240, true);
      return 1;
    },
    bufferWrite(id, offset, pointer, length) {
      assert.equal(id, 1);
      assert.ok(offset + length <= frame.length);
      frame.set(bytes.subarray(pointer, pointer + length), offset);
      stats.bufferWrites++;
    },
  };
  const wasi = { fd_close: () => 0, fd_fdstat_get: () => 0, fd_seek: () => 0,
    fd_write(fd, iov, count, written) {
      let length = 0;
      for (let i = 0; i < count; i++) length += view.getUint32(iov + i * 8 + 4, true);
      view.setUint32(written, length, true);
      return 0;
    } };
  ({ instance } = await WebAssembly.instantiate(binary, { env, wasi_snapshot_preview1: wasi }));
  instance.exports.chipInit();
  function setPin(name, value) {
    const id = pins.get(name);
    if (levels.get(id) === value) return;
    levels.set(id, value);
    const watch = watches.get(id);
    if (watch) instance.exports.__indirect_function_table.get(watch.callback)(watch.user, id, value);
  }
  function transfer(data) {
    for (const byte of data) {
      if (!pending) continue;
      pending.data.push(byte);
      if (pending.data.length === pending.count) complete();
    }
  }
  function segment(dc, data) {
    setPin('DC', dc);
    setPin('CS', 0);
    transfer(data);
    setPin('CS', 1);
  }
  function command(value, args = []) {
    segment(0, [value]);
    if (args.length) segment(1, args);
  }
  function pixel(x, y, rgb565) {
    command(0x2a, [0, x, 0, x]);
    command(0x2b, [0, y, 0, y]);
    command(0x2c);
    // Adafruit may split data bytes/CS transactions; parser state must survive.
    segment(1, [rgb565 >> 8]);
    segment(1, [rgb565 & 255]);
  }
  return { command, pixel, transfer, segment, setPin, stats,
    snapshot: () => frame.slice(),
    at: (x, y) => [...frame.slice((y * 240 + x) * 4, (y * 240 + x) * 4 + 4)] };
}

const left = await createDisplay();
const right = await createDisplay();
// The pinned Adafruit GC9A01A init enables INVON for the panel's normal colors.
for (const eye of [left, right]) {
  eye.command(0x21);
  eye.command(0x29);
  eye.pixel(120, 120, 0);
  assert.deepEqual(eye.at(120, 120), [0, 0, 0, 255], 'Adafruit startup must retain a black background');
  eye.pixel(121, 120, 0x475f); // Tando RGB565(65, 235, 255)
  assert.deepEqual(eye.at(121, 120), [64, 232, 248, 255], 'Normal eye color must remain cyan');
  eye.pixel(0, 0, 0xffff);
  assert.deepEqual(eye.at(0, 0), [0, 0, 0, 255], 'Round mask must keep corners black');
}
left.pixel(120, 120, 0xf800);
assert.deepEqual(left.at(120, 120), [248, 0, 0, 255]);
assert.deepEqual(right.at(120, 120), [0, 0, 0, 255], 'Display state must be independent');
left.transfer([0xff, 0xff]);
assert.deepEqual(left.at(120, 120), [248, 0, 0, 255], 'Deselected SPI data must be ignored');
left.command(0x20);
left.pixel(120, 120, 0);
assert.deepEqual(left.at(120, 120), [255, 255, 255, 255], 'Changing panel inversion must change color polarity');

// The firmware streams a 200x200 RGB565 canvas to each eye in one CS window.
const bulk = await createDisplay();
const initWrites = bulk.stats.bufferWrites;
bulk.command(0x21);
bulk.setPin('CS', 0);
for (const [dc, data] of [[0, [0x2a]], [1, [0, 20, 0, 219]], [0, [0x2b]], [1, [0, 20, 0, 219]], [0, [0x2c]]]) {
  bulk.setPin('DC', dc);
  bulk.transfer(data);
}
bulk.setPin('DC', 1);
const pixels = new Uint8Array(200 * 200 * 2);
const expected = bulk.snapshot();
for (let i = 0; i < 200 * 200; i++) {
  const value = (i * 101) & 0xffff;
  pixels[i * 2] = value >> 8;
  pixels[i * 2 + 1] = value & 255;
  const x = 20 + i % 200;
  const y = 20 + Math.floor(i / 200);
  const color = (x - 120) ** 2 + (y - 120) ** 2 > 120 ** 2
    ? [0, 0, 0, 255]
    : [(value >> 8) & 248, (value >> 3) & 252, (value << 3) & 248, 255];
  expected.set(color, (y * 240 + x) * 4);
}
const before = { ...bulk.stats };
bulk.transfer(pixels);
bulk.setPin('CS', 1);
assert.deepEqual(bulk.snapshot(), expected, 'Bulk frames must preserve every pixel, row boundary and circular mask');
const frameWrites = bulk.stats.bufferWrites - before.bufferWrites;
const frameCallbacks = bulk.stats.spiCallbacks - before.spiCallbacks;
assert.ok(frameWrites <= 600, `A 200x200 eye frame must batch framebuffer writes; got ${frameWrites}`);
assert.ok(frameCallbacks <= 24, `A 200x200 eye frame must use LCD-sized SPI chunks; got ${frameCallbacks}`);
assert.ok(initWrites <= 240, `Display initialization must batch black pixels; got ${initWrites}`);

// Row wrap, partial final packets and a split RGB565 byte must flush promptly.
bulk.command(0x2a, [0, 119, 0, 120]);
bulk.command(0x2b, [0, 119, 0, 120]);
bulk.command(0x2c);
bulk.segment(1, [0xf8]);
bulk.segment(1, [0x00, 0x07, 0xe0, 0x00, 0x1f, 0xff, 0xff, 0x47, 0x5f]);
assert.deepEqual(bulk.at(119, 119), [64, 232, 248, 255], 'Window wrap must replace only its first pixel');
assert.deepEqual(bulk.at(120, 119), [0, 252, 0, 255]);
assert.deepEqual(bulk.at(119, 120), [0, 0, 248, 255]);
assert.deepEqual(bulk.at(120, 120), [248, 252, 248, 255]);

for (const reset of [() => bulk.command(0x01), () => { bulk.setPin('RST', 0); bulk.setPin('RST', 1); }]) {
  bulk.command(0x21);
  bulk.pixel(120, 120, 0xffff);
  const writes = bulk.stats.bufferWrites;
  reset();
  assert.ok(bulk.snapshot().every((value, index) => value === (index % 4 === 3 ? 255 : 0)), 'Reset must clear the complete display to opaque black');
  assert.ok(bulk.stats.bufferWrites - writes <= 240, 'Reset must batch framebuffer clearing');
}
console.log(`Compiled chip boundary tests passed, including bulk frames (${frameWrites} framebuffer writes, ${frameCallbacks} SPI callbacks per eye), window wrap and resets. Cloud/GUI speed remains unverified.`);

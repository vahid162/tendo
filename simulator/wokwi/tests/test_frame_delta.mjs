import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const memory = new WebAssembly.Memory({ initial: 256 });
const view = new DataView(memory.buffer);
const bytes = new Uint8Array(memory.buffer);
let output = '';
const wasi = {
  fd_close: () => 0, fd_fdstat_get: () => 0, fd_seek: () => 0,
  fd_write(fd, iov, count, written) {
    let length = 0;
    for (let i = 0; i < count; i++) {
      const pointer = view.getUint32(iov + i * 8, true);
      const size = view.getUint32(iov + i * 8 + 4, true);
      output += new TextDecoder().decode(bytes.subarray(pointer, pointer + size));
      length += size;
    }
    view.setUint32(written, length, true);
    return 0;
  },
};
const { instance } = await WebAssembly.instantiate(readFileSync(new URL('./frame_delta.test.chip.wasm', import.meta.url)), {
  env: { memory }, wasi_snapshot_preview1: wasi,
});
instance.exports.chipInit();
assert.equal(instance.exports.testResult(), 0, output);
console.log(output.trim());

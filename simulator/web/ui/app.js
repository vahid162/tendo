const $ = (id) => document.getElementById(id);
const commandButtons = [...document.querySelectorAll('[data-command]')];
const allowedCommands = new Set('p12sblweghrui');
const number = new Intl.NumberFormat('fa-IR', { maximumFractionDigits: 1 });
const decoder = new TextDecoder();
let storagePrefix;
let buildManifest;
const memoryPreferences = new Map();
const logLines = [];
let partialLog = '';
let logScheduled = false;
let exports;
let memory;
let explicitlyPaused = false;
let failed = false;
let initialized = false;
let activeElapsedMs = 0;
let lastTimestamp = null;
let previousFrames = -1;
let presentedFrames = 0;
let rateElapsedMs = 0;
let rateStartFrames = 0;
let rate = 0;
let framesAtStart = 0;
let wasmStepMs = 0;
let paintMs = 0;
let maxStepMs = 0;
let maxPaintMs = 0;
let memoryBuffer;
let eyeViews;
const paintTargets = ['eye-left', 'eye-right'].map((id) => {
  const context = $(id).getContext('2d', { alpha: false });
  const image = context.createImageData(240, 240);
  return { context, image, rgba: new Uint32Array(image.data.buffer) };
});

function appendOutput(text) {
  partialLog += text;
  const lines = partialLog.split(/\r?\n/);
  partialLog = lines.pop();
  logLines.push(...lines);
  if (logLines.length > 220) logLines.splice(0, logLines.length - 220);
  // Serial.print() may arrive in many pieces during a single firmware step.
  if (!logScheduled) {
    logScheduled = true;
    queueMicrotask(() => {
      logScheduled = false;
      $('serial-log').textContent = [...logLines, partialLog].join('\n');
    });
  }
}

function stringAt(pointer) {
  const bytes = new Uint8Array(memory.buffer);
  let end = pointer >>> 0;
  while (end < bytes.length && bytes[end]) end++;
  return decoder.decode(bytes.subarray(pointer >>> 0, end));
}

function getPreference(pointer, fallback) {
  const key = storagePrefix + stringAt(pointer);
  let value = memoryPreferences.get(key);
  if (value === undefined) {
    try { value = localStorage.getItem(key); } catch { /* Memory remains usable. */ }
  }
  if (value === undefined || value === null || !/^\d+$/.test(String(value))) return fallback >>> 0;
  const numeric = Number(value);
  return Number.isSafeInteger(numeric) && numeric >= 0 && numeric <= 0xffffffff ? numeric >>> 0 : fallback >>> 0;
}

function putPreference(pointer, value) {
  const key = storagePrefix + stringAt(pointer);
  const text = String(value >>> 0);
  memoryPreferences.set(key, text);
  try { localStorage.setItem(key, text); } catch { /* Browser storage is optional. */ }
}

function clearPreferences() {
  memoryPreferences.clear();
  try {
    for (const key of Object.keys(localStorage)) {
      if (key.startsWith(storagePrefix)) localStorage.removeItem(key);
    }
  } catch { /* Browser storage is optional. */ }
}

function dataView() { return new DataView(memory.buffer); }

const wasi = {
  fd_write(fd, vectors, length, written) {
    const view = dataView();
    let total = 0;
    for (let index = 0; index < length; index++) {
      const pointer = view.getUint32(vectors + index * 8, true);
      const bytes = view.getUint32(vectors + index * 8 + 4, true);
      if (fd === 1 || fd === 2) appendOutput(decoder.decode(new Uint8Array(memory.buffer, pointer, bytes)));
      total += bytes;
    }
    view.setUint32(written, total, true);
    return 0;
  },
  fd_read(_fd, _vectors, _length, read) { dataView().setUint32(read, 0, true); return 0; },
  fd_close() { return 0; },
  fd_seek() { return 70; }, // WASI ESPIPE: the log behaves like a character device.
  fd_sync() { return 0; },
  fd_fdstat_get(_fd, pointer) {
    new Uint8Array(memory.buffer, pointer, 24).fill(0);
    dataView().setUint8(pointer, 2);
    return 0;
  },
  fd_prestat_get() { return 8; },
  fd_prestat_dir_name() { return 8; },
  environ_sizes_get(count, bytes) {
    dataView().setUint32(count, 0, true); dataView().setUint32(bytes, 0, true); return 0;
  },
  environ_get() { return 0; },
  args_sizes_get(count, bytes) {
    dataView().setUint32(count, 0, true); dataView().setUint32(bytes, 0, true); return 0;
  },
  args_get() { return 0; },
  clock_time_get(_clock, _precision, pointer) {
    dataView().setBigUint64(pointer, BigInt(Math.floor(performance.now() * 1e6)), true); return 0;
  },
  clock_res_get(_clock, pointer) { dataView().setBigUint64(pointer, 1000000n, true); return 0; },
  random_get(pointer, bytes) {
    const target = new Uint8Array(memory.buffer, pointer, bytes);
    for (let offset = 0; offset < bytes; offset += 65536) crypto.getRandomValues(target.subarray(offset, offset + 65536));
    return 0;
  },
  proc_exit(code) { throw new Error(`Firmware exited (${code}).`); },
};

function paused() { return explicitlyPaused || document.hidden; }

function refreshControls() {
  const stopped = paused();
  for (const button of commandButtons) button.disabled = !initialized || failed || stopped;
  $('pause').disabled = !initialized || failed;
  $('pause').textContent = explicitlyPaused ? 'ادامه' : 'مکث';
  if (!initialized || failed) return;
  $('run-state').textContent = explicitlyPaused ? 'مکث' : document.hidden ? 'مکث در پس‌زمینه' : 'در حال اجرا';
  $('run-state').className = `state ${stopped ? 'paused' : 'running'}`;
}

function setPaused(value) {
  explicitlyPaused = Boolean(value);
  lastTimestamp = null;
  rateElapsedMs = 0;
  rateStartFrames = exports?.tando_frames() >>> 0;
  refreshControls();
}

function command(character) {
  if (!initialized || failed || paused() || !allowedCommands.has(character)) return false;
  exports.tando_input(character.charCodeAt(0));
  appendOutput(`> ${character}\n`);
  return true;
}

function paint() {
  if (memoryBuffer !== memory.buffer) {
    memoryBuffer = memory.buffer;
    eyeViews = [exports.tando_left(), exports.tando_right()].map((pointer) => new Uint16Array(memoryBuffer, pointer, 240 * 240));
  }
  for (let eye = 0; eye < 2; eye++) {
    const pixels = eyeViews[eye];
    const { context, image, rgba } = paintTargets[eye];
    for (let index = 0; index < pixels.length; index++) {
      const value = pixels[index];
      // Expand the same upper RGB565 bits used by the existing display model.
      rgba[index] = (0xff000000 | ((value & 0xf800) >> 8) | ((value & 0x07e0) << 5) | ((value & 0x001f) << 19)) >>> 0;
    }
    context.putImageData(image, 0, 0);
  }
}

function showTime() {
  const seconds = Math.floor(activeElapsedMs / 1000);
  const minutes = String(Math.floor(seconds / 60)).padStart(2, '0');
  const remaining = String(seconds % 60).padStart(2, '0');
  $('elapsed').textContent = `زمان اجرا ${minutes}:${remaining}`.replace(/\d/g, (digit) => '۰۱۲۳۴۵۶۷۸۹'[Number(digit)]);
}

function frame(timestamp) {
  if (failed) return;
  if (paused()) {
    lastTimestamp = null;
    requestAnimationFrame(frame);
    return;
  }
  try {
    const elapsed = lastTimestamp === null ? 0 : Math.max(0, timestamp - lastTimestamp);
    lastTimestamp = timestamp;
    activeElapsedMs += elapsed;
    rateElapsedMs += elapsed;
    const before = performance.now();
    // The platform wrapper adds the firmware's 500 ms setup delay internally.
    exports.tando_step(Math.floor(activeElapsedMs) >>> 0);
    wasmStepMs = performance.now() - before;
    maxStepMs = Math.max(maxStepMs, wasmStepMs);
    const frames = exports.tando_frames() >>> 0;
    if (frames !== previousFrames) {
      const paintStart = performance.now();
      paint();
      paintMs = performance.now() - paintStart;
      maxPaintMs = Math.max(maxPaintMs, paintMs);
      presentedFrames++;
      previousFrames = frames;
    }
    $('led').style.opacity = String(.08 + .92 * Math.min(255, exports.tando_led() >>> 0) / 255);
    if (rateElapsedMs >= 1000) {
      rate = ((frames - rateStartFrames) >>> 0) * 1000 / rateElapsedMs;
      $('frame-rate').textContent = `${number.format(rate)} تصویر/ثانیه`;
      rateStartFrames = frames;
      rateElapsedMs = 0;
      showTime();
    }
    requestAnimationFrame(frame);
  } catch (error) { showError(error); }
}

function showError(error) {
  failed = true;
  $('run-state').textContent = 'راه‌اندازی متوقف شد';
  $('run-state').className = 'state paused';
  $('error').hidden = false;
  $('error').textContent = `اجرای پیش‌نمایش متوقف شد.\n${error.message || error}`;
  refreshControls();
  console.error(error);
}

async function start() {
  const manifestResponse = await fetch('manifest.json');
  if (!manifestResponse.ok) throw new Error('اطلاعات ساخت پیدا نشد؛ پیش‌نمایش را دوباره بساز.');
  const manifest = await manifestResponse.json();
  if (manifest.contract !== 'tando-web-preview/v1' ||
      typeof manifest.preview_id !== 'string' ||
      !/^[a-zA-Z0-9_-]{1,64}$/.test(manifest.preview_id) ||
      typeof manifest.branch !== 'string' || !manifest.branch ||
      typeof manifest.firmware_version !== 'string' || !manifest.firmware_version ||
      !/^[a-f0-9]{64}$/.test(manifest.wasm_sha256)) {
    throw new Error('اطلاعات ساخت معتبر نیست؛ پیش‌نمایش را دوباره بساز.');
  }

  // Each experimental branch keeps its own progress on the same browser origin.
  storagePrefix = `tando-web-preview:v1:${manifest.preview_id}:`;
  buildManifest = Object.freeze(manifest);
  const response = await fetch('preview.wasm');
  if (!response.ok) throw new Error(`preview.wasm: HTTP ${response.status}. Run the preview build first.`);
  const bytes = await response.arrayBuffer();
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  const hash = Array.from(new Uint8Array(digest), (value) => value.toString(16).padStart(2, '0')).join('');
  if (hash !== manifest.wasm_sha256) {
    throw new Error('فایل‌های ساخت هم‌زمان نیستند؛ پس از پایان ساخت، صفحه را تازه کن.');
  }
  const module = await WebAssembly.compile(bytes);
  const imports = {
    env: {
      preview_get_pref: getPreference,
      preview_put_pref: putPreference,
      preview_clear_prefs: clearPreferences,
      preview_now_us: () => performance.now() * 1000,
    },
    wasi_snapshot_preview1: wasi,
  };
  for (const entry of WebAssembly.Module.imports(module)) {
    if (!(entry.name in (imports[entry.module] || {}))) throw new Error(`Unsupported WASM import: ${entry.module}.${entry.name}`);
  }
  const instance = await WebAssembly.instantiate(module, imports);
  exports = instance.exports;
  memory = exports.memory;
  if (!memory) throw new Error('Firmware WASM does not export memory.');
  exports._initialize();
  exports.tando_seed(crypto.getRandomValues(new Uint32Array(1))[0]);
  exports.tando_setup();
  framesAtStart = exports.tando_frames() >>> 0;
  rateStartFrames = framesAtStart;
  initialized = true;
  for (const [id, label, text] of [
    ['firmware-version', 'فریم‌ور ', manifest.firmware_version],
    ['preview-branch', 'شاخهٔ آزمایشی: ', manifest.branch],
  ]) {
    const value = document.createElement('bdi');
    value.dir = 'ltr';
    value.textContent = text;
    $(id).replaceChildren(document.createTextNode(label), value);
  }
  refreshControls();
  requestAnimationFrame(frame);
  return true;
}

for (const button of commandButtons) button.addEventListener('click', () => command(button.dataset.command));
$('pause').addEventListener('click', () => setPaused(!explicitlyPaused));
document.addEventListener('visibilitychange', () => {
  setPaused(explicitlyPaused);
});

const ready = start();
ready.catch(showError);
// Test/debug access reports real firmware completion and presentation counts.
window.tandoPreview = Object.freeze({
  ready,
  command,
  setPaused,
  get exports() { return exports; },
  get memory() { return memory; },
  get manifest() { return buildManifest; },
  get metrics() {
    return Object.freeze({
      initialized, failed, paused: paused(), activeElapsedMs,
      firmwareFrames: exports ? exports.tando_frames() >>> 0 : 0,
      framesAtStart, presentedFrames, realPairsPerSecond: rate,
      firmwareTimeMs: exports ? exports.tando_time() >>> 0 : 0,
      wasmStepMs, paintMs, maxStepMs, maxPaintMs,
    });
  },
});

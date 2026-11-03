const ESPTOOL_MODULE_URL = 'https://unpkg.com/esptool-js@0.6.1/bundle.js';
const RELEASE_VERSION = '1.4.7';
// Only USB serial chips found on ESP32 boards appear in the port picker:
// Espressif native USB (Book32, LILYGO), WCH CH34x (the Sticky), Silicon
// Labs CP210x and FTDI. Without filters Linux lists every /dev/ttyS* port.
const SERIAL_FILTERS = [0x303a, 0x1a86, 0x10c4, 0x0403].map((usbVendorId) => ({ usbVendorId }));
const IS_LINUX = /Linux/.test(navigator.userAgent || '') && !/Android/.test(navigator.userAgent || '');

// The apps the user ticks reach the firmware as a record in the last 4 KB
// sector of the app0 partition (see AppSelection.h): 0x10000 + app0 size.
const APP_RECORD_SIZE = 0x1000;

const profiles = {
  book32: {
    version: RELEASE_VERSION,
    appRecordAddress: 0x10000 + 0x280000 - APP_RECORD_SIZE,
    apps: ['todo', 'klipper'],
    name: 'Book32 — 7.5 inch',
    manifest: `manifest-update-v${RELEASE_VERSION}.json`,
    flashSize: '16MB',
    description: 'Single-button Book32 with the 7.5-inch e-paper display.',
    after: 'After restart, connect to the InkDeck-Setup Wi-Fi network to configure Wi-Fi.'
  },
  sticky: {
    version: RELEASE_VERSION,
    appRecordAddress: 0x10000 + 0x400000 - APP_RECORD_SIZE,
    apps: ['todo', 'klipper', 'inkade'],
    name: 'Seeed Studio Sticky — 3.97 inch',
    manifest: `manifest-sticky-update-v${RELEASE_VERSION}.json`,
    flashSize: '32MB',
    resetMode: 'atomic-rts',
    description: 'Seeed Studio touch-screen reader with InkDeck installed.',
    after: 'After restart, open Settings, tap Wi-Fi, and choose your network on the touch screen.'
  },
  lilygo: {
    version: RELEASE_VERSION,
    appRecordAddress: 0x10000 + 0x400000 - APP_RECORD_SIZE,
    apps: ['todo', 'klipper', 'inkade'],
    experimental: true,
    name: 'LILYGO T5 E-Paper S3 Pro Lite — H752-01',
    manifest: `manifest-lilygo-update-v${RELEASE_VERSION}.json`,
    flashSize: '16MB',
    description: 'Experimental H752-01 / Pro Lite build with eReader, Todo, Klipper, Settings and Inkade for Game Boy, Game Boy Color and NES. No game audio. Not compatible with the older H752.',
    after: 'If it remains in download mode, press RESET. Open Settings for Wi-Fi and frontlight brightness. Upload your own legally obtained ROMs through the Game ROMs web tab, or copy them into /roms on a FAT32 MicroSD card. Use Save & exit before removing the card. Press BOOT to wake from sleep.'
  }
};

const deviceCards = [...document.querySelectorAll('[data-profile]')];
const appCards = [...document.querySelectorAll('[data-app]')];
const flashButton = document.querySelector('#flash-button');
const flashDeviceName = document.querySelector('#flash-device-name');
const flashDescription = document.querySelector('#flash-description');
const flashNote = document.querySelector('#flash-note');
const afterFlashing = document.querySelector('#after-flashing');
const browserSupport = document.querySelector('#browser-support');
const progressPanel = document.querySelector('#progress-panel');
const progressTitle = document.querySelector('#progress-title');
const progressBar = document.querySelector('#progress-bar');
const progressTrack = document.querySelector('.progress-track');
const progressPercent = document.querySelector('#progress-percent');
const resultMessage = document.querySelector('#result-message');

let selectedProfile = null;
let flashing = false;
let activeStep = 'connect';

function setBrowserSupport() {
  const supported = 'serial' in navigator && window.isSecureContext;
  browserSupport.classList.toggle('supported', supported);
  browserSupport.classList.toggle('unsupported', !supported);
  browserSupport.lastChild.textContent = supported
    ? ' Chrome or Edge is ready for USB flashing'
    : ' USB flashing requires Chrome or Edge on a secure page';
  if (!supported) flashButton.disabled = true;
  return supported;
}

function selectDevice(profileId) {
  if (flashing || !profiles[profileId]) return;
  selectedProfile = profileId;
  const profile = profiles[profileId];

  deviceCards.forEach((card) => {
    const selected = card.dataset.profile === profileId;
    card.classList.toggle('selected', selected);
    card.setAttribute('aria-pressed', String(selected));
  });

  // Only the apps this hardware has; an app it lacks is never flashed ticked.
  appCards.forEach((card) => {
    const available = profile.apps.includes(card.dataset.app);
    card.hidden = !available;
    if (!available) card.querySelector('input').checked = false;
  });

  flashDeviceName.textContent = profile.name;
  flashDescription.textContent = profile.description;
  afterFlashing.textContent = profile.after;
  document.querySelector('#release-name').textContent = `InkDeck ${profile.version}`;
  document.querySelector('#release-status').textContent = profile.experimental ? 'Experimental preview' : 'Stable release';
  document.querySelector('#release-detail').textContent = profile.experimental ? 'H752-01 only · GB / GBC / NES · October 8, 2026' : 'Firmware + web interface · October 8, 2026';
  flashButton.lastChild.textContent = ` Flash InkDeck ${profile.version}`;
  flashNote.textContent = profile.experimental ? 'Check your board revision before flashing. Back up its existing firmware/settings: this installs the InkDeck partition layout.' : "Chrome or Edge will ask you to choose the device's serial port.";
  flashButton.disabled = !setBrowserSupport();

  const url = new URL(window.location.href);
  url.searchParams.set('hardware', profileId);
  window.history.replaceState({}, '', url);
}

function setProgress(percent) {
  const rounded = Math.max(0, Math.min(100, Math.round(percent)));
  progressBar.style.width = `${rounded}%`;
  progressTrack.setAttribute('aria-valuenow', String(rounded));
  progressPercent.textContent = `${rounded}%`;
}

function setStep(stepId, state, detail) {
  activeStep = stepId;
  const row = document.querySelector(`[data-progress-step="${stepId}"]`);
  row.classList.remove('running', 'done', 'error');
  if (state !== 'pending') row.classList.add(state);
  if (detail) row.querySelector('small').textContent = detail;
}

function resetProgress(profile) {
  progressPanel.hidden = false;
  progressTitle.textContent = `Installing InkDeck ${profile.version} on ${profile.name}…`;
  document.querySelectorAll('[data-progress-step]').forEach((row) => {
    row.classList.remove('running', 'done', 'error');
    row.querySelector('small').textContent = row.dataset.progressStep === 'connect'
      ? 'Waiting for the device'
      : 'Waiting';
  });
  resultMessage.hidden = true;
  resultMessage.classList.remove('error');
  setProgress(0);
  progressPanel.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

function showResult(message, isError = false) {
  resultMessage.textContent = message;
  resultMessage.classList.toggle('error', isError);
  resultMessage.hidden = false;
}

async function loadFirmware(profile) {
  const manifestUrl = new URL(profile.manifest, window.location.href);
  const response = await fetch(manifestUrl, { cache: 'no-store' });
  if (!response.ok) throw new Error(`Could not download the InkDeck manifest (HTTP ${response.status}).`);

  const manifest = await response.json();
  if (manifest.version !== profile.version) throw new Error('Firmware version mismatch. Reload the installer before flashing.');
  const build = manifest.builds?.find((item) => item.chipFamily === 'ESP32-S3');
  if (!build?.parts?.length) throw new Error('The selected firmware package does not contain an ESP32-S3 build.');

  const downloaded = [];
  for (let index = 0; index < build.parts.length; index += 1) {
    const part = build.parts[index];
    const partUrl = new URL(part.path, manifestUrl);
    setStep('download', 'running', `Downloading file ${index + 1} of ${build.parts.length}`);
    const partResponse = await fetch(partUrl, { cache: 'no-store' });
    if (!partResponse.ok) throw new Error(`Could not download ${part.path} (HTTP ${partResponse.status}).`);
    downloaded.push({
      address: Number(part.offset),
      data: new Uint8Array(await partResponse.arrayBuffer()),
      path: part.path
    });
    setProgress(8 + ((index + 1) / build.parts.length) * 9);
  }
  return downloaded;
}

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}

// The InstallRecord (AppSelection.h): "INKAPPS1", version 1, the app bits,
// two reserved bytes, a random install ID (applied once) and a CRC-32.
function selectedApps(profile) {
  return appCards
    .filter((card) => profile.apps.includes(card.dataset.app) && card.querySelector('input').checked)
    .reduce((mask, card) => mask | Number(card.querySelector('input').value), 0);
}

function appRecord(apps) {
  const sector = new Uint8Array(APP_RECORD_SIZE).fill(0xff);
  const record = new DataView(sector.buffer, 0, 20);
  new TextEncoder().encode('INKAPPS1').forEach((byte, index) => record.setUint8(index, byte));
  record.setUint8(8, 1);
  record.setUint8(9, apps);
  record.setUint16(10, 0, true);
  record.setUint32(12, crypto.getRandomValues(new Uint32Array(1))[0] || 1, true);
  record.setUint32(16, crc32(sector.subarray(0, 16)), true);
  return sector;
}

function bytesEqual(left, right) {
  if (left.length !== right.length) return false;
  for (let index = 0; index < left.length; index += 1) {
    if (left[index] !== right[index]) return false;
  }
  return true;
}

// Linux blocks serial ports in ways Windows and macOS do not.
const LINUX_PORT_HELP = 'On Linux: add yourself to the dialout group (sudo usermod -aG dialout $USER, then log out and back in); '
  + 'with Snap Chromium also run sudo snap connect chromium:raw-usb; and close any serial monitor using the port. '
  + 'See "Flashing from Linux?" below.';
const LINUX_SYNC_HELP = 'On Linux, ModemManager can grab the port while flashing: stop it (sudo systemctl stop ModemManager) and try again. '
  + 'See "Flashing from Linux?" below.';

function friendlyError(error) {
  const message = error?.message || String(error);
  if (error?.name === 'NetworkError' || /failed to open|access denied|permission/i.test(message)) {
    return `Could not open the serial port. ${IS_LINUX ? LINUX_PORT_HELP : 'Close any other program using the port and try again.'} (${message})`;
  }
  if (/failed to connect|sync|timeout/i.test(message)) {
    return `Could not connect to the ESP32-S3. Hold BOOT, tap RESET, release BOOT, and try again.${IS_LINUX ? ` ${LINUX_SYNC_HELP}` : ''} (${message})`;
  }
  if (/fetch|download|http/i.test(message)) {
    return `Firmware download failed. Check your internet connection and try again. (${message})`;
  }
  return message;
}

function wait(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function restartDevice(profile, loader, port) {
  if (profile.resetMode !== 'atomic-rts') {
    await loader.after('hard_reset');
    return;
  }

  // The Sticky's CH343 bridge needs a real RTS low pulse. esptool-js 0.6.1's
  // hard-reset path only releases RTS, which can leave the ESP32-S3 in the
  // serial bootloader after an otherwise successful flash. Set DTR and RTS
  // together so GPIO0 stays released while EN is pulsed.
  await port.setSignals({ dataTerminalReady: false, requestToSend: true });
  await wait(150);
  await port.setSignals({ dataTerminalReady: false, requestToSend: false });
  await wait(350);
}

async function flashSelectedDevice() {
  if (flashing || !selectedProfile || !setBrowserSupport()) return;
  const profile = profiles[selectedProfile];

  let port;
  try {
    port = await navigator.serial.requestPort({ filters: SERIAL_FILTERS });
  } catch (error) {
    if (error?.name !== 'NotFoundError') {
      flashNote.textContent = `Could not open the serial-port picker: ${friendlyError(error)}`;
    }
    return;
  }

  flashing = true;
  flashButton.disabled = true;
  flashButton.lastChild.textContent = ' Flashing…';
  deviceCards.forEach((card) => { card.disabled = true; });
  appCards.forEach((card) => { card.querySelector('input').disabled = true; });
  resetProgress(profile);

  let transport;
  try {
    setStep('connect', 'running', 'Opening the selected serial port');
    const { ESPLoader, Transport } = await import(ESPTOOL_MODULE_URL);
    transport = new Transport(port, true);
    const terminal = {
      clean() {},
      writeLine(data) { console.info(data); },
      write(data) { console.info(data); }
    };
    const loader = new ESPLoader({ transport, baudrate: 460800, terminal, debugLogging: false });
    const chip = await loader.main();
    if (!chip.includes('ESP32-S3')) throw new Error(`This is ${chip}, but InkDeck requires an ESP32-S3.`);
    setStep('connect', 'done', `Connected to ${chip}`);
    setProgress(8);

    setStep('download', 'running', 'Downloading InkDeck files');
    const parts = await loadFirmware(profile);
    const firmware = parts.find((part) => part.address === 0x10000);
    if (firmware && firmware.address + firmware.data.length > profile.appRecordAddress) {
      throw new Error('The firmware is too large for its app choice record. Please report this.');
    }
    const apps = selectedApps(profile);
    parts.push({ address: profile.appRecordAddress, data: appRecord(apps), path: 'app choice' });
    setStep('download', 'done', `${parts.length - 1} files ready · ${apps ? 'apps chosen' : 'eReader only'}`);
    setProgress(17);

    setStep('write', 'running', 'Preparing flash memory');
    const totalSize = parts.reduce((sum, part) => sum + part.data.length, 0);
    const sizeBefore = parts.map((_, index) => parts
      .slice(0, index)
      .reduce((sum, part) => sum + part.data.length, 0));

    await loader.writeFlash({
      fileArray: parts.map((part) => ({ data: part.data, address: part.address })),
      flashSize: profile.flashSize,
      flashMode: 'dio',
      flashFreq: '80m',
      eraseAll: false,
      compress: true,
      reportProgress(fileIndex, written, total) {
        const currentPart = parts[fileIndex];
        const currentFraction = total > 0 ? written / total : 0;
        const completedBytes = sizeBefore[fileIndex] + currentPart.data.length * currentFraction;
        setProgress(17 + (completedBytes / totalSize) * 73);
        setStep('write', 'running', `Writing file ${fileIndex + 1} of ${parts.length}`);
      }
    });
    setStep('write', 'done', 'System and web interface written');
    setProgress(90);

    setStep('verify', 'running', 'Reading back the partition table');
    const partition = parts.find((part) => part.address === 0x8000);
    if (!partition) throw new Error('The firmware package is missing its partition table.');
    const readBack = await loader.readFlash(partition.address, partition.data.length, (_packet, progress, total) => {
      setProgress(90 + (progress / total) * 7);
    });
    if (!bytesEqual(partition.data, readBack)) throw new Error('Partition-table verification failed. Please flash again.');
    setStep('verify', 'done', 'Partition table verified');
    setProgress(97);

    setStep('reset', 'running', 'Restarting InkDeck');
    await restartDevice(profile, loader, port);
    setStep('reset', 'done', 'Device restarted');
    setProgress(100);
    showResult(`InkDeck ${profile.version} was installed successfully. ${profile.after}`);
    flashNote.textContent = 'Installation complete. You can disconnect the USB cable.';
  } catch (error) {
    console.error(error);
    setStep(activeStep, 'error', 'Stopped');
    showResult(friendlyError(error), true);
    flashNote.textContent = 'Flashing did not finish. Your ebook storage was not erased.';
  } finally {
    if (transport) {
      try { await transport.disconnect(); } catch (_) { /* Port may already be closed after reset. */ }
    }
    flashing = false;
    deviceCards.forEach((card) => { card.disabled = false; });
    appCards.forEach((card) => { card.querySelector('input').disabled = false; });
    flashButton.disabled = !selectedProfile || !setBrowserSupport();
    flashButton.lastChild.textContent = ` Flash InkDeck ${profile.version}`;
  }
}

deviceCards.forEach((card) => {
  card.addEventListener('click', () => selectDevice(card.dataset.profile));
});
flashButton.addEventListener('click', flashSelectedDevice);

setBrowserSupport();
if (IS_LINUX) document.querySelector('#linux-guide').open = true;
const requestedHardware = new URLSearchParams(window.location.search).get('hardware');
if (profiles[requestedHardware]) selectDevice(requestedHardware);

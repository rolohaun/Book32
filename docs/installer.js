const ESPTOOL_MODULE_URL = 'https://unpkg.com/esptool-js@0.6.1/bundle.js';
const RELEASE_VERSION = '1.2.6';

const profiles = {
  book32: {
    name: 'Book32 — 7.5 inch',
    manifest: 'manifest-update-v1.2.6.json',
    flashSize: '16MB',
    description: 'Single-button Book32 with the 7.5-inch e-paper display.',
    after: 'After restart, connect to the InkDeck-Setup Wi-Fi network to configure Wi-Fi.'
  },
  sticky: {
    name: 'Seeed Studio Sticky — 3.97 inch',
    manifest: 'manifest-sticky-update-v1.2.6.json',
    flashSize: '32MB',
    description: 'Seeed Studio touch-screen reader with InkDeck installed.',
    after: 'After restart, tap the Wi-Fi icon and choose your network on the touch screen.'
  }
};

const deviceCards = [...document.querySelectorAll('[data-profile]')];
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

  flashDeviceName.textContent = profile.name;
  flashDescription.textContent = profile.description;
  afterFlashing.textContent = profile.after;
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
  progressTitle.textContent = `Installing InkDeck ${RELEASE_VERSION} on ${profile.name}…`;
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

function bytesEqual(left, right) {
  if (left.length !== right.length) return false;
  for (let index = 0; index < left.length; index += 1) {
    if (left[index] !== right[index]) return false;
  }
  return true;
}

function friendlyError(error) {
  const message = error?.message || String(error);
  if (/failed to connect|sync|timeout/i.test(message)) {
    return 'Could not connect to the ESP32-S3. Hold BOOT, tap RESET, release BOOT, and try again.';
  }
  if (/network|fetch|download|http/i.test(message)) {
    return `Firmware download failed. Check your internet connection and try again. (${message})`;
  }
  return message;
}

async function flashSelectedDevice() {
  if (flashing || !selectedProfile || !setBrowserSupport()) return;
  const profile = profiles[selectedProfile];

  let port;
  try {
    port = await navigator.serial.requestPort();
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
    setStep('download', 'done', `${parts.length} files ready`);
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
    await loader.after('hard_reset');
    setStep('reset', 'done', 'Device restarted');
    setProgress(100);
    showResult(`InkDeck ${RELEASE_VERSION} was installed successfully. ${profile.after}`);
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
    flashButton.disabled = !selectedProfile || !setBrowserSupport();
    flashButton.lastChild.textContent = ' Flash InkDeck 1.2.6';
  }
}

deviceCards.forEach((card) => {
  card.addEventListener('click', () => selectDevice(card.dataset.profile));
});
flashButton.addEventListener('click', flashSelectedDevice);

setBrowserSupport();
const requestedHardware = new URLSearchParams(window.location.search).get('hardware');
if (profiles[requestedHardware]) selectDevice(requestedHardware);

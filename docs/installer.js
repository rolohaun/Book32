const modeButtons = document.querySelectorAll('.mode-button');
const modePanels = document.querySelectorAll('.mode-panel');
const hardwareSelect = document.querySelector('#hardware-select');
const updateInstaller = document.querySelector('#update-installer');
const factoryInstaller = document.querySelector('#factory-installer');

const hardwareProfiles = {
  book32: {
    updateManifest: 'manifest-update-v1.2.json',
    factoryManifest: 'manifest-factory-v1.2.json',
    pageTitle: 'Flash Book32',
    intro: 'Install or update the 7.5-inch Book32 over USB. No PlatformIO, drivers, or command line required.',
    note: 'Seeed XIAO ESP32-S3 with the TRMNL 7.5-inch display.',
    display: '7.5-inch, 800 × 480 E-Ink',
    controls: 'One multifunction button',
    storage: 'Protected 10 MB internal partition',
    factoryKicker: 'For a blank or replacement XIAO ESP32-S3',
    storageTitle: 'Your ebook partition stays separate',
    storageLabel: 'Protected 10 MB'
  },
  sticky: {
    updateManifest: 'manifest-sticky-update-v1.2.json',
    factoryManifest: 'manifest-sticky-factory-v1.2.json',
    pageTitle: 'Flash Book32 Sticky',
    intro: 'Install or update the 3.97-inch touch-screen Book32 Sticky over USB. No PlatformIO, drivers, or command line required.',
    note: 'Seeed reTerminal E1002 / Sticky with 3.97-inch touch display.',
    display: '3.97-inch, 800 × 480 E-Ink',
    controls: 'GT911 touch screen + three buttons',
    storage: 'MicroSD card or 23 MB internal fallback',
    factoryKicker: 'For a blank or replacement Book32 Sticky',
    storageTitle: 'Your ebook storage stays separate',
    storageLabel: 'MicroSD or 23 MB fallback'
  }
};

function applyHardwareProfile() {
  const profile = hardwareProfiles[hardwareSelect.value];
  updateInstaller.setAttribute('manifest', profile.updateManifest);
  factoryInstaller.setAttribute('manifest', profile.factoryManifest);
  document.title = `${profile.pageTitle} — Browser Installer`;
  document.querySelector('#page-title').textContent = profile.pageTitle;
  document.querySelector('#intro-copy').textContent = profile.intro;
  document.querySelector('#hardware-note').textContent = profile.note;
  document.querySelector('#device-display').textContent = profile.display;
  document.querySelector('#device-controls').textContent = profile.controls;
  document.querySelector('#device-storage').textContent = profile.storage;
  document.querySelector('#factory-kicker').textContent = profile.factoryKicker;
  document.querySelector('#storage-title').textContent = profile.storageTitle;
  document.querySelector('#ebook-storage-label').textContent = profile.storageLabel;

  const url = new URL(window.location.href);
  url.searchParams.set('hardware', hardwareSelect.value);
  window.history.replaceState({}, '', url);
}

hardwareSelect.addEventListener('change', applyHardwareProfile);
const requestedHardware = new URLSearchParams(window.location.search).get('hardware');
if (hardwareProfiles[requestedHardware]) hardwareSelect.value = requestedHardware;
applyHardwareProfile();

modeButtons.forEach((button) => {
  button.addEventListener('click', () => {
    const targetId = button.dataset.panel;

    modeButtons.forEach((item) => {
      const selected = item === button;
      item.classList.toggle('active', selected);
      item.setAttribute('aria-selected', String(selected));
    });

    modePanels.forEach((panel) => {
      const selected = panel.id === targetId;
      panel.classList.toggle('active', selected);
      panel.hidden = !selected;
    });
  });
});

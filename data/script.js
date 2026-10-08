let romUploadsSupported = false;
let lilygoControlsSupported = false;
let romMaxBytes = 0;
let romUploading = false;
let romDeleting = false;
let romListRequest = 0;

function updateRomControls() {
    const busy = romUploading || romDeleting;
    document.getElementById('rom-file').disabled = busy;
    document.getElementById('rom-upload-button').disabled = busy;
    document.getElementById('rom-refresh-button').disabled = busy;
    document.querySelectorAll('.rom-delete').forEach(button => button.disabled = busy);
}

function showTab(tabId) {
    if (tabId === 'games' && !romUploadsSupported) return;
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav-links button').forEach(el => el.classList.remove('active'));

    document.getElementById(tabId).classList.add('active');
    const navButton = document.querySelector(`.nav-links button[data-tab="${tabId}"]`);
    if (navButton) navButton.classList.add('active');

    // Load data when switching tabs
    if (tabId === 'ereader') {
        fetchBooks();
        getReaderProgress();
    } else if (tabId === 'games') {
        fetchRoms();
    } else if (tabId === 'klipper') {
        switchDeviceApp('Klipper');
    } else if (tabId === 'todo') {
        fetchTodos();
        switchDeviceApp('Todo');
    } else if (tabId === 'settings') {
        getDisplaySettings();
        getSoundSettings();
        getLilygoControls();
    }
}

async function getLilygoControls() {
    if (!lilygoControlsSupported) return;
    const status = document.getElementById('lilygo-settings-status');
    try {
        const response = await fetch('/api/settings/lilygo');
        if (!response.ok) throw new Error('load');
        const data = await response.json();
        document.getElementById('light-brightness').value = data.brightness;
        document.getElementById('light-brightness-label').textContent = `${data.brightness}%`;
        setRadioValue('front-button', String(data.front));
        setRadioValue('side-button', String(data.side));
    } catch (_) { status.textContent = 'Could not load light and button settings.'; }
}
async function saveLilygoControls() {
    if (!lilygoControlsSupported) return;
    const status = document.getElementById('lilygo-settings-status');
    const button = document.getElementById('lilygo-settings-save');
    button.disabled = true; status.textContent = 'Saving…';
    try {
        const response = await fetch('/api/settings/lilygo', {
            method: 'POST', headers: {'Content-Type':'application/json'},
            body: JSON.stringify({brightness: Number(document.getElementById('light-brightness').value),
                front: Number(getRadioValue('front-button','0')), side: Number(getRadioValue('side-button','0'))})
        });
        const data = await response.json();
        if (!response.ok || data.status !== 'ok') throw new Error('save');
        status.textContent = 'Light and button settings saved. Brightness applies when the light is on.';
    } catch (_) { status.textContent = 'Could not save. Check the connection and try again.'; }
    finally { button.disabled = false; }
}

// === Touch Sounds ===
function getSoundSettings() {
    fetch('/api/settings/sound')
        .then(response => response.json())
        .then(data => {
            const card = document.getElementById('sound-settings-card');
            if (card) card.classList.toggle('hidden', data.supported === false);
            if (data.enabled !== undefined) {
                setRadioValue('sound-enabled', String(data.enabled));
            }
        })
        .catch(error => console.error('Error loading sound settings:', error));
}

function saveSoundSettings() {
    const enabled = getRadioValue('sound-enabled', 'true') === 'true';
    const statusDiv = document.getElementById('sound-settings-status');
    fetch('/api/settings/sound', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled: enabled }),
    })
        .then(response => response.json())
        .then(data => {
            statusDiv.textContent = data.status === 'ok' ? 'Touch sounds saved.' : 'Error saving touch sounds.';
            statusDiv.style.color = data.status === 'ok' ? 'var(--accent)' : 'var(--danger)';
            if (data.status === 'ok') setTimeout(() => statusDiv.textContent = '', 3000);
        })
        .catch(error => {
            console.error('Error saving sound settings:', error);
            statusDiv.textContent = 'Connection error.';
            statusDiv.style.color = 'var(--danger)';
        });
}

// Switch the app on the device
async function switchDeviceApp(appName) {
    try {
        await fetch('/api/app/switch?name=' + encodeURIComponent(appName));
    } catch (e) {
        console.log("Could not switch device app:", e);
    }
}

async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();
        romUploadsSupported = data.romUpload === true;
        const newlySupported = !lilygoControlsSupported && data.lilygoControls === true;
        lilygoControlsSupported = data.lilygoControls === true;
        document.getElementById('lilygo-settings-card').classList.toggle('hidden', !lilygoControlsSupported);
        if (newlySupported) getLilygoControls();
        romMaxBytes = Number(data.romMaxBytes) || 0;
        document.getElementById('games-nav').classList.toggle('hidden', !romUploadsSupported);
        document.getElementById('games').classList.toggle('hidden', !romUploadsSupported);
        document.getElementById('rom-limit').textContent = romUploadsSupported ? `Maximum ROM size: ${formatStorage(romMaxBytes)}. MicroSD required.` : '';
        document.getElementById('battery-val').innerText = data.battery + '%' + (data.charging ? ' (Charging)' : '');
        document.getElementById('uptime-val').innerText = data.uptime;

        // Update version display
        if (data.version) {
            document.getElementById('current-ver').innerText = data.version;
            document.getElementById('version-display').innerText = data.version;
        }

        document.getElementById('freespace-val').innerText =
            formatStorage(data.freeSpace) + ' / ' + formatStorage(data.totalSpace);

        // Update Header
        let voltageText = data.voltage.toFixed(2) + 'V';
        if (data.charging) {
            voltageText += ' ⚡';
            document.getElementById('header-voltage').style.color = '#00ff00'; // Bright Green for charging
        } else {
            document.getElementById('header-voltage').style.color = ''; // Default
        }
        document.getElementById('header-voltage').innerText = voltageText;

        const batIcon = document.getElementById('battery-icon');
        const level = parseInt(data.battery);

        // Snap to grid for CSS classes
        let visualLevel = 0;
        if (level > 90) visualLevel = 100;
        else if (level > 70) visualLevel = 80;
        else if (level > 50) visualLevel = 60;
        else if (level > 30) visualLevel = 40;
        else if (level > 10) visualLevel = 20;
        else visualLevel = 0;

        batIcon.setAttribute('data-level', visualLevel);

        // Update battery icon charging state
        if (data.charging) {
            batIcon.classList.add('charging');
        } else {
            batIcon.classList.remove('charging');
        }

    } catch (e) {
        console.error("Failed to fetch status", e);
    }
}

async function checkUpdate() {
    const btn = document.getElementById('check-update-btn');
    const msg = document.getElementById('update-status');
    const updateBtn = document.getElementById('update-btn');

    btn.innerText = "Checking...";
    msg.innerText = "";
    updateBtn.classList.add('hidden');

    try {
        const res = await fetch('/api/check_update');
        const data = await res.json();

        if (data.hasUpdate) {
            let updateParts = [];
            if (data.hasFirmware) updateParts.push("firmware");
            if (data.hasFilesystem) updateParts.push("web interface");

            msg.innerHTML = `<strong>New version available: ${data.latest}</strong>`;
            if (updateParts.length > 0) {
                msg.innerHTML += `<br><small>Includes: ${updateParts.join(" and ")}</small>`;
            }
            if (data.release_notes) {
                msg.innerHTML += `<br><small>${data.release_notes}</small>`;
            }
            msg.style.color = "var(--accent)";
            updateBtn.classList.remove('hidden');
            btn.innerText = "Check Again";
        } else {
            msg.innerText = "You are up to date.";
            msg.style.color = "var(--muted)";
            btn.innerText = "Check Again";
        }
    } catch (e) {
        msg.innerText = "Error checking update.";
        msg.style.color = "var(--danger)";
        btn.innerText = "Retry";
    }
}

async function performUpdate() {
    if (!confirm("Install update? Device will restart when complete.")) return;

    const msg = document.getElementById('update-status');
    const updateBtn = document.getElementById('update-btn');

    msg.innerText = "Downloading and installing update...";
    msg.style.color = "var(--accent)";
    updateBtn.classList.add('hidden');

    fetch('/api/update/all', { method: 'POST' });
    alert("Update started. The device will reboot when complete. This page will stop responding during the update.");
}

// === LILYGO-only Game ROM Management ===
async function fetchRoms() {
    if (!romUploadsSupported || romUploading || romDeleting) return;
    const requestId = ++romListRequest;
    const list = document.getElementById('rom-list');
    list.textContent = 'Loading…';
    try {
        const response = await fetch('/api/roms');
        const data = await response.json();
        if (requestId !== romListRequest || romUploading || romDeleting) return;
        if (!response.ok) throw new Error(data.error || 'Unable to load ROMs.');
        list.replaceChildren();
        if (!data.roms.length) list.textContent = 'No ROMs yet. Upload a Game Boy, NES or Genesis ROM.';
        for (const rom of data.roms) {
            const row = document.createElement('div'); row.className = 'rom-row';
            const name = document.createElement('span'); name.textContent = rom.name;
            const size = document.createElement('small'); size.textContent = formatFileSize(rom.size);
            if (typeof rom.size === 'number' && Number.isSafeInteger(rom.size) && rom.size >= 0)
                size.title = `${rom.size.toLocaleString()} bytes`;
            const actions = document.createElement('div'); actions.className = 'rom-actions';
            const remove = document.createElement('button');
            remove.type = 'button'; remove.className = 'btn danger-outline rom-delete';
            remove.textContent = 'Delete'; remove.setAttribute('aria-label', `Delete ${rom.name}`);
            remove.disabled = romUploading || romDeleting;
            remove.onclick = () => deleteRom(rom.name);
            actions.append(size, remove); row.append(name, actions); list.append(row);
        }
    } catch (error) {
        if (requestId === romListRequest && !romUploading && !romDeleting)
            list.textContent = error.message || 'Connection lost. Return the device to its home screen.';
    }
}

async function deleteRom(name) {
    if (!romUploadsSupported || romUploading || romDeleting) return;
    if (!confirm(`Delete "${name}" from the SD card?\n\nOnly the ROM will be removed. Saved games will be kept. You will need to upload the ROM again to play it.`)) return;
    romDeleting = true; ++romListRequest; updateRomControls();
    const status = document.getElementById('rom-status');
    status.textContent = 'Deleting ROM…'; status.style.color = 'var(--muted)';
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 15000);
    try {
        const response = await fetch('/api/roms/delete?name=' + encodeURIComponent(name), {
            method: 'DELETE', signal: controller.signal
        });
        const data = await response.json();
        if (!response.ok) throw new Error(data.error || `Delete failed (${response.status}).`);
        status.textContent = `Deleted ${name}. Saved games were kept.`;
        status.style.color = 'var(--accent)';
    } catch (error) {
        status.textContent = error.name === 'AbortError' ?
            'Delete timed out. Check the ROM list before retrying.' :
            (error.message || 'Delete failed. Return InkDeck to its home screen and retry.');
        status.style.color = 'var(--danger)';
    } finally {
        clearTimeout(timeout);
        romDeleting = false; updateRomControls();
        await fetchRoms();
    }
}

function uploadRom() {
    if (!romUploadsSupported || romUploading || romDeleting) return;
    const input = document.getElementById('rom-file');
    const file = input.files[0];
    const status = document.getElementById('rom-status');
    const progress = document.getElementById('rom-progress');
    if (!file || !/\.(gb|gbc|nes|md|gen|bin)$/i.test(file.name)) { status.textContent = 'Choose a .gb/.gbc, .nes or .md/.gen/.bin ROM first.'; return; }
    const isGb = /\.(gb|gbc)$/i.test(file.name), isNes = /\.nes$/i.test(file.name);
    const minimum = isGb ? 32768 : isNes ? 16400 : 512;
    const maximum = Math.min(romMaxBytes, isGb ? 8 * 1024 * 1024 : isNes ? 2 * 1024 * 1024 : 4 * 1024 * 1024);
    if (file.size < minimum || file.size > maximum) { status.textContent = `This ROM must be between ${formatFileSize(minimum)} and ${formatStorage(maximum)}.`; return; }
    romUploading = true; ++romListRequest; updateRomControls();
    progress.value = 0; progress.classList.remove('hidden');
    status.textContent = 'Uploading to /roms…'; status.style.color = 'var(--accent)';
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/roms/upload'); xhr.timeout = 300000;
    xhr.upload.onprogress = event => {
        if (!event.lengthComputable) return;
        const percent = Math.round(event.loaded * 100 / event.total);
        progress.value = percent;
        status.textContent = percent < 100 ? `Uploading… ${percent}%` : 'Verifying ROM and saving to SD…';
    };
    function finish(message, success) {
        romUploading = false; updateRomControls();
        status.textContent = message; status.style.color = success ? 'var(--accent)' : 'var(--danger)';
        if (success) { progress.value = 100; input.value = ''; fetchRoms(); }
    }
    xhr.onload = () => {
        let data;
        try { data = JSON.parse(xhr.responseText); } catch (_) { finish('Unexpected response; check the ROM list before retrying.', false); return; }
        if (xhr.status === 201) finish(`Saved to ${data.path}. Open Ink Boy on your LILYGO to play.`, true);
        else finish(data.error || `Upload failed (${xhr.status}).`, false);
    };
    xhr.onerror = () => finish('Connection lost. Return InkDeck to its home screen and check the ROM list before retrying.', false);
    xhr.ontimeout = () => finish('Upload timed out. Check Wi-Fi and the ROM list before retrying.', false);
    xhr.onabort = () => finish('Upload cancelled.', false);
    const body = new FormData(); body.append('file', file); xhr.send(body);
}

// === Ereader Book Management ===
async function fetchBooks() {
    const bookList = document.getElementById('book-list');
    bookList.innerHTML = '<p>Loading...</p>';

    try {
        const res = await fetch('/api/books');
        const data = await res.json();

        if (data.books && data.books.length > 0) {
            bookList.innerHTML = data.books.map(book => {
                const isFont = book.filename.endsWith('.ttf');
                return `
                <div class="book-item">
                    <span class="book-title">${isFont ? '📂 [Font] ' : '📖 '}${book.name}</span>
                    <span class="book-size">${Math.round(book.size / 1024)} KB</span>
                    <button class="btn-delete" onclick="deleteBook('${book.filename}', '${book.name}')">Delete</button>
                </div>
            `}).join('');
        } else {
            bookList.innerHTML = '<p class="hint">No books uploaded yet.</p>';
        }
    } catch (e) {
        bookList.innerHTML = '<p class="error">Error loading books.</p>';
        console.error("Failed to fetch books", e);
    }
}

function uploadBook() {
    const fileInput = document.getElementById('book-file');
    const status = document.getElementById('upload-status');
    const progressContainer = document.getElementById('upload-progress');
    const progressBar = document.getElementById('upload-progress-bar');

    if (!fileInput.files.length) {
        status.innerText = "Please select a file.";
        status.style.color = "var(--danger)";
        return;
    }

    const file = fileInput.files[0];
    if (!file.name.endsWith('.epub') && !file.name.endsWith('.ttf')) {
        status.innerText = "Only .epub and .ttf files are supported.";
        status.style.color = "var(--danger)";
        return;
    }

    // Show progress bar and reset
    progressContainer.classList.remove('hidden');
    progressBar.style.width = '0%';
    status.innerText = "Uploading...";
    status.style.color = "var(--accent)";

    const formData = new FormData();
    formData.append('file', file);

    // Use XMLHttpRequest for progress tracking
    const xhr = new XMLHttpRequest();

    // Track upload progress
    xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
            const percentComplete = (e.loaded / e.total) * 100;
            progressBar.style.width = percentComplete + '%';
            status.innerText = `Uploading... ${Math.round(percentComplete)}%`;
        }
    });

    // Handle completion
    xhr.addEventListener('load', () => {
        if (xhr.status === 200) {
            progressBar.style.width = '100%';
            status.innerText = "Upload complete!";
            status.style.color = "var(--accent)";
            fileInput.value = '';

            // Hide progress bar after a delay
            setTimeout(() => {
                progressContainer.classList.add('hidden');
            }, 2000);

            fetchBooks();
        } else {
            progressContainer.classList.add('hidden');
            status.innerText = "Upload failed: " + xhr.responseText;
            status.style.color = "var(--danger)";
        }
    });

    // Handle errors
    xhr.addEventListener('error', () => {
        progressContainer.classList.add('hidden');
        status.innerText = "Upload error.";
        status.style.color = "var(--danger)";
        console.error("Upload failed");
    });

    // Send the request
    xhr.open('POST', '/api/books/upload');
    xhr.send(formData);
}

async function deleteBook(filename, displayName) {
    // Use display name for confirmation, filename for API call
    const nameToShow = displayName || filename;
    if (!confirm(`Delete "${nameToShow}"?`)) return;

    try {
        const res = await fetch('/api/books/delete?name=' + encodeURIComponent(filename), {
            method: 'DELETE'
        });

        if (res.ok) {
            fetchBooks();
        } else {
            alert("Failed to delete book.");
        }
    } catch (e) {
        alert("Error deleting book.");
        console.error("Delete failed", e);
    }
}

// Initial Load
setInterval(fetchStatus, 5000);
fetchStatus();
getReaderSettings();
getReaderProgress();
getKlipperSettings();
getSleepSettings();
getDisplaySettings();

function setRadioValue(name, value) {
    const option = document.querySelector(`input[name="${name}"][value="${value}"]`);
    if (option) option.checked = true;
}

function formatStorage(bytes) {
    const value = Number(bytes) || 0;
    const gibibyte = 1024 * 1024 * 1024;
    const mebibyte = 1024 * 1024;
    if (value >= gibibyte) {
        const amount = value / gibibyte;
        return amount.toFixed(amount >= 10 ? 1 : 2) + ' GB';
    }
    return Math.round(value / mebibyte) + ' MB';
}

// ROMs can be far smaller than 1 MB; never round a nonempty ROM down to zero.
// Keep the dashboard's MB/GB formatter separate from individual file sizes.
function formatFileSize(bytes) {
    if (typeof bytes !== 'number' || !Number.isSafeInteger(bytes) || bytes < 0) return 'Unknown size';
    if (bytes < 1024) return `${bytes} B`;
    const units = ['KB', 'MB', 'GB', 'TB', 'PB'];
    let amount = bytes / 1024, unit = 0;
    while (amount >= 1024 && unit < units.length - 1) { amount /= 1024; unit++; }
    return amount.toLocaleString(undefined, { maximumFractionDigits: 2 }) + ' ' + units[unit];
}

function getRadioValue(name, fallback) {
    const selected = document.querySelector(`input[name="${name}"]:checked`);
    return selected ? selected.value : fallback;
}

function getReaderSettings() {
    fetch('/api/settings/reader')
        .then(response => response.json())
        .then(data => {
            if (data.refreshFrequency) {
                document.getElementById('refresh-rate').value = data.refreshFrequency;
            }
            if (data.fontSize) {
                setRadioValue('font-size', String(data.fontSize));
            }
            setRadioValue('font-family', data.fontFamily || 'native');
            document.getElementById('show-chapter').checked = data.showChapter !== false;
            document.getElementById('show-page-number').checked = data.showPageNumber !== false;
            document.getElementById('show-reading-percentage').checked = data.showReadingPercentage !== false;
        })
        .catch(error => console.error('Error loading reader settings:', error));
}

function saveReaderSettings() {
    const refreshRate = parseInt(document.getElementById('refresh-rate').value);
    const fontSize = parseInt(getRadioValue('font-size', '12'));
    const fontFamily = getRadioValue('font-family', 'native');
    const showChapter = document.getElementById('show-chapter').checked;
    const showPageNumber = document.getElementById('show-page-number').checked;
    const showReadingPercentage = document.getElementById('show-reading-percentage').checked;
    const statusDiv = document.getElementById('reader-settings-status');

    fetch('/api/settings/reader', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({
            refreshFrequency: refreshRate,
            fontSize: fontSize,
            fontFamily: fontFamily,
            showChapter: showChapter,
            showPageNumber: showPageNumber,
            showReadingPercentage: showReadingPercentage
        }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Settings saved!";
                statusDiv.style.color = "var(--accent)";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error saving settings.";
                statusDiv.style.color = "var(--danger)";
            }
        })
        .catch(error => {
            console.error('Error saving settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "var(--danger)";
        });
}

function getReaderProgress() {
    fetch('/api/reader/progress')
        .then(response => response.json())
        .then(data => {
            const status = document.getElementById('reader-progress-status');
            if (!status) return;

            if (data.exists) {
                const name = data.displayName || data.lastBook || 'Saved book';
                const page = data.page || 1;
                status.textContent = `${name} - page ${page}${data.resumeOnBoot ? ' (will resume on boot)' : ''}`;
            } else {
                status.textContent = 'No saved reading position.';
            }
        })
        .catch(error => console.error('Error loading reader progress:', error));
}

function resetReaderProgress() {
    if (!confirm('Reset saved reading progress? This will not delete any books.')) return;

    const statusDiv = document.getElementById('reader-progress-reset-status');
    fetch('/api/reader/progress', { method: 'DELETE' })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = 'Reading progress reset.';
                statusDiv.style.color = 'var(--accent)';
                getReaderProgress();
                setTimeout(() => statusDiv.textContent = '', 3000);
            } else {
                statusDiv.textContent = 'Error resetting progress.';
                statusDiv.style.color = 'var(--danger)';
            }
        })
        .catch(error => {
            console.error('Error resetting reader progress:', error);
            statusDiv.textContent = 'Connection error.';
            statusDiv.style.color = 'var(--danger)';
        });
}

// === Klipper Settings ===
function getKlipperSettings() {
    fetch('/api/settings/klipper')
        .then(response => response.json())
        .then(data => {
            if (data.fullRefreshInterval !== undefined) {
                document.getElementById('klipper-refresh').value = data.fullRefreshInterval;
            }
            if (data.statusUpdateInterval !== undefined) {
                document.getElementById('klipper-update-interval').value = data.statusUpdateInterval;
            }
        })
        .catch(error => console.error('Error loading Klipper settings:', error));
}

function saveKlipperSettings() {
    const refreshInterval = parseInt(document.getElementById('klipper-refresh').value);
    const updateInterval = parseInt(document.getElementById('klipper-update-interval').value);
    const statusDiv = document.getElementById('klipper-settings-status');

    fetch('/api/settings/klipper', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ fullRefreshInterval: refreshInterval, statusUpdateInterval: updateInterval }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Settings saved!";
                statusDiv.style.color = "var(--accent)";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error saving settings.";
                statusDiv.style.color = "var(--danger)";
            }
        })
        .catch(error => {
            console.error('Error saving Klipper settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "var(--danger)";
        });
}

// === Sleep Settings ===
function getSleepSettings() {
    fetch('/api/settings/sleep')
        .then(response => response.json())
        .then(data => {
            if (data.sleepTimeout !== undefined) {
                document.getElementById('sleep-timeout').value = data.sleepTimeout;
            }
            if (data.sleepMessage !== undefined) {
                document.getElementById('sleep-message').value = data.sleepMessage;
            }
            if (data.defaultSleepMessage) document.getElementById('sleep-message').placeholder = data.defaultSleepMessage;
        })
        .catch(error => console.error('Error loading sleep settings:', error));
}

function saveSleepSettings() {
    const sleepTimeout = parseInt(document.getElementById('sleep-timeout').value);
    const sleepMessage = document.getElementById('sleep-message').value;
    const statusDiv = document.getElementById('sleep-settings-status');

    fetch('/api/settings/sleep', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ sleepTimeout: sleepTimeout, sleepMessage: sleepMessage }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Settings saved!";
                statusDiv.style.color = "var(--accent)";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error saving settings.";
                statusDiv.style.color = "var(--danger)";
            }
        })
        .catch(error => {
            console.error('Error saving sleep settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "var(--danger)";
        });
}

// === Display Orientation ===
function getDisplaySettings() {
    fetch('/api/settings/display')
        .then(response => response.json())
        .then(data => {
            if (data.rotation !== undefined) {
                setRadioValue('display-rotation', String(data.rotation));
            }
        })
        .catch(error => console.error('Error loading display settings:', error));
}

function saveDisplaySettings() {
    const rotation = parseInt(getRadioValue('display-rotation', '3'));
    const statusDiv = document.getElementById('display-settings-status');

    fetch('/api/settings/display', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ rotation: rotation }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Orientation applied.";
                statusDiv.style.color = "var(--accent)";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error applying orientation.";
                statusDiv.style.color = "var(--danger)";
            }
        })
        .catch(error => {
            console.error('Error saving display settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "var(--danger)";
        });
}

// === Todo Management ===
async function fetchTodos() {
    const todoList = document.getElementById('todo-list');
    todoList.innerHTML = '<p>Loading...</p>';

    try {
        const res = await fetch('/api/todos');
        const data = await res.json();

        if (data.todos && data.todos.length > 0) {
            todoList.innerHTML = data.todos.map(todo => `
                <div class="todo-item ${todo.completed ? 'completed' : ''}" data-id="${todo.id}">
                    <input type="checkbox" class="todo-checkbox" ${todo.completed ? 'checked' : ''} onchange="toggleTodo(${todo.id})">
                    <span class="todo-text" ondblclick="startEditTodo(${todo.id}, this)">${escapeHtml(todo.text)}</span>
                    <div class="todo-actions">
                        <button class="btn-edit" onclick="startEditTodo(${todo.id}, this.parentElement.previousElementSibling)">Edit</button>
                        <button class="btn-delete" onclick="deleteTodo(${todo.id})">Delete</button>
                    </div>
                </div>
            `).join('');
        } else {
            todoList.innerHTML = '<p class="hint">No tasks yet. Add one above!</p>';
        }
    } catch (e) {
        todoList.innerHTML = '<p class="error">Error loading tasks.</p>';
        console.error("Failed to fetch todos", e);
    }
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

async function addTodo() {
    const input = document.getElementById('new-todo-input');
    const text = input.value.trim();

    if (!text) {
        input.focus();
        return;
    }

    try {
        const res = await fetch('/api/todos/add', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ text: text })
        });

        if (res.ok) {
            input.value = '';
            fetchTodos();
        } else {
            alert("Failed to add task.");
        }
    } catch (e) {
        alert("Error adding task.");
        console.error("Add todo failed", e);
    }
}

async function toggleTodo(id) {
    try {
        const res = await fetch('/api/todos/toggle', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id: id })
        });

        if (res.ok) {
            fetchTodos();
        } else {
            alert("Failed to toggle task.");
        }
    } catch (e) {
        alert("Error toggling task.");
        console.error("Toggle todo failed", e);
    }
}

function startEditTodo(id, spanElement) {
    const currentText = spanElement.textContent;
    const input = document.createElement('input');
    input.type = 'text';
    input.value = currentText;
    input.className = 'todo-edit-input';

    const saveEdit = async () => {
        const newText = input.value.trim();
        if (newText && newText !== currentText) {
            await editTodo(id, newText);
        } else {
            fetchTodos(); // Restore original if cancelled
        }
    };

    input.onblur = saveEdit;
    input.onkeydown = (e) => {
        if (e.key === 'Enter') {
            input.blur();
        } else if (e.key === 'Escape') {
            input.value = currentText;
            input.blur();
        }
    };

    spanElement.replaceWith(input);
    input.focus();
    input.select();
}

async function editTodo(id, text) {
    try {
        const res = await fetch('/api/todos/edit', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id: id, text: text })
        });

        if (res.ok) {
            fetchTodos();
        } else {
            alert("Failed to edit task.");
            fetchTodos();
        }
    } catch (e) {
        alert("Error editing task.");
        console.error("Edit todo failed", e);
        fetchTodos();
    }
}

async function deleteTodo(id) {
    if (!confirm("Delete this task?")) return;

    try {
        const res = await fetch('/api/todos/delete?id=' + encodeURIComponent(id), {
            method: 'DELETE'
        });

        if (res.ok) {
            fetchTodos();
        } else {
            alert("Failed to delete task.");
        }
    } catch (e) {
        alert("Error deleting task.");
        console.error("Delete todo failed", e);
    }
}

// Handle Enter key in todo input
document.addEventListener('DOMContentLoaded', () => {
    const todoInput = document.getElementById('new-todo-input');
    if (todoInput) {
        todoInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                addTodo();
            }
        });
    }
});

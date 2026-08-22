# Book32

Book32 is a custom E-Ink application OS for the Seeed Studio XIAO ESP32-S3
TRMNL 7.5 inch OG DIY kit. It includes an EPUB reader, a Todo app, a Klipper
printer monitor, and a local web interface for books, settings, and OTA updates.

## Hardware

- MCU: Seeed Studio XIAO ESP32-S3
- Display: 7.5 inch E-Ink panel, 800 x 480
- Storage layout: separate firmware, web UI, and ebook partitions
- Input: single button navigation
- Battery: LiPo voltage monitoring

## Controls

- Click: move to the next item or page
- Long press: select, open, or go back depending on the app

## Wiring

| E-Ink Pin | Function | XIAO ESP32-S3 Pin |
| --- | --- | --- |
| VCC | 3.3V | 3V3 |
| GND | Ground | GND |
| DIN | MOSI | GPIO 9 |
| CLK | SCK | GPIO 7 |
| CS | Chip select | GPIO 44 |
| DC | Data/command | GPIO 10 |
| RST | Reset | GPIO 38 |
| BUSY | Busy | GPIO 4 |

Button:

- Signal: GPIO 5
- Other side: GND

Battery sense:

- Voltage ADC: GPIO 1
- Measurement switch: GPIO 6, active high

## Install From A Browser

The quickest installation method is the [Book32 Browser Installer](https://rolohaun.github.io/Book32/).
It works in desktop Chrome or Edge with a data-capable USB cable and does not
require PlatformIO or a local development environment.

- **Update existing Book32** flashes the firmware and web interface while
  preserving Wi-Fi settings, ebooks, and reading progress.
- **Set up new hardware** installs the bootloader, custom partition table,
  firmware, and web interface on a blank XIAO ESP32-S3.

The installer does not write to the dedicated ebook partition. If ESP Web Tools
offers an **Erase device** option, leave it unchecked on a device that contains
ebooks.

## Install PlatformIO

The easiest path is Visual Studio Code plus the PlatformIO extension.

1. Install Visual Studio Code.
2. Install the PlatformIO IDE extension.
3. Install Git if it is not already installed.
4. Clone this repo:

```powershell
git clone https://github.com/rolohaun/Book32.git
cd Book32
```

You can also use PlatformIO from the command line:

```powershell
python -m pip install platformio
```

## Flash Book32 To The TRMNL Kit

Connect the XIAO ESP32-S3 to your computer over USB. From the repo folder, flash
the firmware:

```powershell
python -m platformio run --target upload
```

Then flash the web interface filesystem:

```powershell
python -m platformio run --target uploadfs
```

The ebook storage partition is separate. These commands update firmware and the
web UI, but they do not erase uploaded ebooks.

On a brand-new board, the firmware creates the ebook filesystem on first boot.
After the first successful boot, the web interface should report roughly 10 MB
of ebook storage. If it reports 0 bytes, confirm that the firmware was flashed
from this project so the custom `partitions_16MB.csv` partition table was
installed.

To watch boot logs:

```powershell
python -m platformio device monitor
```

If upload fails because the board is not in bootloader mode, hold BOOT, tap
RESET, then run the upload command again.

## First Boot

1. Power on Book32.
2. If WiFi is not configured, connect to the `Book32-Setup` access point.
3. Open `192.168.4.1` if the setup portal does not open automatically.
4. Choose your WiFi network and enter the password.
5. After connection, Book32 shows its IP address on the main menu.
6. Open `http://<BOOK32_IP>/` in a browser to manage books and settings.

## OTA Updates

Book32 now uses the public GitHub release feed:

```text
https://github.com/rolohaun/Book32/releases/latest
```

No GitHub personal access token is required. Releases should include:

- `firmware.bin`
- `littlefs.bin`

The device downloads those public release assets directly when you run an update
from the web interface or the device menu.

## Useful PlatformIO Commands

Build firmware:

```powershell
python -m platformio run
```

Build the web UI filesystem image:

```powershell
python -m platformio run --target buildfs
```

Flash firmware:

```powershell
python -m platformio run --target upload
```

Flash web UI:

```powershell
python -m platformio run --target uploadfs
```

Open serial monitor:

```powershell
python -m platformio device monitor
```

## Features

- Polished boot screen with E-Ink progress feedback
- EPUB reader with saved reading progress and boot resume
- Library menu optimized for E-Ink
- Local web interface for uploading and deleting books
- Todo app
- Klipper printer monitor
- Battery indicator and charging status
- Public GitHub OTA firmware and web UI updates

## Partition Notes

Book32 uses a custom partition table. The ebook partition is mounted separately
from the firmware and web UI filesystem, so normal firmware and `uploadfs`
updates do not overwrite user ebook storage.

Fresh hardware setup uses three pieces:

- `python -m platformio run --target upload` flashes the bootloader, firmware,
  and the custom partition table.
- `python -m platformio run --target uploadfs` flashes the 1 MB LittleFS web UI
  partition named `spiffs`.
- The 10 MB `ebooks` partition is not flashed by PlatformIO. Book32 formats it
  automatically the first time it sees that partition is blank.

# Book32

Book32 is a custom E-Ink application OS for two ESP32-S3 readers: the Seeed
Studio XIAO ESP32-S3 TRMNL 7.5 inch OG DIY kit and the Seeed reTerminal E1002
/ Sticky 3.97 inch touch device. It includes an EPUB reader, a Todo app, a
Klipper printer monitor, and a local web interface for books, settings, and OTA
updates.

## Hardware

| Target | MCU / flash | Display | Input | Ebook storage |
| --- | --- | --- | --- | --- |
| Book32 | XIAO ESP32-S3, 16 MB | 7.5 inch, 800 x 480 | One button | 10 MB internal LittleFS |
| Book32 Sticky | ESP32-S3, 32 MB | 3.97 inch, 800 x 480 | GT911 touch + three buttons + buzzer | MicroSD, with 23 MB internal fallback |

## Controls

Original Book32:

- Click: move to the next item or page.
- Long press: select, open, or go back depending on the app.

Book32 Sticky:

- Tap a main-menu icon to open it and tap a library book to start reading.
- Tap the invisible left or right third of a reading page to turn backward or forward.
- Up/down buttons move backward/forward; the power/OK button selects.

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

The Sticky is a complete device and does not need external display wiring. Its
board profile uses E-Ink SPI on GPIO 13/14/15/16/17/18 (enable GPIO 47), GT911
touch on GPIO 3/2/21/41 (enable GPIO 42), MicroSD CS on GPIO 8, and its PWM
buzzer on GPIO 48. Book32 uses the buzzer only for main-menu touchscreen taps;
the eReader remains silent.

The Sticky build uses DIO flash with octal PSRAM. Do not change its flash mode
to QIO: production Sticky hardware can watchdog-reset in the ESP32-S3 ROM before
the second-stage bootloader finishes loading. Serial diagnostics use the
device's CH343 UART bridge at 115200 baud.

## Install From A Browser

The quickest installation method is the [Book32 Browser Installer](https://rolohaun.github.io/Book32/).
It works in desktop Chrome or Edge with a data-capable USB cable and does not
require PlatformIO or a local development environment.

- **Update existing Book32** flashes the firmware and web interface while
  preserving Wi-Fi settings, ebooks, and reading progress.
- **Set up new hardware** installs the bootloader, hardware-specific partition
  table, firmware, and web interface.

Choose the exact hardware in the installer drop-down before connecting USB.

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

## Flash From PlatformIO

Choose the matching environment:

```powershell
# Original 7.5-inch Book32
python -m platformio run -e seeed_xiao_esp32s3 --target upload

# Book32 Sticky
python -m platformio run -e seeed_reterminal_sticky --target upload
```

Then flash the matching web interface filesystem:

```powershell
python -m platformio run -e seeed_xiao_esp32s3 --target uploadfs
# or
python -m platformio run -e seeed_reterminal_sticky --target uploadfs
```

The ebook storage partition is separate. These commands update firmware and the
web UI, but they do not erase uploaded ebooks.

On a brand-new board, the firmware creates the internal ebook filesystem on
first boot. Sticky mounts a MicroSD card at `/ebooks` when present and otherwise
uses its internal fallback partition.

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
- `book32-sticky-firmware.bin`
- `book32-sticky-littlefs.bin`

The device downloads those public release assets directly when you run an update
from the web interface or the device menu.

## Useful PlatformIO Commands

Build both firmware targets:

```powershell
python -m platformio run -e seeed_xiao_esp32s3
python -m platformio run -e seeed_reterminal_sticky
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
- EPUB reader with streamed chapter parsing, Unicode-stable progress, and boot resume
- PSRAM-backed JPEG/PNG illustrations with high-quality monochrome dithering
- EPUB cover thumbnails decoded on demand without writing extracted cover files
- Bounded 16 KB page index for fast backward navigation without unbounded cache growth
- Reader-only idle CPU scaling for improved battery life
- Library menu optimized for E-Ink
- Local web interface for uploading and deleting books
- Todo app
- Klipper printer monitor
- Battery indicator and charging status
- Public GitHub OTA firmware and web UI updates

The EPUB image pipeline is inspired by the MIT-licensed CrossPoint Reader. It
scales images before applying Atkinson-style 1-bit dithering, preserving useful
detail on the E-Ink panel while keeping temporary working data in PSRAM.

## Partition Notes

Book32 uses a hardware-specific custom partition table. Ebook storage is mounted
separately from firmware and the web UI, so normal firmware and `uploadfs`
updates do not overwrite it. Sticky uses `partitions_32MB_sticky.csv`; its SD
card and internal fallback share the same `/ebooks` path.

Fresh hardware setup uses three pieces:

- `python -m platformio run --target upload` flashes the bootloader, firmware,
  and the custom partition table.
- `python -m platformio run --target uploadfs` flashes the 1 MB LittleFS web UI
  partition named `spiffs`.
- The `ebooks` partition (10 MB original, 23 MB Sticky fallback) is not flashed
  by PlatformIO. Book32 formats it automatically the first time it sees that
  partition is blank.

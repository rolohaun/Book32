# InkDeck on the plain LILYGO T5-4.7 E-Paper S3 (V2.3 / V2.4)

Experimental port for the LILYGO board sold as **T5-4.7 E-Paper S3** (GitHub:
LilyGo-EPD47, `esp32s3` branch; silkscreen `Screen-4.7-S3 V2.4`). It is the
model **without** frontlight, GPS, LoRa or a battery gauge. It is not the
T5 E-Paper S3 Pro / Pro Lite (H752-01) that the `lilygo_t5s3_pro` environment
targets, and the two firmware images are not interchangeable.

## What is different from the Pro

| | Pro / Pro Lite (H752-01) | Plain T5-4.7 S3 |
| --- | --- | --- |
| Panel power and control | TPS65185 + PCA9535 (EPDiy V7 mapping) | 74HCT4094 shift register on GPIO13 / 12 / 0 |
| Panel data bus | D0..D7 = 5,6,7,15,16,17,18,8 | D0..D7 = 8,1,2,3,4,5,6,7; CKV 38, STH 40, CKH 41 |
| Touch (GT911) | SDA 39, SCL 40, INT 3, RST 9 | SDA 18, SCL 17, INT 47, no reset line |
| MicroSD | CS 12, MISO 21, SCK 14, MOSI 13 | CS 42, MISO 16, SCK 11, MOSI 15 |
| Battery | BQ27220 gauge over I2C | VBAT / 2 on GPIO14 (ADC), charging inferred from the voltage trend |
| User button | BOOT (GPIO0) | Side key on GPIO21; GPIO0 is the shift register strobe |
| Frontlight | PT4103 on GPIO11 | None fitted; optional add-on on GPIO10 (see below) |
| Inkade video | Queued DMA transport | Four-pass partial updates only (LE is a register bit, not a GPIO) |

The shift register protocol (bit order, power-up and power-down sequencing)
is the one LILYGO's own `ed047tc1.c` uses, implemented in FastEPD by the
`LilyGoEinkPower` / `LilyGoIOInit` / `LilyGoRowControl` callbacks. This port
adds a panel definition, `BB_PANEL_LILYGO_T5S3`, with the EPD47 pins and reuses
those callbacks unchanged.

## Building

```
pio run -e lilygo_t5s3 -t upload
pio run -e lilygo_t5s3 -t uploadfs
```

The second command writes the web interface (the 1 MB `spiffs` partition);
without it the device boots but the web pages are missing. Same partition
layout as the Pro build (`partitions_16MB_lilygo.csv`), so later updates keep
the internal book storage.

## Controls

The three buttons on the left edge, top to bottom:

- **Top key** (GPIO21, net SENSOP_VN): the user key. Press = back, double
  click = the "side" action from Settings > Light & buttons (light on/off by
  default), hold 2 s and release = sleep, press again = wake.
- **Middle key** (BOOT, GPIO0, net STR_IO0): click = next, double click =
  previous, hold 1 s = home. The line doubles as the display register's
  strobe, driven open-drain and read back between writes, so a press held
  through a page turn can garble that one refresh. It cannot wake the device:
  GPIO0 is a strapping pin and a key held at wake-up would select the ROM
  download mode instead of the firmware.
- **Bottom key**: RESET.

Tap icons and books; while reading, tap the left or right third of the page.

## Optional frontlight

The board has no light. If you add an LED strip with a driver (for example a
PT4103 or any enable/PWM-controlled constant-current driver), connect its
control input to **GPIO10**, which is the unused MOSI pad of the 40-pin
header. InkDeck drives it with 500 Hz PWM at the brightness chosen in
Settings, holds it low in deep sleep, and the settings page, the web UI and the
top key's double click all work as on the Pro Lite. Another free GPIO can be
used instead by changing `PIN_FRONTLIGHT` in `include/Config.h`.

## How the panel is driven

FastEPD's `BB_PANEL_LILYGO_T5S3` uses the shift register callbacks written
for the older H752 Pro (same panel, same register bit layout) with the EPD47
pins. One transport difference matters: on this board STH is an active-high
start pulse clocked in just ahead of the row, not a chip select. FastEPD
therefore routes STH to the I80 D/C line and sends every row as a 10-bit
zero command (D/C high) followed by the pixel bytes (D/C low), exactly as
LilyGo's own `i2s_data_bus.c` does on the ESP32-S3. With STH held low for the
whole row (FastEPD's `BB_PANEL_FLAG_SLOW_SPH` path) the source driver never
starts and the panel stays blank. Verified on a V2.4 board: boot screen, menu,
touch and the reader render correctly.

## Known gaps and tuning

- Gray levels use the H752 pulse tables (`StickyGrayImage::LILYGO_*`); tune
  there if covers look flat or banded.
- No front capacitive Home key: Home is available from a Bluetooth gamepad
  and by returning through the menus.
- Battery percentage is derived from voltage. `BATTERY_VOLTAGE_CALIBRATION`
  in `Config.h` corrects the ADC reading against a multimeter.
- OTA from GitHub Releases looks for `inkdeck-lilygo-t5s3-firmware.bin` and
  `inkdeck-lilygo-t5s3-littlefs.bin`; until releases carry those assets,
  update over USB.

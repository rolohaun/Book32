# InkDeck 1.3.0 — LILYGO experimental preview

This document describes the original 1.3.0 release. For the current CrankBoy,
Game Boy Color and ROM upload implementation, see [the development notes](lilygo-crankboy.md).

## Hardware and installation

Only **T5 E-Paper S3 Pro H752-01** and its Pro Lite variant are targeted.
The older H752 is incompatible. Verify the PCB revision before flashing.
Sources: [manufacturer](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/tree/H752-01)
and its `docs/pinmap.md`.

Select the LILYGO card in the [browser installer](https://rolohaun.github.io/Book32/?hardware=lilygo).
Back up the existing firmware and internal data first: InkDeck installs its
own 16MB partition layout (two 4MB OTA slots, 1MB web UI, internal book fallback).
The installer does not format the SD card. If connection fails, hold BOOT,
press RESET, release BOOT and retry. If the device stays in download mode
after installation, press RESET.

On-device Settings provides Wi-Fi setup, reader preferences and sleep settings.
BOOT is Back; hold it for two seconds to sleep, release it, then press it to wake.
Automatic sleep defaults to ten minutes when not charging. The expander S3 key,
frontlight adjustment, GPS, NFC and LoRa are not exposed by this preview.
There is no built-in speaker on this board; this target has no beep or game audio.

## Game Boy app

The Paperboy menu tile opens an independently integrated, MIT-licensed Peanut-GB
core. It is inspired by the Paperboy workflow; it does not redistribute its
unlicensed application glue or promise the same compatibility or frame rate.

- Put legally obtained/homebrew `.gb` ROMs in `/roms` on a FAT32 SD card.
- Insert the card before boot. Rescan refreshes the file list (up to 64 games).
- Portrait layout: unscaled 160x144 game viewport in the upper half, touch
  D-pad/A/B/Select/Start below. Multiple touches can hold direction plus A/B.
- Back saves cartridge SRAM and returns to the ROM browser; Back again returns
  to InkDeck. Saves use `<rom>.inkdeck.sav` with a `.bak` recovery copy.
- No bundled ROMs, Game Boy Color-only games, RTC cartridges, audio or snapshots.
- Games run independently of display refresh; old display frames can be dropped.
  Serial output separates emulator FPS from physical panel updates per second.
- A separate one-pass differential mode is used for games, with a cleaning
  refresh every 30 seconds. Reader refresh preferences are not changed.

**Experimental:** the driver compiles, but no physical refresh-rate, visual
quality, touch calibration, battery-runtime or long-term waveform validation
has been completed. Avoid unattended continuous game-refresh testing. If the
image becomes faint or develops ghosting, exit to the menu for a cleaning refresh.

## Developer build

```
pio run -e lilygo_t5s3_pro
pio run -e lilygo_t5s3_pro -t buildfs
powershell -File tools/package_release.ps1 -Version 1.3.0 -Target inkdeck-lilygo
```

FastEPD is vendored at `3d2a63a52f527d4b55d4d385cbdfeac98eb6b316` with narrow
H752-01 changes: use the EPDiy V7 mapping, keep expander input 10 as a button,
move unused LCD DC output off BOOT to the disabled GPS TX, and bound the
power-good wait. Display, touch and gauge use the shared GPIO39/40 I2C bus;
LILYGO input is polled on the display-owning loop to prevent concurrent bus use
by the input worker. Native display size is 960x540; portrait is 540x960.

Before promoting to stable, verify: boot on Pro and Pro Lite; both rotations;
touch and multitouch alignment; first Wi-Fi setup and web UI; SD/fallback;
reader pages and full refresh; ROM loading/control/save/recovery; game exit;
power-good failure; timed sleep, BOOT wake and sleep current; measured FPS.

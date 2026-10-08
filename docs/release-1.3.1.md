# InkDeck 1.3.1

Released October 8, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Choose the matching card in the
[browser installer](https://rolohaun.github.io/Book32/).

## LILYGO — experimental

- Clean **InkDeck 1.3.1** header with no Ink Boy test-build suffix.
- Ink Boy library with detected-system tabs: Game Boy / Game Boy Color
  (CrankBoy), NES (Nofrendo), and Genesis (ClownMDEmu). No game audio or bundled ROMs.
- ROM upload and deletion in the device web interface, with readable sizes
  and bounded validation. Files live in `/roms` on a FAT32 MicroSD card.
- Game Boy uses 3× scaling; NES uses exact 2× scaling, 512×480. The game
  viewport updates independently of the static controls.
- Synchronized display pipeline and reduced memory traffic. NES emulation
  measured about 60 FPS; actual display presentation measured roughly 51–60
  FPS (average about 56), depending on the scene. Not a promise of 60 displayed
  FPS for all games. The replacement Genesis core has not yet been timed on hardware.
- Save slots and automatic save on **Save & exit**. Tap the game viewport for
  a manual cleaning refresh; periodic full-screen flashing during play is off.
- Frontlight brightness and configurable IO48 / lower-front button actions.
  Safe display margins and a sleep screen that says **Press BOOT to wake**.
- E-reader, Todo, Klipper and on-device Settings remain available.

Check the PCB revision: **the older H752 is not compatible**. Back up important
data before changing firmware or partition layouts. The installer does not
format the SD card. If the device remains in download mode, press RESET.

Genesis now uses ClownMDEmu with its own Clown68000 and ClownZ80 CPUs, replacing
the non-commercial cores from local test builds. The combined firmware is
AGPLv3-or-later; redistribution still requires compliance with its source and
notice obligations. The tagged repository and release source archive provide
the source, patches, scripts and exact library sources; `inkdeck-licenses.zip`
contains notices. See [licensing](../LICENSE.md) and [third-party notices](../THIRD_PARTY_NOTICES.md).

**Genesis save compatibility:** old snapshots are left untouched, but cannot
be loaded by the replacement core. New saves use a separate
`.inkdeck.clown.sav.stateN` namespace. GB/GBC and NES saves are unchanged.
Genesis synthetic CPU/video/interrupt, silent-timer, input, NTSC/PAL and snapshot
tests pass. Game compatibility and frame rate still need device testing.

## Book32 and Seeed Studio Sticky

Both receive the current reader, settings and web-interface code as 1.3.1.
Ink Boy is LILYGO-only and does not appear on these devices. Firmware and web
filesystem images are rebuilt separately for each board; their flash sizes,
pinouts and partition layouts are unchanged.

The update checker now filters unused GitHub asset metadata so the larger
three-device release can be parsed without exhausting its JSON document.

## Installation

Use desktop Chrome or Edge, select your device, then click **Flash InkDeck
1.3.1**. The installer downloads version-pinned firmware and web-interface
files, verifies the partition table, and restarts the board. Current aliases
are also attached to the GitHub release for the on-device updater.

Flashing through the installer replaces the web-interface filesystem. Existing
book storage is preserved when updating the same InkDeck partition layout.
The corresponding source is available from the `v1.3.1` tag; build with the
pinned PlatformIO environments in `platformio.ini`.

# InkDeck 1.3.2

Released October 8, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Ink Boy on LILYGO

- Removed the experimental Genesis emulator, Sega tab, three-button controls
  and Genesis ROM upload support. Performance did not meet the intended experience.
- Game Boy / Game Boy Color (CrankBoy) and NES (Nofrendo) remain, with tabs
  shown only for games found on the SD card.
- Upload `.gb`, `.gbc` or `.nes` through the device web interface, or copy them
  into `/roms` on a FAT32 MicroSD card. No ROMs are bundled and there is no game audio.
- Existing Genesis ROMs and save files are not deleted or converted. They
  simply no longer appear in the supported-game library.
- Existing GB/GBC and NES saves, controls, display scaling, synchronized
  refresh, manual cleaning and frontlight settings are unchanged.

The LILYGO build remains experimental and supports **H752-01 / Pro Lite only**,
not the older H752. The header shows **InkDeck 1.3.2** without test-build suffixes.

## All three devices

Firmware and web-interface images are rebuilt as 1.3.2 for each board. Book32
and Sticky continue to omit Ink Boy. Their reader and other apps are unchanged.
Pinouts and partition layouts are unchanged. The flasher updates both the
application and its web interface; an application-only update can leave old
web-interface text behind. The installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect.
The release includes complete tagged source, the dependency source archive
and license notices. Earlier release tags and their license records remain
available in Git history.

## Verification

GB/NES execution and save regression tests, ROM validation and retired-format
rejection, upload UI tests, game controls/library geometry, display/DMA timing
tests and all three firmware builds are checked for this release. The connected
device is not flashed automatically as part of publishing.

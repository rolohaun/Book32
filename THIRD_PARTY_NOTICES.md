# Third-Party Notices

## Ink Boy multi-system build (LILYGO only)

InkDeck 1.3.2 includes Game Boy / Game Boy Color and NES emulation. No Genesis
core is shipped in this release, including the earlier experimental
Gwenesis/Musashi/Marat Z80 integration. InkDeck's project license remains
AGPLv3-or-later. See LICENSE.md and COPYING.

- Nofrendo: Matthew Conte and contributors. Imported from
  https://github.com/ducalex/retro-go at commit
  `4ced120669750ca7228fd0414211430c1d923166` using `tools/import_nofrendo.py`.
  Source headers specify GNU Library General Public License version 2; its
  full text is in `lib/InkNes/COPYING.LGPL2`. The upstream `COPYING` and
  `CREDITS` are also preserved. Local changes supply memory-backed saves, bounded ROM validation,
  grayscale video, input and persistence.

Genesis emulation was removed in InkDeck 1.3.2. Its former core and license
notices remain available with the historical 1.3.1 release, but are not included
in this build. GB/GBC and NES retain their existing licenses and save formats.

No game ROMs or BIOS files are included. The tagged source and release source
archive contain the integration, vendored cores, build scripts, local patches,
and exact PlatformIO library sources used. The Arduino core/library sources
are included; the pinned Espressif toolchain and SDK are installed by PlatformIO.
Rebuilding and installing modified firmware does not require a signing key.

## FastEPD (LILYGO target)

Copyright (c) 2024 BitBank Software, Inc., written by Larry Bank.
Source: https://github.com/bitbank2/FastEPD at commit
`3d2a63a52f527d4b55d4d385cbdfeac98eb6b316`. Apache-2.0; the complete license
is in `lib/FastEPD/LICENSE`. Local H752-01 changes to `FastEPD.inl` preserve
the function-button input, change the unused DC GPIO and bound power-good waits.

## CrankBoy (LILYGO Game Boy / Game Boy Color app)

Source: https://github.com/CrankBoyHQ/crankboy-app at development commit
`1501ba6615997df5dd36afa17f1a2fb8813231cc`.
CrankBoy is a modified Peanut-GB core, copyright 2024-2025 CrankBoy contributors,
2018-2023 Mahyar Koshkouei, with portions from SameBoy copyright 2015-2019
Lior Halphon. Complete MIT notices are preserved in
`lib/Apps/AppPaperboy/crankboy/LICENSE` and the source headers.
The included MiniGB APU has its own MIT notice in
`lib/Apps/AppPaperboy/crankboy/minigb_apu/LICENSE`.

`tools/import_crankboy.py` records the pinned, reproducible adaptation: replace
Playdate/ARM platform glue, make unaligned memory access portable, bound small
cartridge RAM, and provide SD-backed ROM banks. InkDeck supplies display,
input, SRAM/RTC persistence and timing. Playdate UI, ARM code relocation,
scripts and save-state support are not included. The APU emulates registers
and timing but InkDeck does not output game audio. No commercial ROMs, BIOS
images or Zephray Paperboy application glue are included.

## Modos Smooth Graphics (MSG)

InkDeck's LILYGO experimental video scan mode adapts the per-pixel four-pulse
tracking approach from Wenting Zhang's MIT-licensed MSG display code:
https://gitlab.com/zephray/paperboy/-/blob/main/main/msg/msg.c

Copyright 2026 Wenting Zhang. The complete MIT license is preserved in
`lib/FastEPD/MSG-LICENSE`. `InkDeckVideoPulse.h` and the H752-01 `videoScan`
integration use InkDeck's existing FastEPD power control, pins and DMA transport;
they do not use the M5PaperS3 power wiring or unlicensed Paperboy application glue.

## esptool-js

The InkDeck browser installer uses Espressif's `esptool-js` Web Serial flasher:
https://github.com/espressif/esptool-js

Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd. `esptool-js` is
distributed under the Apache License 2.0. Its complete license text and source
are available in the linked upstream repository.

## CrossPoint Reader

InkDeck's EPUB image rendering is inspired by and contains adapted concepts from
CrossPoint Reader: https://github.com/crosspoint-reader/crosspoint-reader

MIT License

Copyright (c) 2025 Dave Allie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## bb_epaper

The InkDeck Seeed Studio Sticky display adapter uses Larry Bank's `bb_epaper` library:
https://github.com/bitbank2/bb_epaper

Copyright (c) 2024 BitBank Software, Inc. `bb_epaper` is distributed under the
GNU General Public License, version 3 or later. Its complete license text and
corresponding source are available in the linked upstream repository.

# ClownMDEmu for InkDeck

Source: https://github.com/Clownacy/clownmdemu-core
Pinned revision: `88ef45a6585556e2247dd26b6c937d6b9fb4a12d`.
ClownMDEmu, Clown68000 and ClownZ80 are copyright Clownacy and contributors,
licensed under AGPL-3.0-or-later. Clowncommon is ISC licensed. All original
notices and full licences are retained in `clownmdemu/` and its sublibraries.

Reimport from a recursive checkout using `tools/import_clownmdemu.ps1 -Source
<checkout>`. The script verifies every pinned submodule and applies
`tools/clownmdemu-vdp-memory.patch`: the 96 KiB VDP lookup is allocated only
while Genesis is open, in PSRAM, instead of permanently occupying internal RAM.
`InkGenesis.c` compiles the upstream unity build only for the LILYGO target.

The frontend in `lib/Apps/AppPaperboy/GenesisCore.c` supplies ROM storage,
three-button input, four-shade output, NTSC/PAL timing and save snapshots.
Sound synthesis is disabled; CPU-visible FM timers and BUSY behavior remain.
Sega CD, 32X, SMD and six-button games are not supported. No ROMs or BIOS are
included. Hardware frame rate and game compatibility of this replacement
core have not yet been measured; it is an experimental integration.

Genesis states now use `.inkdeck.clown.sav.stateN`, isolated from the previous
experimental core's `.inkdeck.sav.stateN` files. Old saves are left untouched,
but cannot be loaded into the new core. GB/GBC and NES formats are unchanged.

## Source and rebuilding

The complete integration and build instructions are in the InkDeck source
release: https://github.com/rolohaun/Book32/tree/v1.3.1 . Build with the pinned
PlatformIO environment `lilygo_t5s3_pro`; test with
`tools/test_console_cores.ps1 -Wasi -Optimization '-O3'`.
See the root `LICENSE.md` and `THIRD_PARTY_NOTICES.md` for redistribution terms.

# LILYGO CrankBoy development build

Current release: `1.3.1` (experimental multi-console build).
Last verified USB installation: `1.3.1-inkboy17` (2026-10-07).
LILYGO T5 S3 Pro H752-01 / Pro Lite only.
Book32 and Sticky have neither the emulator nor the Game ROMs web tab/API.
The notes below record development experiments leading to the
[1.3.1 release](release-1.3.1.md); historical test-build names are retained.

**Release change:** Genesis now uses AGPLv3+ ClownMDEmu, Clown68000 and
ClownZ80, replacing the earlier restricted CPU cores. New Genesis saves use
their own namespace; older snapshots remain untouched but are incompatible.
The replacement passes host regression tests and the ESP32 build, but its
hardware frame rate is unmeasured. See [current core notes](../lib/InkGenesis/README.md).
Gwenesis measurements, import scripts and licensing statements below are
historical development notes, not descriptions of the released Genesis core.

## Playing and uploading

### Multi-console additions in inkboy2

The user approved a **personal-use** Genesis build. Its pinned Retro-Go port
contains non-commercial Musashi/Z80 components; this combined build is not
cleared for commercial distribution. The public installer labels it as a
personal-use experiment. See
`THIRD_PARTY_NOTICES.md` for exact revisions and retained license notices.

The new library detects supported extensions in `/roms`, sorts titles, and
shows only populated **Game Boy**, **NES** and **Sega** tabs. GB and GBC share
Game Boy. Cards show format and whether an automatic resume file exists, with
five games per page, Rescan and pagination controls. A loading screen appears
before reading a ROM and restoring its state. Invalid headers or unsupported
mappers produce a visible error.

| Tab | Core | Accepted files | Limit |
| --- | --- | --- | --- |
| Game Boy | CrankBoy DMG/CGB | `.gb`, `.gbc` | 8 MiB |
| NES | Retro-Go Nofrendo | iNES `.nes` | 2 MiB |
| Sega | Retro-Go Gwenesis | raw `.md`, `.gen`, `.bin` | 4 MiB |

NES 2.0, FDS, VS/PlayChoice, SMD-interleaved images, archives, Sega CD and 32X
are not supported. This is a single-player, three-button Genesis pad (A/B/C
and Start), not a six-button controller. Game Boy/NES keep A/B/Select/Start.
NES preserves all 256×240 source pixels at exact 2× scaling (512×480) with
square source pixels, not a television 4:3 stretch. Genesis retains its 4:3
480×360 picture letterboxed inside the unchanged 480×432 fast video window.
Both are converted to four grayscale shades.
PAL games are paced at 50 Hz; NTSC at approximately 60 Hz. Actual emulation
speed is workload-dependent and is not guaranteed by the panel's scan rate.
No game audio is output; sound-chip registers/timing are still emulated.

Pause/saves, three manual slots, automatic resume, restart and manual screen
cleaning use the shared frontend. Existing GB/GBC saves are unchanged. NES
battery SRAM is persisted separately. **Genesis uses complete emulator save
states; cartridge SRAM/EEPROM hardware is not implemented by this port.**
Snapshots are InkDeck-specific, not interchangeable with other emulators.
ROM identity, exact sizes, checksums and state record schemas are checked
before restore. A different ROM revision can invalidate its resume slot;
use Restart from the pause menu in that case.

`tools/import_nofrendo.py` and `tools/import_gwenesis.py` reproduce vendor
changes from Retro-Go revision `4ced120669750ca7228fd0414211430c1d923166`.
Genesis working RAM and VRAM are allocated only while a Genesis game is open.
Starting with inkboy3, audio synthesis and its lookup allocations are omitted.
Both new cores are compiled out of Book32 and Sticky.

Host checks: `tools/test_console_cores.ps1 -Optimization '-O0'` (runtime
alignment/undefined-behavior checks) and `-O3` (release optimization), using
the local Zig compiler. Fixtures contain original tiny CPU programs, not
commercial games. Checks cover CPU execution, frame output, joypad mapping,
PAL/NTSC timing, snapshots/reopening/corruption/wrong-ROM rejection, zero-RAM
restoration and GB→NES→Genesis switching. GFX tests cover conditional tabs,
hit zones, system-specific controls and exact game-window bounds. Web tests cover
new upload extensions/limits and retain non-LILYGO capability gating.

### Native NES 2× video (inkboy9)

The old NES path sampled 256×240 down to 160×120 before the shared GB 3×
packer enlarged it to 480×360. Inkboy9 keeps every source column and line,
including odd scanlines, then emits a 512×480 image at (6,48) within the safe
524×944 portrait canvas. The NES bezel is enlarged and all its controls move
12 pixels lower, using identical draw/touch geometry. GB/Genesis layouts and
emulation policies are unchanged. Tapping anywhere in the NES window, including
its top rows, requests a manual game-only clean; the pause hit zone ends above it.

Four shades use stable 2×2 coverage (0,1,3,4 black dots), without interpolation
or temporal dithering. Each native display row has independent pixel history;
the queued and serial scan transports support one distinct row for NES while
retaining three-row repetition for GB/Genesis. Six-pulse Contrast, voltage,
pixel clock and neutral drive outside the game rectangle are unchanged. NES's
larger packed triple buffers and history prefer PSRAM. The 60 Hz scan target
is not a measured NES frame-rate claim.

Existing NES states keep their exact envelope size and SNSS payload. New saves
include a lossless two-bit 256×240 preview in previously unused capacity, which
older builds ignore. Old saves still restore; their legacy thumbnail is shown
only until the next actual emulated frame. No CPU frame is advanced just to
create a save preview. Battery RAM, GB/GBC and Genesis save formats are unchanged.

Checks: release-optimized WASI core tests cover native-pixel palette conversion,
lossless previews, old-save restoration and all prior GB/NES/Genesis regressions.
The on-boot video self-test checks every output dot in both orientations and
pixels outside the NES rectangle. Mock asynchronous DMA tests exercise distinct
NES rows, six-pulse state evolution, pending-buffer lifetimes and failure paths.
The rendered GFX layout and exhaustive touch-mask checks verify no screen/control
or D-pad/button overlap. Scheduler, network-handoff and web regression checks pass.
All three firmware targets built successfully. On 2026-10-07, the user-approved
application-only COM9 flash wrote 2,544,784 bytes to app0 at 0x10000, with hash
verification. The device identity and active OTA slot were checked first.
ROMs, saves, NVS/settings and web files were not erased or uploaded. The reboot
passed the GB 3× / NES 2× video self-test, mounted the SD card and started the
network/web services (`.pio/flash-inkboy9.log`, `.pio/boot-inkboy9.log`).
The device restored an existing NES automatic save and entered the native 2×
path, but the user reported an unacceptable frame rate. The 25-second live log
`.pio/nes-inkboy9-running.log` confirmed 16.0–16.5 emulated/drawn fps, 19.42–20.87
ms core/conversion plus 40.96–41.01 ms image packing, and 34.5–34.7 panel scans/s
(28.04–28.18 ms drive time). No panic/reset or display error appeared in that
capture. The updater still reports the pre-existing `JSON parse error: NoMemory`.
The resolution fix is not a successful performance result.

### NES PSRAM locality follow-up (inkboy10)

Direct column traversal of the native 61 KB PSRAM image was repeatedly fetching
cache lines before adjacent columns could reuse them. The 2× packer now stages
16-column tiles in about 4 KB of task-stack/internal memory and writes complete
packed row pairs back sequentially. The four grayscale coverage masks, native
pixel coordinates and game rectangle are unchanged. Palette conversion likewise
uses short sequential row copies, and gameplay no longer builds/copies the
legacy 160×144 save thumbnail each frame. Thumbnails are generated on demand
for API callers and saving; legacy restore previews remain unchanged.

The pixel-for-pixel two-orientation, clipping and DMA tests pass with the tiled
implementation, along with the release-optimized console and scheduler suite.
New NES timing counters separate CPU/PPU work from grayscale conversion.
The user approved the follow-up flash after saving. The 2,545,360-byte app-only
COM9 write passed hash verification and its reboot passed the NES/GB video
self-test (`.pio/flash-inkboy10.log`, `.pio/boot-inkboy10.log`). The device then
restored its NES automatic save and ran without a panic, watchdog, display error
or extra reset during the full 55-second capture. After the first maintenance
sample, gameplay measured 30.3–31.7 emulated/drawn fps versus inkboy9's 16.0–16.5.
Packing dropped from about 41 ms to 12.07–12.09 ms. CPU/PPU stage samples were
13.82–15.17 ms and conversion 5.26–5.27 ms. Panel scans remain 34.1–34.5/s, with
27.88–28.30 ms drive time. This is a substantial recovery, **not 60 fps**;
conversion/packing and the larger display scan remain bottlenecks. Different
gameplay moments are not a controlled benchmark. The unrelated updater
`NoMemory` warning remains. The serial port was closed after measurement.

### Compact NES scan and fused conversion (inkboy11)

NES gameplay now converts the pitched indexed framebuffer straight to rotated,
lossless two-bit shades. It no longer builds a full grayscale intermediate or
the legacy thumbnail on each frame. Native and legacy save previews are still
generated on demand, with unchanged save envelopes and restoration behavior.
The native 256×240 picture, exact 512×480 window and four gray levels are retained.

Each 2×2 grayscale cell uses three independent binary thresholds: one of the
four physical dots has an identical partner. The new queued scan stores those
three pulse histories and expands two physical DMA rows together. This needs
3/4 as many paired-state lookups as the old independent-dot path and 92,160
history bytes instead of 122,880. It tries internal RAM with a 24 KiB reserve,
falling back to PSRAM if necessary. Five static DMA rows keep in-flight buffers
immutable. Pulse strength, panel timing and neutral drive outside the game
window are unchanged; this does not skip game simulation or image frames.

Nofrendo scanlines render into a short internal-memory staging buffer before
being copied to PSRAM. Including the ESP attribute definition also fixes the
previously ineffective CPU-loop IRAM annotation. The build map confirms both
`nes6502_execute` and `ppu_renderline` are now in instruction RAM. This consumes
additional instruction RAM shared with the data heap; runtime memory placement
and all-console switching still need hardware verification.

Release `-O3` and `-O1` WASI console regressions pass, including staged-versus-
original scanline pixels and complete PPU state across layer masks, scrolling,
sprite modes and edge overdraw. Indexed packing matches the reference output in
both rotations. Compact pulse tests compare every physical drive byte and
counter against independent histories through resets and interrupted gray
transitions, exercise queued/serial DMA completion schedules, and inject every
send/wait failure. Scheduler, network handoff and web regressions pass. The
LILYGO build succeeds. On 2026-10-07, the user-approved 2,561,632-byte app-only
COM9 write to verified app0 passed hash verification. Reboot passed the video
self-test, mounted the SD card and started the web interface. No settings,
ROMs, saves or web files were erased (`.pio/flash-inkboy11.log`,
`.pio/boot-inkboy11.log`). All three hardware targets built successfully.
The user confirms the NES image remains correct. Steady gameplay in
`.pio/nes-inkboy11-running.log` measured 51.9–52.3 emulated/rendered fps, with
10.86–11.06 ms CPU/PPU and 7.94–7.97 ms packing. However, presentation regressed
to about 27 distinct frames/s (35.46–35.58 ms scan work). This is NOT a smooth
52 fps display result: roughly half the prepared frames are superseded before
presentation. One manual-clean interval was excluded from the steady figures.

### NES internal scan staging (inkboy12)

To address inkboy11's presentation regression, each small history/source row is
copied into internal-stack memory before processing, then copied back in bulk.
A NES-specific table prepositions pulse codes and precalculates threshold masks.
Unchanged cells skip work only when all three pulse histories are fully settled;
unfinished transitions still run exactly as before. The packing and pulse loops
are placed in IRAM, while safe queued DMA ownership and watchdog yields remain.
Logs now call prepared frames "rendered" and report distinct presented frames/s
explicitly, so an emulation improvement cannot be confused with display FPS.
Pixel/counter, queued-DMA lifetime/failure and clipping tests pass with these
shortcuts, as does the scheduler check. The LILYGO build succeeds; symbol
inspection confirms the packing, pulse, NES CPU and PPU loops are in IRAM.
The approved 2,564,816-byte application-only COM9 flash passed hash verification
and boot self-test (`.pio/flash-inkboy12.log`, `.pio/boot-inkboy12.log`). The
user confirms the NES image is correct. Steady gameplay measured 55.6–56.3
emulated/rendered fps and 39.8–40.7 distinct presented frames/s; scan work was
23.42–23.84 ms, CPU/PPU about 10.8–11.1 ms and packing 6.71 ms. The 92,160-byte
history remained in PSRAM (96,776 internal bytes free before allocation).
This improves presentation but does not achieve 60 fps.

### Exact nine-bit NES history (inkboy13)

The three independent six-pulse counters have only 304 reachable combinations
across the four possible incoming shades. A lossless nine-bit state ID replaces
the twelve-bit representation. One table lookup advances all three thresholds
and produces the four physical dot-drive codes, without changing pulses,
gray coverage, mid-transition reversals, resolution or frame simulation.
The table enumerates the closed reachable set; tests exhaustively compare all
304×4 transitions and drive codes with the independent threshold reference.

History is now 69,120 bytes. Eight small internal-memory banks avoid the large
contiguous-allocation requirement while retaining 24 KiB runtime headroom.
Allocation is all-or-nothing, with PSRAM fallback and normal cleanup on app
exit. Queued tests exercise non-contiguous/reordered banks, both orientations,
all history counters, DMA lifetimes, reset and transport failures.
The approved 2,566,784-byte application-only flash passed hash verification and
boot self-test. The user confirms correct output. However, startup reported
93,656 internal bytes free, just 40 bytes below the 24 KiB reserve cutoff;
the history fell back to PSRAM. Presentation was only 31.1–31.2 fps despite
56.2–57.1 emulated fps. A reopen likewise fell back (93,164 bytes free).
This did not validate the intended internal-memory optimization
(`.pio/flash-inkboy13.log`, `.pio/boot-inkboy13.log`).

### NES cache locality and allocation threshold (inkboy14)

The bank allocator now reserves 20 KiB plus explicit allocator overhead rather
than 24 KiB; Wi-Fi remains paused during gameplay and save envelopes use PSRAM.
Allocation is still transactional with fallback. Fully settled state-ID pairs
emit neutral drive without transition lookup or history rewrites.

Packing uses 32×16 tiles, keeping one short horizontal input band active in
cache instead of traversing the entire tall framebuffer before revisiting its
neighboring cache lines. The small packed output remains cache-local and uses
aligned four-byte writes. Both rotations, padded indexed input, all four shades,
pixel histories, clipping, DMA ownership and error-path tests pass.
All three firmware targets build successfully. The user-approved 2,568,144-byte
application-only COM9 flash passed hash verification and boot video self-test
(`.pio/flash-inkboy14.log`, `.pio/boot-inkboy14.log`). The user reports smoother
NES gameplay. Subsequent live captures (`.pio/nes-inkboy14-running.log`,
`.pio/nes-inkboy14-reopen.log`, `.pio/nes-inkboy14-placement.log`) measured about
59.8–60.2 emulated/rendered fps with zero over-budget frames in those samples,
roughly 10.3–10.9 ms CPU/PPU plus 4.39–4.42 ms packing. Presentation remains
about 36–38 distinct frames/s, with about 25.6–26.4 ms scan work. Thus NTSC
simulation is full speed in the tested game, but 60 Hz screen presentation
has NOT been achieved. The later capture windows missed the game's allocation
line, so internal-bank placement on inkboy14 is not yet hardware-confirmed.
Memory/performance after switching to Genesis also remains to be checked;
the new NES IRAM allocation shares physical memory with the data heap.

### NES scan shortcuts and stage profiling (inkboy15)

The 2× NES scan cannot directly reuse the 3× Game Boy row-repetition shortcut:
each NES cell emits two different physical rows to retain four gray levels.
The pulse LUT now stores both orientation-specific drive arrangements in the
same 32-bit entry, eliminating per-pixel drive-bit reshuffling. A 1 KiB table
recognizes eight fully settled cells at once and emits neutral data without
unpacking their individual counters. High state bits are checked as well as
low bytes; pending transitions and mid-pulse reversals are never skipped.
The image geometry, six-pulse waveform, panel clock/voltage, neutral borders,
manual cleaning and DMA buffer ownership remain unchanged.

One out of every 180 ordinary NES scans records copying, pulse computation,
DMA submission and wait times, plus history placement and free internal RAM.
Normal scans compile out the per-row profiling hooks. Sampled durations include
instrumentation/interrupt overhead and describe CPU wall time, not independent
DMA latencies; physical transfers overlap preparation. This allows the next
hardware test to locate the remaining presentation bottleneck and confirm RAM
placement without needing to capture a ROM-open log line.

WASI display tests pass at `-O2` and `-O1`: exact counters/drive codes, both
orientations, clipping, queued DMA lifetimes/errors, all 256 packed shade-byte
patterns, mixed settled/pending groups and high-bit state aliases. Scheduler,
network and ROM/settings web regressions pass. The LILYGO build succeeds.
All three targets compile. The approved 2,572,000-byte app-only COM9 flash
passed hash verification and boot video self-test. The initial serial reader
stopped on a Windows console encoding error (not a device crash); a no-reset
UTF-8 capture then recorded live gameplay (`.pio/nes-inkboy15-running.log`).
Across 34 non-maintenance samples, emulation/rendering were 59.7–60.1 fps;
presentation was 44.3–54.2 fps (mean 50.8) and ordinary scan work 17.81–21.16 ms.
These are varied gameplay samples, not a controlled same-scene benchmark.
The sampled profiler confirms history is in PSRAM: copies around 7 ms, pulse
calculation roughly 4–8 ms and submission roughly 6.4 ms. Sampled totals include
profiling overhead; ordinary scans have no per-row clocks. No panic/watchdog or
display error was seen. This is not yet a 60 Hz display result.

### Fragmentation-tolerant history banks (inkboy16)

Replace the eight 8,640-byte all-or-nothing internal allocations with 32
2,160-byte banks. Each bank independently prefers internal RAM or falls back
to PSRAM; insufficient space for one bank no longer discards all usable SRAM.
The 20 KiB reserve is checked before AND after each internal allocation.
A true allocation failure rolls back every owned bank, retaining the existing
contiguous PSRAM/four-pass safety fallback. Banks are released on app exit and
console switches. The scan accepts explicit bank row counts and validates
whole-row division and every bank pointer before powering the display.

Host tests cover fragmented heaps, varying allocation overhead, partial
internal placement, reserve enforcement and rollback at every failure point,
alongside the waveform/pixel/DMA tests. Full 480×512 scan tests compare every
DMA byte and stored state between contiguous and 32 reordered banks, including
the profiled specialization; WASI tests pass at `-O2` and `-O1`. Stage logs now
report the actual count of internal banks and largest free internal block.
All three hardware builds pass. The approved 2,572,736-byte app-only COM9 flash
passed hash verification; boot self-test, SD mounting and web startup passed
(`.pio/flash-inkboy16.log`, `.pio/boot-inkboy16.log`). The user confirms correct
NES output. All 32 banks now use internal RAM, with 21,088 bytes initially free
and roughly 20,500 bytes during the captured gameplay. Across 28 non-maintenance
samples: emulation/rendering 59.8–60.2 fps; presentation 51.2–59.8 fps (mean 55.1);
ordinary scan work 15.32–18.40 ms. It reaches near 60 but is not locked to 60.

### Direct internal history (inkboy17)

Internal history banks can be processed in place: only the scan task owns
them, and DMA reads the separate output row buffers. External banks retain
the cache-friendly staging path. This removes redundant read/copy-back work
without changing any stored state, pulse count or buffer ownership. Full-size
tests compare all-staged, all-direct and mixed-bank execution, every output
byte and counter, both rotations and the profiled specialization; `-O2` and
`-O1` tests pass. Scheduler, network and ROM/web regressions pass. The LILYGO
build passes (as do Book32 and Sticky); the approved 2,573,312-byte app-only COM9 flash passed hash
verification, followed by boot video self-test, SD and web startup. No ROMs,
saves, settings or web files were erased (`.pio/flash-inkboy17.log`,
`.pio/boot-inkboy17.log`). The user confirms the NES picture remains correct.
Across 27 non-maintenance five-second samples: emulation/rendering 59.7–60.2
fps, with zero over-budget emulation frames; presentation 50.6–59.8 distinct
fps (mean 56.3); ordinary scan work 15.15–18.42 ms. Every scan presented a new
frame, with zero repeats in these samples, but some rendered frames were still
superseded before presentation. All 32 banks were internal, with approximately
21 KB free; no panic, watchdog, video fault or fallback occurred. This reaches
near 60 in lighter scenes but is NOT a locked 60 Hz display. The timing sample
includes instrumentation and DMA interrupt overlap; it is not optical latency.
Per-row preparation and transport submission remain candidates for further
profiling. No panel overclock, voltage change or reduced gray/resolution mode
was used. Genesis memory placement/performance after switching still needs a
separate hardware check before treating this as a public multi-console release.

### Cardputer NES comparison (2026-10-07)

Inspected `geo-tp/Cardputer-Game-Station-Emulators` branch `xip_load`, pinned
revision `74b47763c3f2ccbaed6505b83ef0ef37c2deac82` (research checkout under
`.pio/upstream-cardputer`). No source from this checkout was incorporated.

- Its [6502 core](https://github.com/geo-tp/Cardputer-Game-Station-Emulators/blob/74b47763c3f2ccbaed6505b83ef0ef37c2deac82/lib/arduino-nofrendo/src/cpu/nes6502.c)
  enables GNU computed-goto opcode dispatch and `O3`. Our Retro-Go Nofrendo
  has that dispatch alternative but leaves it disabled; this is a candidate
  for equivalence testing and measured A/B benchmarking, not a verified win.
  Our CPU already uses `O3`, fast-page accesses and instruction RAM.
- Its [NES frontend](https://github.com/geo-tp/Cardputer-Game-Station-Emulators/blob/74b47763c3f2ccbaed6505b83ef0ef37c2deac82/src/nes/nes_osd.c)
  reuses a framebuffer and sends work to a separate display-core task. InkDeck
  already has a separate worker, immutable triple buffers and scan-boundary
  handoff. Cardputer drops stale queue entries; its Nofrendo main loop also
  skips drawing while catching up. This is not proof of 60 distinct images/s.
- Its [LCD renderer](https://github.com/geo-tp/Cardputer-Game-Station-Emulators/blob/74b47763c3f2ccbaed6505b83ef0ef37c2deac82/src/nes/nes_display.cpp)
  converts through an internal scanline buffer and scales for a roughly
  240×135 LCD. It does not perform InkDeck's 512×480 e-ink pulse-history scan.
  Our internal scanline staging and cache-local conversion serve a similar
  purpose but cannot reuse its LCD transfer method.
- Its [build configuration](https://github.com/geo-tp/Cardputer-Game-Station-Emulators/blob/74b47763c3f2ccbaed6505b83ef0ef37c2deac82/platformio.ini)
  requests a 270 MHz CPU clock. InkDeck's hardware log confirms 240 MHz;
  no clock/voltage change was made. Cardputer also maps ROMs from flash for
  memory savings, requiring a different flash/partition workflow.

InkDeck's active NTSC period is 16,667 microseconds (approximately 60 Hz),
verified in the boot/game log and core tests; PAL remains 20,000 microseconds
(50 Hz). The cumulative scheduler follows elapsed microseconds rather than
counting panel scans, so a slower panel does not force slower game simulation.
Every NES frame is still emulated and rendered; the display presents the newest
complete snapshot at a scan boundary. Logs distinguish rendered frames, actual
fresh presentations, repeats and superseded frames. Panel scan rate is measured
in software, not with an oscilloscope or an optical sensor.

### Genesis memory/rendering optimization (inkboy4)

The user confirmed inkboy3 starts games but runs slowly. A live Sonic 2 capture
on 2026-10-07 measured 15.8–17.4 emulated fps, 55.5–62.1 ms core time and
1.24–1.25 ms packing while the panel continued at 55.1–55.9 scans/sec. This is
an emulation bottleneck; a fast panel scan does not mean full-speed gameplay.

Inkboy4 prefers internal RAM for Z80 RAM, then VRAM, then 68000 RAM, retaining
48 KiB free for the display history and runtime operations, with PSRAM fallback.
It replaces the 77,120-byte indexed framebuffer with a 384-byte padded internal
scanline, and caches palette-to-gray conversions and horizontal sample indices.
Palette changes are checked on each sampled line, preserving raster changes.
Every visible VDP line is still rendered so previous-line sprite overflow and
masking are preserved. No CPU steps or drawing frames are skipped in this build.

Serial logs report memory placement and separate 68000, Z80, VDP and grayscale
timings. Host comparisons check the scanline output against the original full
framebuffer in H32/H40, PAL/NTSC and shadow/highlight modes, plus buffer guards
and blanking. Audio remains silent and the six-pulse display path is unchanged.
The app-only COM9 flash passed hash verification and boot/video self-tests.
Live Sonic 2 samples reached 17.8–19.1 fps after the first sample, still too
slow. Stage samples measured approximately 18–22 ms 68000, 13–14 ms Z80,
16–17 ms VDP, and 2.3–2.4 ms conversion. Different scenes are not a controlled
benchmark; these samples establish the remaining bottlenecks, not a speed claim.

### Genesis allocation-order follow-up (inkboy8)

Inkboy7's first hardware sample improved simulation speed but unexpectedly
placed 68000 work RAM in PSRAM despite 140,532 bytes free internally after core
allocation. The large allocation had fallen back, consistent with fragmentation.
Previously, the library allocated about 49 KB of frame buffers internally before
creating its persistent task stack and scanning the ROM list. Relocating those
buffers at ROM-open time frees bytes but cannot move intervening allocations to
recover a single 64 KB block.

Library-entry frame buffers now prefer PSRAM from the start; GB/NES still promote
them to internal RAM when selected. Genesis keeps the compact buffers in PSRAM.
The existing internal-first CPU-RAM allocation, reserve check and safe fallback
remain. A new log reports total free and largest contiguous internal block
before the 68000 allocation. Profiling counters are also reset together before
resuming gameplay so partial pre-pause counts cannot inflate the next sample.
All three firmware builds passed; LILYGO uses 92,836 bytes static DRAM and
2,537,213 bytes application flash. Scheduler source checks and the
release-optimization core suite pass. With fresh approval on 2026-10-07, COM9's
MAC and active app0 OTA record were checked; the application-only flash passed
hash verification. Boot initialized SD, touch, display and the web server, and
the game-video self-test passed. No SD, NVS or SystemFS data was flashed or
erased. Sonic 2 resumed its automatic save. The new allocation log shows 149,296
bytes free internally and a largest block of 69,620 bytes before the 65,536-byte
request. Both 68000 RAM and Z80 RAM are now internal; VRAM remains in PSRAM, with
75,336 internal bytes free before video-history allocation. The allocation-order
correction therefore worked on this boot.

A complete 45-second gameplay capture measured 40.1–43.3 emulated fps,
20.2–21.7 drawn fps and 58.2–59.0 panel scans/sec. Average core-frame samples
were 21.97–23.80 ms plus 0.82–0.84 ms packing. This does not demonstrate another
speed gain over inkboy7 and does NOT establish full-speed gameplay; the live
scenes differ between tests. No panic, watchdog abort, serial disconnect or
additional reset appeared in `.pio/boot-inkboy8.log` or the full
`.pio/genesis-inkboy8-running.log`. The update-metadata `NoMemory` warning still
appears; no GitHub release or public installer change was made.

### 68000 dispatch and catch-up drawing (inkboy7)

The 68000 execution loop and 114 selected branch/register/move handlers now run
from instruction RAM. A 512-entry, 4 KiB internal-RAM cache holds immutable
opcode handler/cycle pairs, avoiding repeated flash-table reads in loops. It is
keyed by the fetched opcode, never the program address, so self-modifying RAM
code cannot reuse a stale decoded instruction. Nested VDP IRQ instruction
execution retains the original post-handler cycle accounting. The exhaustive
lookup test also exposed the upstream compact tables ending at opcode 0xEFBF;
the import now selects the existing full 65,536-entry tables instead. Those
retain identical entries for the entire old range and provide valid line-F
exception handlers beyond it.

When two simulation frames are due, Genesis omits only the first picture's
rasterization, conversion and packing. Both frames still execute CPU steps,
interrupts, controller input and sound-register timing. Undrawn scanlines retain
the same sprite-table evaluation/overflow bookkeeping as inkboy6. A one-frame
batch always draws; there is no permanent 30 fps cap. GB/GBC and NES keep drawing
every frame. The two-frame limit, real RTOS tick yield, scan-boundary publication
and manual-only screen clearing remain unchanged. Serial profiling now reports
**emulated**, **drawn** and **panel scan** rates separately, with packing time
averaged per emulated frame. Save formats and web files are unchanged.

All three firmware builds passed on 2026-10-07. LILYGO uses 92,836 bytes static
DRAM, 2,536,837 bytes app
flash and 141,279 bytes IRAM text. Symbol checks confirm the loop/selected
handlers in IRAM and the 4 KiB cache in internal DRAM. Host tests compare all
opcode lookups across cache collisions and compare complete saved states from
full drawing versus catch-up drawing (NTSC, PAL and interrupt-enabled fixtures).
They also cover no writes to omitted output, sprite overflow/masking across
undrawn rows, console switching and the unchanged snapshot schema. WASI `-O1`
and `-O3` runs pass; the earlier native `-O0` run passed before the final IRQ
fixture was added. Windows Application Control blocked that latest native
executable; WASI `-O0` exceeded the engine's Z80 function-local limit. No security
settings were changed. Video/DMA/window/timing, scheduler, network integration
and web regression checks pass. With fresh user approval, COM9's MAC and active
app0 metadata were verified; the application-only flash passed hash verification
and the device booted, initialized storage/touch and started its web server.
SD, settings and web files were not flashed. Sonic 2's first steady sample ran at
43.3 emulated fps / 21.7 drawn fps, with 59.8 panel scans/sec, 21.92 ms average
core time and 0.84 ms packing per simulation frame. 68000 time was about 11.8 ms,
Z80 4.8–5.3 ms, VDP 3.6 ms and conversion 0.8 ms. The startup/maintenance sample
was lower (34.7 / 17.6 fps). The unexpected PSRAM 68000 placement is addressed
by the follow-up above. No panic, watchdog abort or additional reset appeared in
the full 55-second boot capture (`.pio/boot-inkboy7.log`), though it only contains
the start of gameplay. A later 35-second non-resetting capture measured
44.7–45.3 emulated fps / 22.3–22.7 drawn fps, with 59.8–59.9 scans/sec and no
panic/reset/watchdog events (`.pio/genesis-inkboy7-running.log`). These are
different live scenes, not a controlled same-state benchmark. No 60 fps claim
or public release is made.

### Network handoff and sampled-row rendering (inkboy6)

This build targets the remaining Genesis rendering cost and the startup race
captured in inkboy5. Before Ink Boy stops the web server or radio, an atomic
admission gate blocks new background network work and waits for current owners
to finish. Startup/Wi-Fi wake workers cancel cooperatively; the HTTPS update
check keeps its lease until its HTTP and TLS objects are destroyed. Its connect
and TLS handshake limits are five seconds (the existing read limit is ten).
The preparation screen explains any wait. RTOS wrappers release their C++
leases before deleting the task. Stale `Paperboy` app-name checks are corrected
to `Ink Boy`. The gate reopens on exit; Book32/Sticky do not use this handoff.

Genesis renders pixel data only for the 120 source rows sampled into its output
image. Discarded rows still run sprite evaluation, including overflow and
masking state needed by subsequent rows. All emulated CPU cycles, scanlines,
interrupts and audio-chip timing remain active. CRAM writes invalidate the gray
palette cache directly, avoiding a palette comparison on every sampled row.
Each converted row is copied to PSRAM in one block; the redundant per-frame
image clear is removed. Manual screen cleaning, the six-pulse display driver,
save formats and the watchdog-friendly scheduler are unchanged.

Verification on 2026-10-07: LILYGO, Book32 and Sticky builds passed. LILYGO static
DRAM is 88,740 bytes and application flash is 2,515,517 bytes. Native `-O0` and
WASI `-O3` console suites passed, including sampled-row/full-render equivalence
with synthetic sprite overflow/masking, palette invalidation, memory edges,
audio-timer status, state round trips and console switching. Network gate and
source integration tests passed, as did scheduler and ROM web regression tests.
With fresh user approval, the COM9 device identity and active app0 OTA record
were verified, then the application-only flash passed hash verification. Boot
mounted both filesystems, initialized touch/display, passed the game-video
self-test and resumed Sonic 2's automatic save. SD, NVS, SystemFS and the public
installer were unchanged; no GitHub release was made.

The initial gameplay interval measured 32.6–33.0 emulated fps after startup;
a further 40-second capture measured 31.9–34.9 fps with the panel at 58.2–59.0
scans/sec. That exceeds the 30 fps target in these samples, but is not full-speed
60 Hz Genesis emulation. The first partial sample included startup/maintenance
and measured 26.8 fps. Later core times were 26.75–29.52 ms plus 1.67–1.69 ms
packing; VDP cost fell to approximately 6.1–6.4 ms. No panic, watchdog abort,
serial disconnect or additional reset appeared in either complete capture
(`.pio/boot-inkboy6.log`, `.pio/genesis-inkboy6-running.log`). This boot did not
reproduce the Wi-Fi shutdown panic, but the update check had finished before Ink
Boy opened, so the overlapping-request hardware test remains outstanding. The
GitHub check returned HTTP 200 but reported `JSON parse error: NoMemory`, a
separate update-metadata parsing issue; it did not prevent gameplay.

### Genesis hot-code placement (inkboy5)

This follow-up places the hot Z80 interpreter, bus entry points and VDP raster
functions in ESP32 instruction RAM rather than competing for the flash cache.
It prioritizes the 64 KiB 68000 work RAM ahead of VRAM, and moves Genesis-only
compact presentation buffers to PSRAM to make room. GB/NES retain the original
internal-first preference when switching back. Allocations remain fallible with
safe fallback; the frontend drops display-borrowed pointers before reallocation.
Aligned 16-bit memory operations use word copies with the same bounds checks
and odd/wrapping fallbacks. No emulated CPU, timer or interrupt is disabled.
Startup reports the actual CPU clock and memory placement. Full-speed gameplay
has not yet been demonstrated on hardware.

Verification on 2026-10-07: the native `-O0` suite and optimized `-O3 -Wasi`
suite passed, including rendering equivalence, memory edges, audio-timer status,
save-state/reopen and console switching. The LILYGO firmware built; symbol
addresses confirmed the selected functions in IRAM (131,659 bytes total IRAM
text versus 78,727 before this step). Static DRAM was 88,724 bytes and app flash
2,512,937 bytes. With fresh user approval, the verified COM9 device and app0 OTA
metadata were checked; the application-only write passed hash verification.
Boot mounted SD, initialized touch/video and resumed the user's Sonic 2 save.
The game reported 240 MHz CPU, internal 68000/Z80 RAM, PSRAM VRAM, and 89,120
bytes free internal heap before starting the video history. After startup,
captured gameplay ran at 25.0–26.5 emulated fps with 55.8–57.5 panel scans/sec.
Stage timings were approximately 15–17 ms 68000, 5–6 ms Z80, 10–11 ms VDP and
1.9 ms conversion, plus about 1.77 ms packing. The later gameplay interval had
no further reset, but the full boot log revealed one earlier `LoadProhibited`
panic while opening Ink Boy during the automatic GitHub HTTPS update check.
The exact ELF decodes it to `esp_wifi_internal_free_rx_buffer` through lwIP,
mbedTLS, `GitHubMgr::checkUpdate` and `AppMainMenu::updateCheckTask`, immediately
after the app disabled Wi-Fi and before ROM loading. The update task is not
joined before radio shutdown. `src/main.cpp` also retains stale `Paperboy`
name checks after the Ink Boy rename. That build leaves the lifecycle race
unresolved and is NOT a clean stability pass or release-ready. The measured game
speed is an improvement, not full-speed Genesis.
No SD/settings/web partition was changed, and nothing was published to GitHub.

Host checks can use `tools/test_console_cores.ps1 -Optimization '-O3' -Wasi`
when Windows application control prevents executing a newly built native test.
That runner has no filesystem/network/device access; its test-only longjmp
stub traps rather than recovering. Native `-O0` checks still cover the ordinary
recovery implementation. Memory tests cover aligned, odd and wrapping accesses.

### Genesis watchdog fix and silent audio (inkboy3)

On 2026-10-07, a live Sonic 2 crash capture showed `IDLE0` starvation, followed
by a task-watchdog abort while `InkBoy` ran on CPU0. It had produced 26.7 fps
(29.20 ms core time + 1.24 ms packing), while the panel scanned at 48.5 Hz.
The scan-synchronized worker always had pending notifications, so its wait
never blocked and the lower-priority idle task could not run. This was not
a ROM-header failure. The worker now blocks for **one actual RTOS tick after
each bounded batch**, outside both mutexes; the fallback path also enforces
at least one tick. The watchdog remains enabled. Frame publication and panel
VSYNC/window boundaries are unchanged.

The user authorized removing unused audio resources. Genesis now skips FM
operator/envelope/LFO sample synthesis, PSG waveform generation, and both PCM
buffers. CPU-visible YM2612 register writes, A/B timers, status/reset flags,
CSM timer operations, PSG register latches and Z80 execution remain active.
This is not a fake constant-ready sound chip or a disabled secondary CPU.
The existing save-state layout is retained. Detune data is initialized on
every open/restart, fixing the old one-time initialization with cleared state.

This eliminates 47,104 bytes (46 KiB) of allocated synthesis tables and 4,240
bytes of sample buffers. The LILYGO build reports 87,200 bytes static RAM and
2,506,957 bytes app flash (down from 91,452 and 2,530,281). The table pointer
symbols remain null; the table memory is not allocated in the silent build.

Host silent tests pass in unoptimized/runtime-check and optimized builds,
including save/reopen and dispatch tests. Timer period/flag checks pass, and
3,000 irregular register writes/reads produce the same status trace
(`a5ccc9f0`) with synthesis enabled and disabled. The synthesis-enabled host
reference is checked at `-O3`: its unoptimized upstream waveform path trips a
signed-negative-shift diagnostic in `chan_calc`, a path compiled out of the
silent firmware. `test/emulator_scheduler.cjs` guards the actual unlocked
one-tick delay and models continuous notification backlog; hardware playback
is still needed to verify the watchdog correction under a real ROM workload.

Application-only installation on COM9 verified the known MAC, active app0 OTA
record and firmware hash, then rebooted successfully. Video self-test passed;
SD, display and touch initialized. SystemFS, NVS and SD were not flashed or
erased. No GitHub release or public installer change was made.

### Earlier inkboy2 installation verification

Verification on 2026-10-07: all three firmware builds and LILYGO SystemFS
passed. Unoptimized and optimized native core/dispatcher tests, actual GFX
renders/hit zones, video-window/PAL-clock checks, and web regression tests
passed. Symbol checks found no emulator entry points in Book32 or Sticky.
LILYGO static RAM is 91,452 bytes and application flash is 2,530,281 bytes.

With user approval, COM9 identified MAC `A4:CB:8F:F0:D9:F0`. Partition and OTA
metadata confirmed app0. Application at `0x10000` and SystemFS at `0x810000`
both passed esptool hash verification, followed by USB reset. Boot passed
video self-tests, initialized the inset display/touch, mounted SD and started
the web server. The live API confirmed `1.3.1-inkboy2`; HTML, CSS and JavaScript
matched local files exactly. Live uploads passed for original generated CGB,
NES and all three Genesis extensions, including header/length/duplicate
rejection. Every generated test ROM was deleted and then returned 404. The
connection dropped before the final whole-inventory comparison, so that last
check is unverified. Real-game NES/Genesis compatibility and speed still need
device testing. No SD, ebook or NVS erase, full backup, GitHub push, release or
public installer change was performed.

### Shared frontend and Game Boy details

- The Ink Boy tile (previously Paperboy) runs the DMG **and CGB** interpreters from
  [CrankBoy commit 1501ba6](https://github.com/CrankBoyHQ/crankboy-app/tree/1501ba6615997df5dd36afa17f1a2fb8813231cc).
- In the device web interface, choose **Game ROMs**, select a legally obtained
  `.gb`, `.gbc`, `.nes`, `.md`, `.gen` or `.bin` file, and Upload. Wait for the saved confirmation. Files go
  into `/roms` on the mounted MicroSD, not the internal book partition.
- Return to the device home screen before uploading: Ink Boy turns Wi-Fi off
  while open. Insert the SD before boot. Up to 256 games appear in the device list.
- Game Boy: up to 8 MB per ROM. GB/GBC files larger than 4 MB use two pinned SD banks to fit
  PSRAM; frequent bank switching can reduce performance. Header checksum,
  declared length, extension and safe filename are checked. No archive extraction.
- Existing names are rejected, never overwritten. Partial uploads use a reserved
  temporary file, removed on failure/disconnection. A storage lease prevents
  entering a game or sleeping halfway through an upload.
- Each web ROM row has a confirmed **Delete** action. It removes only that
  supported ROM file from `/roms`, never SRAM/RTC/backup sidecars. Upload the same
  filename again to reuse its saves. Deletion is blocked during uploads/games;
  names containing paths, NULs, sidecar extensions or directories are rejected.
- ROM sizes use B/KB/MB/GB as appropriate, with exact bytes in the size tooltip.
  A genuinely empty file displays `0 B`; missing/invalid sizes display `Unknown
  size`. The dashboard keeps its existing MB/GB presentation.
- Tap **Pause / saves**, then **Save & exit to ROM list** before rebooting or
  removing the card. Three manual state slots and an automatic resume slot are
  available per game. Auto resume is saved on normal exit, Home and sleep;
  opening the same ROM restores it. Raw cartridge saves
  remain `<rom>.inkdeck.sav`, with `.bak` recovery copies. RTC carts also save
  a versioned `.rtc` sidecar. Invalid saves are not overwritten.

## Display and compatibility

The existing 3x 480x432 portrait viewport, scan-boundary frame synchronization,
fixed six-pulse Contrast waveform, touch controls and cleaning refresh are
retained. Continuous video drives only the game window; menu transitions and
initial control rendering still use a normal screen refresh.

CGB colors are converted to four grayscale shades: the panel is monochrome.
The upstream CGB implementation is development code, not a guarantee that
every cartridge runs correctly or at full speed. MBC1/2/3/5, HuC1/HuC3 and
MBC7 emulation are upstream; unsupported mappers are reported when opening.
There is no game audio output, link cable/IR partner, physical tilt input,
Playdate scripting or rewind. MBC7 tilt is neutral. Save/load, pause/resume and
restart use InkDeck's touch UI; this is not the complete Playdate frontend.

RTC ticks while running. LILYGO requests network time asynchronously when Wi-Fi
starts, allowing RTC catch-up across power-off when both saved/current UTC are
valid. Offline boots without a valid clock cannot infer time spent powered off.

## Port and tests

`tools/import_crankboy.py <checkout>` requires the exact pinned revision and
reproduces vendor changes. `InkDeckPlatform.h` is the separate platform adapter.
See `THIRD_PARTY_NOTICES.md` for retained MIT licenses. No games/BIOS are imported.

Host tests use generated CPU programs/tiles only. The suite covers DMG, dual-mode
and CGB-only detection, four-shade output, scrolling/window rendering, CGB VRAM/
WRAM banks, DMA, double-speed STOP, joypad, SRAM, RTC round-trips, 8 MB ROM banking
and invalid input. Build the native test with `test/crankboy_host` on the include
path, `BOARD_LILYGO_T5S3_PRO`, and the vendored `minigb_apu.c`.

The WASI alternative additionally includes `test/crankboy_host/wasm`, targets
`wasm32-wasi`, and sets linker stack size to 1048576. Execute using
`node tools/run_crankboy_wasm.cjs <test.wasm>`. It has no filesystem preopens or
network/device access; its test-only setjmp shim excludes exception-injection
cases. Native tests exercise allocation and bank-read failure paths separately.

Web checks: `node test/rom_upload_ui.cjs`; firmware checks: `pio run -e
lilygo_t5s3_pro -e seeed_reterminal_sticky -e seeed_xiao_esp32s3`, plus LILYGO
`buildfs`. Synthetic checks are not a substitute for real-game testing.

## Verification on 2026-10-06

- All three target builds and the LILYGO web filesystem build passed.
- Synthetic core tests passed in both unoptimized sanitizer-enabled and
  optimized WASI builds. These runs exclude exception injection. An earlier
  native run passed allocation-failure checks; Windows blocked the expanded
  native executable, so the bank-read exception test remains unverified.
- JavaScript syntax and ROM UI tests passed. Symbol checks found no emulator
  or upload handlers in the Book32/Sticky firmware images.
- The approved COM9 application/web flash verified both image hashes. The
  device booted, passed its video self-test, mounted SD and restored Wi-Fi,
  rotation and sleep settings. No NVS, OTA selection, books or SD erase occurred.
- Live web testing verified the exact new HTML/CSS/JS, a successful synthetic
  `.gbc` upload to `/roms`, invalid name/length/checksum rejection and duplicate
  protection. Only the generated test file was removed; the original ROM list
  was unchanged. Real-game CGB compatibility/performance still needs testing.
- Full original flash backup: ignored local directory
  `.pio/lilygo-crankboy-backup-20261006/flash-before.bin`, SHA-256
  `b4fbc1a0f2d1b43256ca621150b45bded4b313a1a3517d63420f505fbfa629ba`.
  Keep it private: full backups can contain device credentials.

### Follow-up startup fix (installed with crankboy4)

The initial adapter incorrectly treated CrankBoy's `GB_INVALID_READ` and
`GB_INVALID_WRITE` notifications as fatal. Upstream `src/scenes/game_scene.c`
explicitly treats both as nonfatal; unmapped reads return 0xFF and writes are
ignored. `crankboy2` matches this policy, retains fatal-opcode handling, and
adds serial logs for load/runtime failures. Synthetic DMG/CGB programs that
probe FF03 now pass, as does the LILYGO build.

### ROM management correction (crankboy3, installed with crankboy4)

The user's screenshot clarified that Super Mario Land displayed **0 MB in the
web UI**, not a verified zero-byte file. The prior whole-MB rounding displays
every file under 512 KB as 0 MB. `formatFileSize` fixes that separately from the
dashboard formatter. The device API subsequently confirmed 65,536 bytes (64 KB)
for Super Mario Land and 1,048,576 bytes (1 MB) for Pokémon Yellow.
Do not infer corruption from the old rounded label. The user reports Pokémon
Yellow `.gbc` loads and plays on the installed build.

ROM UI regression tests cover small/zero/invalid sizes, exact byte tooltips,
safe filename rendering, confirmation cancellation, URL encoding, concurrent
action guards, and delete/network/timeout errors. The opt-in hardware smoke
test now uses the dedicated ROM delete endpoint instead of the books endpoint
and verifies malformed/sidecar paths are rejected and the original ROM list is
unchanged after removing only its uniquely named generated test file.

### Frontlight button (crankboy4, installed)

The physical S3 function button toggles the frontlight in every app, including
the reader and Paperboy. BOOT keeps its existing Back / hold-to-sleep behavior.
The light starts off at boot/wake and is driven off and held low during sleep.
No screen redraw, brightness menu or persistent setting is needed for toggling.

Wiring follows the [official H752-01 pin map](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/blob/H752-01/docs/pinmap.md):
GPIO11 enables the LED driver; S3 is active-low PCA9535 IO1_2 (address 0x20,
input register 1 bit 2), also verified against the factory `io_extend.c`.
Polling shares the display-owning loop with touch, at most once per 20 ms.
Only the input register is read: FastEPD's cached power outputs are untouched.
30 ms debounce suppresses repeats, startup-held presses and failed-I2C events.
`test/frontlight_button.cpp` covers these cases and millisecond wraparound.

Installation verified both application and SystemFS hashes on COM9. Boot passed
the video self-test, mounted SD, and restored Wi-Fi and sleep/orientation settings.
All three target builds passed, as did the debounce and ROM UI tests. Live tests
confirmed version `1.3.1-crankboy4`, exact web assets, upload validation and the
new delete route; only a generated test ROM was deleted, leaving the two user
ROMs unchanged. Physical light output/button operation still needs user confirmation.

The user clarified this is a development device: future authorized flashes do
not require backups or preserving its contents. No full wipe was needed here.

### Front-bottom touch button correction (crankboy5, installed)

The user clarified the intended button is the **front-bottom capacitive key**,
not the separate S3 mechanical switch. The factory firmware handles it through
GT911's Home callback, using bit 4 (HaveKey) of status register 0x814E:
[official GT911 driver](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/blob/H752-01/lib/SensorLib/src/TouchDrvGT911.hpp).
InkDeck previously kept only the touch-point count and discarded this bit.

The front key now toggles the existing GPIO11 light output globally. Each
acknowledged press toggles once; repeated held packets do not. Only a valid,
ready release packet re-arms it, not empty polls, malformed frames or I2C
failures. A front-key press cancels any pending on-screen tap and never opens
an app or turns a book page. The S3 toggle remains available as well.

`test/lilygo_touch_host/test.cpp` compiles the actual touch driver with a mocked
I2C bus. It covers first-event key presses, holds/releases, idle/error frames,
acknowledgement retry and unchanged normal touch coordinate conversion.

The driver tests and LILYGO build passed. Application-only flash verified its
hash, then boot passed the video self-test and restored SD/Wi-Fi. During the
startup capture, real front-button presses produced both `Frontlight: on (front
touch key)` and `Frontlight: off (front touch key)`. This confirms live key
detection/toggling; visible light output still requires the user's confirmation.
No new backup, full wipe or web-files flash was performed for this correction.

### Save states, manual cleaning and hardware settings (crankboy6)

- **Pause / saves** (or a Back-mapped button) pauses at a complete emulator frame.
  Slots 1–3 have Save/Load controls; replacing/loading a slot requires confirmation.
  Restart also asks for confirmation and retains existing slots/cartridge saves.
- `<rom>.inkdeck.sav.state0` is auto resume, with `.state1`–`.state3` manual slots.
  CPU, DMG/CGB RAM, mapper, palettes, RTC and LCD image use upstream v6 snapshot
  helpers. InkDeck adds exact ROM CRC/length, ABI/version and payload checksum
  validation. These files are **not interchangeable with Playdate save states**.
  A temporary file is read back before replacing a slot; `.bak` handles an
  interrupted rename. Failed loads leave the current game unchanged. Large ROM
  bank reads are staged before applying the snapshot. ROMs without cartridge
  SRAM (such as many early Game Boy games) can still have full save states.
- No 30-second automatic game-window clean. Tap the game image to clean it;
  game entry/resume and UI transitions still establish a clean display.
- An 8-pixel bezel margin on every edge reduces the logical portrait canvas to
  524×944, without scaling fonts or the 480×432 game viewport. Both rotations,
  touch mapping, menus and reader pagination use the inset geometry. Reader
  cache keys already include canvas dimensions, so old page layouts invalidate.
- **Settings → Light & buttons** changes frontlight brightness (10/25/50/75/100%)
  and each button's action: Light on/off, Home, Back or Disabled. The web Settings
  tab provides a 10–100% slider and the same action cards. Settings persist in
  NVS; the light itself always boots/wakes off and is held off during sleep.
  GPIO11 uses 500 Hz PWM per the official PT4103 frontlight specification.
- The side switch is PCA9535 IO1_2, not GPIO48 (EPD clock). The front capacitive
  key remains GT911 HaveKey. No BOOT, reset or power remapping is offered.

Host checks cover DMG/CGB/no-SRAM snapshots across reopening, RAM/CPU/mapper/
palette/LCD restoration, malformed/checksum/wrong-ROM rejection without changing
the running state, and failed staged SD bank reads. Touch tests cover every
logical pixel in both orientations; video self-tests cover the inset, unchanged
outside pixels and queued DMA. Web tests include LILYGO capability gating and
brightness-setting success/error responses. Physical brightness, button mapping,
save/reopen and bezel appearance still need the device/user test.

Verification on 2026-10-07: all three firmware targets and the LILYGO SystemFS
image built successfully. Save-state tests passed in unoptimized/sanitizer and
optimized WASI configurations; video/layout, GT911 and web-script tests passed.
The device was initially disconnected. After the user's subsequent "flash now"
authorization it appeared on COM9 with the known MAC A4:CB:8F:F0:D9:F0. OTA
metadata selected app0. Application (0x10000) and SystemFS (0x810000) writes both
passed esptool hash verification, followed by a USB reset. Boot passed the video
self-test and reported the inset 524×944 canvas. The live API confirmed
`1.3.1-crankboy6`, SD storage and default light/button settings (100%, both Light).
The served HTML/JS/CSS exactly matched local files by SHA-256, and the two
existing ROMs remained listed with their correct sizes. No full wipe, SD/NVS
erase, new firmware backup, GitHub push or release publication was performed.
Real-game save/load and the physical brightness/bezel appearance still require
the user's check. The initial serial capture ended due to the host's console
encoding, not a device crash; subsequent serial and live HTTP checks completed.

### Ink Boy control panel and wake label (inkboy1)

The app/menu/web upload text is now **Ink Boy**. The existing class/source
directory and ROM/save sidecar names remain unchanged, so existing game slots
stay compatible. The CrankBoy core and fixed Contrast video driver are unchanged.

The controls are Game Boy-inspired: solid black D-pad, diagonal black B/A
buttons with letters below, tilted gray Select/Start pills with centered tilted
labels, dark-gray screen trim, an italic Ink Boy wordmark and decorative speaker
slots. Gray uses spatial dithering on the existing 1-bit canvas. This is static
art, not a new grayscale video waveform; the speaker motif does not add audio.
Touch regions share constants with the artwork and retain diagonals/multitouch.
The 3x 480×432 game viewport is unchanged; trim is outside it and only the game
window is driven during play. Cleaning remains manual by tapping the game.

LILYGO's default sleep text is **Press BOOT to wake**, matching the existing
GPIO0/ext0 wake configuration and the manufacturer's BOOT pin map. Sleep schema
3 migrates the exact old default once; custom messages are preserved. Book32
and Sticky retain their existing defaults/schema. The web input placeholder
comes from firmware, not a hardcoded power-button instruction. Hold BOOT for
two seconds to sleep, release it, then press it to wake. No wiring or button
assignments changed, and physical wake still requires a hardware check.

`test/inkboy_ui_host` renders the actual Adafruit_GFX code/fonts and checks
control masks, diagonal combinations, separate hit zones and untouched game
pixels. `tools/render_inkboy_ui.cjs` produces `.pio/inkboy-preview.png` from this
test for visual review. `test/sleep_defaults.cpp` covers all three board
defaults, legacy migration and custom-message preservation. Web regression
tests check the new name and board-specific placeholder.

Verification on 2026-10-07: all three firmware targets and the LILYGO SystemFS
image built successfully. The actual GFX control-panel render was visually
reviewed; UI hit-zone, game-window isolation, sleep-default/migration and web
regression tests passed. With the user's flash authorization, COM9 identified
the known LILYGO MAC A4:CB:8F:F0:D9:F0 and OTA metadata selected app0. Application
(0x10000) and SystemFS (0x810000) writes both passed esptool hash verification.
After USB reset, startup passed the game-video self-test, initialized the
524×944 display and GT911 touch, mounted the SD card, and loaded the ten-minute
sleep setting with `Press BOOT to wake`. The web server started successfully.
No SD, NVS or ebook-partition erase was performed; no GitHub release was pushed.
Wi-Fi stopped before the live HTTP asset check, and the USB port subsequently
became unavailable. Live web-content verification and physical control/wake
testing therefore remain outstanding; the flashed image hashes and successful
boot are confirmed.

# LILYGO 3x/windowed-video: 1.3.1-video11

Local build, not a published release. `video11` makes six-pulse Contrast the
fixed gameplay profile and removes the on-screen mode selector and hitboxes.
The user reported residual ghosting in every comparison mode and chose to keep
one profile. Contrast is retained for its earlier clearer-image result at
essentially the same measured scan rate; this is not a claim of zero ghosting.
Original Book32 and Sticky targets remain unchanged.

## Image and controls

- The 160x144 Game Boy image is scaled 3x to **480x432**, at portrait (30,72).
- Every Game Boy pixel is a 3x3 cell. Zero/one/two/three black horizontal
  stripes represent its four shades; no interpolated resizing is used.
- The D-pad and A/B buttons are below the viewport; Select/Start remain lower.
- The former Smooth / Contrast / Scan end / 4-pass buttons are removed, along
  with their touch actions. The footer only explains the manual refresh gesture.
- Six-pulse Contrast is configured on every game entry, without settled boost
  or an extra scan-tail row. The four-pass path remains an internal safety
  fallback only, not a setting; opening a game retries the normal video path.
- Tap the game image to clean only its window if ghosting accumulates.
- BOOT/Back still saves and exits. Hold BOOT for sleep as before.

## Refresh paths

Contrast tracks a six-pulse transition independently for each pixel,
accepting a new game target between scans. A stationary changed pixel gets all
six drive pulses, then neutral output; it is not marked settled after one pulse.
Opening a game performs a game-window clean/history reset.
This adapts the MIT-licensed MSG approach, using the existing H752-01 FastEPD
transport and power control. It does not raise voltage or pixel clock.

The internal four-pass fallback retains the previous sequential differential
driver, at the larger size. Gameplay and 30-second cleaning refreshes are clipped
to the game rectangle. There is no mode-selector redraw path.
Entering/exiting the app can redraw the whole screen normally.

Physical panel scan clocks still traverse all rows; pixels outside the active
rectangle receive neutral/no-change codes, never black or white drive pulses.
Video is neutralized before saves, full/region refreshes, menu exit and sleep.

The renderer stores only the 8,640-byte packed game window, with one copy of
each triple of identical native rows. Eight Game Boy pixels are converted to
three bytes at once. Video scans read that compact buffer directly; they no
longer expand the image into the screen canvas or copy it into FastEPD's full
framebuffer. Full/clean and four-pass refreshes still expand the same pixels.

The queued video driver prepares the next row while three copies of the current
row are in flight. A video-only completion callback steps CKV/LE before the LCD
driver launches its next queued row, avoiding the repeated empty-queue restart.
Each ping-pong row is reused only after all its queued readers have completed.
Dedicated aligned rows include neutral 16-byte padding; an immutable third row
covers the rest of the panel. `setVideoQueued(false)` retains video4's one-third
processing schedule as a diagnostic fallback, with bounded completion waits.

The transfer clock, row-start timing and voltage are unchanged. Six-pulse Contrast
allows fresh targets on every scan, with the same 540-row transport as video10's
Contrast reference. The neutral-tail experiment did not resolve reported ghosting.
Scan starts are capped at 60 Hz, with no catch-up scan bursts. Removing
software gaps still changes optical drive time. This is not proof of zero ghosting.
Any transfer failure disables panel power and latches a reboot-required fault;
partially advanced pulse history is never used to retry a failed scan.

The emulator core now packs the image immediately after emulation, publishing
one of three compact 8,640-byte buffers through a short pointer-swap lock. Its
raw 23,040-byte shade buffer is private. The display borrows an immutable compact
snapshot directly until queued DMA has drained; it rebinds after every swap,
even when four-pass skips an identical image. No image preparation or frame copy
remains on the display core. Internal RAM is preferred with PSRAM fallback;
app exit clears the display borrow and joins the producer before freeing buffers
and restarting Wi-Fi. Orientation is captured before gameplay starts.
The Peanut-GB core no longer generates palette-identity bits that our frontend
discarded. Four-shade output is copied directly, with LCD accuracy unchanged.
Logs now measure emulator work time and frames exceeding its native time budget,
in addition to paced FPS, to distinguish CPU headroom from panel throughput.
Two four-bit pixel histories share a byte and transitions use 1,024-entry tables;
history storage is 34,560 bytes, preferably in internal RAM, instead of 207,360
bytes of per-pixel PSRAM state. All three physical rows still receive their pulses.
Before video8, emulation was independently paced; serial stats report emulator FPS,
panel scan rate, packed-render time and drive time. A scan rate is not a claim
that every pixel reaches a new full-contrast value at that rate.

## video11 implementation and compatibility

The fixed profile is applied by `LilygoDisplay::setGameMode(true)`, so the app no
longer exposes setters for pulse count, scan-tail or fallback-pass selection.
Existing transport/pulse comparison helpers remain in the low-level library
for regression tests; they are not offered as device or web settings.
Touch hitboxes for the D-pad, A/B, Select/Start, Back/save and manual window clean
are unchanged. No automatic cleanup on stopping has been introduced.

In video11 (before the [CrankBoy migration](lilygo-crankboy.md)), the embedded core was **Peanut-GB**, at the revision then recorded in
`THIRD_PARTY_NOTICES.md`, not the CrankBoy fork. `GameCore.c` includes the local
`peanut_gb.h`, uses the original DMG core and rejects Color-only ROMs. The browser
lists `.gb` files only. Full Game Boy Color support, a CrankBoy migration, audio
and RTC support are outside this display-UI change. No ROM/save format changed.
The ROM browser and startup log explicitly identify the compatibility limit.

Build/flash status: LILYGO build passed (RAM 60,164 bytes; ELF flash 1,711,949
bytes; application binary 1,712,368 bytes). After the user's save/flash approval,
only the application at `0x10000` was written on COM9 / MAC `A4:CB:8F:F0:D9:F0`.
Esptool verified its hash. NVS, OTA metadata, filesystems and SD were untouched.
The explicit reboot passed the startup video self-tests, mounted the SD card
with unchanged reported usage, initialized touch, and entered a game. The log
confirmed fixed Contrast, six pulses, 540 rows, 20 MHz and Peanut-GB / DMG only.

Two subsequent regular five-second gameplay samples measured 59.9 panel scans/s
and 59.7 emulator fps, with 15.54 ms drive time and 1.24 ms packing. Each sample
had 299 fresh targets, one repeated target, no skipped targets and no over-budget
emulation/packing frames. The opening sample included a cleaning refresh and
was slower; it is not a steady-state measurement. No transport fault or reboot
was observed. These counters do not establish zero optical ghosting.

Video11 application SHA-256:
`c4e3b23e68ceba948daa5851b06a249c475b69ccd5fd1df48c3e69490cec003e`.
The video10 rollback binary remains in ignored `.pio` storage.
This build has not been published to GitHub or the browser installer. Prior
comparison results below are historical, not active settings or proof that the
display is ghost-free.

### video8 frame synchronization (retained architecture)

Smooth and Contrast now use a display-driven work notification. Just before a
normal scan, the display latches one complete frame and releases the emulator
to prepare the next. The producer waits without holding either mutex; old
notifications are coalesced and it reads the newest cumulative work target.
The compact triple-buffer ownership and DMA-completion protections are retained.
Frame selection occurs after the scan-rate wait, avoiding an unnecessarily old
snapshot when a newly completed frame arrives near the deadline.

The target clock accumulates the existing native 16,743-us game-frame period,
not simply one emulated frame per panel scan. A slower/jittered scan can request
two emulation steps, preserving game simulation time with sufficient CPU budget.
Each batch is limited to two steps for prompt stop/save; presentation can still
skip or repeat frames when deadlines cannot be met. Full cleaning refreshes and
gaps over 100 ms rebase this clock, pausing instead of generating a large catch-up
burst. Four-pass retains the native free-running timer because it scans too
slowly to provide the emulator's work notifications.

Serial frame-sync counters report complete-frame IDs: fresh, repeated, skipped,
and maintenance refreshes per sample. Identical pixels are not counted as a
repeat unless the same source-frame ID was displayed again. Counter continuity
survives log intervals and 32-bit ID wrap. These counters measure software frame
delivery, not optical ghosting or proof of a tear-free physical panel.
Pulse counts, LUTs, bus clock, voltage, game rectangle and controls are unchanged
from video7. Contrast remains the default following the user's cleaner-image report.

### video10 scan-ending comparison (previous build)

Review of upstream Paperboy commit `e70b2936d2f76c1c864fa442584104c6aaa17311`
found an extra neutral transaction after its visible-row scan. There is no
automatic stop/motion cleanup there; `ui_clear_ghosting()` is a manual action.
The four-pulse source-equation model matched all 400 reachable two-pixel
state/input combinations after translating opposite framebuffer color conventions.
This is a behavioral comparison, not hardware validation.

**Contrast** remains the unchanged six-pulse, 540-row reference. **Scan end**
also uses six pulses, with no settled boost, and adds exactly one all-zero DMA
row after the 540 visible rows. This advances/latches once more and drains the
541st transfer before returning. The tail never reads source pixels or advances
history. The existing neutralization on mode exit uses the selected scan length.
Smooth and four-pass retain their baseline behavior. The Scan end card replaces
video9's Settled card, which the user reported did not materially improve residue;
the old pulse helper/tests remain available for regression coverage.

This deliberately changes only the scan epilogue, not all M5PaperS3 timings:
20 MHz, voltage, LILYGO row-start sequence, active-row queue scheduling, the
230-us inter-scan delay, six-pulse LUT, 60-Hz cap and frame synchronization stay
unchanged. The extra row carries neutral data only; it is not an added screen
clear or another black/white pulse. Controls outside the game window are untouched.
Both modes still do the same manual/30-second clean. Switching modes cleans the
game window and resets history, so compare after playing the same stop/start
sequence, not immediately after the switch (which itself clears old residue).

Regression coverage now runs queued and serial scheduling with/without the
tail under immediate, delayed and mixed DMA completion. It checks padding,
clipping, buffer lifetime, source/history advancement, final-tail drain, and
failures at every send/wait including the tail. A boundary case puts active
pixels on the last visible row to catch a dropped row or duplicated drive.

Build/flash status: LILYGO build passed (RAM 60,164 bytes; ELF flash 1,713,529
bytes; application binary 1,713,952 bytes). With the user's save/flash approval,
the application was written only at `0x10000` on COM9 / MAC `A4:CB:8F:F0:D9:F0`;
esptool verified its hash. NVS, OTA metadata, filesystems and SD were untouched.
One explicit reboot confirmed the expanded startup self-test **PASS**, normal
SD/touch initialization, and entry into Contrast. The user selected Scan end;
serial output confirmed six pulses with the neutral tail enabled (541 rows).

Passive Scan end samples measured 59.0-59.3 regular scans/s, 59.6-59.9 emulator
fps, 15.86-16.07 ms drive time and 1.24-1.25 ms packing. No individual emulation
plus packing frame exceeded its budget. Samples including a window clean were
slower (45.0-49.1 scans/s). No transport failure or reboot was observed; these
measurements do not establish an optical improvement.

Optical benefit is unverified; this is not yet a production fix. No automatic
cleanup-on-stop has been added. Other hardware targets and the public installer
are unchanged. The video9 rollback binary is retained in ignored `.pio` storage.

Video10 application SHA-256:
`f35b50279b84060001288f1273007143bf0aa82594b0e998a14d403adfd5a28a`.

### video9 settled-pixel comparison (previous build)

The user reported ghosting mainly after stopping and resuming movement, with a
manual game-window clean removing it completely. This supports investigating
pixel-transition history, but does not establish the exact physical cause.

Settled keeps Contrast's six-pulse budget for reversals interrupted mid-transition.
Only reversals whose previous counter has reached zero receive eight pulses,
symmetrically for black-to-white and white-to-black. A pixel that stays unchanged
and has finished its pulses still receives neutral output indefinitely. Here
"settled" means the software pulse counter completed: it is not a physical sensor
reading, an idle-age measurement, or a claim of calibrated panel behavior.

Eight pulses fit the existing three-bit remaining counter (seven after the first
pulse) and color bit. The new profile adds one 2 KiB lookup table, without growing
the 34,560-byte history or adding per-pixel branches during scanning. Normal
Contrast/Smooth tables remain unchanged. Profile changes neutralize pending drive,
clean the game rectangle and reset history before the new profile starts.

Synchronization, scan-rate cap, DMA transport, voltage, bus clock, periodic cleaning
and viewport geometry are unchanged. Contrast remains the default; tap Settled to
try the experiment briefly in the same stop/start scene and return to Contrast if
it looks worse. Extra pulse duration does not block accepting new targets each scan.
It may improve or worsen residue and is not a validated production waveform.

Tests cover exact eight-pulse settled reversals in both directions, six-pulse
interrupted reversals at every intermediate point, post-completion reversals,
100 unchanged neutral scans, reset initialization, and all 1,024 paired-state/input
combinations. Scalar/packed, clipping, asynchronous DMA lifetime and injected-abort
tests run for all three profiles. No test drives the physical display.

## Validation

- Compile-time tests: four/six-pulse state transitions, settling and reversals.
- Boot scratch-buffer tests: every pixel in both rotations, all four shades,
  untouched pixels outside the viewport, and neutral output outside the DMA
  row's clip rectangle. The packed LUT is compared against the scalar reference
  for all 1,024 two-pixel state/input combinations for each pulse count, including
  the span producer.
  A simulated asynchronous DMA checks the exact pipeline schedule against the
  scalar reference: row order/count, repeated rows, neutral borders and padding,
  history equivalence, resets, and no writes to an in-flight buffer. These tests
  neither drive the display nor modify files.
- The queued producer is additionally tested under immediate, delayed and mixed
  completion schedules, with injected failures at every send position/wait goal.
  Pending pointers must remain byte-identical until completion, including padding.
- Hardware validation still requires observing moving and stopped scenes with
  fixed Contrast. Check ghosting, touch controls, 30-second window-only cleaning,
  return to menu and sleep. Prior mode comparisons are recorded below.

Prior hardware baseline: contrast1 emulator approximately 59.6–59.8 fps,
panel approximately 6.1–7.3 updates/s in four-pass mode (cleaning affects averages).
The first 3x smooth prototype (`video2`) measured approximately 12–13 scans/s;
its renderer took 7–8 ms and its unoptimized drive path 71–75 ms per scan.

USB test on 2026-10-06: `video3` application-only flash completed with hash
verification, followed by a successful boot, SD mount and PASS from the expanded
rendering/pulse/clipping/LUT self-test. Four consecutive five-second game samples
reported 29.8–29.9 panel scans/s and 59.7–59.9 emulator fps. Packed rendering took
5.73–6.07 ms and drive time 26.43–26.81 ms per regular scan. These are instrumented
timings, not a measurement of optical settling or confirmation of visual quality;
contrast and ghosting still need comparison on the physical display.

`video4` application-only flash also verified its hash and booted with a PASS
from the expanded tests, including compact packing and simulated DMA lifetime.
After the opening/cleaning sample, three consecutive five-second Smooth samples
measured 40.0–40.2 panel scans/s, 59.6–59.8 emulator fps, 1.39 ms frame preparation
and 22.48–22.56 ms display time. This is about one-third more scans than `video3`,
not 60 Hz. The user had confirmed `video3` Smooth looked better; `video4` contrast
and ghosting still require their visual comparison.

Local `video4` application SHA-256:
`d2ebeed24e7b5e9023a415eab76e4ac09bf89c570c7e805e58fa5f9a9f14c473`.
The previous `video3` application is retained at `.pio/lilygo-video3-backup.bin`
for rollback; neither this test build nor private device snapshots are published.

`video5` verified its application hash, booted and passed the queued/synchronous
producer tests. Smooth measured 54.4–55.6 scans/s in regular samples, with
15.73–15.96 ms display time and 1.35–1.42 ms preparation still on the display core.
Emulation remained 59.6–59.9 fps, measured 8.77–10.91 ms CPU work and zero
over-budget frames in these samples. Periodic cleaning lowered one five-second
average to 45.5 scans/s. `video6` moves image packing into the spare emulator-core
budget; its subsequent measurements are recorded below.

`video6` subsequently verified its application hash and passed boot tests. Regular
five-second samples measured 59.2–60.0 panel scans/s and 59.6–60.0 emulator fps,
with ~0.03 ms display handoff and 15.42–15.92 ms drive time. Emulator work plus
packing remained within budget in the captured samples. A periodic clean lowered
one sample to 49.3 scans/s, so continuous long-window averages are lower. The user
reported residual ghosting despite the higher rate. `video7` therefore adds the
six-pulse comparison, rather than claiming rate improvements alone solve optics.
At 60 Hz, four fields span about 67 ms and six span about 100 ms; these are drive
intervals, not a promise of measured optical response or zero ghosting.

`video7` built successfully and was flashed on 2026-10-06 with explicit user
approval after saving. Only the application at `0x10000` was written; esptool
verified the flash hash. The device rebooted and passed the expanded four/six-pulse,
clipping, packing and queued-DMA scratch tests. Touch initialized and the SD card
mounted with the same reported usage (43,450,368 of 7,979,663,360 bytes). The prior
`video6` application remains in `.pio/lilygo-video6-backup.bin` for rollback.
In a later passive USB sample, Contrast measured 58.5–59.1 regular scans/s and
59.7–59.9 emulator fps (one cleaning sample: 49.3 scans/s). The user reported less
ghosting in Contrast. Code review found immutable scan buffers but independent
emulator/display timers, motivating the video8 synchronization experiment.

Local `video7` application SHA-256:
`4b5c21d18745a88a97c9bc7fd8f854ac094700edfefbbd31960e2d219ee3117f`.

`video8` adds startup pacing tests for long 60 Hz runs, jittered boundaries,
catch-up, maintenance pauses, frame-ID rollover and presentation counter
continuity. The prior video7 application is retained at
`.pio/lilygo-video7-backup.bin`. Optical comparison remains pending; no claim of
reduced ghosting or smoother motion is made from timing counters alone.

The video8 build passed and its 1,708,672-byte application was flashed at
`0x10000` after explicit save/flash confirmation. Flash hash verification and
the expanded startup self-tests passed, including frame timing. SD storage
mounted with the same reported usage. Current application SHA-256:
`3c27e04e9d2d5275033f986cbf2a8e2b84d4fe666900c063b1767d6749728a13`.

Six regular five-second Contrast samples measured 59.0–59.5 scans/s and
59.6–59.9 emulator fps, with 15.74–15.91 ms drive time and 0.02 ms handoff.
Each had zero repeated targets, 296–298 fresh targets and 1–3 skipped source
frames; no measured individual core+packing frame exceeded its native budget.
Two samples containing a cleaning refresh measured 49.3/49.6 scans/s and
1/2 repeated targets respectively. Emulation pauses during those maintenance
refreshes by design. The opening sample likewise includes initial drawing time
and is not a steady-state speed measurement. No transport fault or reboot was
seen in the capture; mode-switch/save/reopen and optical checks still need user
verification.

The user's subsequent optical report was that motion might be a little smoother,
but ghosting remained especially after stopping and starting. Tapping the game
image cleared it perfectly. This is the comparison case for video9; video8 remains
available for rollback in `.pio/lilygo-video8-backup.bin` with the hash above.
Video9 built successfully (60,156 bytes static RAM; 1,710,592-byte application).
After explicit save/flash confirmation, only the application at `0x10000` was
written. The flash hash verified, followed by a successful reboot, startup
tests for all three pulse profiles, and the same reported SD usage. The user
selected Settled and the serial log confirmed the six/eight-pulse profile.
Optical improvement remains unconfirmed pending the same-scene stop/start test.

The passive Settled capture measured 58.5–59.9 regular scans/s and 59.7–59.8
emulator fps, with 15.94–16.12 ms drive time and 0.02 ms handoff. Regular samples
had 0–2 repeated and 0–7 skipped source frames; no individual core+packing frame
was over budget. A sample including cleaning fell to 50.8 scans/s. No reboot or
transport error was observed. These are throughput results, not optical validation.

Video9 application SHA-256:
`e98ccb9bf3b07e826ad3a56b8050008c3355f884c5c62b98480f92c1e03d984c`.

## Source review for video5/video6

- [Wenting Zhang's full project write-up](https://www.hackster.io/wenting-zhang/60fps-eink-gameboy-emulator-on-m5papers3-57e4e5)
  describes per-pixel timing, compact SRAM buffers, parallel DMA and separate cores.
  Its six-fields-at-60-Hz explanation and today's four-field MSG implementation
  are different snapshots; neither proves a universal waveform for this panel.
- [Current MSG](https://gitlab.com/zephray/paperboy/-/blob/main/main/msg/msg.c)
  uses four pulses. The transport adaptation keeps LILYGO's power mapping.
- [CrankBoy](https://github.com/CrankBoyHQ/crankboy-app) is a modified Peanut-GB
  fork, not the core currently embedded here. A wholesale Playdate-specific port
  would change timing/state handling and is not justified by our capped FPS alone.
  No CrankBoy implementation was copied for the palette-tag optimization.
- ESP-IDF 4.4.7's I80 ISR invokes the completion callback before selecting the
  next queued transaction. The bounded producer relies on that pinned driver
  behavior; this path must be reviewed before changing the framework version.

# InkDeck 1.2.14

- The Sticky home screen now shows InkDeck and the firmware version in the top-left corner, matching Book32.
- Fixed the Sticky partial-refresh buffer bug that caused faint text and ghosting in the library and book pages. The default full refresh interval remains 15 pages.
- Includes the Sticky sleep improvements from 1.2.13: a default 10-minute idle timeout, hold power for two seconds to sleep, and press power to wake.

Install using https://rolohaun.github.io/Book32/ and select your hardware.

For device updates, use firmware.bin and littlefs.bin for Book32, or book32-sticky-firmware.bin and book32-sticky-littlefs.bin for Sticky. These application images are different from the merged factory images.

Both firmware targets and web filesystem images were built successfully. The partial-refresh correction was previously flashed to a Sticky and startup was verified; the new header still needs a physical-device visual check.

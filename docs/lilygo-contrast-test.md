# LILYGO local contrast test: 1.3.1-contrast1

This is a USB test build, not a published installer release.

- Game refresh starts at four passes instead of one.
- Below the native 160x144 game image, tap **1 pass**, **2 passes**, or
  **4 passes** to compare speed and contrast. The selected card has a double border.
- A full cleaning refresh occurs on the first game frame and whenever the mode
  changes. The existing 30-second cleaning interval remains unchanged.
- The choice lasts for this boot only; no reader settings or SD files are changed
  by the contrast controls. Exiting the game restores four-pass normal UI refresh.
- Serial statistics include the pass count, emulator FPS and panel updates/second.

Compare the same moving game scene for several seconds in each mode, not just
the static frame immediately after a cleaning refresh. Four passes are the
contrast baseline; two may offer a better speed/contrast balance. One reproduces
the previous fast mode for comparison. Gray Game Boy shades are still dithered;
solid black pixels should be compared against the surrounding frame and controls.

The test changes neither panel voltages nor the manufacturer's drive timing.
Actual contrast and refresh-rate improvements require observation on the device.

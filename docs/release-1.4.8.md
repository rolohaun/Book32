# InkDeck 1.4.8

Released November 26, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/), or update from the
device.

## Fixes

- **Reboots when using the web page.** Opening or refreshing the device's web
  page, or changing a setting, could make InkDeck restart, sometimes over and
  over. It was most likely on devices set up with only the eReader, where the
  bookshelf is home and the processor slows down to save battery. Loading the
  page opens many connections at once, and the web server's network library
  could deadlock on them until the watchdog restarted the device. The library
  is now patched so this cannot happen.
- **Reading pauses while the web page is open.** The page's status refresh,
  every five seconds, measured free space by walking every book file, which
  froze the device for over a second each time. Free space is now measured
  when InkDeck starts and after books are uploaded or deleted.
- **Restart during the update check.** The automatic check for a new version
  could restart the device when it ran while the processor was slowed down.
  The check now runs at full speed, and "Check for updates" on the web page no
  longer stalls the rest of the page while it waits for GitHub.

## All three devices

Firmware and web-interface images are rebuilt as 1.4.8 for each board. Pinouts
and partition layouts are unchanged.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices. AsyncTCP is now shipped with InkDeck's patch; see
THIRD_PARTY_NOTICES.md.

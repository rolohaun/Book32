# InkDeck 1.4.5

Released October 24, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## New: previous page and home buttons on the Book32

The Book32 now uses the TRMNL 7.5" kit's other two side buttons:

- **KEY1** (GPIO 2) turns back a page, or moves up in menus and the library.
- **KEY2** (GPIO 3) returns to the InkDeck main menu from anywhere.
- **KEY3** (GPIO 5) works as before: next item or page, hold to select.

## All three devices

Firmware and web-interface images are rebuilt as 1.4.5 for each board. The
Sticky and LILYGO builds have no other changes. Pinouts (apart from the two
new Book32 buttons) and partition layouts are unchanged.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

## Verification

All three firmware builds and the repository's tests pass. The new buttons
were tried by hand on a Book32.

# InkDeck 1.4.7

Released October 29, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Choose your apps when installing

The browser flasher has a new step: pick the apps you want beside the eReader
(Todo, Klipper and, on the Sticky and LILYGO, Inkade). The eReader is always
installed. With no apps ticked, InkDeck is a pure eReader that opens straight
to your bookshelf; with any app ticked, it opens to the app menu. Change the
choice later under Settings → Apps on the device's web page (InkDeck restarts
to apply it). Devices updated over the air keep every app, as before.

## A bookshelf library

The library is now a bookcase: covers stand on wooden shelves, nine to a page,
with each title just above its book. The title reads "My Bookshelf", or your
name ("Ron's Bookshelf") once you enter it under Settings → Your bookshelf on
the web page.

When the bookshelf is home (no apps installed), a Settings book ends every
page on the touch-screen devices, and on the Book32 an Update book appears when
a new version is ready.

## A new home footer

The main menu and the bookshelf share a footer: InkDeck's version over a
compact battery gauge on the left, and the web page's address under
"Dashboard" on the right. The main menu drops its title and touch hint, so the
apps sit higher on the screen.

## Wi-Fi from the web page

The web page's Settings tab has a Wi-Fi card: scan for networks and join one.
This helps when you reach InkDeck through its own InkDeck hotspot, after the
first-time setup page has closed.

## Inkade

- Bluetooth LE keyboards work as controllers, with media-key remotes supported.
- Keyboard controls can be rebound.
- Arduboy games are built in, starting with Ultra Hop.

## Fixes

- Opening an app right after Wi-Fi setup could crash InkDeck when the web
  server restarted; port 80 is now freed first.
- LILYGO: sleep used more battery than on the Sticky. The touch controller and
  LoRa radio are now put to sleep, their reset lines are held, and the
  charger's ADC is stopped before deep sleep.
- Sleep screen: "Book cover only" is a third choice beside cover with message
  and message only.

## All three devices

Firmware and web-interface images are rebuilt as 1.4.7 for each board. Pinouts
and partition layouts are unchanged.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

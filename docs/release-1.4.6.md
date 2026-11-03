# InkDeck 1.4.6

Released October 26, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Fixed: umlauts and accented letters in books

With the Open Sans reading font, letters outside basic ASCII were dropped, so
German text such as "äöüÄÖÜß" or the „low quote" disappeared from the page.
Open Sans now covers Western European text: all Latin-1 letters (umlauts,
accents, ß, ñ, ç, ø and more) plus „ ‚ « » € Œ œ Š š Ž ž and similar
punctuation. Books that write these letters as HTML entities (`&auml;`,
`&szlig;`, `&bdquo;` …) are decoded too. Line wrapping now measures these
letters at their real width.

Saved page positions are rebuilt once after the update, since line widths
changed.

## Open Sans is the default reading font

New installs and devices without a saved font choice read in Open Sans. If you
picked a font before, your choice is kept; switch it under Ereader → Reader
appearance in the web interface. The Native (FreeSans) font still shows only
basic ASCII letters.

## All three devices

Firmware and web-interface images are rebuilt as 1.4.6 for each board. Pinouts
and partition layouts are unchanged.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

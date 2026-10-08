# InkDeck licensing

Copyright (c) 2026 Rolohaun and InkDeck contributors.

InkDeck's original project code and integration changes are free software:
you may redistribute and/or modify them under the GNU Affero General Public
License, version 3 or (at your option) any later version.

InkDeck is distributed without any warranty, including the implied warranties
of merchantability or fitness for a particular purpose. The full AGPLv3 text
is provided in [COPYING](COPYING).

Third-party files retain their own copyright notices and licenses. This
grant does not replace those terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and the notices shipped with each dependency. The combined LILYGO firmware
includes AGPLv3-or-later ClownMDEmu and is distributed under those copyleft
requirements. Commercial use is not excluded by AGPL, but source-sharing,
notice and other license obligations still apply.

## Corresponding source

Release binaries are accompanied by the complete tagged InkDeck source,
vendored emulator sources and local patches, build scripts and dependency
specifications. The release also includes a source archive with the exact
PlatformIO libraries and Arduino core/library sources used for the build.
The pinned Espressif toolchain and SDK are installed by PlatformIO. Users of the
device web interface can access the source from its visible source link.

For 1.3.1: https://github.com/rolohaun/Book32/tree/v1.3.1

Build commands, prerequisites, partition layout and installation steps are
in README.md and platformio.ini. No signing key, activation service or locked
bootloader is required to install modified InkDeck firmware. Game ROMs are
not included and must be obtained separately with appropriate permission.

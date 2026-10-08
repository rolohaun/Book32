"""Import the pinned Retro-Go NES core, preserving upstream notices.

Run with the path to a Retro-Go checkout. Generated changes: LILYGO-only guard,
relative includes, InkDeck allocator/CRC glue, bounded memory state I/O, and
always-present RAM blocks (zero-filled RAM must also restore correctly).
"""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
upstream = Path(sys.argv[1]).resolve()
commit = '4ced120669750ca7228fd0414211430c1d923166'
assert subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip() == commit
source = upstream / 'retro-core/components/nofrendo'
target = root / 'lib/InkNes'
files = [p for p in source.rglob('*') if p.is_file() and 'docs' not in p.parts and
         (p.suffix in ('.c', '.h') or p.name in ('COPYING', 'CREDITS'))]
by_name = {p.name: p for p in files}
for path in files:
    text = path.read_text(encoding='utf-8')
    if path.suffix in ('.c', '.h'):
        def relative(match):
            name = match.group(1)
            found = by_name.get(name)
            if not found or (path.parent / name).exists():
                return match.group(0)
            import os
            return '#include "' + os.path.relpath(found, path.parent).replace('\\', '/') + '"'
        text = re.sub(r'#include "([^"/]+)"', relative, text)
        text = text.replace('"config.h"','"InkNesConfig.h"').replace('"../config.h"','"../InkNesConfig.h"')
    if path.name == 'utils.h':
        text = text.replace('#ifdef RETRO_GO', '#include "../InkNesPort.h"\n#if 0')
        text = text.replace('#define IRAM_ATTR', '#ifndef IRAM_ATTR\n#define IRAM_ATTR\n#endif')
        text = text.replace('#define CRC32(a, b, c) (0)', '#define CRC32(a, b, c) inkNesCrc(a, b, c)')
        text = text.replace('#define LOG_PRINTF(level, x...) printf(x)', '#define LOG_PRINTF(level, x...) ((void)0)')
    if path.name == 'state.c':
        text = text.replace('#include "nes.h"', '#include "nes.h"\n#include "../InkNesStateIO.h"')
        text = text.replace('if (memory_zone_dirty(machine->cart->chr_ram, 0x2000 * machine->cart->chr_ram_banks))', 'if (machine->cart->chr_ram_banks > 0)')
        text = text.replace('if (memory_zone_dirty(machine->cart->prg_ram, 0x2000 * machine->cart->prg_ram_banks))', 'if (machine->cart->prg_ram_banks > 0)')
        text = text.replace('uint8 buffer[600];', 'uint8 buffer[600] = {0};')
        text = re.sub(r'\*\(\(uint16\*\)&buffer\[([^\]]+)\]\)',r'inkNesRead16(&buffer[\1])',text)
        text = re.sub(r'\*\(\(uint32\*\)&buffer\[([^\]]+)\]\)',r'inkNesRead32(&buffer[\1])',text)
        text = re.sub(r'\(\(uint16\*\)buffer\)\[([^\]]+)\]',r'inkNesRead16(buffer+2*(\1))',text)
    if path.name == 'cpu.c':
        text = text.replace('(*(uint16 *)((page) + (addr)))', 'inkNesRead16((page) + (addr))')
    if path.name == 'ppu.c':
        text = text.replace('static ppu_t ppu;', '''static ppu_t ppu;
#ifndef ESP_PLATFORM
bool inkNesStageRows = true;
#endif''')
        assert text.count('void ppu_renderline(uint8 *bmp, int scanline, bool draw_flag)') == 1
        text = text.replace('void ppu_renderline(uint8 *bmp, int scanline, bool draw_flag)',
                            'IRAM_ATTR void ppu_renderline(uint8 *bmp, int scanline, bool draw_flag)')
        assert text.count('uint8 *vidbuf = NES_SCREEN_GETPTR(bmp, 0, scanline);') == 1
        text = text.replace('uint8 *vidbuf = NES_SCREEN_GETPTR(bmp, 0, scanline);', '''uint8 *original = NES_SCREEN_GETPTR(bmp, 0, scanline);
      uint32 row[(NES_SCREEN_PITCH + 3) / 4];
      bool staged = draw_flag;
#ifndef ESP_PLATFORM
      staged = staged && inkNesStageRows;
#endif
      if (staged) memcpy(row, original - NES_SCREEN_OVERDRAW, NES_SCREEN_PITCH);
      uint8 *vidbuf = staged ? (uint8 *)row + NES_SCREEN_OVERDRAW : original;''')
        anchor = 'ppu_renderoam(vidbuf, scanline, draw_flag && OPT(PPU_DRAW_SPRITES));'
        assert text.count(anchor) == 1
        text = text.replace(anchor, anchor + '''
      if (staged) memcpy(original - NES_SCREEN_OVERDRAW, row, NES_SCREEN_PITCH);''')
    if path.suffix == '.c':
        text = '#if defined(BOARD_LILYGO_T5S3_PRO)\n' + text + '\n#endif\n'
    if path.suffix in ('.c', '.h'):
        text = '/* Modified for InkDeck, 2026-10-08; see THIRD_PARTY_NOTICES.md and tools/import_nofrendo.py. */\n' + text
    dest = target / ('InkNesConfig.h' if path.name=='config.h' else path.relative_to(source))
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(text, encoding='utf-8', newline='\n')
print('Imported Nofrendo from Retro-Go', commit)

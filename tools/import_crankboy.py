"""Reproducible, mechanical platform adaptation of pinned CrankBoy sources.

Run with the path to a checkout of the revision below. No ROMs are imported.
CPU/PPU algorithms remain upstream; InkDeck supplies storage, UI and timing.
"""
from pathlib import Path
import subprocess
import sys
import re

REV = '1501ba6615997df5dd36afa17f1a2fb8813231cc'
source = Path(sys.argv[1]).resolve()
assert subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() == REV
dest = Path(__file__).resolve().parents[1] / 'lib/Apps/AppPaperboy/crankboy'

def replace(s, old, new):
    assert old in s, old
    return s.replace(old, new)

def cut(s, start, end, replacement=''):
    a = s.index(start)
    b = s.index(end, a)
    return s[:a] + replacement + s[b:]

files = ['peanut_gb.h', 'peanut_gb_core.h', 'pgb/pgb_common.h', 'pgb/pgb_v6.h',
         'pgb/pgb_version.h', 'minigb_apu/minigb_apu.h', 'minigb_apu/minigb_apu.c',
         'minigb_apu/LICENSE']
for name in files:
    s = (source / 'libs' / name).read_text(encoding='utf-8')
    if name == 'peanut_gb.h':
        s = replace(s, '#include "../src/app.h"', '#include "InkDeckPlatform.h"')
        s = replace(s, '#ifdef PGB_IMPL\n#define PGB_SAVESTATE_UPGRADE_IMPL\n#endif',
                    '// InkDeck wraps the v6 snapshot helpers in its own checked state format.')
        s = cut(s, '// relocatable and tightly-packed interpreter code', '// Offset of the relocated draw cluster',
                '// ESP32 executes the portable C core; no ARM TCM relocation.\n' +
                '\n'.join('#define ' + n for n in ['__core_dmg', '__core_dmg_section(x)',
                    '__core_cgb', '__core_cgb_section(x)', '__draw_dmg', '__draw_cgb',
                    '__rare_dmg', '__rare_cgb', '__hle_cgb', '__rare_shell']) + '\n\n')
        s = cut(s, '// Note: this function can be called on unswizzled structs;',
                '/**\n * Gets the size of the save file required for the ROM.')
        s = cut(s, 'typedef typeof(playdate->graphics->markUpdatedRows)', '#endif  // PGB_IMPL')
        # These are host-only extension calls, not Game Boy hardware.
        s = replace(s, 'playdate->system->logToConsole', 'inkCrankLog')
        s = replace(s, 'playdate->system->setPeripheralsEnabled', 'inkCrankPeripherals')
        s = replace(s, 'playdate->system->getAccelerometer', 'inkCrankAccelerometer')
        # Correct upstream's muted APU mask indexing (0xFF10, not 0xFF00).
        s = replace(s, 'ortab[addr - IO_ADDR]', 'ortab[addr - 0xFF10]')
        # Keep the normal in-memory path; large ROMs use two pinned SD banks.
        s = replace(s, 'gb->rom_bank_base[1][i] = gb->gb_rom + offset;',
                    'gb->rom_bank_base[1][i] = inkCrankRomBank(gb->gb_rom, effective_bank & gb->num_rom_banks_mask, 1) - ROM_BANK_SIZE;')
        s = replace(s, 'gb->gb_rom + (gb->zero_bank_base & ((gb->num_rom_banks_mask + 1) * ROM_BANK_SIZE - 1));',
                    'inkCrankRomBank(gb->gb_rom, (gb->zero_bank_base / ROM_BANK_SIZE) & gb->num_rom_banks_mask, 0);')
        s = replace(s, 'if (gb->mbc != 1 || gb->gb_rom_size < 0x80000)',
                    'if (inkCrankIsPaged() || gb->mbc != 1 || gb->gb_rom_size < 0x80000)')
        s = replace(s, 'void __gb_hle_scan_rom(gb_s* gb)\n{',
                    'void __gb_hle_scan_rom(gb_s* gb)\n{\n    if (inkCrankIsPaged()) return;')
        s = replace(s, 'int set_hw_breakpoint(gb_s* gb, uint32_t rom_addr)\n{',
                    'int set_hw_breakpoint(gb_s* gb, uint32_t rom_addr)\n{\n    if (inkCrankIsPaged()) return -2;')
        # Small SRAM must mirror within its allocation; RTC-only carts have none.
        marker = '__section__(".text.cb") static void __gb_update_selected_cart_bank_addr(gb_s* gb)'
        helpers = '''static uint8_t inkCartRead(gb_s* gb, size_t index) {
    if (gb->gb_cart_ram_size == 2048) index &= 2047;
    return index < gb->gb_cart_ram_size ? gb->gb_cart_ram[index] : 0xff;
}
static void inkCartWrite(gb_s* gb, size_t index, uint8_t value) {
    if (gb->gb_cart_ram_size == 2048) index &= 2047;
    if (index >= gb->gb_cart_ram_size) return;
    gb->direct.sram_updated |= gb->gb_cart_ram[index] != value;
    gb->gb_cart_ram[index] = value;
}

'''
        s = replace(s, marker, helpers + marker)
        s = replace(s, 'if (gb->enable_cart_ram && gb->num_ram_banks > 0)',
                    'if (gb->enable_cart_ram && gb->num_ram_banks > 0 && gb->gb_cart_ram_size >= CRAM_BANK_SIZE)')
        s, count = re.subn(r'return gb\s*->gb_cart_ram\s*\[([^\]]+)\];', r'return inkCartRead(gb, \1);', s)
        assert count == 4, count
        s, count = re.subn(r'CB_ASSERT\(idx < gb->gb_cart_ram_size\);\s*const u8 prev = gb->gb_cart_ram\[idx\];\s*gb->gb_cart_ram\[idx\] = val;\s*gb->direct.sram_updated \|= prev != val;',
                          'inkCartWrite(gb, idx, val);', s)
        assert count == 4, count
    elif name == 'peanut_gb_core.h':
        s = replace(s, '#include "../src/app.h"  // IWYU pragma: keep\n#include "../src/preferences.h"\n#include "../src/utility.h"',
                    '#include "InkDeckPlatform.h"')
        # ARM permits unaligned native loads; Xtensa needs byte-safe accesses.
        s = replace(s, 'return *(uint16_t*)ptr;', 'return inkRead16(ptr);')
        s = replace(s, 'return *(uint32_t*)ptr;', 'return inkRead32(ptr);')
        s = replace(s, '*(uint16_t*)ptr = v;', 'inkWrite16(ptr, v);')
        s = replace(s, 'return *(uint16_t*)rom_ptr;', 'return inkRead16(rom_ptr);')
        # Each DMG tile writes two bitplane bytes. A word at odd tile offsets
        # is unaligned and the final tile also crosses the scanline buffer.
        s = replace(s, '*(uint32_t*)&out[0] &= combined_mask;\n            *(uint32_t*)&out[0] |= combined_planes;',
                    'out[0] = (out[0] & bgmask) | raw1;\n            out[2] = (out[2] & bgmask) | raw2;')
    elif name == 'minigb_apu/minigb_apu.h':
        s = replace(s, '#include "../../src/app.h"', '#include "../InkDeckPlatform.h"')
        s = cut(s, '/* TCM clusters', '// master audio control', '#define __apu_write\n#define __apu_sample_gen\n\n')
    elif name == 'minigb_apu/minigb_apu.c':
        s = replace(s, '#include "../../src/app.h"\n#include "../../src/dtcm.h"\n#include "../../src/preferences.h"\n#include "../../src/scenes/game_scene.h"',
                    '#include "../InkDeckPlatform.h"')
        s = cut(s, '__apu_sample_gen static int audio_callback_render(', 'void audio_update_square(',
                '// Playdate audio callback omitted: InkDeck has no game-audio output.\n')
        s = '#if defined(BOARD_LILYGO_T5S3_PRO)\n' + s + '\n#endif\n'
    out = dest / name
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(s, encoding='utf-8', newline='\n')
(dest / 'LICENSE').write_text((source / 'LICENSE').read_text(encoding='utf-8'), encoding='utf-8', newline='\n')
print('Imported CrankBoy', REV)

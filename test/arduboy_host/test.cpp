// Host test for Inkade's Arduboy layer: runs the built-in Ultra Hop through
// the console API (inkArduboy*) the way Inkade does, one loop() per frame.
// Build (from the repo root):
//   zig c++ -std=c++17 -DBOARD_WAVESHARE_RLCD42 -Itest/arduboy_host/stubs
//     -I".pio/libdeps/waveshare_rlcd_42/Adafruit GFX Library"
//     -Ilib/Apps/AppPaperboy/arduboy test/arduboy_host/test.cpp
//     lib/Apps/AppPaperboy/arduboy/ArduboyCore.cpp lib/Apps/AppPaperboy/arduboy/ArduboyGames.cpp
//     lib/Apps/AppPaperboy/arduboy/UltraHop/UltraHop.cpp
//     ".pio/libdeps/waveshare_rlcd_42/Adafruit GFX Library/Adafruit_GFX.cpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ArduboyCore.h"

static uint8_t shades[160 * 144];

static void show(const char* title) {
    printf("--- %s\n", title);
    for (int y = 0; y < 64; y += 2) {
        for (int x = 0; x < 128; ++x) {
            const bool top = shades[y * 128 + x], bottom = shades[(y + 1) * 128 + x];
            putchar(top && bottom ? '#' : top ? '\'' : bottom ? '.' : ' ');
        }
        putchar('\n');
    }
}
static unsigned ink() {
    unsigned n = 0;
    for (int i = 0; i < 128 * 64; ++i) n += shades[i] != 0;
    return n;
}
static void run(unsigned frames, uint16_t pad) {
    for (unsigned i = 0; i < frames; ++i) assert(inkArduboyFrame(pad, shades));
}

int main() {
    enum { A = 1, B = 2, RIGHT = 0x10, LEFT = 0x20, UP = 0x40, DOWN = 0x80 };
    assert(inkArduboyCount() == 1);
    assert(!strcmp(inkArduboyPath(0), "/roms/Ultra_Hop.arduboy"));
    assert(inkArduboyFind("/roms/Ultra_Hop.arduboy") == 0 && inkArduboyFind("/roms/x.gb") == -1);
    assert(inkArduboyOpen(0));
    assert(inkArduboyPeriod() == 16666);

    run(30, 0);
    show("title");
    assert(ink() > 200);

    // Up toggles the sound on the title screen and plays a coin chime.
    size_t bytes; uint8_t* eeprom = inkArduboyRam(&bytes);
    assert(eeprom && bytes == 1024 && eeprom[2] != 0);  // erased EEPROM: sound on
    run(1, UP); run(1, 0);
    assert(eeprom[2] == 0);
    run(1, UP); run(1, 0);
    assert(eeprom[2] != 0);
    int16_t pcm[1024];
    const size_t samples = inkArduboyAudio(pcm, 1024);
    assert(samples == 734);
    int peak = 0;
    for (size_t i = 0; i < samples; ++i) peak = pcm[i] > peak ? pcm[i] : peak;
    assert(peak > 0);  // the chime is sounding

    // Jump (Inkade A = Arduboy B) starts world 1; the intro then the level.
    run(1, A); run(1, 0);
    run(40, 0);
    show("intro");
    run(1, A); run(1, 0);
    run(60, 0);
    show("level start");
    unsigned before = ink();
    run(90, RIGHT);
    show("after running right");
    assert(ink() != before);

    // Jumping with B held (Inkade A) for a few frames.
    run(12, A | RIGHT);
    show("jumping");

    // The EEPROM is what Inkade saves: the sound setting lives in byte 2,
    // and a reopened game starts from it again.
    eeprom[2] = 0;
    inkArduboyClose();
    assert(inkArduboyOpen(0));
    eeprom = inkArduboyRam(&bytes);
    assert(eeprom[2] == 0xFF);  // erased until Inkade loads the save file
    eeprom[2] = 0;              // as loaded from the save
    run(2, 0);
    assert(inkArduboyAudio(pcm, 1024) == 734);
    for (size_t i = 0; i < 734; ++i) assert(pcm[i] == 0);  // sound off: silent

    inkArduboyClose();
    puts("Arduboy PASS: title, sound toggle, beeper audio, start, run, jump, EEPROM reload");
    return 0;
}

#include <assert.h>
#include <stdio.h>
#include "../lib/Book32_Core/DebouncedPress.h"

int main() {
    DebouncedPress b;
    assert(!b.update(true, false, 0));
    assert(!b.update(true, true, 10));
    assert(!b.update(true, false, 20)); // contact bounce
    assert(!b.update(true, true, 30));
    assert(!b.update(true, true, 59));
    assert(b.update(true, true, 60));
    assert(!b.update(true, true, 5000)); // hold must not repeat
    assert(!b.update(false, false, 5010)); // failed read must not release
    assert(!b.update(true, true, 5050));
    assert(!b.update(true, false, 5060));
    assert(!b.update(true, false, 5090));
    assert(!b.update(true, true, 5100));
    assert(!b.update(false, true, 5120)); // failed read cancels pending press
    assert(!b.update(true, true, 5160));
    assert(b.update(true, true, 5190));

    DebouncedPress bootHeld;
    assert(!bootHeld.update(false, false, 0));
    assert(!bootHeld.update(true, true, 10));
    assert(!bootHeld.update(true, true, 100));
    assert(!bootHeld.update(true, false, 110));
    assert(!bootHeld.update(true, false, 140));
    assert(!bootHeld.update(true, true, 150));
    assert(bootHeld.update(true, true, 180));

    DebouncedPress wrap;
    assert(!wrap.update(true, false, 0xffffffe0u));
    assert(!wrap.update(true, true, 0xfffffff0u));
    assert(!wrap.update(true, true, 0x0du));
    assert(wrap.update(true, true, 0x0eu));
    puts("Frontlight button PASS: bounce, hold, startup, I2C errors, millis wrap");
}

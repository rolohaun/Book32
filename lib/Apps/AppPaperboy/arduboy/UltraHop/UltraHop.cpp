// Ultra Hop (https://github.com/rolohaun/ultra-hop), an original Arduboy
// platformer, MIT licensed. UltraHop.inc is UltraHop.ino with its three
// Arduboy includes replaced; assets.h and types.h are unchanged.
#if defined(BOARD_WAVESHARE_RLCD42)
#include "../ArduboyCompat.h"

namespace ultrahop {
#include "UltraHop.inc"
}
#endif

#if defined(BOARD_LILYGO_T5S3_PRO) || defined(BOARD_SEEED_STICKY)
#include "ArduboyGames.h"

namespace ultrahop { void setup(); void loop(); }

const ArduboyGame ARDUBOY_GAMES[ARDUBOY_GAME_COUNT] = {
    {"Ultra Hop", ultrahop::setup, ultrahop::loop},
};
#endif

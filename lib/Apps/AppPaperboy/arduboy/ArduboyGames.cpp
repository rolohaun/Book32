#if defined(BOARD_WAVESHARE_RLCD42)
#include "ArduboyGames.h"

namespace ultrahop { void setup(); void loop(); }

const ArduboyGame ARDUBOY_GAMES[ARDUBOY_GAME_COUNT] = {
    {"Ultra Hop", ultrahop::setup, ultrahop::loop},
};
#endif

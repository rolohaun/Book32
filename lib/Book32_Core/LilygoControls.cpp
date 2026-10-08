#include "LilygoControls.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <Preferences.h>
#include "DisplayMgr.h"

LilygoControls& LilygoControls::instance() { static LilygoControls c; return c; }
const char* LilygoControls::actionName(Action action) {
    static const char* names[] = {"Light on/off", "Home", "Back", "Disabled"};
    return action >= LIGHT && action < ACTION_COUNT ? names[action] : names[0];
}
void LilygoControls::init() {
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
    Preferences prefs;
    uint32_t config = 100;
    if (prefs.begin("lilygo-controls", true)) { config = prefs.getUInt("config", 100); prefs.end(); }
    if ((config & 255) < 10 || (config & 255) > 100 ||
        ((config >> 8) & 255) >= ACTION_COUNT || (config >> 16) >= ACTION_COUNT) config = 100;
    _config = config;
    apply();
}
bool LilygoControls::save(int brightness, int front, int side) {
    if (!_mutex || brightness < 10 || brightness > 100 || front < 0 || front >= ACTION_COUNT ||
        side < 0 || side >= ACTION_COUNT) return false;
    uint32_t config = brightness | (front << 8) | (side << 16);
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool ok = true;
    if (_config.load() != config) {
        Preferences prefs;
        ok = prefs.begin("lilygo-controls", false);
        if (ok) { ok = prefs.putUInt("config", config) == sizeof(config); prefs.end(); }
        if (ok) _config = config;
    }
    xSemaphoreGive(_mutex);
    return ok;
}
void LilygoControls::apply() {
    const uint32_t config = _config.load();
    if (config == _applied) return;
    DisplayMgr::getInstance().getDisplay().setBrightness(config & 255);
    _applied = config;
}
#endif

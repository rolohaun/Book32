// Inkade on the Waveshare RLCD: no touch screen, so the game list, pause
// menu and controls are lists moved through with KEY (next; hold to select),
// BOOT (previous; hold for back) or a Bluetooth gamepad. The game list and
// controls are portrait like the rest of InkDeck; a game and its pause menu
// are landscape, the way the 400x300 panel shows a 2x Game Boy picture.
#include "AppPaperboy.h"
#if defined(BOARD_WAVESHARE_RLCD42)
#include "AppMgr.h"
#include "Book32FS.h"
#include "ConsoleCore.h"
#include "DisplayMgr.h"
#include "GameAudio.h"
#include "GamepadMgr.h"
#include "InkBoyLibraryUi.h"
#include "NesViewport.h"

namespace {
void text(Book32Display& d, const String& s, int x, int y, int size = 2, uint16_t color = GxEPD_BLACK) {
    d.setFont(nullptr);
    d.setTextSize(size);
    d.setTextWrap(false);
    d.setTextColor(color);
    d.setCursor(x, y);
    d.print(s);
    d.setTextSize(1);
}

// At most `chars` characters, ending in "..." when cut.
String fit(String s, unsigned chars) {
    if (s.length() > chars) s = s.substring(0, chars - 3) + "...";
    return s;
}

// A list row; the selected one is drawn white on black.
void row(Book32Display& d, int x, int y, int w, int h, const String& label, bool selected) {
    if (selected) d.fillRoundRect(x, y, w, h, 6, GxEPD_BLACK);
    text(d, fit(label, (w - 20) / 12), x + 10, y + (h - 16) / 2, 2, selected ? GxEPD_WHITE : GxEPD_BLACK);
}

constexpr int LIST_TOP = 46, LIST_ROW = 34, LIST_ROWS = 9;  // portrait game list
constexpr int PAUSE_TOP = 42, PAUSE_ROW = 22;               // landscape pause menu
constexpr int CTRL_TOP = 46, CTRL_ROW = 26;                 // portrait controls
}

void AppPaperboy::useLandscape(bool landscape) {
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.setRotation(landscape ? landscapeRotation() : DisplayMgr::getInstance().getRotation());
}

void AppPaperboy::rlcdKey(InputAction a) {
    if (a == INPUT_NONE) return;
    if (_playing) { back(); return; }  // any key pauses; the gamepad plays
    if (_controls) { rlcdControlsKey(a); return; }
    if (_paused) { rlcdPauseKey(a); return; }
    rlcdLibraryKey(a);
}

// Game list rows: back, each game, then Controls and Rescan.
void AppPaperboy::rlcdLibraryKey(InputAction a) {
    const unsigned games = _filtered.size(), rows = games + 3;
    if (a == INPUT_NEXT || a == INPUT_PREV) {
        _cursor = (_cursor + (a == INPUT_NEXT ? 1 : rows - 1)) % rows;
        _redraw = true;
    } else if (a == INPUT_SELECT) {
        if (_cursor == 0) AppMgr::getInstance().switchTo(0);
        else if (_cursor <= games) openBook(_roms[_filtered[_cursor - 1]]);
        else if (_cursor == games + 1) openControls();
        else { scan(); _cursor = _filtered.size() + 2; }
    } else if (a == INPUT_BACK) {
        back();
    }
}

void AppPaperboy::paintRlcdLibrary() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    useLandscape(false);
    d.setGameMode(false);
    d.setFullWindow();
    d.fillScreen(GxEPD_WHITE);
    const int w = d.width(), h = d.height();
    text(d, "Inkade", 12, 10, 3);
    char count[24];
    snprintf(count, sizeof(count), "%u game%s", unsigned(_filtered.size()), _filtered.size() == 1 ? "" : "s");
    text(d, count, w - 12 - int(strlen(count)) * 6, 20, 1);
    d.drawFastHLine(12, 38, w - 24, GxEPD_BLACK);

    const unsigned games = _filtered.size(), rows = games + 3;
    if (_cursor >= rows) _cursor = 0;
    const unsigned first = (_cursor / LIST_ROWS) * LIST_ROWS;
    for (unsigned i = first; i < rows && i < first + LIST_ROWS; ++i) {
        const int y = LIST_TOP + int(i - first) * LIST_ROW;
        const bool selected = i == _cursor;
        const uint16_t ink = selected ? GxEPD_WHITE : GxEPD_BLACK;
        if (i == 0) { row(d, 8, y, w - 16, LIST_ROW - 4, "< InkDeck", selected); continue; }
        if (i == games + 1) { row(d, 8, y, w - 16, LIST_ROW - 4, "Gamepad controls", selected); continue; }
        if (i == games + 2) { row(d, 8, y, w - 16, LIST_ROW - 4, "Rescan SD card", selected); continue; }
        // A game: its name, then the system and whether a resume save waits.
        const String& path = _roms[_filtered[i - 1]];
        String name = path.substring(6);
        name = name.substring(0, name.lastIndexOf('.'));
        name.replace('_', ' ');
        const String state = path + ".inkdeck.sav.state0";
        const bool resume = EbookFS.exists(state) || EbookFS.exists(state + ".bak");
        String detail = inkSystemName(inkRomSystem(path.c_str()));
        if (resume) detail += "  -  resume saved";
        if (selected) d.fillRoundRect(8, y, w - 16, LIST_ROW - 2, 6, GxEPD_BLACK);
        text(d, fit(name, (w - 36) / 12), 18, y + 3, 2, ink);
        text(d, detail, 18, y + 22, 1, ink);
        if (!selected) d.drawFastHLine(18, y + LIST_ROW - 1, w - 36, GxEPD_BLACK);
    }
    if (rows > LIST_ROWS) {
        char page[16];
        snprintf(page, sizeof(page), "%u/%u", _cursor / LIST_ROWS + 1, (rows + LIST_ROWS - 1) / LIST_ROWS);
        text(d, page, w - 12 - int(strlen(page)) * 6, 30, 1);
    }
    if (!games) {
        text(d, "No games yet", 18, 170, 2);
        text(d, "Copy .gb, .gbc or .nes ROMs to", 18, 196, 1);
        text(d, "/roms on the MicroSD card, or", 18, 208, 1);
        text(d, "upload them from the web page.", 18, 220, 1);
        text(d, "Play with a Bluetooth gamepad;", 18, 244, 1);
        text(d, "pair it from the web page too.", 18, 256, 1);
    }
    d.drawFastHLine(12, h - 32, w - 24, GxEPD_BLACK);
    text(d, fit(_message, (w - 24) / 6), 12, h - 27, 1);
    text(d, "KEY: next   Hold: open", 12, h - 13, 1);
    d.refresh();
}

std::vector<AppPaperboy::PauseRow> AppPaperboy::pauseRows() const {
    std::vector<PauseRow> rows;
    if (_confirmation) {
        rows.push_back({PAUSE_CANCEL, 0});
        rows.push_back({PAUSE_CONFIRM, 0});
        return rows;
    }
    if (!_coreFault) rows.push_back({PAUSE_RESUME, 0});
    rows.push_back({PAUSE_SOUND, 0});
    // Arduboy games keep only their own saves (EEPROM): no slots or restart.
    if (inkConsoleSystem() != INK_SYSTEM_ARDUBOY) {
        if (!_coreFault) for (uint8_t slot = 1; slot < 4; ++slot) rows.push_back({PAUSE_SAVE, slot});
        for (uint8_t slot = 0; slot < 4; ++slot) if (_slots[slot]) rows.push_back({PAUSE_LOAD, slot});
        rows.push_back({PAUSE_RESTART, 0});
    }
    rows.push_back({PAUSE_EXIT, 0});
    return rows;
}

void AppPaperboy::rlcdPauseKey(InputAction a) {
    const std::vector<PauseRow> rows = pauseRows();
    const int count = rows.size();
    if (a == INPUT_NEXT || a == INPUT_PREV) {
        _menuCursor = (_menuCursor + (a == INPUT_NEXT ? 1 : count - 1)) % count;
        _redraw = true;
        return;
    }
    if (a == INPUT_BACK) { _menuCursor = 0; back(); return; }
    if (a != INPUT_SELECT) return;
    const PauseRow choice = rows[constrain(_menuCursor, 0, count - 1)];
    _menuCursor = 0;
    _redraw = true;
    switch (choice.item) {
    case PAUSE_RESUME: resumeGame(); return;
    case PAUSE_SOUND: {  // Off, Low, Medium, High, then Off again
        GameAudio& audio = GameAudio::getInstance();
        audio.setLevel((audio.level() + 1) % GameAudio::LEVELS);
        _menuCursor = !_coreFault;  // stay on this row
        return;
    }
    case PAUSE_SAVE:
        _selectedSlot = choice.slot;
        if (_slots[choice.slot]) _confirmation = 1;
        else saveState(choice.slot);
        return;
    case PAUSE_LOAD: _selectedSlot = choice.slot; _confirmation = 2; return;
    case PAUSE_RESTART: _confirmation = 3; return;
    case PAUSE_EXIT: exitGame(); return;
    case PAUSE_CANCEL: _confirmation = 0; return;
    case PAUSE_CONFIRM: break;
    }
    // Confirmed: the same as the touch boards' Confirm button.
    const uint8_t action = _confirmation;
    _confirmation = 0;
    if (action == 1) saveState(_selectedSlot);
    else if (action == 2) { if (loadState(_selectedSlot)) resumeGame(); }
    else {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        const bool ok = inkConsoleRestart();
        _frameReady = false; _snapshotValid = false;
        memset(_snapshot, whiteFill(), packedBytes());
        xSemaphoreGive(_mutex);
        _coreFault = !ok;
        if (ok) resumeGame();
        else _message = inkConsoleError();
    }
}

void AppPaperboy::paintRlcdPause() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.setGameMode(false);
    useLandscape(true);
    d.setFullWindow();
    d.fillScreen(GxEPD_WHITE);
    const int w = d.width(), h = d.height();
    text(d, "Paused", 12, 10, 3);
    text(d, inkConsoleName(), w - 12 - int(strlen(inkConsoleName())) * 6, 18, 1);
    d.drawFastHLine(12, 36, w - 24, GxEPD_BLACK);

    int top = PAUSE_TOP;
    if (_confirmation) {
        text(d, _confirmation == 1 ? "Replace this save slot?" :
                _confirmation == 2 ? "Load this save?" : "Restart from the title?", 12, 50, 2);
        text(d, _confirmation == 2 ? "Unsaved play will be lost." :
                _confirmation == 3 ? "Saved slots are kept." : "The old save is replaced.", 12, 76, 1);
        top = 100;
    }
    const std::vector<PauseRow> rows = pauseRows();
    _menuCursor = constrain(_menuCursor, 0, int(rows.size()) - 1);
    for (int i = 0; i < int(rows.size()); ++i) {
        const PauseRow& r = rows[i];
        String label;
        switch (r.item) {
        case PAUSE_RESUME: label = "Resume game"; break;
        case PAUSE_SOUND: label = String("Sound: ") + GameAudio::getInstance().levelName(); break;
        case PAUSE_SAVE: label = "Save to slot " + String(r.slot) + (_slots[r.slot] ? "  (replace)" : ""); break;
        case PAUSE_LOAD: label = r.slot ? "Load slot " + String(r.slot) : String("Load auto resume"); break;
        case PAUSE_RESTART: label = "Restart game"; break;
        case PAUSE_EXIT: label = "Save & exit to games"; break;
        case PAUSE_CANCEL: label = "Cancel"; break;
        case PAUSE_CONFIRM: label = "Confirm"; break;
        }
        row(d, 8, top + i * PAUSE_ROW, w - 16, PAUSE_ROW - 2, label, i == _menuCursor);
    }
    text(d, fit(_message.length() ? _message : String("Auto resume saves on exit and sleep."), (w - 24) / 6),
         12, h - 10, 1);
    d.refresh();
}

void AppPaperboy::paintRlcdGame() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    useLandscape(true);
    d.setFullWindow();
    d.fillScreen(GxEPD_WHITE);
    const InkSystem system = inkConsoleSystem();
    const bool nes = system == INK_SYSTEM_NES;
    if (system == INK_SYSTEM_ARDUBOY) {
        // 128x64 at 3x fills the width; the title and key hint sit above it.
        constexpr int x = RlcdDisplay::ARDUBOY_X, y = RlcdDisplay::ARDUBOY_Y;
        constexpr int w = RlcdDisplay::ARDUBOY_W, h = RlcdDisplay::ARDUBOY_H;
        d.drawRect(x - 2, y - 2, w + 4, h + 4, GxEPD_BLACK);
        text(d, fit(inkConsoleName(), 20), x, 14, 2);
        text(d, "KEY: pause", d.width() - 8 - 60, 20, 1);
        d.setGameRotation(landscapeRotation());
        return;
    }
    const int x = nes ? NesViewport::X : GameViewport::X, y = nes ? NesViewport::Y : GameViewport::Y;
    const int w = nes ? NesViewport::W : GameViewport::W, h = nes ? NesViewport::H : GameViewport::H;
    d.drawRect(x - 2, y - 2, w + 4, h + 4, GxEPD_BLACK);
    // The side margins carry a reminder of the pause key.
    text(d, "KEY", 6, 8, 1);
    text(d, "pause", 4, 18, 1);
    text(d, nes ? "NES" : "GB", d.width() - 32, 8, 1);
    DisplayMgr::getInstance().getDisplay().setGameRotation(landscapeRotation());
}

void AppPaperboy::rlcdControlsKey(InputAction a) {
    // While waiting for a gamepad button, any key cancels.
    if (_capture >= 0) { controlsBack(); return; }
    constexpr int ROWS = InkBoyLibraryUi::CONTROL_ROWS + 1;  // + "Back"
    if (_controlsCursor < 0) _controlsCursor = 0;
    if (a == INPUT_NEXT || a == INPUT_PREV) {
        _controlsCursor = (_controlsCursor + (a == INPUT_NEXT ? 1 : ROWS - 1)) % ROWS;
        _redraw = true;
    } else if (a == INPUT_SELECT) {
        if (_controlsCursor == ROWS - 1) controlsBack();
        else activateControlRow(_controlsCursor);
    } else if (a == INPUT_BACK) {
        controlsBack();
    }
}

void AppPaperboy::paintRlcdControls() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    useLandscape(false);
    d.setGameMode(false);
    d.setFullWindow();
    d.fillScreen(GxEPD_WHITE);
    const int w = d.width(), h = d.height();
    text(d, "Controls", 12, 8, 2);
    text(d, "Gamepad buttons in games", 12, 28, 1);
    d.drawFastHLine(12, 40, w - 24, GxEPD_BLACK);
    const int cursor = _controlsCursor < 0 ? 0 : _controlsCursor;
    constexpr int ROWS = InkBoyLibraryUi::CONTROL_ROWS + 1;
    for (int i = 0; i < ROWS; ++i) {
        const int y = CTRL_TOP + i * CTRL_ROW;
        const bool selected = i == cursor || i == _capture;
        const uint16_t ink = selected ? GxEPD_WHITE : GxEPD_BLACK;
        if (i == ROWS - 1) { row(d, 8, y, w - 16, CTRL_ROW - 2, "< Back to Inkade", selected); continue; }
        if (i == PadMapping::ACTION_COUNT) { row(d, 8, y, w - 16, CTRL_ROW - 2, "Reset to defaults", selected); continue; }
        if (selected) d.fillRoundRect(8, y, w - 16, CTRL_ROW - 2, 6, GxEPD_BLACK);
        text(d, PadMapping::actionName(i), 18, y + 5, 2, ink);
        char value[64];
        if (i == _capture) snprintf(value, sizeof(value), "Press a pad button");
        else PadMapping::describe(_padMap.masks[i], value, sizeof(value));
        const String shown = fit(value, 20);
        text(d, shown, w - 18 - int(shown.length()) * 6, y + 9, 1, ink);
    }
    text(d, fit(_controlsMessage, (w - 24) / 6), 12, h - 48, 1);
    text(d, "Hold KEY on an action, then", 12, h - 34, 1);
    text(d, "press the pad button for it.", 12, h - 24, 1);
    text(d, "Select + Start always pauses.", 12, h - 12, 1);
    d.refresh(!_controlsFull);
    _controlsFull = false;
}
#endif

#include "ShelfName.h"
#include <Preferences.h>

namespace {
constexpr const char* PREFS = "inkdeck";
constexpr const char* KEY_OWNER = "owner";
}

std::atomic<uint32_t> ShelfName::_revision{0};

String ShelfName::name() {
    Preferences prefs;
    if (!prefs.begin(PREFS, true)) return String();
    const String owner = prefs.getString(KEY_OWNER, "");
    prefs.end();
    return owner;
}

String ShelfName::title() {
    const String owner = name();
    if (owner.isEmpty()) return "My Bookshelf";
    return owner + "'s Bookshelf";
}

bool ShelfName::save(String owner) {
    owner.trim();
    if (owner.length() > MAX_LENGTH) owner = owner.substring(0, MAX_LENGTH);
    Preferences prefs;
    if (!prefs.begin(PREFS, false)) return false;
    const bool ok = owner.isEmpty() ? (prefs.remove(KEY_OWNER), true) : prefs.putString(KEY_OWNER, owner) > 0;
    prefs.end();
    ++_revision;
    return ok;
}

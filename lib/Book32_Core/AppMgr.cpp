#include "AppMgr.h"
#include "Config.h"
#include "RomStorage.h"

AppMgr::AppMgr() {}

AppMgr& AppMgr::getInstance() {
    static AppMgr instance;
    return instance;
}

void AppMgr::registerApp(App* app) {
    apps.push_back(app);
}

void AppMgr::switchTo(int index) {
#if BOOK32_HAS_INKBOY
    if (RomStorage::uploading()) return;
#endif
    if (index == 0) index = home;
    if (index >= 0 && index < apps.size()) {
        if (currentApp) currentApp->stop();
        currentApp = apps[index];
        currentApp->start(); // Use start() instead of setup()
        currentApp->draw(); // Initial draw
    }
}

void AppMgr::switchTo(const char* name) {
    for (size_t i = 0; i < apps.size(); i++) {
        if (strcmp(apps[i]->getName(), name) == 0) {
            switchTo(i);
            return;
        }
    }
}

void AppMgr::update() {
    if (currentApp) {
        currentApp->update();
    }
}

void AppMgr::draw() {
#if BOOK32_HAS_INKBOY
    // Uploads have the storage card to themselves; on the Sticky it shares
    // the display's SPI bus. Screens catch up when the upload ends.
    if (RomStorage::uploading()) return;
#endif
    if (currentApp) {
        currentApp->draw();
    }
}

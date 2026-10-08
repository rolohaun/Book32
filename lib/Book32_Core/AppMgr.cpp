#include "AppMgr.h"
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
#if defined(BOARD_LILYGO_T5S3_PRO)
    if (RomStorage::uploading()) return;
#endif
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
    if (currentApp) {
        currentApp->draw();
    }
}

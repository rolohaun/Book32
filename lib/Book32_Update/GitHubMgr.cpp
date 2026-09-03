#include "GitHubMgr.h"
#include "VersionUtils.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "../../include/Config.h"
#include "../Book32_Core/DisplayMgr.h"
#include "../Book32_Core/FontMgr.h"

// Draw OTA progress bar on display
static void drawOTAProgress(int progress, const char* title, const char* status) {
    auto& display = DisplayMgr::getInstance().getDisplay();
    auto& fontMgr = FontMgr::getInstance();

    int screenW = display.width();
    int screenH = display.height();

    // Progress bar dimensions
    int barWidth = screenW - 100;
    int barHeight = 30;
    int barX = 50;
    int barY = screenH / 2;
    int fillWidth = (barWidth * progress) / 100;

    // Clear and draw with partial refresh for faster updates
    display.setPartialWindow(0, 0, screenW, screenH);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Title (e.g., "Updating Firmware (1/2)" or "Updating Filesystem (2/2)")
        fontMgr.drawTextCentered(display, title, barY - 60, FONT_SIZE_TITLE, GxEPD_BLACK);

        // Status text
        fontMgr.drawTextCentered(display, status, barY - 25, FONT_SIZE_BODY, GxEPD_BLACK);

        // Progress bar outline
        display.drawRect(barX, barY, barWidth, barHeight, GxEPD_BLACK);
        display.drawRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, GxEPD_BLACK);

        // Progress bar fill
        if (fillWidth > 4) {
            display.fillRect(barX + 2, barY + 2, fillWidth - 4, barHeight - 4, GxEPD_BLACK);
        }

        // Percentage text
        char percentText[16];
        snprintf(percentText, sizeof(percentText), "%d%%", progress);
        fontMgr.drawTextCentered(display, percentText, barY + barHeight + 40, FONT_SIZE_TITLE, GxEPD_BLACK);

    } while (display.nextPage());
}


GitHubMgr::GitHubMgr() {}

GitHubMgr& GitHubMgr::getInstance() {
    static GitHubMgr instance;
    return instance;
}

void GitHubMgr::init() {
    // any init?
}

UpdateInfo GitHubMgr::checkUpdate(const char* currentVersion) {
    UpdateInfo info = {false, "", "", "", "", false, false};

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, cannot check for updates");
        return info;
    }

    HTTPClient http;
    String apiURL = String("https://api.github.com/repos/") + GITHUB_REPO + "/releases/latest";

    Serial.printf("Checking: %s\n", apiURL.c_str());

    http.begin(apiURL);
    http.setUserAgent("InkDeck-ESP32");
    http.setTimeout(10000);  // 10 second timeout

    Serial.println("Using public GitHub release API");

    int httpCode = http.GET();
    Serial.printf("HTTP Response: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(8192);  // Increased size for release notes
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("JSON parse error: %s\n", err.c_str());
            http.end();
            return info;
        }

        const char* tagName = doc["tag_name"];
        info.version = tagName ? tagName : "";
        info.notes = doc["body"].as<String>();

        Serial.printf("Latest version: %s, Current: %s\n", info.version.c_str(), currentVersion);

        // Only offer true upgrades. Older, equal, and malformed release tags
        // are ignored so a stale GitHub release can never prompt a downgrade.
        if (VersionUtils::isNewer(info.version, String(currentVersion))) {
            info.available = true;
            Serial.println("Update IS available");

            // Find asset URLs
            JsonArray assets = doc["assets"];
            for (JsonObject asset : assets) {
                String name = asset["name"].as<String>();

                String url = asset["browser_download_url"].as<String>();

                if (name == OTA_FIRMWARE_ASSET) {
                    info.firmwareUrl = url;
                    info.hasFirmware = true;
                    Serial.printf("Found firmware: %s\n", name.c_str());
                }
                else if (name == OTA_FILESYSTEM_ASSET) {
                    info.filesystemUrl = url;
                    info.hasFilesystem = true;
                    Serial.printf("Found filesystem: %s\n", name.c_str());
                }
            }
        } else {
            Serial.println("No newer release available");
        }
    } else if (httpCode == 404) {
        Serial.println("No releases found on GitHub");
    } else if (httpCode == 403) {
        Serial.println("GitHub API rate limited - try again later");
    } else {
        Serial.printf("GitHub API Failed: %d\n", httpCode);
    }
    http.end();
    return info;
}

bool GitHubMgr::performFirmwareUpdate(const char* url, bool restartAfter, int step, int totalSteps) {
    if (WiFi.status() != WL_CONNECTED) return false;

    Serial.printf("Downloading firmware from: %s\n", url);

    // Build title with step indicator if provided
    char title[64];
    if (totalSteps > 1) {
        snprintf(title, sizeof(title), "Firmware (%d/%d)", step, totalSteps);
    } else {
        snprintf(title, sizeof(title), "Firmware Update");
    }

    HTTPClient http;
    http.begin(url);
    http.setUserAgent("InkDeck-ESP32");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);  // 30 second timeout for large downloads

    http.addHeader("Accept", "application/octet-stream");

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        Serial.printf("Firmware size: %d bytes\n", contentLength);

        if (!Update.begin(contentLength, U_FLASH)) {
            Serial.println("Not enough space for firmware update");
            http.end();
            return false;
        }

        // Show initial progress on display
        drawOTAProgress(0, title, "Downloading...");

        WiFiClient *stream = http.getStreamPtr();

        // Use chunked download with periodic yields to prevent watchdog
        uint8_t buff[4096];
        size_t written = 0;
        int lastProgress = 0;

        while (written < contentLength) {
            // Read chunk
            size_t available = stream->available();
            if (available == 0) {
                delay(1);  // Yield to other tasks
                continue;
            }

            size_t toRead = min(available, sizeof(buff));
            size_t bytesRead = stream->readBytes(buff, toRead);

            if (bytesRead > 0) {
                size_t bytesWritten = Update.write(buff, bytesRead);
                if (bytesWritten != bytesRead) {
                    Serial.println("Write error during update");
                    Update.abort();
                    http.end();
                    return false;
                }
                written += bytesWritten;

                // Progress and yield every ~5%
                int progress = (written * 100) / contentLength;
                if (progress / 5 > lastProgress / 5) {
                    Serial.printf("Progress: %d%%\n", progress);
                    drawOTAProgress(progress, title, "Downloading...");
                    lastProgress = progress;
                }

                // Yield frequently to feed watchdog
                yield();
            }
        }

        if (written == contentLength) {
            Serial.println("Firmware written successfully");
            drawOTAProgress(100, title, "Installing...");
            if (Update.end()) {
                Serial.println("Firmware update complete");
                drawOTAProgress(100, title, "Complete!");
                delay(500);
                http.end();
                if (restartAfter) {
                    Serial.println("Restarting...");
                    ESP.restart();
                }
                return true;
            }
        } else {
            Serial.printf("Firmware write failed. Written: %d / %d\n", written, contentLength);
        }
    } else {
        Serial.printf("Firmware download failed: %d\n", httpCode);
    }
    http.end();
    return false;
}

bool GitHubMgr::performFilesystemUpdate(const char* url, bool restartAfter, int step, int totalSteps) {
    if (WiFi.status() != WL_CONNECTED) return false;

    Serial.printf("Downloading filesystem from: %s\n", url);

    // Build title with step indicator if provided
    char title[64];
    if (totalSteps > 1) {
        snprintf(title, sizeof(title), "Web Interface (%d/%d)", step, totalSteps);
    } else {
        snprintf(title, sizeof(title), "Web Interface Update");
    }

    HTTPClient http;
    http.begin(url);
    http.setUserAgent("InkDeck-ESP32");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);  // 30 second timeout for large downloads

    http.addHeader("Accept", "application/octet-stream");

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        Serial.printf("Filesystem size: %d bytes\n", contentLength);

        // U_SPIFFS is used for both SPIFFS and LittleFS partitions
        if (!Update.begin(contentLength, U_SPIFFS)) {
            Serial.println("Not enough space for filesystem update");
            http.end();
            return false;
        }

        // Show initial progress on display
        drawOTAProgress(0, title, "Downloading...");

        WiFiClient *stream = http.getStreamPtr();

        // Use chunked download with periodic yields to prevent watchdog
        uint8_t buff[4096];
        size_t written = 0;
        int lastProgress = 0;

        while (written < contentLength) {
            size_t available = stream->available();
            if (available == 0) {
                delay(1);
                continue;
            }

            size_t toRead = min(available, sizeof(buff));
            size_t bytesRead = stream->readBytes(buff, toRead);

            if (bytesRead > 0) {
                size_t bytesWritten = Update.write(buff, bytesRead);
                if (bytesWritten != bytesRead) {
                    Serial.println("Write error during filesystem update");
                    Update.abort();
                    http.end();
                    return false;
                }
                written += bytesWritten;

                int progress = (written * 100) / contentLength;
                if (progress / 5 > lastProgress / 5) {
                    Serial.printf("FS Progress: %d%%\n", progress);
                    drawOTAProgress(progress, title, "Downloading...");
                    lastProgress = progress;
                }
                yield();
            }
        }

        if (written == contentLength) {
            Serial.println("Filesystem written successfully");
            drawOTAProgress(100, title, "Installing...");
            if (Update.end()) {
                Serial.println("Filesystem update complete");
                drawOTAProgress(100, title, "Complete!");
                delay(500);
                http.end();
                if (restartAfter) {
                    Serial.println("Restarting...");
                    ESP.restart();
                }
                return true;
            }
        } else {
            Serial.printf("Filesystem write failed. Written: %d / %d\n", written, contentLength);
        }
    } else {
        Serial.printf("Filesystem download failed: %d\n", httpCode);
    }
    http.end();
    return false;
}

void GitHubMgr::triggerUpdate(const char* currentVersion) {
    Serial.println("Triggering Update Check...");
    UpdateInfo info = checkUpdate(currentVersion);
    if (info.available && info.hasFirmware) {
        Serial.printf("Update Available: %s. Starting firmware download.\n", info.version.c_str());
        performFirmwareUpdate(info.firmwareUrl.c_str(), true, 1, 1);
    } else {
        Serial.println("No firmware update available.");
    }
}

bool GitHubMgr::performFullUpdate(const char* currentVersion) {
    Serial.println("Starting full update check...");
    UpdateInfo info = checkUpdate(currentVersion);

    if (!info.available) {
        Serial.println("No update available.");
        return false;
    }

    Serial.printf("Update available: %s\n", info.version.c_str());

    bool firmwareUpdated = false;
    bool filesystemUpdated = false;

    // Calculate total steps for progress display
    int totalSteps = (info.hasFirmware ? 1 : 0) + (info.hasFilesystem ? 1 : 0);
    int currentStep = 0;

    // Update firmware first (don't restart yet)
    if (info.hasFirmware) {
        currentStep++;
        Serial.println("Updating firmware...");
        firmwareUpdated = performFirmwareUpdate(info.firmwareUrl.c_str(), false, currentStep, totalSteps);
        if (!firmwareUpdated) {
            Serial.println("Firmware update failed!");
            return false;
        }
    }

    // Then update filesystem (don't restart yet)
    if (info.hasFilesystem) {
        currentStep++;
        Serial.println("Updating filesystem...");
        filesystemUpdated = performFilesystemUpdate(info.filesystemUrl.c_str(), false, currentStep, totalSteps);
        if (!filesystemUpdated) {
            Serial.println("Filesystem update failed!");
            // Still restart if firmware was updated
            if (firmwareUpdated) {
                Serial.println("Restarting after firmware update...");
                ESP.restart();
            }
            return false;
        }
    }

    // Restart after all updates complete
    if (firmwareUpdated || filesystemUpdated) {
        // Show restarting message
        drawOTAProgress(100, "Update Complete!", "Restarting...");
        delay(1000);
        Serial.println("All updates complete. Restarting...");
        ESP.restart();
        return true;
    }

    return false;
}

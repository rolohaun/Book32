#pragma once

// System Information
#define SYSTEM_VERSION "1.1"
#define DEVICE_NAME "Book32"

// Offline management hotspot (SoftAP). When the device can't reach a known
// WiFi network, the main menu broadcasts this network so a phone can connect
// directly and reach the web interface at 192.168.4.1 (no router needed).
#define AP_SSID "Book32"

// Pin Definitions for Seeed XIAO ESP32-S3 (TRMNL 7.5" OG DIY Kit)
#define PIN_BAT_VOLT 1  
#define PIN_VBAT_SWITCH 6
#define VBAT_SWITCH_LEVEL HIGH
#define PIN_BUTTON   5  // "KEY3" button

// Display Pins (TRMNL 7.5" OG DIY Kit)
#define EPD_SCK     7
#define EPD_MOSI    9
#define EPD_MISO    -1 
#define EPD_CS      44
#define EPD_DC      10
#define EPD_RST     38
#define EPD_BUSY    4

// Display Settings (Portrait 7.5")
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 800
#define FONT_SIZE_DEFAULT 28 // Default font size (maps to FreeSans18pt GFX font)

// Boot diagnostics
// Set to 1 when debugging partition/filesystem issues. Keeping this off makes
// normal startup quieter and avoids walking the ebook filesystem every boot.
#define BOOK32_VERBOSE_BOOT_LOG 0

// Battery calibration
// Fully charged LiPo cells should read 4.20V. This board's ADC path reads a
// known-full pack around 3.91V with the raw divider math, so compensate here.
#define BATTERY_VOLTAGE_CALIBRATION 1.075f
#define BATTERY_EMPTY_VOLTAGE 3.00f
#define BATTERY_FULL_VOLTAGE 4.20f

// GitHub OTA Config
#define GITHUB_REPO "rolohaun/Book32"
#define GITHUB_USER "rolohaun"

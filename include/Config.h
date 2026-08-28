#pragma once

// System Information
#define SYSTEM_VERSION "1.2"

#if defined(BOARD_SEEED_STICKY)
#define DEVICE_NAME "Book32 Sticky"
#else
#define DEVICE_NAME "Book32"
#endif

// Offline management hotspot (SoftAP). When the device can't reach a known
// WiFi network, the main menu broadcasts this network so a phone can connect
// directly and reach the web interface at 192.168.4.1 (no router needed).
#define AP_SSID "Book32"

// Both supported panels are 800x480 natively and run the Book32 UI in
// 480x800 portrait orientation.
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 800
#define FONT_SIZE_DEFAULT 28

#if defined(BOARD_SEEED_STICKY)
// Seeed reTerminal E1002 / Sticky 3.97" (SSD1677)
#define PIN_BUTTON       4
#define PIN_BUTTON_UP    5
#define PIN_BUTTON_DOWN  6

#define EPD_SCK          13
#define EPD_MOSI         14
#define EPD_MISO         12
#define EPD_CS           15
#define EPD_DC           16
#define EPD_RST          17
#define EPD_BUSY         18
#define EPD_ENABLE       47

#define SD_CS            8
#define SD_MISO          12
#define SD_SCK           13
#define SD_MOSI          14
#define SD_POWER_ENABLE  10

#define TOUCH_SDA        3
#define TOUCH_SCL        2
#define TOUCH_INT        21
#define TOUCH_RST        41
#define TOUCH_ENABLE     42

#define PIN_POWER_HOLD    45
#define PIN_POWER_LOCK    46
#define PIN_CHARGE_ENABLE 39
#define PIN_CHARGE_STATUS 40
#define BATTERY_I2C_SDA   1
#define BATTERY_I2C_SCL   0

#define BOOK32_HAS_TOUCH 1
#define BOOK32_HAS_SD    1

#define OTA_FIRMWARE_ASSET "book32-sticky-firmware.bin"
#define OTA_FILESYSTEM_ASSET "book32-sticky-littlefs.bin"
#else
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

#define BOOK32_HAS_TOUCH 0
#define BOOK32_HAS_SD    0

#define OTA_FIRMWARE_ASSET "firmware.bin"
#define OTA_FILESYSTEM_ASSET "littlefs.bin"
#endif

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

#pragma once

#if defined(BOARD_LILYGO_T5S3_PRO)
#define SYSTEM_VERSION "1.4.6"
#define DEVICE_NAME "LILYGO T5 E-Paper S3 Pro Lite (H752-01)"
#define SLEEP_TIMEOUT_DEFAULT_MINUTES 10
#elif defined(BOARD_SEEED_STICKY)
#define SYSTEM_VERSION "1.4.6"
#define DEVICE_NAME "Seeed Studio Sticky"
#define SLEEP_TIMEOUT_DEFAULT_MINUTES 10
#elif defined(BOARD_WAVESHARE_RLCD42)
#define SYSTEM_VERSION "1.4.6"
#define DEVICE_NAME "Waveshare RLCD 4.2"
#define SLEEP_TIMEOUT_DEFAULT_MINUTES 10
#else
#define SYSTEM_VERSION "1.4.6"
#define DEVICE_NAME "Book32"
#define SLEEP_TIMEOUT_DEFAULT_MINUTES 10
#endif

#if defined(BOARD_LILYGO_T5S3_PRO)
#define SLEEP_CONFIG_VERSION 3
#define SLEEP_MESSAGE_DEFAULT "Press BOOT to wake"
#elif defined(BOARD_SEEED_STICKY)
#define SLEEP_CONFIG_VERSION 2
#define SLEEP_MESSAGE_DEFAULT "Press power to wake"
#elif defined(BOARD_WAVESHARE_RLCD42)
#define SLEEP_CONFIG_VERSION 3
#define SLEEP_MESSAGE_DEFAULT "Press KEY to wake"
#else
// Book32 v3: idle sleep became on-by-default (10 min); see loadSleepSettings().
#define SLEEP_CONFIG_VERSION 3
#define SLEEP_MESSAGE_DEFAULT "Press power to wake"
#endif

// Offline management hotspot (SoftAP). When the device can't reach a known
// WiFi network, the main menu broadcasts this network so a phone can connect
// directly and reach the web interface at 192.168.4.1 (no router needed).
#define AP_SSID "InkDeck"

// Both supported panels are 800x480 natively and run the InkDeck UI in
// 480x800 portrait orientation.
#if defined(BOARD_LILYGO_T5S3_PRO)
#define SCREEN_WIDTH 524
#define SCREEN_HEIGHT 944
#elif defined(BOARD_WAVESHARE_RLCD42)
// 400x300 landscape panel; the UI runs 300x400 portrait, games landscape.
#define SCREEN_WIDTH 300
#define SCREEN_HEIGHT 400
#else
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 800
#endif
#if defined(BOARD_LILYGO_T5S3_PRO)
#define PANEL_WIDTH 960
#define PANEL_HEIGHT 540
#else
#define PANEL_WIDTH SCREEN_HEIGHT
#define PANEL_HEIGHT SCREEN_WIDTH
#endif
#define FONT_SIZE_DEFAULT 28
#define READER_FULL_REFRESH_INTERVAL_DEFAULT 15
#if defined(BOARD_SEEED_STICKY) || defined(BOARD_LILYGO_T5S3_PRO)
#define READER_FONT_SIZE_DEFAULT 18
#else
#define READER_FONT_SIZE_DEFAULT 9
#endif
#define READER_FONT_FAMILY_DEFAULT "openSans"

#if defined(BOARD_LILYGO_T5S3_PRO)
// H752-01 ONLY. The older H752 has a different display and power pinout.
#define PIN_BUTTON 0 // BOOT: back; hold 2s to sleep, release, then press to wake
#define PIN_FRONTLIGHT 11 // PT4103 enable: LOW off, HIGH on
#define FRONTLIGHT_BUTTON_I2C_ADDRESS 0x20 // PCA9535, S3 on IO1_2 (active low)
#define FRONTLIGHT_BUTTON_INPUT_REGISTER 1
#define FRONTLIGHT_BUTTON_MASK (1u << 2)
#define SD_CS 12
#define SD_MISO 21
#define SD_SCK 14
#define SD_MOSI 13
#define TOUCH_SDA 39
#define TOUCH_SCL 40
#define TOUCH_INT 3
#define TOUCH_RST 9
#define BOOK32_HAS_TOUCH 1
#define BOOK32_HAS_SD 1
#define BOOK32_HAS_BUZZER 0
#define OTA_FIRMWARE_ASSET "inkdeck-lilygo-firmware.bin"
#define OTA_FILESYSTEM_ASSET "inkdeck-lilygo-littlefs.bin"
#elif defined(BOARD_WAVESHARE_RLCD42)
// Waveshare ESP32-S3-RLCD-4.2: ST7305 reflective LCD on SPI, no touch.
#define PIN_BUTTON       18 // "KEY": next, hold to select
#define PIN_BUTTON_PREV  0  // BOOT: previous, hold for back

#define RLCD_SCK         11
#define RLCD_MOSI        12
#define RLCD_DC          5
#define RLCD_CS          40
#define RLCD_RST         41
#define RLCD_TE          6

#define SD_MMC_CLK       38
#define SD_MMC_CMD       21
#define SD_MMC_D0        39

#define PIN_BAT_VOLT     4  // 1:3 divider
#define BATTERY_DIVIDER  3.0f

#define BOOK32_HAS_TOUCH 0
#define BOOK32_HAS_SD    1
#define BOOK32_HAS_BUZZER 0
// Not in GitHub releases yet: no over-the-air updates.
#define BOOK32_HAS_OTA   0

#define OTA_FIRMWARE_ASSET "inkdeck-rlcd42-firmware.bin"
#define OTA_FILESYSTEM_ASSET "inkdeck-rlcd42-littlefs.bin"
#elif defined(BOARD_SEEED_STICKY)
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

#define PIN_BUZZER       48
#define PIN_MIC_ENABLE   38

#define PIN_POWER_HOLD    45
#define PIN_POWER_LOCK    46
#define PIN_CHARGE_ENABLE 39
#define PIN_CHARGE_STATUS 40
#define BATTERY_I2C_SDA   1
#define BATTERY_I2C_SCL   0

#define BOOK32_HAS_TOUCH 1
#define BOOK32_HAS_SD    1
#define BOOK32_HAS_BUZZER 1

#define OTA_FIRMWARE_ASSET "book32-sticky-firmware.bin"
#define OTA_FILESYSTEM_ASSET "book32-sticky-littlefs.bin"
#else
// Pin Definitions for Seeed XIAO ESP32-S3 (TRMNL 7.5" OG DIY Kit)
#define PIN_BAT_VOLT 1  
#define PIN_VBAT_SWITCH 6
#define VBAT_SWITCH_LEVEL HIGH
#define PIN_BUTTON   5  // "KEY3" button
#define PIN_BUTTON_PREV 2 // "KEY1": previous page
#define PIN_BUTTON_HOME 3 // "KEY2": InkDeck main menu

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
#define BOOK32_HAS_BUZZER 0

#define OTA_FIRMWARE_ASSET "firmware.bin"
#define OTA_FILESYSTEM_ASSET "littlefs.bin"
#endif

#ifndef BOOK32_HAS_OTA
#define BOOK32_HAS_OTA 1
#endif
#ifndef BATTERY_DIVIDER
#define BATTERY_DIVIDER 2.0f
#endif

// The original Book32: GxEPD2 on the 7.5" GD7965 panel.
#if !defined(BOARD_LILYGO_T5S3_PRO) && !defined(BOARD_SEEED_STICKY) && !defined(BOARD_WAVESHARE_RLCD42)
#define BOOK32_GXEPD_PANEL 1
#else
#define BOOK32_GXEPD_PANEL 0
#endif

// Inkade (Game Boy / NES) runs on the boards with fast video drivers.
#if defined(BOARD_LILYGO_T5S3_PRO) || defined(BOARD_SEEED_STICKY) || defined(BOARD_WAVESHARE_RLCD42)
#define BOOK32_HAS_INKBOY 1
#else
#define BOOK32_HAS_INKBOY 0
#endif

// Page books in the eReader (CBZ manga, and PDFs converted when uploaded),
// with pinch to zoom: the touch boards, which keep books on a MicroSD card.
#if BOOK32_HAS_TOUCH && BOOK32_HAS_SD
#define BOOK32_HAS_PAGE_BOOKS 1
#else
#define BOOK32_HAS_PAGE_BOOKS 0
#endif

// Live frames while a gesture moves a page (StickyDisplay / LilygoDisplay
// liveBegin, liveFrame, liveEnd).
#if BOOK32_HAS_PAGE_BOOKS && (defined(BOARD_SEEED_STICKY) || defined(BOARD_LILYGO_T5S3_PRO))
#define BOOK32_HAS_LIVE_VIEW 1
#else
#define BOOK32_HAS_LIVE_VIEW 0
#endif
// Boards whose Inkade video is the port of InkBoy's drive (320x288 window on
// an 800x480 panel).
#if defined(BOARD_SEEED_STICKY)
#define INKADE_INKBOY_VIDEO 1
#else
#define INKADE_INKBOY_VIDEO 0
#endif

// The RLCD's 300x400 screen: smaller icons, rows and margins.
#if defined(BOARD_WAVESHARE_RLCD42)
#define BOOK32_COMPACT_UI 1
#else
#define BOOK32_COMPACT_UI 0
#endif

// Boot diagnostics
// Set to 1 when debugging partition/filesystem issues. Keeping this off makes
// normal startup quieter and avoids walking the ebook filesystem every boot.
#define BOOK32_VERBOSE_BOOT_LOG 0

// Battery calibration
// Fully charged LiPo cells should read 4.20V. This board's ADC path reads a
// known-full pack around 3.91V with the raw divider math, so compensate here.
// The RLCD board reads its divider with the ADC's calibrated millivolts.
#if defined(BOARD_WAVESHARE_RLCD42)
#define BATTERY_VOLTAGE_CALIBRATION 1.0f
#else
#define BATTERY_VOLTAGE_CALIBRATION 1.075f
#endif
#define BATTERY_EMPTY_VOLTAGE 3.00f
#define BATTERY_FULL_VOLTAGE 4.20f

// GitHub OTA Config
#define GITHUB_REPO "rolohaun/Book32"
#define GITHUB_USER "rolohaun"

#include "GameAudio.h"
#include "Config.h"

GameAudio& GameAudio::getInstance() {
    static GameAudio instance;
    return instance;
}

#if defined(AUDIO_I2S_DOUT)
#include <Preferences.h>
#include <Wire.h>
#include <driver/i2s.h>

namespace {
constexpr i2s_port_t PORT = I2S_NUM_0;
constexpr uint8_t CODEC = 0x18;
constexpr const char* PREFS = "gameaudio";
// DAC volume (register 0x32): 0xBF is 0 dB, in 0.5 dB steps.
constexpr uint8_t VOLUME[GameAudio::LEVELS] = {0x00, 0xA3, 0xB3, 0xBF};
constexpr const char* NAMES[GameAudio::LEVELS] = {"Off", "Low", "Medium", "High"};
constexpr size_t CHUNK = 256;  // stereo frames converted per I2S write

bool reg(uint8_t r, uint8_t v) {
    Wire.beginTransmission(CODEC);
    Wire.write(r);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}
}

bool GameAudio::isSupported() const { return true; }

// I2S master with MCLK = 256 x 44.1 kHz; the ES8311 as slave DAC, set up as
// Espressif's esp_codec_dev driver does for 16-bit I2S at this clock.
bool GameAudio::begin() {
    if (_tried) return _ready;
    _tried = true;
    pinMode(AUDIO_PA, OUTPUT);
    digitalWrite(AUDIO_PA, LOW);

    i2s_config_t config = {};
    config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = SAMPLE_RATE;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.dma_buf_count = 8;  // 8 x 256 frames: about 46 ms queued
    config.dma_buf_len = 256;
    config.use_apll = true;
    config.tx_desc_auto_clear = true;  // silence, not repeats, on underrun
    config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = AUDIO_I2S_MCLK;
    pins.bck_io_num = AUDIO_I2S_BCLK;
    pins.ws_io_num = AUDIO_I2S_WS;
    pins.data_out_num = AUDIO_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_driver_install(PORT, &config, 0, nullptr) != ESP_OK) {
        Serial.println("Game audio: I2S driver failed");
        return false;
    }
    if (i2s_set_pin(PORT, &pins) != ESP_OK) {
        Serial.println("Game audio: I2S pins failed");
        i2s_driver_uninstall(PORT);
        return false;
    }

    Wire.begin(AUDIO_I2C_SDA, AUDIO_I2C_SCL, 400000);
    reg(0x44, 0x08);  // the first write after power-up can fail: repeat it
    bool ok = reg(0x44, 0x08);
    static const uint8_t INIT[][2] = {
        // Open: clocks off, defaults, then on with MCLK from the pin, slave mode.
        {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10}, {0x05, 0x00},
        {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F}, {0x00, 0x80},
        {0x01, 0x3F}, {0x06, 0x00}, {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A}, {0x44, 0x58},
        // 44.1 kHz from 11.2896 MHz: dividers 1, OSR 0x10, LRCK 256, BCLK MCLK/4.
        {0x02, 0x00}, {0x05, 0x00}, {0x03, 0x10}, {0x04, 0x10}, {0x07, 0x00}, {0x08, 0xFF},
        {0x06, 0x03},
        // 16-bit I2S in; the ADC output stays muted.
        {0x09, 0x0C}, {0x0A, 0x4C},
        // Start the DAC path.
        {0x00, 0x80}, {0x01, 0x3F}, {0x17, 0xBF}, {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A},
        {0x0D, 0x01}, {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00}, {0x31, 0x00},
    };
    for (const auto& r : INIT) ok &= reg(r[0], r[1]);
    if (!ok) {
        Serial.println("Game audio: ES8311 codec did not answer");
        i2s_driver_uninstall(PORT);
        return false;
    }
    i2s_stop(PORT);
    _ready = true;
    applyLevel();
    Serial.println("Game audio: ES8311 ready, 44.1 kHz");
    return true;
}

uint8_t GameAudio::level() {
    if (!_loaded) {
        _loaded = true;
        Preferences prefs;
        if (prefs.begin(PREFS, true)) {
            _level = min<uint8_t>(prefs.getUChar("level", _level), LEVELS - 1);
            prefs.end();
        }
    }
    return _level;
}

void GameAudio::setLevel(uint8_t value) {
    _level = min<uint8_t>(value, LEVELS - 1);
    _loaded = true;
    Preferences prefs;
    if (prefs.begin(PREFS, false)) {
        prefs.putUChar("level", _level);
        prefs.end();
    }
    applyLevel();
}

const char* GameAudio::levelName() { return NAMES[level()]; }

void GameAudio::applyLevel() {
    if (_ready) reg(0x32, VOLUME[level()]);
}

void GameAudio::start() {
    if (!level() || !begin() || _running) return;
    i2s_zero_dma_buffer(PORT);
    i2s_start(PORT);
    digitalWrite(AUDIO_PA, HIGH);
    _running = true;
}

void GameAudio::stop() {
    if (!_running) return;
    _running = false;
    digitalWrite(AUDIO_PA, LOW);
    i2s_stop(PORT);
    i2s_zero_dma_buffer(PORT);
}

void GameAudio::write(const int16_t* samples, size_t count) {
    static int16_t frames[CHUNK * 2];
    while (_running && count) {
        const size_t n = min(count, CHUNK);
        for (size_t i = 0; i < n; ++i) frames[2 * i] = frames[2 * i + 1] = samples[i];
        size_t written = 0;
        // A full queue means the game runs ahead of the speaker: wait for
        // room, but never long enough to stall a frame when audio stalls.
        i2s_write(PORT, frames, n * 4, &written, pdMS_TO_TICKS(40));
        samples += n;
        count -= n;
    }
}

#else
bool GameAudio::isSupported() const { return false; }
bool GameAudio::begin() { return false; }
uint8_t GameAudio::level() { return 0; }
void GameAudio::setLevel(uint8_t) {}
const char* GameAudio::levelName() { return "Off"; }
void GameAudio::applyLevel() {}
void GameAudio::start() {}
void GameAudio::stop() {}
void GameAudio::write(const int16_t*, size_t) {}
#endif

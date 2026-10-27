#pragma once

#include <Arduino.h>

// Inkade game sound on boards with an ES8311 codec (the Waveshare RLCD): the
// emulators render 44.1 kHz mono PCM each frame and write() queues it on I2S.
// The codec starts on first use; the amplifier is on only while a game plays.
class GameAudio {
public:
    static constexpr uint32_t SAMPLE_RATE = 44100;
    static constexpr uint8_t LEVELS = 4;  // off, low, medium, high

    static GameAudio& getInstance();

    bool isSupported() const;
    void start();  // a game starts or resumes
    void stop();   // paused, exited or going to sleep
    bool running() const { return _running; }
    // Queues mono samples; blocks briefly when the queue is full, which keeps
    // the emulator in step with the audio clock.
    void write(const int16_t* samples, size_t count);

    uint8_t level();
    void setLevel(uint8_t level);
    const char* levelName();

private:
    GameAudio() = default;
    bool begin();
    void applyLevel();

    bool _tried = false, _ready = false, _loaded = false;
    volatile bool _running = false;  // read by the emulator task
    uint8_t _level = 2;
};

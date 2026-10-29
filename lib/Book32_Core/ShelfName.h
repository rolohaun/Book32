#pragma once
#include <Arduino.h>
#include <atomic>

// The owner's name for the library's title: "Ron's Bookshelf", or
// "My Bookshelf" without one. Kept in NVS, so it survives updates; the web
// page sets it, and the library repaints its header when it changes.
class ShelfName {
public:
    static constexpr size_t MAX_LENGTH = 20;

    static String name();
    static String title();
    // Trims the name and keeps at most MAX_LENGTH characters; empty clears it.
    static bool save(String name);
    // Bumped by every save: compare with a value taken when drawing.
    static uint32_t revision() { return _revision.load(); }

private:
    static std::atomic<uint32_t> _revision;
};

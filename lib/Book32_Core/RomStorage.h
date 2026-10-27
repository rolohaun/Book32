#pragma once
#if defined(BOARD_LILYGO_T5S3_PRO) || defined(BOARD_SEEED_STICKY) || defined(BOARD_WAVESHARE_RLCD42)
#include <atomic>
namespace RomStorage {
enum Owner { Idle, Upload, Game };
extern std::atomic<int> owner;
inline bool acquire(Owner who) { int idle = Idle; return owner.compare_exchange_strong(idle, who); }
inline void release(Owner who) { int expected = who; owner.compare_exchange_strong(expected, Idle); }
inline bool uploading() { return owner.load() == Upload; }
}
#include "Book32FS.h"
// ROMs live in /roms on the MicroSD card.
inline bool romStorageReady() { return ebookStorageUsesSD(); }
#endif

#pragma once
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <atomic>
namespace RomStorage {
enum Owner { Idle, Upload, Game };
extern std::atomic<int> owner;
inline bool acquire(Owner who) { int idle = Idle; return owner.compare_exchange_strong(idle, who); }
inline void release(Owner who) { int expected = who; owner.compare_exchange_strong(expected, Idle); }
inline bool uploading() { return owner.load() == Upload; }
}
#endif

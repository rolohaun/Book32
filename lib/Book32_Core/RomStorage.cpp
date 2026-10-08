#include "RomStorage.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
std::atomic<int> RomStorage::owner{RomStorage::Idle};
#endif

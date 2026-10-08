#if defined(BOARD_LILYGO_T5S3_PRO)
#include "ConsoleCore.h"
#include "NesCore.h"
#include "GenesisCore.h"
static InkSystem activeSystem=INK_SYSTEM_NONE;
static const char* fallback="No game loaded";
static bool dirty;
void inkConsoleClose(void){
    if(activeSystem==INK_SYSTEM_GB)inkGameClose();
    else if(activeSystem==INK_SYSTEM_NES)inkNesCloseGame();
    else if(activeSystem==INK_SYSTEM_SEGA)inkGenesisCloseGame();
    activeSystem=INK_SYSTEM_NONE;dirty=false;
}
bool inkConsoleOpen(InkSystem selected,uint8_t* rom,size_t bytes,InkRomRead read,void* context){
    inkConsoleClose();activeSystem=selected;dirty=false;
    if(activeSystem==INK_SYSTEM_GB)return inkGameOpenBanked(rom,bytes,read,context);
    if(read){fallback="This core requires a resident ROM";return false;}
    if(activeSystem==INK_SYSTEM_NES)return inkNesOpenGame(rom,bytes);
    if(activeSystem==INK_SYSTEM_SEGA)return inkGenesisOpenGame(rom,bytes);
    fallback="Unsupported ROM format";return false;
}
bool inkConsoleFrame(uint16_t buttons,uint8_t* shades){
    return inkConsoleStep(buttons,shades,true);
}
bool inkConsoleStep(uint16_t buttons,uint8_t* shades,bool render){
    dirty=true;
    if(activeSystem==INK_SYSTEM_GB)return inkGameFrame((uint8_t)buttons,shades);
    if(activeSystem==INK_SYSTEM_NES)return inkNesFrame(buttons,shades);
    if(activeSystem==INK_SYSTEM_SEGA)return inkGenesisStep(buttons,shades,render);
    return false;
}
bool inkConsoleImage(uint8_t* shades){
    if(activeSystem==INK_SYSTEM_GB)return inkGameImage(shades);
    if(activeSystem==INK_SYSTEM_NES)return inkNesImage(shades);
    if(activeSystem==INK_SYSTEM_SEGA)return inkGenesisImage(shades);
    return false;
}
bool inkConsoleRestart(void){
    if(activeSystem==INK_SYSTEM_GB)return inkGameRestart();
    if(activeSystem==INK_SYSTEM_NES)return inkNesRestart();
    if(activeSystem==INK_SYSTEM_SEGA)return inkGenesisRestart();
    return false;
}
uint8_t* inkConsoleRam(size_t* bytes){
    if(activeSystem==INK_SYSTEM_GB)return inkGameRam(bytes);
    if(activeSystem==INK_SYSTEM_NES)return inkNesRam(bytes);
    *bytes=0;return NULL; // Genesis persistence uses complete emulator states.
}
bool inkConsoleDirty(void){return activeSystem==INK_SYSTEM_GB?inkGameDirty():dirty;}
void inkConsoleSaved(void){if(activeSystem==INK_SYSTEM_GB)inkGameSaved();dirty=false;}
bool inkConsoleHasRtc(void){return activeSystem==INK_SYSTEM_GB&&inkGameHasRtc();}
bool inkConsoleRtcExport(uint8_t* s){return activeSystem==INK_SYSTEM_GB&&inkGameRtcExport(s);}
bool inkConsoleRtcImport(const uint8_t* s){return activeSystem==INK_SYSTEM_GB&&inkGameRtcImport(s);}
const char* inkConsoleError(void){return activeSystem==INK_SYSTEM_GB?inkGameError():activeSystem==INK_SYSTEM_NES?inkNesError():activeSystem==INK_SYSTEM_SEGA?inkGenesisError():fallback;}
const char* inkConsoleName(void){return activeSystem==INK_SYSTEM_GB?(inkGameIsColor()?"CrankBoy / GBC":"CrankBoy / GB"):activeSystem==INK_SYSTEM_NES?"Nofrendo / NES":"ClownMDEmu / Genesis";}
InkSystem inkConsoleSystem(void){return activeSystem;}
unsigned inkConsolePeriod(void){return activeSystem==INK_SYSTEM_GB?16743:activeSystem==INK_SYSTEM_NES?inkNesPeriod():inkGenesisPeriod();}
size_t inkConsoleStateSize(void){return activeSystem==INK_SYSTEM_GB?inkGameStateSize():activeSystem==INK_SYSTEM_NES?inkNesStateSize():activeSystem==INK_SYSTEM_SEGA?inkGenesisStateSize():0;}
bool inkConsoleStateExport(void* data,size_t bytes){return activeSystem==INK_SYSTEM_GB?inkGameStateExport(data,bytes):activeSystem==INK_SYSTEM_NES?inkNesStateExport(data,bytes):activeSystem==INK_SYSTEM_SEGA&&inkGenesisStateExport(data,bytes);}
bool inkConsoleStateImport(const void* data,size_t bytes){return activeSystem==INK_SYSTEM_GB?inkGameStateImport(data,bytes):activeSystem==INK_SYSTEM_NES?inkNesStateImport(data,bytes):activeSystem==INK_SYSTEM_SEGA&&inkGenesisStateImport(data,bytes);}
#endif

#pragma once
#include <Adafruit_GFX.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "RomFormat.h"
#include "PadMapping.h"
namespace InkBoyLibraryUi {
#if INKADE_INKBOY_VIDEO
// 480x800 portrait.
constexpr int CONTENT_W=440, ROWS=4, ROW_Y=250, ROW_H=96, ROW_STEP=102;
constexpr int TAB_Y=166,TAB_H=54,NAV_Y=666,NAV_H=56, NAV_W=136, NAV_GAP=16;
constexpr int PAGE_Y=738, STATUS_Y=762, COUNT_Y=234, KICKER_X=310;
constexpr unsigned TITLE_CHARS=28;
constexpr int CARD_ART_DY=24, CARD_LINE1_DY=15, CARD_LINE2_DY=38, NAV_SMALL_DY=24, NAV_BIG_DY=20;
// "Display >" opens Inkade's display settings; taps right of SETTINGS_X.
constexpr int SETTINGS_X=330;
// "Controls >" opens the gamepad mapping.
constexpr int CONTROLS_X=190, CONTROLS_W=130;
// Controls screen rows.
constexpr int CTRL_Y=150, CTRL_H=50, CTRL_STEP=56, CTRL_HINT_Y=722;
#else
constexpr int CONTENT_W=484, ROWS=5, ROW_Y=264, ROW_H=100, ROW_STEP=106;
constexpr int TAB_Y=174,TAB_H=58,NAV_Y=818,NAV_H=58, NAV_W=144, NAV_GAP=26;
constexpr int PAGE_Y=894, STATUS_Y=920, COUNT_Y=246, KICKER_X=352;
constexpr unsigned TITLE_CHARS=32;
constexpr int CARD_ART_DY=23, CARD_LINE1_DY=17, CARD_LINE2_DY=40, NAV_SMALL_DY=22, NAV_BIG_DY=20;
constexpr int CONTROLS_X=KICKER_X, CONTROLS_W=CONTENT_W+20-KICKER_X;
constexpr int CTRL_Y=170, CTRL_H=58, CTRL_STEP=66, CTRL_HINT_Y=850;
#endif
constexpr int LEFT=20, RIGHT=LEFT+CONTENT_W;
inline int navX(int index){return LEFT+index*(NAV_W+NAV_GAP);}
// 0 = previous, 1 = rescan, 2 = next, -1 = none.
inline int navAt(int x,int y){
    if(y<NAV_Y||y>=NAV_Y+NAV_H)return -1;
    for(int i=0;i<3;i++)if(x>=navX(i)&&x<navX(i)+NAV_W)return i;
    return -1;
}
inline unsigned tabCount(uint8_t mask){unsigned n=0;for(int i=INK_SYSTEM_GB;i<=INK_SYSTEM_NES;i++)if(mask&(1<<i))++n;return n;}
inline int tabWidth(uint8_t mask){unsigned n=tabCount(mask);return n ? (CONTENT_W-8*(n-1))/n : 0;}
inline InkSystem tabAt(uint8_t mask,int x,int y){
    if(y<TAB_Y||y>=TAB_Y+TAB_H)return INK_SYSTEM_NONE;
    int left=LEFT,w=tabWidth(mask);
    for(int i=INK_SYSTEM_GB;i<=INK_SYSTEM_NES;i++)if(mask&(1<<i)){if(x>=left&&x<left+w)return (InkSystem)i;left+=w+8;}
    return INK_SYSTEM_NONE;
}
inline bool controlsAt(int x,int y){return y<60&&x>=CONTROLS_X&&x<CONTROLS_X+CONTROLS_W;}
// Controls screen row under a tap, or -1.
inline int controlRowAt(int x,int y,int rows){
    if(x<LEFT||x>=RIGHT||y<CTRL_Y)return -1;
    int r=(y-CTRL_Y)/CTRL_STEP;
    return r<rows&&(y-CTRL_Y)%CTRL_STEP<CTRL_H?r:-1;
}
inline int rowAt(int x,int y){
    if(x<LEFT||x>=RIGHT||y<ROW_Y)return -1;
    int r=(y-ROW_Y)/ROW_STEP;
    return r<ROWS&&(y-ROW_Y)%ROW_STEP<ROW_H?r:-1;
}
inline void text(Adafruit_GFX& d,const char* s,int x,int y,int size=2,uint16_t color=0){
    d.setFont(nullptr);d.setTextSize(size);d.setTextWrap(false);d.setTextColor(color);d.setCursor(x,y);d.print(s);d.setTextSize(1);
}
inline void center(Adafruit_GFX& d,const char* s,int x,int y,int w,int size=2,uint16_t color=0){
    text(d,s,x+(w-int(strlen(s))*6*size)/2,y,size,color);
}
inline void header(Adafruit_GFX& d,uint8_t mask,InkSystem selected,unsigned count){
    d.fillScreen(1);text(d,"< InkDeck",LEFT,26);
#if defined(BOARD_SEEED_STICKY)
    d.drawRoundRect(SETTINGS_X,12,RIGHT-SETTINGS_X,40,8,0);center(d,"Display >",SETTINGS_X,24,RIGHT-SETTINGS_X);
#endif
    d.drawRoundRect(CONTROLS_X,12,CONTROLS_W,40,8,0);center(d,"Controls >",CONTROLS_X,24,CONTROLS_W);
    text(d,"Inkade",LEFT,91,4);text(d,"Your games, one place.",LEFT+2,138,2);
    int left=LEFT,w=tabWidth(mask);char label[24];
    for(int i=INK_SYSTEM_GB;i<=INK_SYSTEM_NES;i++)if(mask&(1<<i)){
        if(i==selected)d.fillRoundRect(left,TAB_Y,w,TAB_H,10,0);
        else d.drawRoundRect(left,TAB_Y,w,TAB_H,10,0);
        center(d,inkSystemName((InkSystem)i),left,TAB_Y+(TAB_H-16)/2,w,2,i==selected?1:0);left+=w+8;
    }
    snprintf(label,sizeof(label),"%u GAME%s",count,count==1?"":"S");text(d,label,LEFT+4,COUNT_Y,1);
}
inline void card(Adafruit_GFX& d,unsigned row,const char* name,const char* detail){
    int y=ROW_Y+row*ROW_STEP;
    d.drawRoundRect(LEFT,y,CONTENT_W,ROW_H,10,0);
    // A crisp outlined cartridge motif, no external artwork/downloads.
    const int top=y+CARD_ART_DY;
    d.drawRoundRect(34,top,34,48,4,0);d.drawRect(40,top+7,22,19,0);
    for(int i=0;i<4;i++)d.drawLine(42+i*5,top+38,42+i*5,top+45,0);
    const unsigned width=TITLE_CHARS;char first[TITLE_CHARS+1],second[TITLE_CHARS+1];size_t n=strlen(name),cut=n<width?n:width;
    if(n>width)for(size_t i=width;i>12;i--)if(name[i]==' '){cut=i;break;}
    memcpy(first,name,cut);first[cut]=0;text(d,first,84,y+CARD_LINE1_DY,2);
    const char* rest=name+cut;while(*rest==' ')rest++;
    if(*rest){snprintf(second,sizeof(second),"%s",rest);if(strlen(rest)>width)memcpy(second+width-3,"...",3);text(d,second,84,y+CARD_LINE2_DY,2);}
    text(d,detail,84,y+ROW_H-26,1);text(d,">",RIGHT-26,y+ROW_H-26,2);
}
inline void footer(Adafruit_GFX& d,unsigned page,unsigned pages,const char* message){
    for(int i=0;i<3;i++)d.drawRoundRect(navX(i),NAV_Y,NAV_W,NAV_H,9,0);
    center(d,page?"< Previous":"First",navX(0),NAV_Y+NAV_SMALL_DY,NAV_W,1);
    center(d,"Rescan",navX(1),NAV_Y+NAV_BIG_DY,NAV_W);
    center(d,page+1<pages?"Next >":"Last",navX(2),NAV_Y+NAV_SMALL_DY,NAV_W,1);
    char pageLabel[40];snprintf(pageLabel,sizeof(pageLabel),"PAGE %u / %u",page+1,pages?pages:1);center(d,pageLabel,LEFT,PAGE_Y,CONTENT_W,1);
    char status[80];snprintf(status,sizeof(status),"%.78s",message);center(d,status,LEFT,STATUS_Y,CONTENT_W,1);
    d.setTextWrap(true);
}
// Controls screen: one row per game action, then "Reset to defaults".
// `values` names each action's button or key, `keyboard` says which.
// `cursor` is the key/gamepad selection (-1 none), `capture` the action
// waiting for a press (-1 none).
constexpr int CONTROL_ROWS=PadMapping::ACTION_COUNT+1;
inline void controls(Adafruit_GFX& d,const char* const* values,bool keyboard,int cursor,int capture,const char* message){
    d.fillScreen(1);text(d,"< Inkade",LEFT,26);
    text(d,"Controls",LEFT,CTRL_Y-86,4);text(d,keyboard?"Keyboard keys in games":"Gamepad buttons in games",LEFT+2,CTRL_Y-40,2);
    for(int row=0;row<CONTROL_ROWS;row++){
        const int y=CTRL_Y+row*CTRL_STEP,textY=y+(CTRL_H-16)/2;
        if(row==PadMapping::ACTION_COUNT){
            d.drawRoundRect(LEFT,y,CONTENT_W,CTRL_H,10,0);center(d,"Reset to defaults",LEFT,textY,CONTENT_W);
        }else if(row==capture){
            d.fillRoundRect(LEFT,y,CONTENT_W,CTRL_H,10,0);text(d,PadMapping::actionName(row),LEFT+14,textY,2,1);
            const char* prompt=keyboard?"Press a key":"Press a gamepad button";text(d,prompt,RIGHT-14-int(strlen(prompt))*6,textY+4,1,1);
        }else{
            d.drawRoundRect(LEFT,y,CONTENT_W,CTRL_H,10,0);
            const char* name=PadMapping::actionName(row);text(d,name,LEFT+14,textY);
            const char* value=values[row];
            // Size 2 when it fits beside the name, else the smaller size.
            const int room=CONTENT_W-28-int(strlen(name))*12-16;
            const int size=int(strlen(value))*12<=room?2:1;
            text(d,value,RIGHT-14-int(strlen(value))*6*size,size==2?textY:textY+4,size);
        }
        if(row==cursor&&row!=capture)
            for(int inset=1;inset<=3;inset++)d.drawRoundRect(LEFT+inset,y+inset,CONTENT_W-2*inset,CTRL_H-2*inset,9,0);
    }
    text(d,keyboard?"Tap an action, then press the key for it.":"Tap an action, then press the gamepad button for it.",LEFT,CTRL_HINT_Y,1);
    text(d,keyboard?"A key already in use swaps with that action.":"A button already in use swaps with that action.",LEFT,CTRL_HINT_Y+16,1);
    text(d,"Select + Start together always opens the pause menu.",LEFT,CTRL_HINT_Y+32,1);
    if(message&&*message){char status[80];snprintf(status,sizeof(status),"%.78s",message);text(d,status,LEFT,CTRL_HINT_Y+56,1);}
    d.setTextWrap(true);
}
}

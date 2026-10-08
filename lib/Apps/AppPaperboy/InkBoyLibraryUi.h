#pragma once
#include <Adafruit_GFX.h>
#include <stdio.h>
#include <string.h>
#include "RomFormat.h"
namespace InkBoyLibraryUi {
constexpr int ROWS=5, ROW_Y=264, ROW_H=100, ROW_STEP=106;
constexpr int TAB_Y=174,TAB_H=58,NAV_Y=818,NAV_H=58;
inline unsigned tabCount(uint8_t mask){unsigned n=0;for(int i=INK_SYSTEM_GB;i<=INK_SYSTEM_NES;i++)if(mask&(1<<i))++n;return n;}
inline int tabWidth(uint8_t mask){unsigned n=tabCount(mask);return n ? (484-8*(n-1))/n : 0;}
inline InkSystem tabAt(uint8_t mask,int x,int y){
    if(y<TAB_Y||y>=TAB_Y+TAB_H)return INK_SYSTEM_NONE;
    int left=20,w=tabWidth(mask);
    for(int i=INK_SYSTEM_GB;i<=INK_SYSTEM_NES;i++)if(mask&(1<<i)){if(x>=left&&x<left+w)return (InkSystem)i;left+=w+8;}
    return INK_SYSTEM_NONE;
}
inline int rowAt(int x,int y){
    if(x<20||x>=504||y<ROW_Y)return -1;
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
    d.fillScreen(1);text(d,"< InkDeck",20,26);text(d,"GAME LIBRARY",352,30,1);
    text(d,"Ink Boy",20,91,4);text(d,"Your games, one place.",22,138,2);
    int left=20,w=tabWidth(mask);char label[24];
    for(int i=INK_SYSTEM_GB;i<=INK_SYSTEM_NES;i++)if(mask&(1<<i)){
        if(i==selected)d.fillRoundRect(left,TAB_Y,w,TAB_H,10,0);
        else d.drawRoundRect(left,TAB_Y,w,TAB_H,10,0);
        center(d,inkSystemName((InkSystem)i),left,TAB_Y+21,w,2,i==selected?1:0);left+=w+8;
    }
    snprintf(label,sizeof(label),"%u GAME%s",count,count==1?"":"S");text(d,label,24,246,1);
}
inline void card(Adafruit_GFX& d,unsigned row,const char* name,const char* detail){
    int y=ROW_Y+row*ROW_STEP;
    d.drawRoundRect(20,y,484,ROW_H,10,0);
    // A crisp outlined cartridge motif, no external artwork/downloads.
    d.drawRoundRect(34,y+23,34,48,4,0);d.drawRect(40,y+30,22,19,0);
    for(int i=0;i<4;i++)d.drawLine(42+i*5,y+61,42+i*5,y+68,0);
    const unsigned width=32;char first[width+1],second[width+1];size_t n=strlen(name),cut=n<width?n:width;
    if(n>width)for(size_t i=width;i>12;i--)if(name[i]==' '){cut=i;break;}
    memcpy(first,name,cut);first[cut]=0;text(d,first,84,y+17,2);
    const char* rest=name+cut;while(*rest==' ')rest++;
    if(*rest){snprintf(second,sizeof(second),"%s",rest);if(strlen(rest)>width)memcpy(second+width-3,"...",3);text(d,second,84,y+40,2);}
    text(d,detail,84,y+74,1);text(d,">",478,y+74,2);
}
inline void footer(Adafruit_GFX& d,unsigned page,unsigned pages,const char* message){
    d.drawRoundRect(20,NAV_Y,144,NAV_H,9,0);d.drawRoundRect(190,NAV_Y,144,NAV_H,9,0);d.drawRoundRect(360,NAV_Y,144,NAV_H,9,0);
    center(d,page?"< Previous":"First",20,NAV_Y+22,144,1);
    center(d,"Rescan",190,NAV_Y+20,144);
    center(d,page+1<pages?"Next >":"Last",360,NAV_Y+22,144,1);
    char pageLabel[40];snprintf(pageLabel,sizeof(pageLabel),"PAGE %u / %u",page+1,pages?pages:1);center(d,pageLabel,20,894,484,1);
    char status[80];snprintf(status,sizeof(status),"%.78s",message);center(d,status,20,920,484,1);
    d.setTextWrap(true);
}
}

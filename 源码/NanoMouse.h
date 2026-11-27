#pragma once
#include "Devices.h"
#include <algorithm>
#include <array>

// M185 M-R0085 ships with C-U0010 (046D:C52F). HID++ device indices refer
// to paired slots. Neither the receiver PID nor the slot identifies a mouse.
namespace NanoMouse {
using Bytes = std::vector<BYTE>;
using Params = std::array<BYTE,3>;
enum class Reply { Unrelated, Match, Error };
inline Reply Match(const BYTE* frame, size_t size, BYTE slot, BYTE feature, BYTE function) {
    if (!((size==7 && frame[0]==0x10) || (size==20 && frame[0]==0x11)) || frame[1]!=slot)
        return Reply::Unrelated;
    if (frame[2]==feature && frame[3]==function) return Reply::Match;
    if ((frame[2]==0xff || frame[2]==0x8f) && frame[3]==feature && frame[4]==function) return Reply::Error;
    return Reply::Unrelated;
}
inline const wchar_t* Level(BYTE mask) {
    switch(mask) {
    case 1: return L"极低";
    case 2: return L"偏低";
    case 4: return L"良好";
    case 8: return L"充足";
    default: return L"";
    }
}
// Query receives a feature index, function number and three parameters. The
// transport supplies a changing software ID and filters replies for this slot.
enum class Result { Asleep, UnknownType, OtherDevice, Mouse };
template<class Query> Result Poll(Query query, Device& mouse) {
    auto feature=[&](USHORT id)->BYTE {
        auto reply=query(0,0,Params{BYTE(id>>8),BYTE(id),0});
        return reply.empty()?0:reply.front();
    };
    auto ping=query(0,1,Params{0,0,0x6b});
    if (ping.size()<3 || ping[0]<2 || ping[2]!=0x6b) return Result::Asleep;
    BYTE identity=feature(5);
    auto kind=identity?query(identity,2,Params{}):Bytes{};
    if (kind.empty()) return Result::UnknownType;
    if (kind[0]!=3) return Result::OtherDevice;
    mouse.connected=1;
    mouse.name=L"罗技 Nano 无线鼠标";
    auto length=query(identity,0,Params{});
    if (!length.empty() && length[0]>0 && length[0]<=96) {
        std::string name;
        while (name.size()<length[0]) {
            auto chunk=query(identity,1,Params{BYTE(name.size()),0,0});
            if (chunk.empty()) break;
            name.append(reinterpret_cast<const char*>(chunk.data()),std::min(chunk.size(),length[0]-name.size()));
        }
        if (name.size()==length[0] && name.find_first_of("\r\n\t\0",0,4)==std::string::npos) {
            wchar_t text[97]{};
            if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),int(name.size()),text,96)>0)
                mouse.name=L"罗技 "+std::wstring(text);
        }
    }
    if (BYTE index=feature(0x1000)) {
        auto value=query(index,0,Params{});
        int percent=-1;
        if (LogitechBattery(value.data(),value.size(),percent)) {
            mouse.battery=percent;
            mouse.approximate=true;
            mouse.source=L"HID++ 电量（1000）";
            return Result::Mouse;
        }
    }
    if (BYTE index=feature(0x1004)) {
        auto caps=query(index,0,Params{});
        auto status=query(index,1,Params{});
        if (caps.size()>=2 && status.size()>=3) {
            if ((caps[1]&2) && status[0]<=100) mouse.battery=status[0];
            else if ((caps[0]&status[1])==status[1]) mouse.level=Level(status[1]);
            if (mouse.battery>=0 || !mouse.level.empty()) mouse.source=L"HID++ 统一电量（1004）";
        }
    }
    if (mouse.source.empty()) mouse.detail=L"已识别鼠标，本次没有取得可用的电量报告";
    return Result::Mouse;
}
}

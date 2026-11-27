#pragma once
#include <windows.h>
#include "MonitorItem.h"
#include <string>
#include <vector>
#include <memory>
#include <atomic>

struct Device {
    std::wstring key, name, address, transport, source, detail, level;
    std::wstring alias;
    const std::wstring& DisplayName() const { return alias.empty() ? name : alias; }
    int connected=-1, battery=-1;
    bool approximate=false, receiver=false;
    ULONGLONG batteryTime=0;
    std::wstring BatteryText() const;
    std::wstring StateText() const;
    std::wstring Note() const;
};
struct Resources {
    int cpu=-1, memory=-1;
    int computerBattery=-1;
    bool computerBatteryPresent=false, computerCharging=false, computerAc=false;
    ULONGLONG used=0,total=0,workingSet=0,privateWorkingSet=0,commit=0;
};
Resources ReadResources();
std::wstring FormatMemory(ULONGLONG bytes);
bool Current(const Device& device);
int NormalizeBattery(int minimum,int maximum,ULONG value);
int RapooBattery(const unsigned char* report,size_t length);
bool LogitechBattery(const unsigned char* report,size_t length,int& value);
std::wstring LogitechModelName(const std::wstring& name);
std::vector<MonitorItem> MonitorItems(const Resources&,const std::vector<Device>&,bool,bool,bool,bool);
void WriteUtf8(const std::wstring& path,const std::wstring& text);
class Devices {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit Devices(std::atomic<bool>& stopping);
    ~Devices();
    std::vector<Device> Scan();
    std::wstring Diagnostic(const std::vector<Device>& devices);
};


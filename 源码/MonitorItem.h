#pragma once
#include <string>

struct MonitorItem {
    std::wstring name, value;
    bool batteryIcon = false;
    std::wstring shortName;

    const std::wstring& Label() const { return shortName.empty() ? name : shortName; }
    std::wstring Text() const { return value.empty() ? name : name + L": " + value; }
    bool operator==(const MonitorItem&) const = default;
};

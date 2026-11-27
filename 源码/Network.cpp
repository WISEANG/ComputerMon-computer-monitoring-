#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include "Network.h"
#include <algorithm>
#include <cmath>
#include <memory>

NetworkRates NetworkDelta(const std::vector<InterfaceBytes>& before,
                          const std::vector<InterfaceBytes>& after, uint64_t elapsedMs) {
    if (!elapsedMs || elapsedMs > 10000) return {};
    double sent = 0, received = 0;
    bool matched = false;
    for (const auto& row : after) {
        auto old = std::find_if(before.begin(), before.end(), [&](auto& p){ return p.id == row.id; });
        if (old == before.end() || row.sent < old->sent || row.received < old->received) continue;
        sent += double(row.sent - old->sent);
        received += double(row.received - old->received);
        matched = true;
    }
    if (!matched) return {};
    return {sent * 1000 / elapsedMs, received * 1000 / elapsedMs};
}

NetworkRates ReadNetworkRates(bool enabled) {
    static uint64_t lastRead = 0;
    static NetworkRates cached;
    static std::vector<InterfaceBytes> previous, current;
    if (!enabled) {
        previous.clear();
        lastRead = 0;
        cached = {};
        return {};
    }
    const uint64_t now = GetTickCount64();
    if (lastRead && now - lastRead < 750) return cached;
    PMIB_IF_TABLE2 raw = nullptr;
    current.clear();
    if (GetIfTable2(&raw) == NO_ERROR) {
        std::unique_ptr<MIB_IF_TABLE2, decltype(&FreeMibTable)> table(raw, FreeMibTable);
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const auto& row = table->Table[i];
            if (row.OperStatus != IfOperStatusUp || !row.InterfaceAndOperStatusFlags.HardwareInterface ||
                row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) continue;
            current.push_back({row.InterfaceLuid.Value, row.OutOctets, row.InOctets});
        }
    }
    cached = NetworkDelta(previous, current, now - lastRead);
    previous.swap(current);
    lastRead = now;
    return cached;
}

std::wstring FormatRate(double bytesPerSecond) {
    if (bytesPerSecond < 0 || !std::isfinite(bytesPerSecond)) return L"—";
    constexpr const wchar_t* units[] = {L"B/s", L"KB/s", L"MB/s", L"GB/s", L"TB/s"};
    int unit = 0;
    while (bytesPerSecond >= 999.5 && unit < 4) { bytesPerSecond /= 1000; ++unit; }
    wchar_t text[48];
    swprintf(text, 48, unit && bytesPerSecond < 9.95 ? L"%.1f %ls" : L"%.0f %ls", bytesPerSecond, units[unit]);
    return text;
}

#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct NetworkRates {
    double upload = -1, download = -1;
};
struct InterfaceBytes {
    uint64_t id, sent, received;
};
NetworkRates NetworkDelta(const std::vector<InterfaceBytes>& before,
                          const std::vector<InterfaceBytes>& after, uint64_t elapsedMs);
NetworkRates ReadNetworkRates(bool enabled);
std::wstring FormatRate(double bytesPerSecond);

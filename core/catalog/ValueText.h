#pragma once
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>

namespace vdev {
inline bool ParseIntegerValue(const std::string &text, int32_t &out) {
    if (text.empty() || text.find('\0') != std::string::npos) return false;
    char *end = nullptr;
    errno = 0;
    const auto value = std::strtoll(text.c_str(), &end, 10);
    if (errno || end == text.c_str() || value < INT32_MIN || value > INT32_MAX) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end) return false;
    out = int32_t(value);
    return true;
}
inline bool ParseRealValue(const std::string &text, double &out, double scale = 1) {
    if (text.empty() || text.find('\0') != std::string::npos || !std::isfinite(scale) || scale <= 0) return false;
    char *end = nullptr;
    errno = 0;
    const auto value = std::strtod(text.c_str(), &end) / scale;
    if (errno || end == text.c_str() || !std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
    if (scale != 1 && *end == '%') ++end;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end) return false;
    out = float(value);
    return true;
}
} // namespace vdev

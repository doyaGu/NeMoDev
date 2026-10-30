#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace vdev {
struct ParameterCurvePoint {
    uint32_t flags = 0;
    // Position, tension, continuity, bias, incoming and outgoing tangents.
    std::array<double, 9> values{};
    // Native derived data is retained for exact undo, never supplied by editors.
    std::array<double, 3> derived{};
};
struct ParameterCurve {
    double fitting = 0;
    double length = 0;
    std::vector<ParameterCurvePoint> points;
};
bool operator==(const ParameterCurve &, const ParameterCurve &);
bool ValidParameterCurve(const ParameterCurve &);
bool NormalizeParameterCurve(ParameterCurve &);
bool ParameterCurvePreview(const ParameterCurve &, std::vector<double> &samples, std::vector<double> &tangents);
}

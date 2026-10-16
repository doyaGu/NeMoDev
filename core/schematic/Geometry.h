#pragma once

#include <cmath>
#include <algorithm>
#include <cassert>
#include <limits>

#undef min
#undef max

//==============================================================================
// COORDINATE POLICY MODULE
//==============================================================================
/**
 * @file Geometry.h
 * @section CoordinatePolicy Coordinate Semantic Documentation
 *
 * Virtools Interface uses TWO coordinate systems with DIFFERENT storage semantics:
 *
 * 1. **PIXEL COORDINATES (PixelF)**
 *    - Unit: floating-point pixels
 *    - Grid: 20.0 px standard, snapped via binary-verified formula
 *    - Used by: Operation.hPos/vPos, Behavior.rect, Link.points, Comment positions
 *    - Storage: float in InterfaceData chunk
 *
 * 2. **PARAMETER GRID INDICES (ParamGridI)**
 *    - Unit: integer grid cell indices
 *    - Grid: Each cell = 20.0 px (so index 5 = pixel 100)
 *    - Used by: Parameter.hPos/vPos ONLY
 *    - Storage: int in InterfaceData chunk
 *
 * CRITICAL: These are NOT interchangeable!
 *   - Parameter positions are stored as (col, row) indices
 *   - Converting to pixels: pixel = index * 20.0
 *   - Converting to index: index = round(pixel / 20.0)
 *
 * Boundary Behavior (binary-verified from sub_4D8400):
 *   - Snap formula: (int)((rel - grid*0.5) / grid + 1) * grid + base
 *   - At exactly half-grid (10.0), snaps UP to next cell
 *   - Negative coordinates: supported, snap rounds toward zero
 */

namespace CoordinatePolicy {

//------------------------------------------------------------------------------
// Coordinate constants
//------------------------------------------------------------------------------
constexpr float GRID_SIZE = 20.0f;           // g_fHorizontalSpacing @ 0x642B0C
constexpr float PARAM_SCALE = 0.2f;          // Literal 0x3E4CCCCD (NOT used for storage)
constexpr float PARAM_GRID_UI = GRID_SIZE * PARAM_SCALE;  // 4.0 px (UI snap only)
constexpr float GRID_HALF = GRID_SIZE / 2.0f;             // 10.0 px (snap threshold)

//------------------------------------------------------------------------------
// Pixel Coordinate Functions
//------------------------------------------------------------------------------

/**
 * @brief Snap a pixel value to the 20.0 px grid (binary-verified formula)
 * @param value Raw pixel coordinate
 * @param base Optional base offset for relative snapping
 * @return Snapped pixel coordinate
 *
 * Original formula from sub_4D8400: (int)((rel - grid*0.5) / grid + 1) * grid + base
 * Note: Using floor() for consistent rounding direction with negative values.
 */
inline float SnapPixel20(float value, float base = 0.0f) {
    float rel = value - base;
    // Use floor to ensure consistent rounding for negative values
    // This rounds to nearest grid cell (value + halfGrid) then snaps
    float snapped = std::floor((rel + GRID_HALF) / GRID_SIZE) * GRID_SIZE;
    return snapped + base;
}

/**
 * @brief Snap a pixel value to a custom grid size
 * @param value Raw pixel coordinate
 * @param gridSize Grid cell size in pixels
 * @param base Optional base offset
 * @return Snapped pixel coordinate
 */
inline float SnapPixelCustom(float value, float gridSize, float base = 0.0f) {
    float rel = value - base;
    float halfGrid = gridSize / 2.0f;
    int cell = static_cast<int>((rel - halfGrid) / gridSize + 1.0f);
    return static_cast<float>(cell) * gridSize + base;
}

/**
 * @brief Check if a pixel value is valid (not NaN or infinite)
 * @param value Pixel coordinate to check
 * @return true if valid
 */
inline bool IsValidPixel(float value) {
    return std::isfinite(value);
}

/**
 * @brief Clamp a pixel coordinate to a valid range
 * @param value Pixel coordinate
 * @param minVal Minimum allowed value
 * @param maxVal Maximum allowed value
 * @return Clamped value
 */
inline float ClampPixel(float value, float minVal, float maxVal) {
    if (!std::isfinite(value)) return minVal;
    return std::max(minVal, std::min(maxVal, value));
}

//------------------------------------------------------------------------------
// Parameter Grid Index Functions
//------------------------------------------------------------------------------

/**
 * @brief Convert pixel coordinate to parameter grid index
 * @param pixel Pixel coordinate
 * @return Grid index (col or row)
 *
 * BOUNDARY ASSERTION: Input must be valid pixel value
 * Uses std::lround for IEEE 754 compliant rounding
 */
inline int PixelToParamGrid(float pixel) {
    assert(IsValidPixel(pixel) && "PixelToParamGrid: input must be finite");
    return static_cast<int>(std::lround(pixel / GRID_SIZE));
}

/**
 * @brief Convert parameter grid index to pixel coordinate
 * @param index Grid index (col or row)
 * @return Pixel coordinate (center of grid cell)
 */
inline float ParamGridToPixel(int index) {
    return static_cast<float>(index) * GRID_SIZE;
}

/**
 * @brief Check if a grid index is within reasonable bounds
 * @param index Grid index to check
 * @param maxIndex Maximum expected index (default: 卤10000 cells = 卤200000 px)
 * @return true if within bounds
 */
inline bool IsValidParamGrid(int index, int maxIndex = 10000) {
    return index >= -maxIndex && index <= maxIndex;
}

//------------------------------------------------------------------------------
// Coordinate Validation & Normalization
//------------------------------------------------------------------------------

/**
 * @brief Normalize a pixel coordinate (remove -0.0, clamp NaN/Inf)
 * @param value Input pixel value
 * @param defaultVal Default value if invalid
 * @return Normalized pixel value
 */
inline float NormalizePixel(float value, float defaultVal = 0.0f) {
    if (!std::isfinite(value)) return defaultVal;
    // Remove negative zero
    if (value == 0.0f) return 0.0f;
    return value;
}

/**
 * @brief Normalize a parameter grid index
 * @param index Input grid index
 * @param minIndex Minimum allowed index
 * @param maxIndex Maximum allowed index
 * @return Clamped index
 */
inline int NormalizeParamGrid(int index, int minIndex = -10000, int maxIndex = 10000) {
    return std::max(minIndex, std::min(maxIndex, index));
}

//------------------------------------------------------------------------------
// Boundary Assertions (for debug builds)
//------------------------------------------------------------------------------

/**
 * @brief Assert that a pixel coordinate is valid before serialization
 * @param pixel Pixel coordinate
 * @param context Description for error message (unused in release builds)
 */
inline void AssertValidPixelForWrite(float pixel, const char* /* context */ = "pixel") {
    assert(IsValidPixel(pixel) && "Invalid pixel coordinate for serialization");
    assert(pixel >= -1e6f && pixel <= 1e6f && "Pixel coordinate out of expected range");
    (void)pixel;  // Suppress warning in release builds
}

/**
 * @brief Assert that a parameter grid index is valid before serialization
 * @param index Grid index
 * @param context Description for error message (unused in release builds)
 */
inline void AssertValidParamGridForWrite(int index, const char* /* context */ = "param_grid") {
    assert(IsValidParamGrid(index) && "Parameter grid index out of expected range");
    (void)index;  // Suppress warning in release builds
}

} // namespace CoordinatePolicy

//==============================================================================
// END COORDINATE POLICY MODULE
//==============================================================================

/**
 * @class Point
 * @brief Represents a 2D point in the interface
 */
class Point {
public:
    float h = 0.0f; ///< Horizontal coordinate
    float v = 0.0f; ///< Vertical coordinate

    /**
     * @brief Default constructor
     */
    Point() = default;

    /**
     * @brief Constructor with coordinates
     * @param horizontal Horizontal coordinate
     * @param vertical Vertical coordinate
     */
    Point(float horizontal, float vertical) : h(horizontal), v(vertical) {}

    /**
     * @brief Calculates distance to another point
     * @param other The other point
     * @return The Euclidean distance
     */
    float DistanceTo(const Point &other) const {
        float dx = h - other.h;
        float dy = v - other.v;
        return std::sqrt(dx * dx + dy * dy);
    }

    /**
     * @brief Calculates squared distance to another point
     * @param other The other point
     * @return The squared Euclidean distance
     */
    float DistanceSquaredTo(const Point &other) const {
        float dx = h - other.h;
        float dy = v - other.v;
        return dx * dx + dy * dy;
    }

    // Mathematical operators
    Point operator+(const Point &other) const { return {h + other.h, v + other.v}; }
    Point operator-(const Point &other) const { return {h - other.h, v - other.v}; }
    Point operator*(float factor) const { return {h * factor, v * factor}; }
    Point operator/(float factor) const { return {h / factor, v / factor}; }

    Point &operator+=(const Point &other) {
        h += other.h;
        v += other.v;
        return *this;
    }

    Point &operator-=(const Point &other) {
        h -= other.h;
        v -= other.v;
        return *this;
    }

    Point &operator*=(float factor) {
        h *= factor;
        v *= factor;
        return *this;
    }

    Point &operator/=(float factor) {
        h /= factor;
        v /= factor;
        return *this;
    }

    bool operator==(const Point &other) const { return h == other.h && v == other.v; }
    bool operator!=(const Point &other) const { return !(*this == other); }

    /**
     * @brief Checks if the point is zero
     * @return True if both coordinates are zero
     */
    bool Zero() const {
        return h == 0.0f && v == 0.0f;
    }

    /**
     * @brief Calculates the dot product with another point
     * @param other The other point
     * @return The dot product
     */
    float Dot(const Point &other) const {
        return h * other.h + v * other.v;
    }

    /**
     * @brief Calculates the cross product with another point
     * @param other The other point
     * @return The cross product
     */
    float Cross(const Point &other) const {
        return h * other.v - v * other.h;
    }

    /**
     * @brief Returns the length of the vector
     * @return The Euclidean length
     */
    float Length() const {
        return std::sqrt(h * h + v * v);
    }

    /**
     * @brief Returns the squared length of the vector
     * @return The squared length
     */
    float LengthSquared() const {
        return h * h + v * v;
    }

    /**
     * @brief Returns a normalized version of this point
     * @return A unit vector in the same direction
     */
    Point Normalized() const {
        float len = Length();
        if (len > 0) {
            return {h / len, v / len};
        }
        return *this;
    }

    /**
     * @brief Rotates the point around the origin
     * @param angle Angle in radians
     * @return The rotated point
     */
    Point Rotated(float angle) const {
        float s = std::sin(angle);
        float c = std::cos(angle);
        return {h * c - v * s, h * s + v * c};
    }

    /**
     * @brief Calculates the angle between this point and another
     * @param other The other point
     * @return The angle in radians
     */
    float AngleTo(const Point &other) const {
        return std::atan2(Cross(other), Dot(other));
    }

    /**
     * @brief Reflects the point across a normal vector
     * @param normal The normal to reflect across
     * @return The reflected point
     */
    Point Reflected(const Point &normal) const {
        Point n = normal.Normalized();
        return *this - n * (2.0f * Dot(n));
    }

    /**
     * @brief Linearly interpolates between this point and another
     * @param other The target point
     * @param t The interpolation factor (0-1)
     * @return The interpolated point
     */
    Point Lerp(const Point &other, float t) const {
        return {h + (other.h - h) * t, v + (other.v - v) * t};
    }

    /**
     * @brief Checks if the point is approximately equal to another
     * @param other The other point
     * @param epsilon The tolerance
     * @return True if approximately equal
     */
    bool ApproximatelyEqual(const Point &other, float epsilon = 0.001f) const {
        return std::abs(h - other.h) < epsilon && std::abs(v - other.v) < epsilon;
    }
};

/**
 * @class Rect
 * @brief Represents a rectangular area in the interface
 */
class Rect {
public:
    float hPos = 0.0f;  ///< Horizontal position (left)
    float vPos = 0.0f;  ///< Vertical position (top)
    float hSize = 0.0f; ///< Width
    float vSize = 0.0f; ///< Height

    /**
     * @brief Default constructor
     */
    Rect() = default;

    /**
     * @brief Constructor with all parameters
     * @param hPosition Horizontal position
     * @param vPosition Vertical position
     * @param width Width
     * @param height Height
     */
    Rect(float hPosition, float vPosition, float width, float height)
        : hPos(hPosition), vPos(vPosition), hSize(width), vSize(height) {}

    /**
     * @brief Sets the position of the rectangle
     * @param h Horizontal position
     * @param v Vertical position
     */
    void SetPosition(float h, float v) {
        hPos = h;
        vPos = v;
    }

    /**
     * @brief Sets the size of the rectangle
     * @param width Width
     * @param height Height
     */
    void SetSize(float width, float height) {
        hSize = width;
        vSize = height;
    }

    /**
     * @brief Gets the right edge position
     * @return The right edge position
     */
    float Right() const {
        return hPos + hSize;
    }

    /**
     * @brief Gets the bottom edge position
     * @return The bottom edge position
     */
    float Bottom() const {
        return vPos + vSize;
    }

    /**
     * @brief Gets the center X coordinate
     * @return The center X coordinate
     */
    float CenterX() const {
        return hPos + hSize / 2.0f;
    }

    /**
     * @brief Gets the center Y coordinate
     * @return The center Y coordinate
     */
    float CenterY() const {
        return vPos + vSize / 2.0f;
    }

    /**
     * @brief Checks if the rectangle contains a point
     * @param x X coordinate
     * @param y Y coordinate
     * @return True if the point is inside the rectangle
     */
    bool Contains(float x, float y) const {
        return x >= hPos && x <= Right() && y >= vPos && y <= Bottom();
    }

    /**
     * @brief Checks if the rectangle contains a point
     * @param point The point to check
     * @return True if the point is inside the rectangle
     */
    bool Contains(const Point &point) const {
        return Contains(point.h, point.v);
    }

    /**
     * @brief Checks if this rectangle intersects with another
     * @param other The other rectangle
     * @return True if the rectangles intersect
     */
    bool Intersects(const Rect &other) const {
        return !(other.hPos > Right() || other.Right() < hPos ||
            other.vPos > Bottom() || other.Bottom() < vPos);
    }

    /**
     * @brief Calculates the area of intersection with another rectangle
     * @param other The other rectangle
     * @return The area of intersection (0 if no intersection)
     */
    float IntersectionArea(const Rect &other) const {
        if (!Intersects(other)) return 0.0f;

        float xOverlap = std::min(Right(), other.Right()) - std::max(hPos, other.hPos);
        float yOverlap = std::min(Bottom(), other.Bottom()) - std::max(vPos, other.vPos);

        return xOverlap * yOverlap;
    }

    /**
     * @brief Expands the rectangle to include a point
     * @param x X coordinate
     * @param y Y coordinate
     */
    void ExpandToInclude(float x, float y) {
        float newRight = std::max(Right(), x);
        float newBottom = std::max(Bottom(), y);

        hPos = std::min(hPos, x);
        vPos = std::min(vPos, y);

        hSize = newRight - hPos;
        vSize = newBottom - vPos;
    }

    /**
     * @brief Expands the rectangle to include another rectangle
     * @param other The other rectangle
     */
    void ExpandToInclude(const Rect &other) {
        float newRight = std::max(Right(), other.Right());
        float newBottom = std::max(Bottom(), other.Bottom());

        hPos = std::min(hPos, other.hPos);
        vPos = std::min(vPos, other.vPos);

        hSize = newRight - hPos;
        vSize = newBottom - vPos;
    }

    /**
     * @brief Creates a rectangle that is the union of this and another
     * @param other The other rectangle
     * @return The union rectangle
     */
    Rect Union(const Rect &other) const {
        Rect result = *this;
        result.ExpandToInclude(other);
        return result;
    }

    /**
     * @brief Creates a rectangle that is the intersection of this and another
     * @param other The other rectangle
     * @return The intersection rectangle (empty if no intersection)
     */
    Rect Intersection(const Rect &other) const {
        if (!Intersects(other)) return {0, 0, 0, 0};

        float left = std::max(hPos, other.hPos);
        float top = std::max(vPos, other.vPos);
        float right = std::min(Right(), other.Right());
        float bottom = std::min(Bottom(), other.Bottom());

        return {left, top, right - left, bottom - top};
    }

    /**
     * @brief Insets the rectangle by the specified amounts
     * @param horizontal Horizontal inset
     * @param vertical Vertical inset
     */
    void Inset(float horizontal, float vertical) {
        hPos += horizontal;
        vPos += vertical;
        hSize -= horizontal * 2;
        vSize -= vertical * 2;

        // Ensure dimensions don't go negative
        hSize = std::max(0.0f, hSize);
        vSize = std::max(0.0f, vSize);
    }

    /**
     * @brief Offsets the rectangle by the specified amounts
     * @param horizontal Horizontal offset
     * @param vertical Vertical offset
     */
    void Offset(float horizontal, float vertical) {
        hPos += horizontal;
        vPos += vertical;
    }
};

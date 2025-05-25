#pragma once

#include <glm/glm.hpp>

struct alignas(16) Vec3Q {
    alignas(16) int64_t x, y, z;

    Vec3Q() : x(0), y(0), z(0) {}
    Vec3Q(int64_t x, int64_t y, int64_t z) : x(x), y(y), z(z) {}

    // Conversion from floating point to quantized
    static Vec3Q fromFloat(const glm::vec3& v) {
        // Convert from meters to 1/128 mm
        // 1 unit = 1/128 millimeter = 0.0078125 mm
        constexpr double scale = 128000.0; // 128 units per mm * 1000 mm per meter
        return {
            static_cast<int64_t>(v.x * scale),
            static_cast<int64_t>(v.y * scale),
            static_cast<int64_t>(v.z * scale)
        };
    }

    // Conversion from quantized to floating point
    glm::vec3 toFloat() const {
        constexpr double invScale = 1.0 / 128000.0;
        return glm::vec3(
            static_cast<float>(x * invScale),
            static_cast<float>(y * invScale),
            static_cast<float>(z * invScale)
        );
    }

    // Basic vector operations
    Vec3Q operator+(const Vec3Q& other) const {
        return Vec3Q(x + other.x, y + other.y, z + other.z);
    }

    Vec3Q operator-(const Vec3Q& other) const {
        return Vec3Q(x - other.x, y - other.y, z - other.z);
    }

    Vec3Q operator*(int64_t scalar) const {
        return Vec3Q(x * scalar, y * scalar, z * scalar);
    }

    Vec3Q operator/(int64_t scalar) const {
        return Vec3Q(x / scalar, y / scalar, z / scalar);
    }

    // Comparisons (useful for sorting in spatial data structures)
    bool operator==(const Vec3Q& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator!=(const Vec3Q& other) const {
        return !(*this == other);
    }
};

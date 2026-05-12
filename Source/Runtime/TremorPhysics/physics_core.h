#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tremor::physics {

using PhysicsObjectLayer = uint32_t;

struct PhysicsBodyHandle {
    static constexpr uint64_t InvalidValue = std::numeric_limits<uint64_t>::max();

    uint64_t value = InvalidValue;

    constexpr PhysicsBodyHandle() = default;
    constexpr explicit PhysicsBodyHandle(uint64_t rawValue) : value(rawValue) {}

    [[nodiscard]] constexpr bool IsInvalid() const { return value == InvalidValue; }
    [[nodiscard]] constexpr explicit operator bool() const { return !IsInvalid(); }
    [[nodiscard]] constexpr uint64_t raw() const { return value; }

    friend constexpr bool operator==(PhysicsBodyHandle, PhysicsBodyHandle) = default;
};

struct PhysicsBody {
    PhysicsBodyHandle bodyId{};
    bool isKinematic = false;
};

namespace DefaultPhysicsLayers {
    static constexpr PhysicsObjectLayer Static = 0;
    static constexpr PhysicsObjectLayer Dynamic = 1;
    static constexpr PhysicsObjectLayer Player = 2;
    static constexpr PhysicsObjectLayer Enemy = 3;
    static constexpr PhysicsObjectLayer Projectile = 4;
    static constexpr PhysicsObjectLayer Pickup = 5;
    static constexpr PhysicsObjectLayer Count = 6;
}

struct PhysicsLayerConfig {
    struct Layer {
        std::string name;
        PhysicsObjectLayer broadPhase = 0;
    };

    std::vector<Layer> layers;
    std::vector<std::vector<bool>> objectCollisions;
    std::vector<std::vector<bool>> broadPhaseCollisions;

    static PhysicsLayerConfig makeDefault();
};

class PhysicsLayerConfigBuilder {
public:
    PhysicsLayerConfigBuilder();

    void clear();
    bool empty() const;
    bool defineLayer(std::string_view name, std::string_view broadPhaseName = {});
    bool allowCollision(std::string_view left, std::string_view right);
    bool blockCollision(std::string_view left, std::string_view right);
    std::optional<PhysicsObjectLayer> findLayer(std::string_view nameOrNumber) const;
    PhysicsLayerConfig build() const;

private:
    struct DraftLayer {
        std::string name;
        std::string broadPhaseName;
    };

    void resizeCollisionMatrix();
    bool setCollision(std::string_view left, std::string_view right, bool shouldCollide);

    std::vector<DraftLayer> layers_;
    std::vector<std::vector<bool>> objectCollisions_;
};

} // namespace tremor::physics

#pragma once

#include "flecs_interpreter.h"
#include "jolt_physics_world.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include <glm/glm.hpp>

namespace tremor::physics {

using PhysicsBodyHandle = uint64_t;

struct ScriptPhysicsBody {
    PhysicsBodyHandle handle = 0;
    bool isKinematic = false;
};

class PhysicsInteropAdapter {
public:
    virtual ~PhysicsInteropAdapter() = default;

    virtual std::optional<PhysicsBodyHandle> createDynamicCapsule(
        const glm::vec3& position,
        float radius,
        float height,
        std::string_view layer
    ) = 0;

    virtual std::optional<PhysicsBodyHandle> createKinematicBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        std::string_view layer
    ) = 0;

    virtual std::optional<PhysicsBodyHandle> createStaticBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        std::string_view layer
    ) = 0;

    virtual void removeBody(PhysicsBodyHandle handle) = 0;
    virtual bool setPosition(PhysicsBodyHandle handle, const glm::vec3& position) = 0;
    virtual bool setVelocity(PhysicsBodyHandle handle, const glm::vec3& velocity) = 0;
    virtual bool addImpulse(PhysicsBodyHandle handle, const glm::vec3& impulse) = 0;
    virtual bool addForce(PhysicsBodyHandle handle, const glm::vec3& force) = 0;
};

void registerPhysicsInteropCommands(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    PhysicsInteropAdapter& adapter
);

void registerPhysicsLayerConfigCommands(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    PhysicsLayerConfigBuilder& layerConfig
);

} // namespace tremor::physics

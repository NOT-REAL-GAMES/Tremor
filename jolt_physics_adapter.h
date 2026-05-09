#pragma once

#include "jolt_physics_world.h"
#include "physics_interop.h"

namespace tremor::physics {

class JoltPhysicsAdapter final : public PhysicsInteropAdapter {
public:
    explicit JoltPhysicsAdapter(JoltPhysicsWorld& physicsWorld);

    std::optional<PhysicsBodyHandle> createDynamicCapsule(
        const glm::vec3& position,
        float radius,
        float height,
        std::string_view layer
    ) override;

    std::optional<PhysicsBodyHandle> createKinematicBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        std::string_view layer
    ) override;

    std::optional<PhysicsBodyHandle> createStaticBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        std::string_view layer
    ) override;

    void removeBody(PhysicsBodyHandle handle) override;
    bool setPosition(PhysicsBodyHandle handle, const glm::vec3& position) override;
    bool setVelocity(PhysicsBodyHandle handle, const glm::vec3& velocity) override;
    bool addImpulse(PhysicsBodyHandle handle, const glm::vec3& impulse) override;
    bool addForce(PhysicsBodyHandle handle, const glm::vec3& force) override;

private:
    static PhysicsBodyHandle toHandle(JPH::BodyID bodyId);
    static JPH::BodyID toBodyId(PhysicsBodyHandle handle);

    JoltPhysicsWorld& physicsWorld_;
};

} // namespace tremor::physics

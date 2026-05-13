#pragma once

#include "Source/Runtime/TremorPhysics/physics_backend.h"

#include <memory>

namespace tremor::physics {

class PhysXPhysicsWorld final : public PhysicsWorldBackend {
public:
    explicit PhysXPhysicsWorld(PhysicsSettings settings = {});
    ~PhysXPhysicsWorld() override;

    bool initialize() override;
    void shutdown() override;
    void update(float deltaTime) override;

    PhysicsBodyHandle createDynamicCapsule(
        const glm::vec3& position,
        float radius,
        float height,
        PhysicsObjectLayer layer = DefaultPhysicsLayers::Dynamic
    ) override;

    PhysicsBodyHandle createKinematicBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        PhysicsObjectLayer layer = DefaultPhysicsLayers::Dynamic
    ) override;

    PhysicsBodyHandle createStaticBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        PhysicsObjectLayer layer = DefaultPhysicsLayers::Static
    ) override;

    glm::vec3 getBodyPosition(PhysicsBodyHandle bodyId) const override;
    void setBodyPosition(PhysicsBodyHandle bodyId, const glm::vec3& position) override;
    glm::vec3 getBodyVelocity(PhysicsBodyHandle bodyId) const override;
    void setBodyVelocity(PhysicsBodyHandle bodyId, const glm::vec3& velocity) override;
    void addImpulse(PhysicsBodyHandle bodyId, const glm::vec3& impulse) override;
    void addForce(PhysicsBodyHandle bodyId, const glm::vec3& force) override;
    bool isBodySleeping(PhysicsBodyHandle bodyId) const override;
    void wakeBody(PhysicsBodyHandle bodyId) override;
    void sleepBody(PhysicsBodyHandle bodyId) override;
    void setBodySleepingAllowed(PhysicsBodyHandle bodyId, bool allowed) override;
    void removeBody(PhysicsBodyHandle bodyId) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tremor::physics

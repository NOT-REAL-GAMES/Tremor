#pragma once

#include "Source/Runtime/TremorPhysics/physics_core.h"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace tremor::physics {

struct PhysicsSettings {
    uint32_t maxBodies = 65536;
    uint32_t numBodyMutexes = 0;
    uint32_t maxBodyPairs = 65536;
    uint32_t maxContactConstraints = 10240;
    uint32_t maxPhysicsJobs = 2048;
    uint32_t maxPhysicsBarriers = 8;
    uint32_t collisionSteps = 1;
    uint32_t tempAllocatorBytes = 32 * 1024 * 1024;
    glm::vec3 gravity{0.0f, 9.81f, 0.0f};
    PhysicsLayerConfig layers = PhysicsLayerConfig::makeDefault();
};

enum class PhysicsBackendKind {
    Jolt,
    PhysX
};

enum class PhysicsContactPhase : uint8_t {
    Added,
    Persisted,
    Removed
};

struct PhysicsContactEvent {
    PhysicsBodyHandle leftBody{};
    PhysicsBodyHandle rightBody{};
    PhysicsContactPhase phase = PhysicsContactPhase::Added;
};

class PhysicsWorldBackend {
public:
    using ContactCallback = std::function<void(const PhysicsContactEvent&)>;

    explicit PhysicsWorldBackend(PhysicsSettings settings = {});
    virtual ~PhysicsWorldBackend() = default;

    PhysicsWorldBackend(const PhysicsWorldBackend&) = delete;
    PhysicsWorldBackend& operator=(const PhysicsWorldBackend&) = delete;

    virtual bool initialize() = 0;
    bool Initialize() { return initialize(); }

    virtual void shutdown() = 0;
    void Shutdown() { shutdown(); }

    virtual void update(float deltaTime) = 0;
    void Update(float deltaTime) { update(deltaTime); }

    virtual PhysicsBodyHandle createDynamicCapsule(
        const glm::vec3& position,
        float radius,
        float height,
        PhysicsObjectLayer layer = DefaultPhysicsLayers::Dynamic
    ) = 0;
    PhysicsBodyHandle CreateDynamicBody(
        const glm::vec3& position,
        float radius,
        float height,
        PhysicsObjectLayer layer = DefaultPhysicsLayers::Dynamic
    ) {
        return createDynamicCapsule(position, radius, height, layer);
    }

    virtual PhysicsBodyHandle createKinematicBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        PhysicsObjectLayer layer = DefaultPhysicsLayers::Dynamic
    ) = 0;
    PhysicsBodyHandle CreateKinematicBody(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        PhysicsObjectLayer layer = DefaultPhysicsLayers::Dynamic
    ) {
        return createKinematicBox(position, halfExtents, layer);
    }

    virtual PhysicsBodyHandle createStaticBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        PhysicsObjectLayer layer = DefaultPhysicsLayers::Static
    ) = 0;
    PhysicsBodyHandle CreateStaticBody(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        PhysicsObjectLayer layer = DefaultPhysicsLayers::Static
    ) {
        return createStaticBox(position, halfExtents, layer);
    }

    virtual glm::vec3 getBodyPosition(PhysicsBodyHandle bodyId) const = 0;
    glm::vec3 GetBodyPosition(PhysicsBodyHandle bodyId) const { return getBodyPosition(bodyId); }

    virtual void setBodyPosition(PhysicsBodyHandle bodyId, const glm::vec3& position) = 0;
    void SetBodyPosition(PhysicsBodyHandle bodyId, const glm::vec3& position) { setBodyPosition(bodyId, position); }

    virtual glm::vec3 getBodyVelocity(PhysicsBodyHandle bodyId) const = 0;
    glm::vec3 GetBodyVelocity(PhysicsBodyHandle bodyId) const { return getBodyVelocity(bodyId); }

    virtual void setBodyVelocity(PhysicsBodyHandle bodyId, const glm::vec3& velocity) = 0;
    void SetBodyVelocity(PhysicsBodyHandle bodyId, const glm::vec3& velocity) { setBodyVelocity(bodyId, velocity); }

    virtual void addImpulse(PhysicsBodyHandle bodyId, const glm::vec3& impulse) = 0;
    void AddImpulse(PhysicsBodyHandle bodyId, const glm::vec3& impulse) { addImpulse(bodyId, impulse); }

    virtual void addForce(PhysicsBodyHandle bodyId, const glm::vec3& force) = 0;
    void AddForce(PhysicsBodyHandle bodyId, const glm::vec3& force) { addForce(bodyId, force); }

    virtual void removeBody(PhysicsBodyHandle bodyId) = 0;
    void RemoveBody(PhysicsBodyHandle bodyId) { removeBody(bodyId); }

    virtual std::optional<PhysicsObjectLayer> findLayer(std::string_view nameOrNumber) const;
    void setContactCallback(ContactCallback callback);
    void clearContactCallback();
    void emitContactEvent(const PhysicsContactEvent& event) const;

    [[nodiscard]] const PhysicsSettings& settings() const { return settings_; }

protected:
    PhysicsSettings settings_;
    ContactCallback contactCallback_;
};

std::unique_ptr<PhysicsWorldBackend> createPhysicsWorldBackend(
    PhysicsBackendKind backendKind,
    PhysicsSettings settings = {}
);

} // namespace tremor::physics

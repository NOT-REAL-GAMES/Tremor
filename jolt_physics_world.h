#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "Source/Runtime/TremorPhysics/physics_backend.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace tremor::physics {

using JoltPhysicsBody = PhysicsBody;
using JoltPhysicsSettings = PhysicsSettings;

class JoltPhysicsWorld;

class ConfigurableBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    explicit ConfigurableBroadPhaseLayerInterface(const PhysicsLayerConfig& config);

    JPH::uint GetNumBroadPhaseLayers() const override;
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override;

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif

private:
    std::vector<JPH::BroadPhaseLayer> objectToBroadPhase_;
    std::vector<std::string> broadPhaseNames_;
};

class ConfigurableObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    explicit ConfigurableObjectVsBroadPhaseLayerFilter(const PhysicsLayerConfig& config);

    bool ShouldCollide(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer) const override;

private:
    std::vector<std::vector<bool>> collisions_;
};

class ConfigurableObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    explicit ConfigurableObjectLayerPairFilter(const PhysicsLayerConfig& config);

    bool ShouldCollide(JPH::ObjectLayer left, JPH::ObjectLayer right) const override;

private:
    std::vector<std::vector<bool>> collisions_;
};

class LoggingContactListener : public JPH::ContactListener {
public:
    explicit LoggingContactListener(JoltPhysicsWorld& owner);

    void OnContactAdded(
        const JPH::Body& left,
        const JPH::Body& right,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings& settings
    ) override;

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override;

private:
    JoltPhysicsWorld& owner_;
};

class JoltPhysicsWorld final : public PhysicsWorldBackend {
public:
    explicit JoltPhysicsWorld(JoltPhysicsSettings settings = {});
    ~JoltPhysicsWorld() override;

    JoltPhysicsWorld(const JoltPhysicsWorld&) = delete;
    JoltPhysicsWorld& operator=(const JoltPhysicsWorld&) = delete;

    bool initialize() override;
    bool Initialize() { return initialize(); }

    void shutdown() override;
    void Shutdown() { shutdown(); }

    void update(float deltaTime) override;
    void Update(float deltaTime) { update(deltaTime); }

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
    void removeBody(PhysicsBodyHandle bodyId) override;

    JPH::PhysicsSystem* getSystem() { return physicsSystem_.get(); }
    JPH::PhysicsSystem* GetSystem() { return getSystem(); }
    JPH::BodyInterface& getBodyInterface() { return physicsSystem_->GetBodyInterface(); }
    JPH::BodyInterface& GetBodyInterface() { return getBodyInterface(); }

private:
    friend class LoggingContactListener;

    static PhysicsBodyHandle toHandle(JPH::BodyID bodyId);
    static JPH::BodyID toBodyId(PhysicsBodyHandle handle);
    static JPH::RVec3 toJoltPosition(const glm::vec3& value);
    static JPH::Vec3 toJoltVector(const glm::vec3& value);
    static glm::vec3 fromJolt(const JPH::RVec3& value);
    static glm::vec3 fromJolt(const JPH::Vec3& value);

    bool joltRegistered_ = false;

    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
    std::unique_ptr<ConfigurableBroadPhaseLayerInterface> broadPhaseLayerInterface_;
    std::unique_ptr<ConfigurableObjectVsBroadPhaseLayerFilter> objectVsBroadPhaseLayerFilter_;
    std::unique_ptr<ConfigurableObjectLayerPairFilter> objectLayerPairFilter_;
    std::unique_ptr<LoggingContactListener> contactListener_;
};

} // namespace tremor::physics

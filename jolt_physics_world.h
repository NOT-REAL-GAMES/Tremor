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

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tremor::physics {

namespace DefaultPhysicsLayers {
    static constexpr JPH::ObjectLayer Static = 0;
    static constexpr JPH::ObjectLayer Dynamic = 1;
    static constexpr JPH::ObjectLayer Player = 2;
    static constexpr JPH::ObjectLayer Enemy = 3;
    static constexpr JPH::ObjectLayer Projectile = 4;
    static constexpr JPH::ObjectLayer Pickup = 5;
    static constexpr JPH::ObjectLayer Count = 6;
}

struct JoltPhysicsBody {
    JPH::BodyID bodyId;
    bool isKinematic = false;

    JoltPhysicsBody() = default;
    JoltPhysicsBody(JPH::BodyID id, bool kinematic = false)
        : bodyId(id), isKinematic(kinematic) {
    }
};

struct PhysicsLayerConfig {
    struct Layer {
        std::string name;
        JPH::BroadPhaseLayer broadPhase;
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
    std::optional<JPH::ObjectLayer> findLayer(std::string_view nameOrNumber) const;
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

struct JoltPhysicsSettings {
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
    void OnContactAdded(
        const JPH::Body& left,
        const JPH::Body& right,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings& settings
    ) override;

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override;
};

class JoltPhysicsWorld {
public:
    explicit JoltPhysicsWorld(JoltPhysicsSettings settings = {});
    ~JoltPhysicsWorld();

    JoltPhysicsWorld(const JoltPhysicsWorld&) = delete;
    JoltPhysicsWorld& operator=(const JoltPhysicsWorld&) = delete;

    bool initialize();
    bool Initialize() { return initialize(); }

    void shutdown();
    void Shutdown() { shutdown(); }

    void update(float deltaTime);
    void Update(float deltaTime) { update(deltaTime); }

    JPH::BodyID createDynamicCapsule(
        const glm::vec3& position,
        float radius,
        float height,
        JPH::ObjectLayer layer = DefaultPhysicsLayers::Dynamic
    );
    JPH::BodyID CreateDynamicBody(
        const glm::vec3& position,
        float radius,
        float height,
        JPH::ObjectLayer layer = DefaultPhysicsLayers::Dynamic
    ) {
        return createDynamicCapsule(position, radius, height, layer);
    }

    JPH::BodyID createKinematicBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        JPH::ObjectLayer layer = DefaultPhysicsLayers::Dynamic
    );
    JPH::BodyID CreateKinematicBody(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        JPH::ObjectLayer layer = DefaultPhysicsLayers::Dynamic
    ) {
        return createKinematicBox(position, halfExtents, layer);
    }

    JPH::BodyID createStaticBox(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        JPH::ObjectLayer layer = DefaultPhysicsLayers::Static
    );
    JPH::BodyID CreateStaticBody(
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        JPH::ObjectLayer layer = DefaultPhysicsLayers::Static
    ) {
        return createStaticBox(position, halfExtents, layer);
    }

    glm::vec3 getBodyPosition(JPH::BodyID bodyId) const;
    glm::vec3 GetBodyPosition(JPH::BodyID bodyId) const { return getBodyPosition(bodyId); }

    void setBodyPosition(JPH::BodyID bodyId, const glm::vec3& position);
    void SetBodyPosition(JPH::BodyID bodyId, const glm::vec3& position) { setBodyPosition(bodyId, position); }

    glm::vec3 getBodyVelocity(JPH::BodyID bodyId) const;
    glm::vec3 GetBodyVelocity(JPH::BodyID bodyId) const { return getBodyVelocity(bodyId); }

    void setBodyVelocity(JPH::BodyID bodyId, const glm::vec3& velocity);
    void SetBodyVelocity(JPH::BodyID bodyId, const glm::vec3& velocity) { setBodyVelocity(bodyId, velocity); }

    void addImpulse(JPH::BodyID bodyId, const glm::vec3& impulse);
    void AddImpulse(JPH::BodyID bodyId, const glm::vec3& impulse) { addImpulse(bodyId, impulse); }

    void addForce(JPH::BodyID bodyId, const glm::vec3& force);
    void AddForce(JPH::BodyID bodyId, const glm::vec3& force) { addForce(bodyId, force); }

    void removeBody(JPH::BodyID bodyId);
    void RemoveBody(JPH::BodyID bodyId) { removeBody(bodyId); }

    JPH::PhysicsSystem* getSystem() { return physicsSystem_.get(); }
    JPH::PhysicsSystem* GetSystem() { return getSystem(); }
    JPH::BodyInterface& getBodyInterface() { return physicsSystem_->GetBodyInterface(); }
    JPH::BodyInterface& GetBodyInterface() { return getBodyInterface(); }

    std::optional<JPH::ObjectLayer> findLayer(std::string_view nameOrNumber) const;

private:
    static JPH::RVec3 toJoltPosition(const glm::vec3& value);
    static JPH::Vec3 toJoltVector(const glm::vec3& value);
    static glm::vec3 fromJolt(const JPH::RVec3& value);
    static glm::vec3 fromJolt(const JPH::Vec3& value);

    JoltPhysicsSettings settings_;
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

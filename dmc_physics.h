#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterMask.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <unordered_map>

// Jolt uses its own namespace
using namespace JPH;
using namespace JPH::literals;

namespace DMCSurvivors {

// Layer definitions for collision filtering
namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING = 1;
    static constexpr ObjectLayer PLAYER = 2;
    static constexpr ObjectLayer ENEMY = 3;
    static constexpr ObjectLayer PROJECTILE = 4;
    static constexpr ObjectLayer PICKUP = 5;
    static constexpr ObjectLayer NUM_LAYERS = 6;
};

// Broad phase layer definitions
namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr BroadPhaseLayer PLAYER(2);
    static constexpr BroadPhaseLayer ENEMY(3);
    static constexpr BroadPhaseLayer PROJECTILE(4);
    static constexpr uint NUM_LAYERS = 5;
};

// BroadPhaseLayerInterface implementation
class DMCBroadPhaseLayerInterface final : public BroadPhaseLayerInterface {
public:
    DMCBroadPhaseLayerInterface() {
        // Map object layers to broadphase layers
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::PLAYER] = BroadPhaseLayers::PLAYER;
        mObjectToBroadPhase[Layers::ENEMY] = BroadPhaseLayers::ENEMY;
        mObjectToBroadPhase[Layers::PROJECTILE] = BroadPhaseLayers::PROJECTILE;
        mObjectToBroadPhase[Layers::PICKUP] = BroadPhaseLayers::MOVING;
    }

    virtual uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return mObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override {
        switch ((BroadPhaseLayer::Type)inLayer) {
            case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::PLAYER:		return "PLAYER";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::ENEMY:		return "ENEMY";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::PROJECTILE:	return "PROJECTILE";
            default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif

private:
    BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

// Object vs Broad Phase Layer filter
class DMCObjectVsBroadPhaseLayerFilter : public ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
        case Layers::NON_MOVING:
            return inLayer2 == BroadPhaseLayers::MOVING || inLayer2 == BroadPhaseLayers::PLAYER ||
                   inLayer2 == BroadPhaseLayers::ENEMY || inLayer2 == BroadPhaseLayers::PROJECTILE;
        case Layers::MOVING:
            return true; // Moving collides with everything
        case Layers::PLAYER:
            return inLayer2 != BroadPhaseLayers::PROJECTILE; // Player doesn't collide with projectiles
        case Layers::ENEMY:
            return inLayer2 != BroadPhaseLayers::ENEMY; // Enemies don't collide with each other
        case Layers::PROJECTILE:
            return inLayer2 == BroadPhaseLayers::ENEMY || inLayer2 == BroadPhaseLayers::NON_MOVING; // Projectiles hit enemies and walls
        case Layers::PICKUP:
            return inLayer2 == BroadPhaseLayers::PLAYER; // Pickups only collide with player
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

// Object layer filter
class DMCObjectLayerPairFilter : public ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override {
        switch (inObject1) {
        case Layers::NON_MOVING:
            return inObject2 == Layers::MOVING || inObject2 == Layers::PLAYER ||
                   inObject2 == Layers::ENEMY || inObject2 == Layers::PROJECTILE;
        case Layers::MOVING:
            return true; // Moving collides with everything
        case Layers::PLAYER:
            return inObject2 != Layers::PROJECTILE; // Player doesn't collide with own projectiles
        case Layers::ENEMY:
            return inObject2 != Layers::ENEMY; // Enemies don't collide with each other
        case Layers::PROJECTILE:
            return inObject2 == Layers::ENEMY || inObject2 == Layers::NON_MOVING; // Projectiles hit enemies and walls
        case Layers::PICKUP:
            return inObject2 == Layers::PLAYER; // Pickups only collide with player
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

// Contact listener for handling collisions
class DMCContactListener : public ContactListener {
public:
    // Called when two bodies start colliding
    virtual void OnContactAdded(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override;

    // Called when two bodies stop colliding
    virtual void OnContactRemoved(const SubShapeIDPair &inSubShapePair) override;
};

// Physics component for ECS entities
struct PhysicsBody {
    BodyID bodyId;
    bool isKinematic = false;

    PhysicsBody() = default;
    PhysicsBody(BodyID id, bool kinematic = false) : bodyId(id), isKinematic(kinematic) {}
};

// Main physics world manager
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    // Initialize the physics world
    bool Initialize();

    // Shutdown and cleanup
    void Shutdown();

    // Update physics simulation
    void Update(float deltaTime);

    // Create a dynamic body (for characters)
    BodyID CreateDynamicBody(const glm::vec3& position, float radius, float height, ObjectLayer layer = Layers::MOVING);

    // Create a kinematic body (for moving platforms)
    BodyID CreateKinematicBody(const glm::vec3& position, const glm::vec3& halfExtents, ObjectLayer layer = Layers::MOVING);

    // Create a static body (for level geometry)
    BodyID CreateStaticBody(const glm::vec3& position, const glm::vec3& halfExtents);

    // Get/Set body position
    glm::vec3 GetBodyPosition(BodyID bodyId) const;
    void SetBodyPosition(BodyID bodyId, const glm::vec3& position);

    // Get/Set body velocity
    glm::vec3 GetBodyVelocity(BodyID bodyId) const;
    void SetBodyVelocity(BodyID bodyId, const glm::vec3& velocity);

    // Apply forces
    void AddImpulse(BodyID bodyId, const glm::vec3& impulse);
    void AddForce(BodyID bodyId, const glm::vec3& force);

    // Remove a body
    void RemoveBody(BodyID bodyId);

    // Get the physics system (for advanced operations)
    PhysicsSystem* GetSystem() { return mPhysicsSystem.get(); }
    BodyInterface& GetBodyInterface() { return mPhysicsSystem->GetBodyInterface(); }

private:
    // Jolt systems
    std::unique_ptr<JobSystemThreadPool> mJobSystem;
    std::unique_ptr<TempAllocatorImpl> mTempAllocator;
    std::unique_ptr<PhysicsSystem> mPhysicsSystem;

    // Layer interfaces
    std::unique_ptr<DMCBroadPhaseLayerInterface> mBroadPhaseLayerInterface;
    std::unique_ptr<DMCObjectVsBroadPhaseLayerFilter> mObjectVsBroadPhaseLayerFilter;
    std::unique_ptr<DMCObjectLayerPairFilter> mObjectLayerPairFilter;
    std::unique_ptr<DMCContactListener> mContactListener;

    // Configuration
    const uint cMaxBodies = 65536;
    const uint cNumBodyMutexes = 0; // 0 = auto detect
    const uint cMaxBodyPairs = 65536;
    const uint cMaxContactConstraints = 10240;
    const uint cMaxPhysicsJobs = 2048;
    const uint cMaxPhysicsBarriers = 8;

    // Helper functions (compatible with both single and double precision)
    static RVec3 ToJoltPosition(const glm::vec3& v) { return RVec3(static_cast<Real>(v.x), static_cast<Real>(v.y), static_cast<Real>(v.z)); }
    static Vec3 ToJoltVector(const glm::vec3& v) { return Vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)); }
    static glm::vec3 FromJolt(const RVec3& v) { return glm::vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())); }
    static glm::vec3 FromJolt(const Vec3& v) { return glm::vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())); }
    static Quat ToJolt(const glm::quat& q) { return Quat(static_cast<Real>(q.x), static_cast<Real>(q.y), static_cast<Real>(q.z), static_cast<Real>(q.w)); }
    static glm::quat FromJolt(const Quat& q) { return glm::quat(static_cast<float>(q.GetW()), static_cast<float>(q.GetX()), static_cast<float>(q.GetY()), static_cast<float>(q.GetZ())); }
};

} // namespace DMCSurvivors
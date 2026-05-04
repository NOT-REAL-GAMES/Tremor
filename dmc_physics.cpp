#include "main.h"
#include "dmc_physics.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Core/Memory.h>

namespace DMCSurvivors {

// Contact listener implementation
void DMCContactListener::OnContactAdded(const Body &inBody1, const Body &inBody2,
                                        const ContactManifold &inManifold, ContactSettings &ioSettings) {
    // This will be connected to the ECS for collision events
    // For now, just log collisions
    Logger::get().debug("Contact added between bodies {} and {}",
                       inBody1.GetID().GetIndex(), inBody2.GetID().GetIndex());
}

void DMCContactListener::OnContactRemoved(const SubShapeIDPair &inSubShapePair) {
    // Handle contact removal if needed
}

// Physics world implementation
PhysicsWorld::PhysicsWorld() {
    // Register Jolt physics types
    RegisterDefaultAllocator();

    // Create a factory
    Factory::sInstance = new Factory();

    // Register all Jolt physics types
    RegisterTypes();
}

PhysicsWorld::~PhysicsWorld() {
    Shutdown();

    // Unregister types
    UnregisterTypes();

    // Destroy the factory
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

bool PhysicsWorld::Initialize() {
    Logger::get().info("🚀 Initializing Jolt Physics for DMC Survivors...");

    // Create the job system (uses all available cores)
    mJobSystem = std::make_unique<JobSystemThreadPool>(
        cMaxPhysicsJobs,
        cMaxPhysicsBarriers,
        std::thread::hardware_concurrency() - 1);  // Leave one core for main thread

    // Create temp allocator
    mTempAllocator = std::make_unique<TempAllocatorImpl>(32 * 1024 * 1024);  // 32MB

    // Create layer interfaces
    mBroadPhaseLayerInterface = std::make_unique<DMCBroadPhaseLayerInterface>();
    mObjectVsBroadPhaseLayerFilter = std::make_unique<DMCObjectVsBroadPhaseLayerFilter>();
    mObjectLayerPairFilter = std::make_unique<DMCObjectLayerPairFilter>();

    // Create the physics system
    mPhysicsSystem = std::make_unique<PhysicsSystem>();
    mPhysicsSystem->Init(
        cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        *mBroadPhaseLayerInterface,
        *mObjectVsBroadPhaseLayerFilter,
        *mObjectLayerPairFilter);

    // Create and set contact listener
    mContactListener = std::make_unique<DMCContactListener>();
    mPhysicsSystem->SetContactListener(mContactListener.get());

    // Set gravity (stronger for game feel)
    mPhysicsSystem->SetGravity(Vec3(0.0f, 9.81f, 0.0f));

    Logger::get().info("✅ Jolt Physics initialized successfully!");
    Logger::get().info("   - Using {} CPU threads", std::thread::hardware_concurrency() - 1);
    Logger::get().info("   - Max bodies: {}", cMaxBodies);
    Logger::get().info("   - Gravity: (0, -9.81, 0)");
#ifdef JPH_DOUBLE_PRECISION
    Logger::get().info("   - Precision: DOUBLE (64-bit)");
#else
    Logger::get().info("   - Precision: SINGLE (32-bit)");
#endif

    return true;
}

void PhysicsWorld::Shutdown() {
    if (mPhysicsSystem) {
        // Clean up all bodies
        BodyInterface &bodyInterface = mPhysicsSystem->GetBodyInterface();

        // Get all bodies and remove them
        BodyIDVector bodies;
        mPhysicsSystem->GetBodies(bodies);

        for (BodyID bodyId : bodies) {
            bodyInterface.RemoveBody(bodyId);
            bodyInterface.DestroyBody(bodyId);
        }

        Logger::get().info("🛑 Jolt Physics shut down");
    }

    mPhysicsSystem.reset();
    mContactListener.reset();
    mObjectLayerPairFilter.reset();
    mObjectVsBroadPhaseLayerFilter.reset();
    mBroadPhaseLayerInterface.reset();
    mTempAllocator.reset();
    mJobSystem.reset();
}

void PhysicsWorld::Update(float deltaTime) {
    if (!mPhysicsSystem) return;

    // Jolt physics uses a fixed timestep internally
    // We'll do 1 collision step per frame
    const int collisionSteps = 1;

    // Step the world
    mPhysicsSystem->Update(deltaTime, collisionSteps, mTempAllocator.get(), mJobSystem.get());
}

BodyID PhysicsWorld::CreateDynamicBody(const glm::vec3& position, float radius, float height, ObjectLayer layer) {
    BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();

    // Create a capsule shape for characters (good for humanoid characters)
    RefConst<Shape> capsuleShape = new CapsuleShape(static_cast<Real>(height * 0.5f), static_cast<Real>(radius));

    // Create the body
    BodyCreationSettings bodySettings(
        capsuleShape.GetPtr(),
        ToJoltPosition(position),
        Quat::sIdentity(),
        EMotionType::Dynamic,
        layer
    );

    // Character-specific settings
    bodySettings.mFriction = static_cast<Real>(0.5f);
    bodySettings.mRestitution = static_cast<Real>(0.0f);  // No bouncing
    bodySettings.mLinearDamping = static_cast<Real>(0.0001f);  // Minimal damping for faster falling
    bodySettings.mAngularDamping = static_cast<Real>(0.05f);
    bodySettings.mGravityFactor = static_cast<Real>(1.0f);

    // Prevent rotation on X and Z axes (keep character upright)
    bodySettings.mAllowedDOFs = EAllowedDOFs::TranslationX | EAllowedDOFs::TranslationY | EAllowedDOFs::TranslationZ | EAllowedDOFs::RotationY;

    // Create the body
    Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        Logger::get().error("Failed to create dynamic body!");
        return BodyID();
    }

    // Add to physics world
    bodyInterface.AddBody(body->GetID(), EActivation::Activate);

    Logger::get().debug("Created dynamic body at ({}, {}, {})", position.x, position.y, position.z);
    return body->GetID();
}

BodyID PhysicsWorld::CreateKinematicBody(const glm::vec3& position, const glm::vec3& halfExtents, ObjectLayer layer) {
    BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();

    // Create a box shape
    RefConst<Shape> boxShape = new BoxShape(ToJoltVector(halfExtents));

    // Create the body
    BodyCreationSettings bodySettings(
        boxShape.GetPtr(),
        ToJoltPosition(position),
        Quat::sIdentity(),
        EMotionType::Kinematic,
        layer
    );

    bodySettings.mFriction = static_cast<Real>(0.5f);
    bodySettings.mRestitution = static_cast<Real>(0.0f);

    // Create the body
    Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        Logger::get().error("Failed to create kinematic body!");
        return BodyID();
    }

    // Add to physics world
    bodyInterface.AddBody(body->GetID(), EActivation::Activate);

    return body->GetID();
}

BodyID PhysicsWorld::CreateStaticBody(const glm::vec3& position, const glm::vec3& halfExtents) {
    BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();

    // Create a box shape
    RefConst<Shape> boxShape = new BoxShape(ToJoltVector(halfExtents));

    // Create the body
    BodyCreationSettings bodySettings(
        boxShape.GetPtr(),
        ToJoltPosition(position),
        Quat::sIdentity(),
        EMotionType::Static,
        Layers::NON_MOVING
    );

    bodySettings.mFriction = static_cast<Real>(0.5f);
    bodySettings.mRestitution = static_cast<Real>(0.0f);

    // Create the body
    Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        Logger::get().error("Failed to create static body!");
        return BodyID();
    }

    // Add to physics world (static bodies don't need activation)
    bodyInterface.AddBody(body->GetID(), EActivation::DontActivate);

    return body->GetID();
}

glm::vec3 PhysicsWorld::GetBodyPosition(BodyID bodyId) const {
    if (!mPhysicsSystem) return glm::vec3(0.0f);

    const BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();
    return FromJolt(bodyInterface.GetCenterOfMassPosition(bodyId));
}

void PhysicsWorld::SetBodyPosition(BodyID bodyId, const glm::vec3& position) {
    if (!mPhysicsSystem) return;

    BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();
    bodyInterface.SetPosition(bodyId, ToJoltPosition(position), EActivation::Activate);
}

glm::vec3 PhysicsWorld::GetBodyVelocity(BodyID bodyId) const {
    if (!mPhysicsSystem) return glm::vec3(0.0f);

    const BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();
    return FromJolt(bodyInterface.GetLinearVelocity(bodyId));
}

void PhysicsWorld::SetBodyVelocity(BodyID bodyId, const glm::vec3& velocity) {
    if (!mPhysicsSystem) return;

    BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();
    bodyInterface.SetLinearVelocity(bodyId, ToJoltVector(velocity));
}

void PhysicsWorld::AddImpulse(BodyID bodyId, const glm::vec3& impulse) {
    if (!mPhysicsSystem) return;

    BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();
    bodyInterface.AddImpulse(bodyId, ToJoltVector(impulse));
}

void PhysicsWorld::AddForce(BodyID bodyId, const glm::vec3& force) {
    if (!mPhysicsSystem) return;

    BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();
    bodyInterface.AddForce(bodyId, ToJoltVector(force));
}

void PhysicsWorld::RemoveBody(BodyID bodyId) {
    if (!mPhysicsSystem) return;

    BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();
    bodyInterface.RemoveBody(bodyId);
    bodyInterface.DestroyBody(bodyId);
}

} // namespace DMCSurvivors
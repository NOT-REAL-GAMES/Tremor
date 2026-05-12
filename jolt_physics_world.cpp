#include "jolt_physics_world.h"

#include "logger.h"

#include <Jolt/Core/Memory.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include <algorithm>
#include <thread>
#include <utility>

namespace tremor::physics {

ConfigurableBroadPhaseLayerInterface::ConfigurableBroadPhaseLayerInterface(const PhysicsLayerConfig& config) {
    objectToBroadPhase_.resize(config.layers.size(), JPH::BroadPhaseLayer(0));
    broadPhaseNames_.resize(config.layers.size());
    for (size_t index = 0; index < config.layers.size(); ++index) {
        objectToBroadPhase_[index] = JPH::BroadPhaseLayer(config.layers[index].broadPhase);
        const size_t broadIndex = static_cast<size_t>(config.layers[index].broadPhase);
        if (broadIndex >= broadPhaseNames_.size()) {
            broadPhaseNames_.resize(broadIndex + 1);
        }
        if (broadPhaseNames_[broadIndex].empty()) {
            broadPhaseNames_[broadIndex] = config.layers[index].name;
        }
    }
}

JPH::uint ConfigurableBroadPhaseLayerInterface::GetNumBroadPhaseLayers() const {
    return static_cast<JPH::uint>(std::max<size_t>(1, broadPhaseNames_.size()));
}

JPH::BroadPhaseLayer ConfigurableBroadPhaseLayerInterface::GetBroadPhaseLayer(JPH::ObjectLayer layer) const {
    if (layer < objectToBroadPhase_.size()) {
        return objectToBroadPhase_[layer];
    }
    return JPH::BroadPhaseLayer(0);
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char* ConfigurableBroadPhaseLayerInterface::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const {
    const size_t index = static_cast<size_t>(layer.GetValue());
    if (index < broadPhaseNames_.size() && !broadPhaseNames_[index].empty()) {
        return broadPhaseNames_[index].c_str();
    }
    return "unknown";
}
#endif

ConfigurableObjectVsBroadPhaseLayerFilter::ConfigurableObjectVsBroadPhaseLayerFilter(
    const PhysicsLayerConfig& config
) : collisions_(config.broadPhaseCollisions) {
}

bool ConfigurableObjectVsBroadPhaseLayerFilter::ShouldCollide(
    JPH::ObjectLayer objectLayer,
    JPH::BroadPhaseLayer broadPhaseLayer
) const {
    return objectLayer < collisions_.size() &&
        broadPhaseLayer.GetValue() < collisions_[objectLayer].size() &&
        collisions_[objectLayer][broadPhaseLayer.GetValue()];
}

ConfigurableObjectLayerPairFilter::ConfigurableObjectLayerPairFilter(
    const PhysicsLayerConfig& config
) : collisions_(config.objectCollisions) {
}

bool ConfigurableObjectLayerPairFilter::ShouldCollide(JPH::ObjectLayer left, JPH::ObjectLayer right) const {
    return left < collisions_.size() && right < collisions_[left].size() && collisions_[left][right];
}

LoggingContactListener::LoggingContactListener(JoltPhysicsWorld& owner)
    : owner_(owner) {
}

void LoggingContactListener::OnContactAdded(
    const JPH::Body& left,
    const JPH::Body& right,
    const JPH::ContactManifold&,
    JPH::ContactSettings&
) {
    Logger::get().debug(
        "Physics contact added between bodies {} and {}",
        left.GetID().GetIndex(),
        right.GetID().GetIndex()
    );
    owner_.emitContactEvent({
        JoltPhysicsWorld::toHandle(left.GetID()),
        JoltPhysicsWorld::toHandle(right.GetID()),
        PhysicsContactPhase::Added
    });
}

void LoggingContactListener::OnContactRemoved(const JPH::SubShapeIDPair& pair) {
    owner_.emitContactEvent({
        JoltPhysicsWorld::toHandle(pair.GetBody1ID()),
        JoltPhysicsWorld::toHandle(pair.GetBody2ID()),
        PhysicsContactPhase::Removed
    });
}

JoltPhysicsWorld::JoltPhysicsWorld(JoltPhysicsSettings settings)
    : PhysicsWorldBackend(std::move(settings)) {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    joltRegistered_ = true;
}

JoltPhysicsWorld::~JoltPhysicsWorld() {
    shutdown();
    if (joltRegistered_) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

bool JoltPhysicsWorld::initialize() {
    Logger::get().info("Initializing Jolt physics world...");

    jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(
        settings_.maxPhysicsJobs,
        settings_.maxPhysicsBarriers,
        std::max(1u, std::thread::hardware_concurrency() > 1
            ? std::thread::hardware_concurrency() - 1
            : 1)
    );
    tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(settings_.tempAllocatorBytes);
    broadPhaseLayerInterface_ = std::make_unique<ConfigurableBroadPhaseLayerInterface>(settings_.layers);
    objectVsBroadPhaseLayerFilter_ = std::make_unique<ConfigurableObjectVsBroadPhaseLayerFilter>(settings_.layers);
    objectLayerPairFilter_ = std::make_unique<ConfigurableObjectLayerPairFilter>(settings_.layers);

    physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
    physicsSystem_->Init(
        settings_.maxBodies,
        settings_.numBodyMutexes,
        settings_.maxBodyPairs,
        settings_.maxContactConstraints,
        *broadPhaseLayerInterface_,
        *objectVsBroadPhaseLayerFilter_,
        *objectLayerPairFilter_
    );

    contactListener_ = std::make_unique<LoggingContactListener>(*this);
    physicsSystem_->SetContactListener(contactListener_.get());
    physicsSystem_->SetGravity(toJoltVector(settings_.gravity));

    Logger::get().info(
        "Jolt physics initialized: maxBodies={}, layers={}",
        settings_.maxBodies,
        settings_.layers.layers.size()
    );
    return true;
}

void JoltPhysicsWorld::shutdown() {
    if (physicsSystem_) {
        JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
        JPH::BodyIDVector bodies;
        physicsSystem_->GetBodies(bodies);
        for (JPH::BodyID bodyId : bodies) {
            bodyInterface.RemoveBody(bodyId);
            bodyInterface.DestroyBody(bodyId);
        }
        Logger::get().info("Jolt physics world shut down");
    }

    physicsSystem_.reset();
    contactListener_.reset();
    objectLayerPairFilter_.reset();
    objectVsBroadPhaseLayerFilter_.reset();
    broadPhaseLayerInterface_.reset();
    tempAllocator_.reset();
    jobSystem_.reset();
}

void JoltPhysicsWorld::update(float deltaTime) {
    if (!physicsSystem_) {
        return;
    }
    physicsSystem_->Update(deltaTime, settings_.collisionSteps, tempAllocator_.get(), jobSystem_.get());
}

PhysicsBodyHandle JoltPhysicsWorld::createDynamicCapsule(
    const glm::vec3& position,
    float radius,
    float height,
    PhysicsObjectLayer layer
) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::RefConst<JPH::Shape> shape = new JPH::CapsuleShape(
        static_cast<JPH::Real>(height * 0.5f),
        static_cast<JPH::Real>(radius)
    );

    JPH::BodyCreationSettings bodySettings(
        shape.GetPtr(),
        toJoltPosition(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        static_cast<JPH::ObjectLayer>(layer)
    );
    bodySettings.mFriction = static_cast<JPH::Real>(0.5f);
    bodySettings.mRestitution = static_cast<JPH::Real>(0.0f);
    bodySettings.mLinearDamping = static_cast<JPH::Real>(0.0001f);
    bodySettings.mAngularDamping = static_cast<JPH::Real>(0.05f);
    bodySettings.mGravityFactor = static_cast<JPH::Real>(1.0f);
    bodySettings.mAllowedDOFs =
        JPH::EAllowedDOFs::TranslationX |
        JPH::EAllowedDOFs::TranslationY |
        JPH::EAllowedDOFs::TranslationZ |
        JPH::EAllowedDOFs::RotationY;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        Logger::get().error("Failed to create dynamic capsule body");
        return {};
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return toHandle(body->GetID());
}

PhysicsBodyHandle JoltPhysicsWorld::createKinematicBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    PhysicsObjectLayer layer
) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(toJoltVector(halfExtents));
    JPH::BodyCreationSettings bodySettings(
        shape.GetPtr(),
        toJoltPosition(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic,
        static_cast<JPH::ObjectLayer>(layer)
    );
    bodySettings.mFriction = static_cast<JPH::Real>(0.5f);
    bodySettings.mRestitution = static_cast<JPH::Real>(0.0f);

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        Logger::get().error("Failed to create kinematic box body");
        return {};
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return toHandle(body->GetID());
}

PhysicsBodyHandle JoltPhysicsWorld::createStaticBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    PhysicsObjectLayer layer
) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(toJoltVector(halfExtents));
    JPH::BodyCreationSettings bodySettings(
        shape.GetPtr(),
        toJoltPosition(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        static_cast<JPH::ObjectLayer>(layer)
    );
    bodySettings.mFriction = static_cast<JPH::Real>(0.5f);
    bodySettings.mRestitution = static_cast<JPH::Real>(0.0f);

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        Logger::get().error("Failed to create static box body");
        return {};
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return toHandle(body->GetID());
}

glm::vec3 JoltPhysicsWorld::getBodyPosition(PhysicsBodyHandle bodyId) const {
    if (!physicsSystem_) {
        return glm::vec3(0.0f);
    }
    return fromJolt(physicsSystem_->GetBodyInterface().GetCenterOfMassPosition(toBodyId(bodyId)));
}

void JoltPhysicsWorld::setBodyPosition(PhysicsBodyHandle bodyId, const glm::vec3& position) {
    if (!physicsSystem_) {
        return;
    }
    physicsSystem_->GetBodyInterface().SetPosition(
        toBodyId(bodyId),
        toJoltPosition(position),
        JPH::EActivation::Activate
    );
}

glm::vec3 JoltPhysicsWorld::getBodyVelocity(PhysicsBodyHandle bodyId) const {
    if (!physicsSystem_) {
        return glm::vec3(0.0f);
    }
    return fromJolt(physicsSystem_->GetBodyInterface().GetLinearVelocity(toBodyId(bodyId)));
}

void JoltPhysicsWorld::setBodyVelocity(PhysicsBodyHandle bodyId, const glm::vec3& velocity) {
    if (!physicsSystem_) {
        return;
    }
    physicsSystem_->GetBodyInterface().SetLinearVelocity(toBodyId(bodyId), toJoltVector(velocity));
}

void JoltPhysicsWorld::addImpulse(PhysicsBodyHandle bodyId, const glm::vec3& impulse) {
    if (!physicsSystem_) {
        return;
    }
    physicsSystem_->GetBodyInterface().AddImpulse(toBodyId(bodyId), toJoltVector(impulse));
}

void JoltPhysicsWorld::addForce(PhysicsBodyHandle bodyId, const glm::vec3& force) {
    if (!physicsSystem_) {
        return;
    }
    physicsSystem_->GetBodyInterface().AddForce(toBodyId(bodyId), toJoltVector(force));
}

void JoltPhysicsWorld::removeBody(PhysicsBodyHandle bodyId) {
    const JPH::BodyID joltBodyId = toBodyId(bodyId);
    if (!physicsSystem_ || joltBodyId.IsInvalid()) {
        return;
    }
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.RemoveBody(joltBodyId);
    bodyInterface.DestroyBody(joltBodyId);
}

PhysicsBodyHandle JoltPhysicsWorld::toHandle(JPH::BodyID bodyId) {
    return PhysicsBodyHandle(bodyId.GetIndexAndSequenceNumber());
}

JPH::BodyID JoltPhysicsWorld::toBodyId(PhysicsBodyHandle handle) {
    if (handle.IsInvalid()) {
        return JPH::BodyID();
    }
    return JPH::BodyID(static_cast<JPH::uint32>(handle.raw()));
}

JPH::RVec3 JoltPhysicsWorld::toJoltPosition(const glm::vec3& value) {
    return JPH::RVec3(
        static_cast<JPH::Real>(value.x),
        static_cast<JPH::Real>(value.y),
        static_cast<JPH::Real>(value.z)
    );
}

JPH::Vec3 JoltPhysicsWorld::toJoltVector(const glm::vec3& value) {
    return JPH::Vec3(
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z)
    );
}

glm::vec3 JoltPhysicsWorld::fromJolt(const JPH::RVec3& value) {
    return glm::vec3(
        static_cast<float>(value.GetX()),
        static_cast<float>(value.GetY()),
        static_cast<float>(value.GetZ())
    );
}

glm::vec3 JoltPhysicsWorld::fromJolt(const JPH::Vec3& value) {
    return glm::vec3(
        static_cast<float>(value.GetX()),
        static_cast<float>(value.GetY()),
        static_cast<float>(value.GetZ())
    );
}

} // namespace tremor::physics

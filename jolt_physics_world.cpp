#include "jolt_physics_world.h"

#include "logger.h"

#include <Jolt/Core/Memory.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include <algorithm>
#include <thread>
#include <utility>

namespace tremor::physics {
namespace {

bool matrixValue(const std::vector<std::vector<bool>>& matrix, size_t row, size_t column) {
    return row < matrix.size() && column < matrix[row].size() && matrix[row][column];
}

std::vector<std::vector<bool>> makeMatrix(size_t rows, size_t columns, bool value = false) {
    return std::vector<std::vector<bool>>(rows, std::vector<bool>(columns, value));
}

} // namespace

PhysicsLayerConfig PhysicsLayerConfig::makeDefault() {
    PhysicsLayerConfig config;
    config.layers = {
        {"static", JPH::BroadPhaseLayer(0)},
        {"dynamic", JPH::BroadPhaseLayer(1)},
        {"player", JPH::BroadPhaseLayer(2)},
        {"enemy", JPH::BroadPhaseLayer(3)},
        {"projectile", JPH::BroadPhaseLayer(4)},
        {"pickup", JPH::BroadPhaseLayer(1)},
    };

    config.objectCollisions = makeMatrix(DefaultPhysicsLayers::Count, DefaultPhysicsLayers::Count, false);
    config.broadPhaseCollisions = makeMatrix(DefaultPhysicsLayers::Count, DefaultPhysicsLayers::Count, false);

    const auto allowObject = [&config](JPH::ObjectLayer left, JPH::ObjectLayer right) {
        config.objectCollisions[left][right] = true;
        config.objectCollisions[right][left] = true;
    };
    const auto allowBroadPhase = [&config](JPH::ObjectLayer left, JPH::ObjectLayer right) {
        config.broadPhaseCollisions[left][right] = true;
        config.broadPhaseCollisions[right][left] = true;
    };

    allowObject(DefaultPhysicsLayers::Static, DefaultPhysicsLayers::Dynamic);
    allowObject(DefaultPhysicsLayers::Static, DefaultPhysicsLayers::Player);
    allowObject(DefaultPhysicsLayers::Static, DefaultPhysicsLayers::Enemy);
    allowObject(DefaultPhysicsLayers::Static, DefaultPhysicsLayers::Projectile);
    allowObject(DefaultPhysicsLayers::Dynamic, DefaultPhysicsLayers::Dynamic);
    allowObject(DefaultPhysicsLayers::Dynamic, DefaultPhysicsLayers::Player);
    allowObject(DefaultPhysicsLayers::Dynamic, DefaultPhysicsLayers::Enemy);
    allowObject(DefaultPhysicsLayers::Dynamic, DefaultPhysicsLayers::Projectile);
    allowObject(DefaultPhysicsLayers::Dynamic, DefaultPhysicsLayers::Pickup);
    allowObject(DefaultPhysicsLayers::Player, DefaultPhysicsLayers::Enemy);
    allowObject(DefaultPhysicsLayers::Player, DefaultPhysicsLayers::Pickup);
    allowObject(DefaultPhysicsLayers::Enemy, DefaultPhysicsLayers::Projectile);
    allowObject(DefaultPhysicsLayers::Pickup, DefaultPhysicsLayers::Player);

    for (JPH::ObjectLayer layer = 0; layer < DefaultPhysicsLayers::Count; ++layer) {
        allowBroadPhase(layer, DefaultPhysicsLayers::Static);
        allowBroadPhase(layer, DefaultPhysicsLayers::Dynamic);
        allowBroadPhase(layer, DefaultPhysicsLayers::Player);
        allowBroadPhase(layer, DefaultPhysicsLayers::Enemy);
        allowBroadPhase(layer, DefaultPhysicsLayers::Projectile);
    }

    return config;
}

PhysicsLayerConfigBuilder::PhysicsLayerConfigBuilder() {
    clear();
}

void PhysicsLayerConfigBuilder::clear() {
    layers_.clear();
    objectCollisions_.clear();
}

bool PhysicsLayerConfigBuilder::empty() const {
    return layers_.empty();
}

bool PhysicsLayerConfigBuilder::defineLayer(std::string_view name, std::string_view broadPhaseName) {
    if (name.empty() || findLayer(name)) {
        return false;
    }

    const std::string layerName(name);
    layers_.push_back({
        layerName,
        broadPhaseName.empty() ? layerName : std::string(broadPhaseName)
    });
    resizeCollisionMatrix();
    return true;
}

bool PhysicsLayerConfigBuilder::allowCollision(std::string_view left, std::string_view right) {
    return setCollision(left, right, true);
}

bool PhysicsLayerConfigBuilder::blockCollision(std::string_view left, std::string_view right) {
    return setCollision(left, right, false);
}

std::optional<JPH::ObjectLayer> PhysicsLayerConfigBuilder::findLayer(std::string_view nameOrNumber) const {
    const std::string value(nameOrNumber);
    for (JPH::ObjectLayer index = 0; index < layers_.size(); ++index) {
        if (layers_[index].name == value) {
            return index;
        }
    }

    try {
        size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed);
        if (consumed == value.size() && parsed < layers_.size()) {
            return static_cast<JPH::ObjectLayer>(parsed);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

PhysicsLayerConfig PhysicsLayerConfigBuilder::build() const {
    PhysicsLayerConfig config;

    std::unordered_map<std::string, JPH::BroadPhaseLayer> broadPhaseLayers;
    for (const DraftLayer& layer : layers_) {
        auto found = broadPhaseLayers.find(layer.broadPhaseName);
        if (found == broadPhaseLayers.end()) {
            const auto broadPhaseIndex = static_cast<JPH::uint>(broadPhaseLayers.size());
            found = broadPhaseLayers.emplace(layer.broadPhaseName, JPH::BroadPhaseLayer(broadPhaseIndex)).first;
        }
        config.layers.push_back({layer.name, found->second});
    }

    config.objectCollisions = objectCollisions_;
    config.broadPhaseCollisions = makeMatrix(
        layers_.size(),
        std::max<size_t>(1, broadPhaseLayers.size()),
        false
    );

    for (size_t left = 0; left < objectCollisions_.size(); ++left) {
        for (size_t right = 0; right < objectCollisions_[left].size(); ++right) {
            if (!objectCollisions_[left][right]) {
                continue;
            }
            const size_t broadPhaseRight = static_cast<size_t>(config.layers[right].broadPhase.GetValue());
            config.broadPhaseCollisions[left][broadPhaseRight] = true;
        }
    }

    return config;
}

void PhysicsLayerConfigBuilder::resizeCollisionMatrix() {
    const size_t size = layers_.size();
    objectCollisions_.resize(size);
    for (std::vector<bool>& row : objectCollisions_) {
        row.resize(size, false);
    }
}

bool PhysicsLayerConfigBuilder::setCollision(std::string_view left, std::string_view right, bool shouldCollide) {
    const std::optional<JPH::ObjectLayer> leftLayer = findLayer(left);
    const std::optional<JPH::ObjectLayer> rightLayer = findLayer(right);
    if (!leftLayer || !rightLayer) {
        return false;
    }

    objectCollisions_[*leftLayer][*rightLayer] = shouldCollide;
    objectCollisions_[*rightLayer][*leftLayer] = shouldCollide;
    return true;
}

ConfigurableBroadPhaseLayerInterface::ConfigurableBroadPhaseLayerInterface(const PhysicsLayerConfig& config) {
    objectToBroadPhase_.resize(config.layers.size(), JPH::BroadPhaseLayer(0));
    broadPhaseNames_.resize(config.layers.size());
    for (size_t index = 0; index < config.layers.size(); ++index) {
        objectToBroadPhase_[index] = config.layers[index].broadPhase;
        const size_t broadIndex = static_cast<size_t>(config.layers[index].broadPhase.GetValue());
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
    return matrixValue(collisions_, objectLayer, broadPhaseLayer.GetValue());
}

ConfigurableObjectLayerPairFilter::ConfigurableObjectLayerPairFilter(
    const PhysicsLayerConfig& config
) : collisions_(config.objectCollisions) {
}

bool ConfigurableObjectLayerPairFilter::ShouldCollide(JPH::ObjectLayer left, JPH::ObjectLayer right) const {
    return matrixValue(collisions_, left, right);
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
}

void LoggingContactListener::OnContactRemoved(const JPH::SubShapeIDPair&) {
}

JoltPhysicsWorld::JoltPhysicsWorld(JoltPhysicsSettings settings)
    : settings_(std::move(settings)) {
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

    contactListener_ = std::make_unique<LoggingContactListener>();
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

JPH::BodyID JoltPhysicsWorld::createDynamicCapsule(
    const glm::vec3& position,
    float radius,
    float height,
    JPH::ObjectLayer layer
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
        layer
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
        return JPH::BodyID();
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID();
}

JPH::BodyID JoltPhysicsWorld::createKinematicBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    JPH::ObjectLayer layer
) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(toJoltVector(halfExtents));
    JPH::BodyCreationSettings bodySettings(
        shape.GetPtr(),
        toJoltPosition(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic,
        layer
    );
    bodySettings.mFriction = static_cast<JPH::Real>(0.5f);
    bodySettings.mRestitution = static_cast<JPH::Real>(0.0f);

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        Logger::get().error("Failed to create kinematic box body");
        return JPH::BodyID();
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID();
}

JPH::BodyID JoltPhysicsWorld::createStaticBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    JPH::ObjectLayer layer
) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(toJoltVector(halfExtents));
    JPH::BodyCreationSettings bodySettings(
        shape.GetPtr(),
        toJoltPosition(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        layer
    );
    bodySettings.mFriction = static_cast<JPH::Real>(0.5f);
    bodySettings.mRestitution = static_cast<JPH::Real>(0.0f);

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        Logger::get().error("Failed to create static box body");
        return JPH::BodyID();
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return body->GetID();
}

glm::vec3 JoltPhysicsWorld::getBodyPosition(JPH::BodyID bodyId) const {
    if (!physicsSystem_) {
        return glm::vec3(0.0f);
    }
    return fromJolt(physicsSystem_->GetBodyInterface().GetCenterOfMassPosition(bodyId));
}

void JoltPhysicsWorld::setBodyPosition(JPH::BodyID bodyId, const glm::vec3& position) {
    if (!physicsSystem_) {
        return;
    }
    physicsSystem_->GetBodyInterface().SetPosition(bodyId, toJoltPosition(position), JPH::EActivation::Activate);
}

glm::vec3 JoltPhysicsWorld::getBodyVelocity(JPH::BodyID bodyId) const {
    if (!physicsSystem_) {
        return glm::vec3(0.0f);
    }
    return fromJolt(physicsSystem_->GetBodyInterface().GetLinearVelocity(bodyId));
}

void JoltPhysicsWorld::setBodyVelocity(JPH::BodyID bodyId, const glm::vec3& velocity) {
    if (!physicsSystem_) {
        return;
    }
    physicsSystem_->GetBodyInterface().SetLinearVelocity(bodyId, toJoltVector(velocity));
}

void JoltPhysicsWorld::addImpulse(JPH::BodyID bodyId, const glm::vec3& impulse) {
    if (!physicsSystem_) {
        return;
    }
    physicsSystem_->GetBodyInterface().AddImpulse(bodyId, toJoltVector(impulse));
}

void JoltPhysicsWorld::addForce(JPH::BodyID bodyId, const glm::vec3& force) {
    if (!physicsSystem_) {
        return;
    }
    physicsSystem_->GetBodyInterface().AddForce(bodyId, toJoltVector(force));
}

void JoltPhysicsWorld::removeBody(JPH::BodyID bodyId) {
    if (!physicsSystem_ || bodyId.IsInvalid()) {
        return;
    }
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.RemoveBody(bodyId);
    bodyInterface.DestroyBody(bodyId);
}

std::optional<JPH::ObjectLayer> JoltPhysicsWorld::findLayer(std::string_view nameOrNumber) const {
    const std::string value(nameOrNumber);
    for (JPH::ObjectLayer index = 0; index < settings_.layers.layers.size(); ++index) {
        if (settings_.layers.layers[index].name == value) {
            return index;
        }
    }

    try {
        size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed);
        if (consumed == value.size() && parsed < settings_.layers.layers.size()) {
            return static_cast<JPH::ObjectLayer>(parsed);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
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

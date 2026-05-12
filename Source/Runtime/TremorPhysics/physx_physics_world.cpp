#include "Source/Runtime/TremorPhysics/physx_physics_world.h"

#include "logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <thread>
#include <unordered_map>

#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
// PhysX requires exactly one of `_DEBUG` or `NDEBUG` on Windows.
// Visual Studio's CMake/IntelliSense pipeline can be inconsistent here,
// so we normalize the macro state only for the PhysX include boundary.
//#pragma push_macro("_DEBUG")
//#pragma push_macro("NDEBUG")

#if defined(_DEBUG) 
    #undef NDEBUG
#endif

#include <PxPhysicsAPI.h>
#include <extensions/PxDefaultAllocator.h>
#include <extensions/PxDefaultCpuDispatcher.h>
#include <extensions/PxDefaultErrorCallback.h>
#include <extensions/PxDefaultSimulationFilterShader.h>
#include <extensions/PxExtensionsAPI.h>
#include <extensions/PxSimpleFactory.h>
#include <PxSimulationEventCallback.h>

#pragma pop_macro("NDEBUG")
#pragma pop_macro("_DEBUG")
#endif

namespace tremor::physics {

#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
namespace {

physx::PxVec3 toPxVec3(const glm::vec3& value) {
    return physx::PxVec3(value.x, value.y, value.z);
}

glm::vec3 fromPxVec3(const physx::PxVec3& value) {
    return glm::vec3(value.x, value.y, value.z);
}

physx::PxTransform makeWorldTransform(const glm::vec3& position) {
    return physx::PxTransform(toPxVec3(position));
}

physx::PxTransform makeCapsuleTransform(const glm::vec3& position) {
    return physx::PxTransform(
        toPxVec3(position),
        physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f))
    );
}

size_t broadPhaseLayerCount(const PhysicsLayerConfig& config) {
    size_t count = config.broadPhaseCollisions.size();
    for (const auto& layer : config.layers) {
        count = std::max(count, static_cast<size_t>(layer.broadPhase) + 1);
    }
    return std::max<size_t>(1, count);
}

std::vector<physx::PxU32> buildFilterShaderData(const PhysicsLayerConfig& config) {
    const physx::PxU32 layerCount = static_cast<physx::PxU32>(config.layers.size());
    const physx::PxU32 broadPhaseCount = static_cast<physx::PxU32>(broadPhaseLayerCount(config));

    std::vector<physx::PxU32> data;
    data.reserve(
        2 +
        layerCount +
        (layerCount * layerCount) +
        (broadPhaseCount * broadPhaseCount)
    );

    data.push_back(layerCount);
    data.push_back(broadPhaseCount);

    for (physx::PxU32 layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        const auto broadPhase = layerIndex < config.layers.size()
            ? config.layers[layerIndex].broadPhase
            : 0;
        data.push_back(static_cast<physx::PxU32>(broadPhase));
    }

    for (physx::PxU32 left = 0; left < layerCount; ++left) {
        for (physx::PxU32 right = 0; right < layerCount; ++right) {
            const bool shouldCollide =
                left < config.objectCollisions.size() &&
                right < config.objectCollisions[left].size() &&
                config.objectCollisions[left][right];
            data.push_back(shouldCollide ? 1u : 0u);
        }
    }

    for (physx::PxU32 left = 0; left < broadPhaseCount; ++left) {
        for (physx::PxU32 right = 0; right < broadPhaseCount; ++right) {
            const bool shouldCollide =
                left < config.broadPhaseCollisions.size() &&
                right < config.broadPhaseCollisions[left].size() &&
                config.broadPhaseCollisions[left][right];
            data.push_back(shouldCollide ? 1u : 0u);
        }
    }

    return data;
}

bool shouldLayersCollide(
    const physx::PxU32* data,
    physx::PxU32 layerCount,
    physx::PxU32 broadPhaseCount,
    physx::PxU32 leftLayer,
    physx::PxU32 rightLayer
) {
    if (leftLayer >= layerCount || rightLayer >= layerCount) {
        return true;
    }

    const physx::PxU32* objectToBroadPhase = data + 2;
    const physx::PxU32* objectCollisionMatrix = objectToBroadPhase + layerCount;
    const physx::PxU32* broadPhaseCollisionMatrix = objectCollisionMatrix + (layerCount * layerCount);

    const auto objectCollisionIndex = static_cast<size_t>(leftLayer) * layerCount + rightLayer;
    if (objectCollisionMatrix[objectCollisionIndex] == 0) {
        return false;
    }

    const physx::PxU32 leftBroadPhase = objectToBroadPhase[leftLayer];
    const physx::PxU32 rightBroadPhase = objectToBroadPhase[rightLayer];
    if (leftBroadPhase >= broadPhaseCount || rightBroadPhase >= broadPhaseCount) {
        return true;
    }

    const auto broadPhaseCollisionIndex = static_cast<size_t>(leftBroadPhase) * broadPhaseCount + rightBroadPhase;
    return broadPhaseCollisionMatrix[broadPhaseCollisionIndex] != 0;
}

physx::PxFilterFlags TremorPhysicsFilterShader(
    physx::PxFilterObjectAttributes attributes0,
    physx::PxFilterData filterData0,
    physx::PxFilterObjectAttributes attributes1,
    physx::PxFilterData filterData1,
    physx::PxPairFlags& pairFlags,
    const void* constantBlock,
    physx::PxU32 constantBlockSize
) {
    if (physx::PxFilterObjectIsTrigger(attributes0) || physx::PxFilterObjectIsTrigger(attributes1)) {
        pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
        return physx::PxFilterFlag::eDEFAULT;
    }

    pairFlags =
        physx::PxPairFlag::eCONTACT_DEFAULT |
        physx::PxPairFlag::eNOTIFY_TOUCH_FOUND |
        physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
        physx::PxPairFlag::eNOTIFY_TOUCH_LOST;

    if (constantBlock == nullptr || constantBlockSize < sizeof(physx::PxU32) * 2) {
        return physx::PxFilterFlag::eDEFAULT;
    }

    const auto* data = static_cast<const physx::PxU32*>(constantBlock);
    const physx::PxU32 layerCount = data[0];
    const physx::PxU32 broadPhaseCount = data[1];
    if (!shouldLayersCollide(data, layerCount, broadPhaseCount, filterData0.word0, filterData1.word0)) {
        pairFlags = physx::PxPairFlags();
        return physx::PxFilterFlag::eSUPPRESS;
    }

    return physx::PxFilterFlag::eDEFAULT;
}

void applyFilterData(physx::PxRigidActor& actor, PhysicsObjectLayer layer) {
    const physx::PxFilterData filterData(static_cast<physx::PxU32>(layer), 0, 0, 0);
    physx::PxShape* shapes[8] = {};
    const physx::PxU32 shapeCount = actor.getShapes(shapes, std::size(shapes));
    for (physx::PxU32 index = 0; index < shapeCount; ++index) {
        if (shapes[index] != nullptr) {
            shapes[index]->setSimulationFilterData(filterData);
            shapes[index]->setQueryFilterData(filterData);
        }
    }
}

} // namespace

struct PhysXPhysicsWorld::Impl {
    struct SimulationEvents : physx::PxSimulationEventCallback {
        explicit SimulationEvents(Impl& ownerImpl)
            : owner(ownerImpl) {
        }

        void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
        void onWake(physx::PxActor**, physx::PxU32) override {}
        void onSleep(physx::PxActor**, physx::PxU32) override {}
        void onTrigger(physx::PxTriggerPair*, physx::PxU32) override {}
        void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, const physx::PxU32) override {}

        void onContact(
            const physx::PxContactPairHeader& pairHeader,
            const physx::PxContactPair* pairs,
            physx::PxU32 nbPairs
        ) override {
            const auto leftFound = owner.actorHandles.find(pairHeader.actors[0]);
            const auto rightFound = owner.actorHandles.find(pairHeader.actors[1]);
            if (leftFound == owner.actorHandles.end() || rightFound == owner.actorHandles.end()) {
                return;
            }

            for (physx::PxU32 index = 0; index < nbPairs; ++index) {
                const physx::PxContactPair& pair = pairs[index];
                if (pair.events.isSet(physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)) {
                    owner.world.emitContactEvent({leftFound->second, rightFound->second, PhysicsContactPhase::Added});
                }
                if (pair.events.isSet(physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS)) {
                    owner.world.emitContactEvent({leftFound->second, rightFound->second, PhysicsContactPhase::Persisted});
                }
                if (pair.events.isSet(physx::PxPairFlag::eNOTIFY_TOUCH_LOST)) {
                    owner.world.emitContactEvent({leftFound->second, rightFound->second, PhysicsContactPhase::Removed});
                }
            }
        }

        Impl& owner;
    };

    explicit Impl(PhysXPhysicsWorld& worldRef)
        : simulationEvents(*this),
          world(worldRef) {
    }

    physx::PxDefaultAllocator allocator;
    physx::PxDefaultErrorCallback errorCallback;
    physx::PxFoundation* foundation = nullptr;
    physx::PxPhysics* physics = nullptr;
    physx::PxScene* scene = nullptr;
    physx::PxDefaultCpuDispatcher* dispatcher = nullptr;
    physx::PxMaterial* defaultMaterial = nullptr;
    std::unordered_map<uint64_t, physx::PxRigidActor*> actors;
    std::unordered_map<const physx::PxActor*, PhysicsBodyHandle> actorHandles;
    std::vector<physx::PxU32> filterShaderData;
    SimulationEvents simulationEvents;
    PhysXPhysicsWorld& world;
    uint64_t nextHandle = 1;
    bool extensionsInitialized = false;

    PhysicsBodyHandle registerActor(physx::PxRigidActor* actor) {
        if (actor == nullptr) {
            return {};
        }

        const PhysicsBodyHandle handle(nextHandle++);
        actors.emplace(handle.raw(), actor);
        actorHandles.emplace(actor, handle);
        return handle;
    }

    physx::PxRigidActor* findActor(PhysicsBodyHandle handle) const {
        if (handle.IsInvalid()) {
            return nullptr;
        }

        const auto found = actors.find(handle.raw());
        return found != actors.end() ? found->second : nullptr;
    }
};

#else

struct PhysXPhysicsWorld::Impl {
};

namespace {

void logUnavailable(std::string_view operation) {
    Logger::get().warning("PhysX backend stub: '{}' requested but the PhysX SDK is not built or available yet", operation);
}

} // namespace
#endif

PhysXPhysicsWorld::PhysXPhysicsWorld(PhysicsSettings settings)
    : PhysicsWorldBackend(std::move(settings)) {
}

PhysXPhysicsWorld::~PhysXPhysicsWorld() {
    shutdown();
}

bool PhysXPhysicsWorld::initialize() {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    shutdown();
    impl_ = std::make_unique<Impl>(*this);

    impl_->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, impl_->allocator, impl_->errorCallback);
    if (impl_->foundation == nullptr) {
        Logger::get().error("PhysX initialization failed: PxCreateFoundation returned null");
        impl_.reset();
        return false;
    }

    const physx::PxTolerancesScale tolerances;
    impl_->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *impl_->foundation, tolerances, false, nullptr);
    if (impl_->physics == nullptr) {
        Logger::get().error("PhysX initialization failed: PxCreatePhysics returned null");
        shutdown();
        return false;
    }

    impl_->extensionsInitialized = PxInitExtensions(*impl_->physics, nullptr);
    if (!impl_->extensionsInitialized) {
        Logger::get().warning("PhysX extensions failed to initialize; some helper APIs may be unavailable");
    }

    impl_->dispatcher = physx::PxDefaultCpuDispatcherCreate(std::max(1u,
        std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1));
    if (impl_->dispatcher == nullptr) {
        Logger::get().error("PhysX initialization failed: PxDefaultCpuDispatcherCreate returned null");
        shutdown();
        return false;
    }

    impl_->filterShaderData = buildFilterShaderData(settings_.layers);

    physx::PxSceneDesc sceneDesc(impl_->physics->getTolerancesScale());
    sceneDesc.gravity = toPxVec3(settings_.gravity);
    sceneDesc.cpuDispatcher = impl_->dispatcher;
    sceneDesc.filterShader = TremorPhysicsFilterShader;
    sceneDesc.filterShaderData = impl_->filterShaderData.data();
    sceneDesc.filterShaderDataSize = static_cast<physx::PxU32>(impl_->filterShaderData.size() * sizeof(physx::PxU32));
    sceneDesc.simulationEventCallback = &impl_->simulationEvents;

    impl_->scene = impl_->physics->createScene(sceneDesc);
    if (impl_->scene == nullptr) {
        Logger::get().error("PhysX initialization failed: PxPhysics::createScene returned null");
        shutdown();
        return false;
    }

    impl_->defaultMaterial = impl_->physics->createMaterial(0.5f, 0.5f, 0.0f);
    if (impl_->defaultMaterial == nullptr) {
        Logger::get().error("PhysX initialization failed: PxPhysics::createMaterial returned null");
        shutdown();
        return false;
    }

    Logger::get().info(
        "PhysX physics initialized: gravity=({}, {}, {}), maxBodies={}, layers={}, broadPhaseLayers={}",
        settings_.gravity.x,
        settings_.gravity.y,
        settings_.gravity.z,
        settings_.maxBodies,
        settings_.layers.layers.size(),
        broadPhaseLayerCount(settings_.layers)
    );
    return true;
#else
    Logger::get().warning("PhysX backend selected, but the PhysX SDK is not built into this Tremor configuration yet.");
    return false;
#endif
}

void PhysXPhysicsWorld::shutdown() {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_) {
        return;
    }

    if (impl_->scene != nullptr) {
        for (auto& [_, actor] : impl_->actors) {
            if (actor != nullptr) {
                impl_->scene->removeActor(*actor, false);
                actor->release();
            }
        }
        impl_->actors.clear();
        impl_->actorHandles.clear();
    }

    if (impl_->defaultMaterial != nullptr) {
        impl_->defaultMaterial->release();
        impl_->defaultMaterial = nullptr;
    }

    if (impl_->scene != nullptr) {
        impl_->scene->release();
        impl_->scene = nullptr;
    }

    if (impl_->dispatcher != nullptr) {
        impl_->dispatcher->release();
        impl_->dispatcher = nullptr;
    }

    if (impl_->extensionsInitialized) {
        PxCloseExtensions();
        impl_->extensionsInitialized = false;
    }

    if (impl_->physics != nullptr) {
        impl_->physics->release();
        impl_->physics = nullptr;
    }

    if (impl_->foundation != nullptr) {
        impl_->foundation->release();
        impl_->foundation = nullptr;
    }

    impl_.reset();
#endif
}

void PhysXPhysicsWorld::update(float deltaTime) {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_ || impl_->scene == nullptr) {
        return;
    }

    if (!(deltaTime > 0.0f) || !std::isfinite(deltaTime)) {
        return;
    }

    const uint32_t stepCount = std::max(1u, settings_.collisionSteps);
    const float stepDelta = deltaTime / static_cast<float>(stepCount);
    if (!(stepDelta > 0.0f) || !std::isfinite(stepDelta)) {
        return;
    }

    for (uint32_t stepIndex = 0; stepIndex < stepCount; ++stepIndex) {
        impl_->scene->simulate(stepDelta);
        impl_->scene->fetchResults(true);
    }
#else
    (void)deltaTime;
#endif
}

PhysicsBodyHandle PhysXPhysicsWorld::createDynamicCapsule(
    const glm::vec3& position,
    float radius,
    float height,
    PhysicsObjectLayer layer
) {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_ || impl_->physics == nullptr || impl_->scene == nullptr || impl_->defaultMaterial == nullptr) {
        return {};
    }

    physx::PxRigidDynamic* actor = physx::PxCreateDynamic(
        *impl_->physics,
        makeCapsuleTransform(position),
        physx::PxCapsuleGeometry(radius, height * 0.5f),
        *impl_->defaultMaterial,
        1.0f
    );
    if (actor == nullptr) {
        Logger::get().error("PhysX failed to create dynamic capsule body");
        return {};
    }

    applyFilterData(*actor, layer);
    impl_->scene->addActor(*actor);
    return impl_->registerActor(actor);
#else
    (void)position;
    (void)radius;
    (void)height;
    (void)layer;
    logUnavailable("createDynamicCapsule");
    return {};
#endif
}

PhysicsBodyHandle PhysXPhysicsWorld::createKinematicBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    PhysicsObjectLayer layer
) {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_ || impl_->physics == nullptr || impl_->scene == nullptr || impl_->defaultMaterial == nullptr) {
        return {};
    }

    physx::PxRigidDynamic* actor = physx::PxCreateDynamic(
        *impl_->physics,
        makeWorldTransform(position),
        physx::PxBoxGeometry(halfExtents.x, halfExtents.y, halfExtents.z),
        *impl_->defaultMaterial,
        1.0f
    );
    if (actor == nullptr) {
        Logger::get().error("PhysX failed to create kinematic box body");
        return {};
    }

    actor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
    applyFilterData(*actor, layer);
    impl_->scene->addActor(*actor);
    return impl_->registerActor(actor);
#else
    (void)position;
    (void)halfExtents;
    (void)layer;
    logUnavailable("createKinematicBox");
    return {};
#endif
}

PhysicsBodyHandle PhysXPhysicsWorld::createStaticBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    PhysicsObjectLayer layer
) {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_ || impl_->physics == nullptr || impl_->scene == nullptr || impl_->defaultMaterial == nullptr) {
        return {};
    }

    physx::PxRigidStatic* actor = physx::PxCreateStatic(
        *impl_->physics,
        makeWorldTransform(position),
        physx::PxBoxGeometry(halfExtents.x, halfExtents.y, halfExtents.z),
        *impl_->defaultMaterial
    );
    if (actor == nullptr) {
        Logger::get().error("PhysX failed to create static box body");
        return {};
    }

    applyFilterData(*actor, layer);
    impl_->scene->addActor(*actor);
    return impl_->registerActor(actor);
#else
    (void)position;
    (void)halfExtents;
    (void)layer;
    logUnavailable("createStaticBox");
    return {};
#endif
}

glm::vec3 PhysXPhysicsWorld::getBodyPosition(PhysicsBodyHandle bodyId) const {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_) {
        return glm::vec3(0.0f);
    }

    if (physx::PxRigidActor* actor = impl_->findActor(bodyId)) {
        return fromPxVec3(actor->getGlobalPose().p);
    }
#else
    (void)bodyId;
#endif
    return glm::vec3(0.0f);
}

void PhysXPhysicsWorld::setBodyPosition(PhysicsBodyHandle bodyId, const glm::vec3& position) {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_) {
        return;
    }

    physx::PxRigidActor* actor = impl_->findActor(bodyId);
    if (actor == nullptr) {
        return;
    }

    const physx::PxTransform transform = makeWorldTransform(position);
    if (physx::PxRigidDynamic* dynamicActor = actor->is<physx::PxRigidDynamic>()) {
        if (dynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC)) {
            dynamicActor->setKinematicTarget(transform);
            return;
        }
        dynamicActor->setGlobalPose(transform, true);
        dynamicActor->wakeUp();
        return;
    }

    actor->setGlobalPose(transform, true);
#else
    (void)bodyId;
    (void)position;
    logUnavailable("setBodyPosition");
#endif
}

glm::vec3 PhysXPhysicsWorld::getBodyVelocity(PhysicsBodyHandle bodyId) const {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_) {
        return glm::vec3(0.0f);
    }

    if (physx::PxRigidActor* actor = impl_->findActor(bodyId)) {
        if (physx::PxRigidBody* body = actor->is<physx::PxRigidBody>()) {
            return fromPxVec3(body->getLinearVelocity());
        }
    }
#else
    (void)bodyId;
#endif
    return glm::vec3(0.0f);
}

void PhysXPhysicsWorld::setBodyVelocity(PhysicsBodyHandle bodyId, const glm::vec3& velocity) {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_) {
        return;
    }

    if (physx::PxRigidActor* actor = impl_->findActor(bodyId)) {
        if (physx::PxRigidDynamic* dynamicActor = actor->is<physx::PxRigidDynamic>()) {
            dynamicActor->setLinearVelocity(toPxVec3(velocity), true);
        }
    }
#else
    (void)bodyId;
    (void)velocity;
    logUnavailable("setBodyVelocity");
#endif
}

void PhysXPhysicsWorld::addImpulse(PhysicsBodyHandle bodyId, const glm::vec3& impulse) {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_) {
        return;
    }

    if (physx::PxRigidActor* actor = impl_->findActor(bodyId)) {
        if (physx::PxRigidDynamic* dynamicActor = actor->is<physx::PxRigidDynamic>()) {
            dynamicActor->addForce(toPxVec3(impulse), physx::PxForceMode::eIMPULSE, true);
        }
    }
#else
    (void)bodyId;
    (void)impulse;
    logUnavailable("addImpulse");
#endif
}

void PhysXPhysicsWorld::addForce(PhysicsBodyHandle bodyId, const glm::vec3& force) {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_) {
        return;
    }

    if (physx::PxRigidActor* actor = impl_->findActor(bodyId)) {
        if (physx::PxRigidDynamic* dynamicActor = actor->is<physx::PxRigidDynamic>()) {
            dynamicActor->addForce(toPxVec3(force), physx::PxForceMode::eFORCE, true);
        }
    }
#else
    (void)bodyId;
    (void)force;
    logUnavailable("addForce");
#endif
}

void PhysXPhysicsWorld::removeBody(PhysicsBodyHandle bodyId) {
#if defined(TREMOR_HAS_PHYSX_SDK) && __has_include(<PxConfig.h>) && __has_include(<PxPhysicsAPI.h>)
    if (!impl_) {
        return;
    }

    const auto found = impl_->actors.find(bodyId.raw());
    if (found == impl_->actors.end()) {
        return;
    }

    if (impl_->scene != nullptr && found->second != nullptr) {
        impl_->actorHandles.erase(found->second);
        impl_->scene->removeActor(*found->second, false);
        found->second->release();
    }
    impl_->actors.erase(found);
#else
    (void)bodyId;
#endif
}

} // namespace tremor::physics

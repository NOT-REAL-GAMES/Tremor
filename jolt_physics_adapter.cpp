#include "jolt_physics_adapter.h"

#include <string>

namespace tremor::physics {

JoltPhysicsAdapter::JoltPhysicsAdapter(JoltPhysicsWorld& physicsWorld)
    : physicsWorld_(physicsWorld) {
}

std::optional<PhysicsBodyHandle> JoltPhysicsAdapter::createDynamicCapsule(
    const glm::vec3& position,
    float radius,
    float height,
    std::string_view layer
) {
    if (layer == "default") {
        layer = "dynamic";
    }
    const std::optional<JPH::ObjectLayer> objectLayer = physicsWorld_.findLayer(layer);
    if (!objectLayer) {
        return std::nullopt;
    }

    JPH::BodyID body = physicsWorld_.CreateDynamicBody(position, radius, height, *objectLayer);
    if (body.IsInvalid()) {
        return std::nullopt;
    }
    return toHandle(body);
}

std::optional<PhysicsBodyHandle> JoltPhysicsAdapter::createKinematicBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    std::string_view layer
) {
    if (layer == "default") {
        layer = "dynamic";
    }
    const std::optional<JPH::ObjectLayer> objectLayer = physicsWorld_.findLayer(layer);
    if (!objectLayer) {
        return std::nullopt;
    }

    JPH::BodyID body = physicsWorld_.CreateKinematicBody(position, halfExtents, *objectLayer);
    if (body.IsInvalid()) {
        return std::nullopt;
    }
    return toHandle(body);
}

std::optional<PhysicsBodyHandle> JoltPhysicsAdapter::createStaticBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    std::string_view layer
) {
    if (layer == "default") {
        layer = "static";
    }
    const std::optional<JPH::ObjectLayer> objectLayer = physicsWorld_.findLayer(layer);
    if (!objectLayer) {
        return std::nullopt;
    }

    JPH::BodyID body = physicsWorld_.CreateStaticBody(position, halfExtents, *objectLayer);
    if (body.IsInvalid()) {
        return std::nullopt;
    }
    return toHandle(body);
}

void JoltPhysicsAdapter::removeBody(PhysicsBodyHandle handle) {
    physicsWorld_.RemoveBody(toBodyId(handle));
}

bool JoltPhysicsAdapter::setPosition(PhysicsBodyHandle handle, const glm::vec3& position) {
    physicsWorld_.SetBodyPosition(toBodyId(handle), position);
    return true;
}

bool JoltPhysicsAdapter::setVelocity(PhysicsBodyHandle handle, const glm::vec3& velocity) {
    physicsWorld_.SetBodyVelocity(toBodyId(handle), velocity);
    return true;
}

bool JoltPhysicsAdapter::addImpulse(PhysicsBodyHandle handle, const glm::vec3& impulse) {
    physicsWorld_.AddImpulse(toBodyId(handle), impulse);
    return true;
}

bool JoltPhysicsAdapter::addForce(PhysicsBodyHandle handle, const glm::vec3& force) {
    physicsWorld_.AddForce(toBodyId(handle), force);
    return true;
}

PhysicsBodyHandle JoltPhysicsAdapter::toHandle(JPH::BodyID bodyId) {
    return static_cast<PhysicsBodyHandle>(bodyId.GetIndexAndSequenceNumber());
}

JPH::BodyID JoltPhysicsAdapter::toBodyId(PhysicsBodyHandle handle) {
    return JPH::BodyID(static_cast<JPH::uint32>(handle));
}

} // namespace tremor::physics

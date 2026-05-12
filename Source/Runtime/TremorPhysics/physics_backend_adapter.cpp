#include "Source/Runtime/TremorPhysics/physics_backend_adapter.h"

namespace tremor::physics {

PhysicsInteropBackendAdapter::PhysicsInteropBackendAdapter(PhysicsWorldBackend& physicsWorld)
    : physicsWorld_(physicsWorld) {
}

std::optional<PhysicsBodyHandle> PhysicsInteropBackendAdapter::createDynamicCapsule(
    const glm::vec3& position,
    float radius,
    float height,
    std::string_view layer
) {
    if (layer == "default") {
        layer = "dynamic";
    }
    const std::optional<PhysicsObjectLayer> objectLayer = physicsWorld_.findLayer(layer);
    if (!objectLayer) {
        return std::nullopt;
    }

    const PhysicsBodyHandle body = physicsWorld_.CreateDynamicBody(position, radius, height, *objectLayer);
    return body.IsInvalid() ? std::nullopt : std::optional<PhysicsBodyHandle>(body);
}

std::optional<PhysicsBodyHandle> PhysicsInteropBackendAdapter::createKinematicBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    std::string_view layer
) {
    if (layer == "default") {
        layer = "dynamic";
    }
    const std::optional<PhysicsObjectLayer> objectLayer = physicsWorld_.findLayer(layer);
    if (!objectLayer) {
        return std::nullopt;
    }

    const PhysicsBodyHandle body = physicsWorld_.CreateKinematicBody(position, halfExtents, *objectLayer);
    return body.IsInvalid() ? std::nullopt : std::optional<PhysicsBodyHandle>(body);
}

std::optional<PhysicsBodyHandle> PhysicsInteropBackendAdapter::createStaticBox(
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    std::string_view layer
) {
    if (layer == "default") {
        layer = "static";
    }
    const std::optional<PhysicsObjectLayer> objectLayer = physicsWorld_.findLayer(layer);
    if (!objectLayer) {
        return std::nullopt;
    }

    const PhysicsBodyHandle body = physicsWorld_.CreateStaticBody(position, halfExtents, *objectLayer);
    return body.IsInvalid() ? std::nullopt : std::optional<PhysicsBodyHandle>(body);
}

void PhysicsInteropBackendAdapter::removeBody(PhysicsBodyHandle handle) {
    physicsWorld_.RemoveBody(handle);
}

bool PhysicsInteropBackendAdapter::setPosition(PhysicsBodyHandle handle, const glm::vec3& position) {
    physicsWorld_.SetBodyPosition(handle, position);
    return true;
}

bool PhysicsInteropBackendAdapter::setVelocity(PhysicsBodyHandle handle, const glm::vec3& velocity) {
    physicsWorld_.SetBodyVelocity(handle, velocity);
    return true;
}

bool PhysicsInteropBackendAdapter::addImpulse(PhysicsBodyHandle handle, const glm::vec3& impulse) {
    physicsWorld_.AddImpulse(handle, impulse);
    return true;
}

bool PhysicsInteropBackendAdapter::addForce(PhysicsBodyHandle handle, const glm::vec3& force) {
    physicsWorld_.AddForce(handle, force);
    return true;
}

} // namespace tremor::physics

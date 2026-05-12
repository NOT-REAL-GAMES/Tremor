#include "Source/Runtime/TremorPhysics/physics_backend.h"

#include "Source/Runtime/TremorPhysics/physx_physics_world.h"
#include "jolt_physics_world.h"

#include <exception>
#include <string>
#include <utility>

namespace tremor::physics {

PhysicsWorldBackend::PhysicsWorldBackend(PhysicsSettings settings)
    : settings_(std::move(settings)) {
}

std::optional<PhysicsObjectLayer> PhysicsWorldBackend::findLayer(std::string_view nameOrNumber) const {
    const std::string value(nameOrNumber);
    for (PhysicsObjectLayer index = 0; index < settings_.layers.layers.size(); ++index) {
        if (settings_.layers.layers[index].name == value) {
            return index;
        }
    }

    try {
        size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed);
        if (consumed == value.size() && parsed < settings_.layers.layers.size()) {
            return static_cast<PhysicsObjectLayer>(parsed);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

void PhysicsWorldBackend::setContactCallback(ContactCallback callback) {
    contactCallback_ = std::move(callback);
}

void PhysicsWorldBackend::clearContactCallback() {
    contactCallback_ = nullptr;
}

void PhysicsWorldBackend::emitContactEvent(const PhysicsContactEvent& event) const {
    if (contactCallback_) {
        contactCallback_(event);
    }
}

std::unique_ptr<PhysicsWorldBackend> createPhysicsWorldBackend(
    PhysicsBackendKind backendKind,
    PhysicsSettings settings
) {
    switch (backendKind) {
        case PhysicsBackendKind::PhysX:
            return std::make_unique<PhysXPhysicsWorld>(std::move(settings));
        case PhysicsBackendKind::Jolt:
        default:
            return std::make_unique<JoltPhysicsWorld>(std::move(settings));
    }
}

} // namespace tremor::physics

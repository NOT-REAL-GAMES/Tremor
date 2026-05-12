#pragma once

#include "Source/Runtime/TremorPhysics/physics_backend.h"

namespace DMCSurvivors {

namespace Layers {
    static constexpr tremor::physics::PhysicsObjectLayer NON_MOVING = tremor::physics::DefaultPhysicsLayers::Static;
    static constexpr tremor::physics::PhysicsObjectLayer MOVING = tremor::physics::DefaultPhysicsLayers::Dynamic;
    static constexpr tremor::physics::PhysicsObjectLayer PLAYER = tremor::physics::DefaultPhysicsLayers::Player;
    static constexpr tremor::physics::PhysicsObjectLayer ENEMY = tremor::physics::DefaultPhysicsLayers::Enemy;
    static constexpr tremor::physics::PhysicsObjectLayer PROJECTILE = tremor::physics::DefaultPhysicsLayers::Projectile;
    static constexpr tremor::physics::PhysicsObjectLayer PICKUP = tremor::physics::DefaultPhysicsLayers::Pickup;
    static constexpr tremor::physics::PhysicsObjectLayer NUM_LAYERS = tremor::physics::DefaultPhysicsLayers::Count;
}

using BodyID = tremor::physics::PhysicsBodyHandle;
using ObjectLayer = tremor::physics::PhysicsObjectLayer;
using PhysicsBody = tremor::physics::PhysicsBody;
using PhysicsWorld = tremor::physics::PhysicsWorldBackend;

} // namespace DMCSurvivors

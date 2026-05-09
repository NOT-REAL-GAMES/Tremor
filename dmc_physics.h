#pragma once

#include "jolt_physics_world.h"

namespace DMCSurvivors {

namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = tremor::physics::DefaultPhysicsLayers::Static;
    static constexpr JPH::ObjectLayer MOVING = tremor::physics::DefaultPhysicsLayers::Dynamic;
    static constexpr JPH::ObjectLayer PLAYER = tremor::physics::DefaultPhysicsLayers::Player;
    static constexpr JPH::ObjectLayer ENEMY = tremor::physics::DefaultPhysicsLayers::Enemy;
    static constexpr JPH::ObjectLayer PROJECTILE = tremor::physics::DefaultPhysicsLayers::Projectile;
    static constexpr JPH::ObjectLayer PICKUP = tremor::physics::DefaultPhysicsLayers::Pickup;
    static constexpr JPH::ObjectLayer NUM_LAYERS = tremor::physics::DefaultPhysicsLayers::Count;
}

using JPH::BodyID;
using JPH::ObjectLayer;
using PhysicsBody = tremor::physics::JoltPhysicsBody;
using PhysicsWorld = tremor::physics::JoltPhysicsWorld;

} // namespace DMCSurvivors

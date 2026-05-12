#include "Source/Runtime/TremorPhysics/physics_core.h"

#include <algorithm>
#include <exception>
#include <unordered_map>

namespace tremor::physics {
namespace {

std::vector<std::vector<bool>> makeMatrix(size_t rows, size_t columns, bool value = false) {
    return std::vector<std::vector<bool>>(rows, std::vector<bool>(columns, value));
}

} // namespace

PhysicsLayerConfig PhysicsLayerConfig::makeDefault() {
    PhysicsLayerConfig config;
    config.layers = {
        {"static", 0},
        {"dynamic", 1},
        {"player", 2},
        {"enemy", 3},
        {"projectile", 4},
        {"pickup", 1},
    };

    config.objectCollisions = makeMatrix(DefaultPhysicsLayers::Count, DefaultPhysicsLayers::Count, false);
    config.broadPhaseCollisions = makeMatrix(DefaultPhysicsLayers::Count, DefaultPhysicsLayers::Count, false);

    const auto allowObject = [&config](PhysicsObjectLayer left, PhysicsObjectLayer right) {
        config.objectCollisions[left][right] = true;
        config.objectCollisions[right][left] = true;
    };
    const auto allowBroadPhase = [&config](PhysicsObjectLayer left, PhysicsObjectLayer right) {
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

    for (PhysicsObjectLayer layer = 0; layer < DefaultPhysicsLayers::Count; ++layer) {
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

std::optional<PhysicsObjectLayer> PhysicsLayerConfigBuilder::findLayer(std::string_view nameOrNumber) const {
    const std::string value(nameOrNumber);
    for (PhysicsObjectLayer index = 0; index < layers_.size(); ++index) {
        if (layers_[index].name == value) {
            return index;
        }
    }

    try {
        size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed);
        if (consumed == value.size() && parsed < layers_.size()) {
            return static_cast<PhysicsObjectLayer>(parsed);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

PhysicsLayerConfig PhysicsLayerConfigBuilder::build() const {
    PhysicsLayerConfig config;

    std::unordered_map<std::string, PhysicsObjectLayer> broadPhaseLayers;
    for (const DraftLayer& layer : layers_) {
        auto found = broadPhaseLayers.find(layer.broadPhaseName);
        if (found == broadPhaseLayers.end()) {
            const auto broadPhaseIndex = static_cast<PhysicsObjectLayer>(broadPhaseLayers.size());
            found = broadPhaseLayers.emplace(layer.broadPhaseName, broadPhaseIndex).first;
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
            const size_t broadPhaseRight = static_cast<size_t>(config.layers[right].broadPhase);
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
    const std::optional<PhysicsObjectLayer> leftLayer = findLayer(left);
    const std::optional<PhysicsObjectLayer> rightLayer = findLayer(right);
    if (!leftLayer || !rightLayer) {
        return false;
    }

    objectCollisions_[*leftLayer][*rightLayer] = shouldCollide;
    objectCollisions_[*rightLayer][*leftLayer] = shouldCollide;
    return true;
}

} // namespace tremor::physics

#pragma once

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>

namespace VampireSurvivors {

// Components
struct Position {
    glm::vec3 value{0.0f};
};

struct Velocity {
    glm::vec3 value{0.0f};
};

struct Rotation {
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

struct Scale {
    glm::vec3 value{1.0f};
};

struct Health {
    float current = 100.0f;
    float max = 100.0f;
};

struct Damage {
    float value = 10.0f;
    float cooldown = 0.0f;
    float attackRate = 1.0f;
};

struct Speed {
    float value = 5.0f;
};

struct CollisionRadius {
    float value = 0.5f;
};

struct Experience {
    float current = 0.0f;
    float toNextLevel = 10.0f;
    int level = 1;
};

struct XPOrb {
    float value = 1.0f;
    float magnetRange = 0.0f;
    bool magnetized = false;
};

struct PickupRadius {
    float value = 2.0f;
};

// Tags
struct Player {};
struct Enemy {};
struct Projectile {
    int piercing = 1;
    float lifetime = 5.0f;
};

// Weapon components
struct WeaponKnife {
    float damage = 10.0f;
    float cooldown = 0.0f;
    float attackSpeed = 2.0f;
    int projectileCount = 1;
    float range = 15.0f;
};

struct WeaponGarlic {
    float damage = 5.0f;
    float radius = 3.0f;
    float tickRate = 2.0f;
    float cooldown = 0.0f;
};

struct WeaponMagic {
    float damage = 15.0f;
    float cooldown = 0.0f;
    float attackSpeed = 1.0f;
    int projectileCount = 1;
    float range = 20.0f;
};

struct WeaponWhip {
    float damage = 20.0f;
    float cooldown = 0.0f;
    float attackSpeed = 1.5f;
    float arc = 180.0f; // degrees
    float range = 5.0f;
};

// Enemy AI
struct EnemyAI {
    flecs::entity target;
    float aggroRange = 30.0f;
};

// Wave spawning
struct WaveSpawner {
    int currentWave = 1;
    float timeSinceLastSpawn = 0.0f;
    float spawnInterval = 2.0f;
    int enemiesPerWave = 10;
    int enemiesSpawned = 0;
};

// Rendering component
struct MeshRenderer {
    uint32_t meshId = 0;
    glm::vec4 color{1.0f};
};

class Game {
private:
    flecs::world world;
    flecs::entity player;
    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> angleDist{0.0f, 2.0f * 3.14159f};
    std::uniform_real_distribution<float> radiusDist{10.0f, 20.0f};

public:
    Game() {
        setupComponents();
        setupSystems();
        createPlayer();
        createWaveSpawner();
    }

    void update(float deltaTime) {
        world.progress(deltaTime);
    }

    void movePlayer(const glm::vec3& direction) {
        player.set<Velocity>({direction * player.get<Speed>()->value});
    }

    flecs::entity getPlayer() { return player; }
    flecs::world& getWorld() { return world; }

private:
    void setupComponents() {
        world.component<Position>();
        world.component<Velocity>();
        world.component<Rotation>();
        world.component<Scale>();
        world.component<Health>();
        world.component<Damage>();
        world.component<Speed>();
        world.component<CollisionRadius>();
        world.component<Experience>();
        world.component<XPOrb>();
        world.component<PickupRadius>();
        world.component<Player>();
        world.component<Enemy>();
        world.component<Projectile>();
        world.component<WeaponKnife>();
        world.component<WeaponGarlic>();
        world.component<WeaponMagic>();
        world.component<WeaponWhip>();
        world.component<EnemyAI>();
        world.component<WaveSpawner>();
        world.component<MeshRenderer>();
    }

    void setupSystems() {
        // Movement system
        world.system<Position, const Velocity>("MovementSystem")
            .each([](flecs::entity e, Position& pos, const Velocity& vel) {
                pos.value += vel.value * e.delta_time();
            });

        // Enemy AI system
        world.system<Position, Velocity, const Speed, const EnemyAI, const Enemy>("EnemyAISystem")
            .each([](flecs::entity e, Position& pos, Velocity& vel, const Speed& speed, const EnemyAI& ai, const Enemy&) {
                if (ai.target) {
                    auto targetPos = ai.target.get<Position>();
                    if (targetPos) {
                        glm::vec3 direction = targetPos->value - pos.value;
                        float distance = glm::length(direction);

                        if (distance > 0.1f && distance < ai.aggroRange) {
                            direction = glm::normalize(direction);
                            vel.value = direction * speed.value;
                        } else {
                            vel.value = glm::vec3(0.0f);
                        }
                    }
                }
            });

        // Projectile lifetime system
        world.system<Projectile>("ProjectileLifetimeSystem")
            .each([](flecs::entity e, Projectile& proj) {
                proj.lifetime -= e.delta_time();
                if (proj.lifetime <= 0) {
                    e.destruct();
                }
            });

        // Collision system
        world.system<const Position, const CollisionRadius>("CollisionSystem")
            .each([this](flecs::entity e1, const Position& pos1, const CollisionRadius& rad1) {
                world.each([&](flecs::entity e2, const Position& pos2, const CollisionRadius& rad2) {
                    if (e1 == e2) return;

                    float dist = glm::length(pos1.value - pos2.value);
                    if (dist < rad1.value + rad2.value) {
                        handleCollision(e1, e2);
                    }
                });
            });

        // XP magnet system
        world.system<Position, Velocity, XPOrb>("XPMagnetSystem")
            .each([this](flecs::entity e, Position& pos, Velocity& vel, XPOrb& orb) {
                auto playerPos = player.get<Position>();
                auto pickupRad = player.get<PickupRadius>();

                if (playerPos && pickupRad) {
                    float dist = glm::length(playerPos->value - pos.value);

                    if (dist < pickupRad->value || orb.magnetized) {
                        orb.magnetized = true;
                        glm::vec3 direction = playerPos->value - pos.value;
                        if (glm::length(direction) > 0.01f) {
                            direction = glm::normalize(direction);
                            vel.value = direction * 10.0f;
                        }
                    }
                }
            });

        // Wave spawning system
        world.system<WaveSpawner>("WaveSpawnSystem")
            .each([this](flecs::entity e, WaveSpawner& spawner) {
                spawner.timeSinceLastSpawn += e.delta_time();

                if (spawner.timeSinceLastSpawn >= spawner.spawnInterval) {
                    spawner.timeSinceLastSpawn = 0.0f;

                    if (spawner.enemiesSpawned < spawner.enemiesPerWave) {
                        spawnEnemy(spawner.currentWave);
                        spawner.enemiesSpawned++;
                    } else {
                        // Wave complete, prepare next wave
                        spawner.currentWave++;
                        spawner.enemiesSpawned = 0;
                        spawner.enemiesPerWave = 10 + spawner.currentWave * 2;
                        spawner.spawnInterval = std::max(0.5f, 2.0f - spawner.currentWave * 0.1f);
                    }
                }
            });

        // Weapon systems
        setupWeaponSystems();
    }

    void setupWeaponSystems() {
        // Knife weapon system
        world.system<WeaponKnife>("KnifeWeaponSystem")
            .each([this](flecs::entity e, WeaponKnife& knife) {
                knife.cooldown -= e.delta_time();

                if (knife.cooldown <= 0) {
                    knife.cooldown = 1.0f / knife.attackSpeed;

                    // Find nearest enemy
                    flecs::entity nearestEnemy;
                    float nearestDist = knife.range;
                    auto playerPos = player.get<Position>();

                    if (playerPos) {
                        world.each([&](flecs::entity enemy, const Position& enemyPos, const Enemy&) {
                            float dist = glm::length(enemyPos.value - playerPos->value);
                            if (dist < nearestDist) {
                                nearestDist = dist;
                                nearestEnemy = enemy;
                            }
                        });

                        if (nearestEnemy) {
                            auto enemyPos = nearestEnemy.get<Position>();
                            if (enemyPos) {
                                glm::vec3 direction = glm::normalize(enemyPos->value - playerPos->value);

                                // Create projectiles
                                for (int i = 0; i < knife.projectileCount; i++) {
                                    float angle = (i - knife.projectileCount/2) * 0.1f;
                                    glm::vec3 projDir = direction;
                                    float cosA = cos(angle);
                                    float sinA = sin(angle);
                                    projDir.x = direction.x * cosA - direction.z * sinA;
                                    projDir.z = direction.x * sinA + direction.z * cosA;

                                    auto proj = world.entity()
                                        .set<Position>({playerPos->value})
                                        .set<Velocity>({projDir * 10.0f})
                                        .set<CollisionRadius>({0.2f})
                                        .set<Damage>({knife.damage})
                                        .set<Projectile>({1, 5.0f})
                                        .set<MeshRenderer>({1, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)});
                                }
                            }
                        }
                    }
                }
            });

        // Garlic weapon system
        world.system<WeaponGarlic>("GarlicWeaponSystem")
            .each([this](flecs::entity e, WeaponGarlic& garlic) {
                garlic.cooldown -= e.delta_time();

                if (garlic.cooldown <= 0) {
                    garlic.cooldown = 1.0f / garlic.tickRate;

                    auto playerPos = player.get<Position>();
                    if (playerPos) {
                        // Damage all enemies in radius
                        world.each([&](flecs::entity enemy, const Position& enemyPos, Health& health, const Enemy&) {
                            float dist = glm::length(enemyPos.value - playerPos->value);
                            if (dist < garlic.radius) {
                                health.current -= garlic.damage;
                                if (health.current <= 0) {
                                    spawnXPOrb(enemyPos.value);
                                    enemy.destruct();
                                }
                            }
                        });
                    }
                }
            });
    }

    void createPlayer() {
        player = world.entity("Player")
            .set<Position>({glm::vec3(0.0f, 0.0f, 0.0f)})
            .set<Velocity>({glm::vec3(0.0f)})
            .set<Rotation>({glm::quat(1.0f, 0.0f, 0.0f, 0.0f)})
            .set<Scale>({glm::vec3(1.0f)})
            .set<Health>({100.0f, 100.0f})
            .set<Speed>({5.0f})
            .set<CollisionRadius>({0.5f})
            .set<Experience>({0.0f, 10.0f, 1})
            .set<PickupRadius>({2.0f})
            .set<Player>({})
            .set<MeshRenderer>({0, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)})
            .set<WeaponKnife>({}); // Start with knife weapon
    }

    void createWaveSpawner() {
        world.entity("WaveSpawner")
            .set<WaveSpawner>({});
    }

    void spawnEnemy(int wave) {
        float angle = angleDist(rng);
        float radius = radiusDist(rng);

        auto playerPos = player.get<Position>();
        glm::vec3 spawnPos = playerPos ?
            playerPos->value + glm::vec3(cos(angle) * radius, 0.0f, sin(angle) * radius) :
            glm::vec3(cos(angle) * radius, 0.0f, sin(angle) * radius);

        float healthMult = 1.0f + wave * 0.2f;
        float speedMult = 1.0f + wave * 0.05f;
        float damageMult = 1.0f + wave * 0.1f;

        auto enemy = world.entity()
            .set<Position>({spawnPos})
            .set<Velocity>({glm::vec3(0.0f)})
            .set<Health>({10.0f * healthMult, 10.0f * healthMult})
            .set<Speed>({2.0f * speedMult})
            .set<Damage>({10.0f * damageMult})
            .set<CollisionRadius>({0.4f})
            .set<Enemy>({})
            .set<EnemyAI>({player})
            .set<MeshRenderer>({2, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)});
    }

    void spawnXPOrb(const glm::vec3& position) {
        world.entity()
            .set<Position>({position})
            .set<Velocity>({glm::vec3(0.0f)})
            .set<CollisionRadius>({0.3f})
            .set<XPOrb>({1.0f, 0.0f, false})
            .set<MeshRenderer>({3, glm::vec4(0.0f, 0.5f, 1.0f, 1.0f)});
    }

    void handleCollision(flecs::entity e1, flecs::entity e2) {
        // Player-Enemy collision
        if (e1.has<Player>() && e2.has<Enemy>()) {
            auto health = e1.get<Health>();
            auto damage = e2.get<Damage>();
            if (health && damage && damage->cooldown <= 0) {
                health->current -= damage->value;
                damage->cooldown = 1.0f;
            }
        }

        // Projectile-Enemy collision
        if (e1.has<Projectile>() && e2.has<Enemy>()) {
            auto projDamage = e1.get<Damage>();
            auto enemyHealth = e2.get<Health>();
            auto proj = e1.get<Projectile>();

            if (projDamage && enemyHealth && proj) {
                enemyHealth->current -= projDamage->value;
                proj->piercing--;

                if (enemyHealth->current <= 0) {
                    auto pos = e2.get<Position>();
                    if (pos) spawnXPOrb(pos->value);
                    e2.destruct();
                }

                if (proj->piercing <= 0) {
                    e1.destruct();
                }
            }
        }

        // Player-XP collision
        if (e1.has<Player>() && e2.has<XPOrb>()) {
            auto exp = e1.get<Experience>();
            auto orb = e2.get<XPOrb>();

            if (exp && orb) {
                exp->current += orb->value;
                while (exp->current >= exp->toNextLevel) {
                    exp->current -= exp->toNextLevel;
                    exp->level++;
                    exp->toNextLevel *= 1.5f;
                    // TODO: Trigger upgrade selection UI
                }
                e2.destruct();
            }
        }
    }
};

} // namespace VampireSurvivors
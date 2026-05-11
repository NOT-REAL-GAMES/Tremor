#pragma once

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Important for Vulkan depth range
#endif

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../Taffy/include/quan.h"
#include "flecs_interpreter.h"
#include "vk.h"  // Include VulkanBackend for rendering
#include "dmc_physics.h"  // Include Jolt Physics integration
#include "jolt_physics_adapter.h"
#include "physics_interop.h"
#include "render_interop.h"
#include "script_ecs_components.h"
#include "script_render_system.h"
#include <random>
#include <cmath>
#include <optional>
#include <string>
#include <deque>
#include <vector>
#include <utility>
#include "logger.h"

namespace DMCSurvivors {

// Core Components
struct Position {
    Vec3Q quantized{0, 0, 0};        // Precise quantized position
    glm::vec3 cached{0.0f};          // Cached float version for physics

    Position() = default;
    Position(const glm::vec3& pos) : quantized(Vec3Q::fromFloat(pos)), cached(pos) {}
    Position(const Vec3Q& pos) : quantized(pos), cached(pos.toFloat()) {}

    // Update cached version when quantized changes
    void updateCache() { cached = quantized.toFloat(); }

    // Set from float and update both
    void setFloat(const glm::vec3& pos) {
        cached = pos;
        quantized = Vec3Q::fromFloat(pos);
    }

    // Get current position
    glm::vec3 getFloat() const { return cached; }
    Vec3Q getQuantized() const { return quantized; }
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
    float regenRate = 0.0f;
};

struct CollisionRadius {
    float value = 0.5f;
};

// Combat Components
struct CombatStats {
    float damage = 10.0f;
    float attackSpeed = 1.0f;
    float critChance = 0.1f;
    float critMultiplier = 2.0f;
};

struct ComboState {
    int hitCount = 0;
    float timer = 0.0f;
    float maxTime = 2.0f; // Time before combo resets
    std::string currentCombo = "";
    std::deque<char> inputBuffer;
    float inputBufferTime = 0.5f;
};

enum StyleRank {
    DISMAL = 0,
    CRAZY = 1,
    BADASS = 2,
    APOCALYPTIC = 3,
    SAVAGE = 4,
    SICK_SKILLS = 5,
    SMOKIN_SEXY_STYLE = 6
};

struct StyleMeter {
    float points = 0.0f;
    StyleRank rank = DISMAL;
    float decayRate = 2.0f; // Points lost per second
    float multiplier = 1.0f;

    static const char* getRankName(StyleRank rank) {
        switch(rank) {
            case DISMAL: return "Dismal";
            case CRAZY: return "Crazy!";
            case BADASS: return "Badass!!";
            case APOCALYPTIC: return "Apocalyptic!!!";
            case SAVAGE: return "Savage!!!!";
            case SICK_SKILLS: return "SicK SkillS!!!!!";
            case SMOKIN_SEXY_STYLE: return "SSS!!!!!!";
            default: return "Unknown";
        }
    }
};

// Movement Components
struct MovementState {
    float moveSpeed = 7.0f;
    float dashSpeed = 20.0f;
    float dashDuration = 0.2f;
    float dashCooldown = 0.0f;
    float dashCooldownMax = 1.0f;
    bool isDashing = false;
    float dashTimer = 0.0f;
    glm::vec3 dashDirection{0.0f};
    int airDashesRemaining = 1;
    int maxAirDashes = 1;
};

struct JumpState {
    bool isGrounded = true;
    bool isJumping = false;
    float jumpForce = 15.0f;
    float gravity = 30.0f;
    float groundY = 0.0f;
    int jumpsRemaining = 2; // Double jump
    int maxJumps = 2;
};

// Attack Components
enum AttackType {
    LIGHT_ATTACK,
    HEAVY_ATTACK,
    LAUNCHER,
    AIR_COMBO,
    HELM_BREAKER, // Downward slam
    STINGER,      // Forward dash attack
    HIGH_TIME,    // Launcher with follow-up
    MILLION_STAB  // Rapid stabs
};

struct AttackState {
    bool isAttacking = false;
    AttackType currentAttack = LIGHT_ATTACK;
    float attackTimer = 0.0f;
    float attackDuration = 0.0f;
    int attackChain = 0; // For chaining light attacks
    float attackCooldown = 0.0f;
};

struct LaunchState {
    bool isLaunched = false;
    float launchHeight = 5.0f;
    float launchTime = 0.0f;
    float juggleWindow = 2.0f;
};

// Weapon Components
enum WeaponType {
    REBELLION,    // Balanced sword
    EBONY_IVORY,  // Dual pistols
    CERBERUS,     // Tri-nunchaku
    BALROG,       // Fist/kick weapons
    CAVALIERE     // Motorcycle buzzsaws
};

struct WeaponSlot {
    WeaponType equipped = REBELLION;
    float switchCooldown = 0.0f;
    float switchSpeed = 0.2f;
};

struct GunState {
    int ammo = 30;
    int maxAmmo = 30;
    float fireRate = 10.0f;
    float reloadTime = 1.5f;
    bool isReloading = false;
    float reloadTimer = 0.0f;
};

// Enemy Components
struct EnemyAI {
    flecs::entity target;
    float aggroRange = 30.0f;
    float attackRange = 2.0f;
    float attackCooldown = 0.0f;
    float stunDuration = 0.0f;
};

struct Experience {
    float current = 0.0f;
    float toNextLevel = 100.0f;
    int level = 1;
};

struct RedOrb {
    float value = 10.0f;
    float magnetRange = 5.0f;
    bool magnetized = false;
};

// Wave System
struct WaveSpawner {
    int currentWave = 1;
    float timeSinceLastSpawn = 0.0f;
    float spawnInterval = 3.0f;
    int enemiesPerWave = 5;
    int enemiesSpawned = 0;
    float waveIntensity = 1.0f;
};

// Rendering
struct MeshRenderer {
    uint32_t meshId = 0;
    glm::vec4 color{1.0f};
};

struct ParticleEffect {
    std::string effectName;
    float lifetime = 1.0f;
};

// PhysicsBody component is defined in dmc_physics.h

// Tags (add a dummy member to avoid zero-size issues)
struct Player {
    char _dummy = 0;
};
struct Enemy {
    char _dummy = 0;
};
struct Boss {
    char _dummy = 0;
};
struct Projectile {
    float lifetime = 5.0f;
    int piercing = 1;
};

// Input handling
struct InputCommand {
    bool lightAttack = false;
    bool heavyAttack = false;
    bool jump = false;
    bool dash = false;
    bool shoot = false;
    bool weaponSwitch = false;
    bool lockOn = false;
    glm::vec3 moveDirection{0.0f};
    glm::vec3 aimDirection{0.0f};
};

class Game {
private:
    flecs::world world;
    flecs::entity player;
    flecs::entity currentTarget; // Lock-on target
    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> angleDist{0.0f, 2.0f * 3.14159f};
    std::uniform_real_distribution<float> radiusDist{15.0f, 25.0f};
    float gameTime = 0.0f;
    std::vector<flecs::entity> entitiesMarkedForDeletion;
    std::unique_ptr<tremor::script::FlecsInterpreterHost> interpreterHost;

    // Jolt Physics integration
    std::unique_ptr<DMCSurvivors::PhysicsWorld> physicsWorld;
    std::unique_ptr<tremor::physics::JoltPhysicsAdapter> physicsAdapter;
    tremor::physics::PhysicsLayerConfigBuilder physicsLayerConfig;
    tremor::render::RenderInteropRegistry renderRegistry;
    tremor::render::ScriptRenderCamera renderCamera;

public:
    Game() {
        // Initialize physics world first
        initializePhysics();

        setupComponents();
        setupInterpreterHost();
        setupSystems();
        createPlayer();
        createWaveSpawner();
        loadBuiltInInterpreterPrograms();
        if (interpreterHost) {
            interpreterHost->update(0.0f);
            primeInitialWaveFromScript();
            interpreterHost->emitEvent("game_start");
        }
    }

    void update(float deltaTime) {
        gameTime += deltaTime;

        // Update physics world
        if (physicsWorld) {
            physicsWorld->Update(deltaTime);
        }

        world.progress(deltaTime);
        if (interpreterHost) {
            interpreterHost->update(deltaTime);
        }

        // Process entity deletion queue after all systems have run
        for (auto& entity : entitiesMarkedForDeletion) {
            entity.destruct();
        }
        entitiesMarkedForDeletion.clear();
    }

    void processInput(const InputCommand& input) {
        if (!player) return;

        auto movement = player.get_mut<MovementState>();
        auto attack = player.get_mut<AttackState>();
        auto jump = player.get_mut<JumpState>();
        auto combo = player.get_mut<ComboState>();

        // Movement - always update velocity (even if zero)
        if (movement) {
            // Calculate horizontal movement velocity
            glm::vec3 moveVel = input.moveDirection * movement->moveSpeed;

            // Set velocity (Y will be handled by physics/gravity)
            player.set<Velocity>({glm::vec3(moveVel.x, 0.0f, moveVel.z)});

            // Dash
            if (input.dash && movement->dashCooldown <= 0 && !movement->isDashing) {
                startDash(input.moveDirection);
            }
        }

        // Jump
        if (input.jump && jump) {
            if (jump->isGrounded || jump->jumpsRemaining > 0) {
                performJump();
            }
        }

        // Combat
        if (attack && !attack->isAttacking) {
            if (input.lightAttack) {
                performAttack(LIGHT_ATTACK);
                if (combo) combo->inputBuffer.push_back('L');
            } else if (input.heavyAttack) {
                performAttack(HEAVY_ATTACK);
                if (combo) combo->inputBuffer.push_back('H');
            }
        }

        // Lock-on
        if (input.lockOn) {
            toggleLockOn();
        }
    }

    flecs::entity getPlayer() { return player; }
    flecs::world& getWorld() { return world; }

    // Render all entities - called from main loop
    void renderEntities(void* renderBackend) {
        if (tremor::render::renderScriptFrame(renderRegistry, renderCamera, world, renderBackend)) {
            return;
        }

        // Cast to VulkanBackend for mesh rendering
        auto* vulkanBackend = static_cast<tremor::gfx::VulkanBackend*>(renderBackend);
        if (!vulkanBackend) return;

        // Get overlay manager for rendering TAF assets
        auto* overlayManager = vulkanBackend->m_overlayManager.get();
        if (!overlayManager) return;

        // Get current command buffer and create a view-projection matrix that follows the player
        VkCommandBuffer cmd = vulkanBackend->getCurrentCommandBuffer();
        const VkExtent2D swapchainExtent = vulkanBackend->getSwapchainExtent();
        if (swapchainExtent.width == 0 || swapchainExtent.height == 0) {
            return;
        }
        const float aspectRatio =
            static_cast<float>(swapchainExtent.width) /
            static_cast<float>(swapchainExtent.height);

        // Get player position for camera following
        glm::vec3 playerPos{0.0f, 0.0f, 0.0f};
        world.each([&](flecs::entity entity, const Position& pos, const Player& playerTag) {
            playerPos = pos.getFloat();
        });

        // Create a camera that follows the player from behind and above
        glm::vec3 cameraOffset(0, -10, 12);  // Behind and above the player
        Vec3Q cameraPos = Vec3Q::fromFloat(playerPos);
        glm::vec3 lookTarget = glm::vec3(0, 2, 0); // Look slightly above player

        glm::mat4 view = glm::lookAt(
            cameraOffset,      // Camera position (follows player)
            lookTarget,     // Look at player
            glm::vec3(0, 1, 0)  // Up vector
        );
        glm::mat4 proj = glm::perspectiveZO(
            glm::radians(45.0f),   // FOV
            aspectRatio,          // Aspect ratio
            100000.0f,                 // Near plane
            0.1f               // Far plane
        );


        glm::mat4 viewProj = proj * view;

        // Debug output for player position and camera
        static int frameCount = 0;
        if (frameCount++ % 60 == 0) {
            /*printf("Player at: %.2f, %.2f, %.2f | Camera at: %.2f, %.2f, %.2f\n",
                   playerPos.x, playerPos.y, playerPos.z,
                   cameraPos.x, cameraPos.y, cameraPos.z);*/
        }

        // Render player entities
        world.each([&](flecs::entity entity, const Position& pos, const Player& playerTag, const MeshRenderer& mesh) {
            glm::vec3 worldPos = pos.quantized.relativeTo(cameraPos);
            glm::mat4 transform = glm::scale(glm::translate(glm::mat4(1.0f), worldPos), glm::vec3(1.0f, 2.0f, 1.0f));
            overlayManager->renderMeshAsset("assets/cube.taf", cmd, viewProj * transform);
        });

        // Render enemy entities
        world.each([&](flecs::entity entity, const Position& pos, const Enemy& enemyTag, const MeshRenderer& mesh) {
            glm::vec3 worldPos = pos.quantized.relativeTo(cameraPos);
            glm::mat4 transform = glm::scale(glm::translate(glm::mat4(1.0f), worldPos), glm::vec3(1.2f));
            overlayManager->renderMeshAsset("assets/sphere.taf", cmd, viewProj * transform);
        });

        // Render projectiles
        world.each([&](flecs::entity entity, const Position& pos, const Projectile& proj, const MeshRenderer& mesh) {
            glm::vec3 worldPos = pos.quantized.relativeTo(cameraPos);
            glm::mat4 transform = glm::scale(glm::translate(glm::mat4(1.0f), worldPos), glm::vec3(0.2f));
            overlayManager->renderMeshAsset("assets/cube.taf", cmd, viewProj * transform);
        });

        // Render XP orbs
        world.each([&](flecs::entity entity, const Position& pos, const RedOrb& orb, const MeshRenderer& mesh) {
            glm::vec3 worldPos = pos.quantized.relativeTo(cameraPos);
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), worldPos);
            transform = glm::translate(transform, glm::vec3(0, 0.5f, 0));
            transform = glm::scale(transform, glm::vec3(0.4f));
            overlayManager->renderMeshAsset("assets/cube.taf", cmd, viewProj * transform);
        });
    }

private:
    void setupInterpreterHost() {
        interpreterHost = std::make_unique<tremor::script::FlecsInterpreterHost>(world);
        tremor::ecs::registerScriptComponentCommands(*interpreterHost);
        tremor::physics::registerPhysicsLayerConfigCommands(*interpreterHost, physicsLayerConfig);
        tremor::render::registerScriptRenderFrameCommands(*interpreterHost, renderRegistry, renderCamera);

        interpreterHost->registerCommand("set_wave_spawn_interval", [this](
            const tremor::script::CommandContext&,
            std::string_view argument
        ) {
            try {
                const float interval = std::stof(std::string(argument));
                world.each([interval](flecs::entity, WaveSpawner& spawner) {
                    spawner.spawnInterval = std::max(0.1f, interval);
                });
                Logger::get().info("Interpreter set wave spawn interval to {}", interval);
                return true;
            } catch (const std::exception&) {
                Logger::get().error("Interpreter failed to parse wave spawn interval '{}'", argument);
                return false;
            }
        });

        interpreterHost->registerCommand("set_wave_enemy_count", [this](
            const tremor::script::CommandContext&,
            std::string_view argument
        ) {
            try {
                const int enemiesPerWave = std::max(1, std::stoi(std::string(argument)));
                world.each([enemiesPerWave](flecs::entity, WaveSpawner& spawner) {
                    spawner.enemiesPerWave = enemiesPerWave;
                });
                Logger::get().info("Interpreter set enemies per wave to {}", enemiesPerWave);
                return true;
            } catch (const std::exception&) {
                Logger::get().error("Interpreter failed to parse enemies per wave '{}'", argument);
                return false;
            }
        });

        interpreterHost->registerCommand("spawn_enemy", [this](
            const tremor::script::CommandContext&,
            std::string_view
        ) {
            world.each([this](flecs::entity, WaveSpawner& spawner) {
                spawnEnemy(spawner.currentWave, spawner.waveIntensity);
            });
            Logger::get().info("Interpreter requested an enemy spawn");
            return true;
        });

        if (physicsWorld) {
            physicsAdapter = std::make_unique<tremor::physics::JoltPhysicsAdapter>(*physicsWorld);
            tremor::physics::registerPhysicsInteropCommands(*interpreterHost, *physicsAdapter);
        }
    }

    void applyWaveSpawnIntervalPolicy(WaveSpawner& spawner, float baseInterval) {
        if (!interpreterHost || !interpreterHost->hasBoundHostCallback("wave_spawn_interval_policy")) {
            return;
        }

        tremor::script::ValueMap callbackArgs;
        callbackArgs.emplace("wave", tremor::script::Value(static_cast<double>(spawner.currentWave)));
        callbackArgs.emplace("base_interval", tremor::script::Value(static_cast<double>(baseInterval)));
        callbackArgs.emplace("wave_intensity", tremor::script::Value(static_cast<double>(spawner.waveIntensity)));
        callbackArgs.emplace("enemies_per_wave", tremor::script::Value(static_cast<double>(spawner.enemiesPerWave)));

        std::string callbackError;
        std::optional<tremor::script::Value> callbackResult = interpreterHost->invokeHostCallback(
            "wave_spawn_interval_policy",
            callbackArgs,
            &callbackError
        );
        if (!callbackResult) {
            Logger::get().warn("Interpreter wave_spawn_interval_policy failed: {}", callbackError);
            return;
        }

        const std::optional<double> intervalValue = callbackResult->asNumber();
        if (!intervalValue) {
            Logger::get().warn(
                "Interpreter wave_spawn_interval_policy returned non-numeric value {}",
                callbackResult->debugString()
            );
            return;
        }

        spawner.spawnInterval = std::max(0.1f, static_cast<float>(*intervalValue));
        Logger::get().info(
            "Interpreter host bridge set wave {} spawn interval to {}",
            spawner.currentWave,
            spawner.spawnInterval
        );
    }

    void applyWaveEnemyCountPolicy(WaveSpawner& spawner, int baseCount) {
        if (!interpreterHost || !interpreterHost->hasBoundHostCallback("wave_enemy_count_policy")) {
            return;
        }

        tremor::script::ValueMap callbackArgs;
        callbackArgs.emplace("wave", tremor::script::Value(static_cast<double>(spawner.currentWave)));
        callbackArgs.emplace("base_count", tremor::script::Value(static_cast<double>(baseCount)));
        callbackArgs.emplace("wave_intensity", tremor::script::Value(static_cast<double>(spawner.waveIntensity)));
        callbackArgs.emplace("spawn_interval", tremor::script::Value(static_cast<double>(spawner.spawnInterval)));

        std::string callbackError;
        std::optional<tremor::script::Value> callbackResult = interpreterHost->invokeHostCallback(
            "wave_enemy_count_policy",
            callbackArgs,
            &callbackError
        );
        if (!callbackResult) {
            Logger::get().warn("Interpreter wave_enemy_count_policy failed: {}", callbackError);
            return;
        }

        const std::optional<double> countValue = callbackResult->asNumber();
        if (!countValue) {
            Logger::get().warn(
                "Interpreter wave_enemy_count_policy returned non-numeric value {}",
                callbackResult->debugString()
            );
            return;
        }

        spawner.enemiesPerWave = std::max(1, static_cast<int>(std::lround(*countValue)));
        Logger::get().info(
            "Interpreter host bridge set wave {} enemy count to {}",
            spawner.currentWave,
            spawner.enemiesPerWave
        );
    }

    void syncWaveStateToInterpreter(
        const WaveSpawner& spawner,
        int defaultEnemyCount = -1,
        float defaultSpawnInterval = -1.0f,
        float defaultWaveIntensity = -1.0f
    ) {
        if (!interpreterHost) {
            return;
        }

        std::string ignoredError;
        interpreterHost->setBlackboardValue("wave_state.current_wave", tremor::script::Value(static_cast<double>(spawner.currentWave)), &ignoredError);
        interpreterHost->setBlackboardValue("wave_state.enemies_spawned", tremor::script::Value(static_cast<double>(spawner.enemiesSpawned)), &ignoredError);
        interpreterHost->setBlackboardValue("wave_state.enemies_per_wave", tremor::script::Value(static_cast<double>(spawner.enemiesPerWave)), &ignoredError);
        interpreterHost->setBlackboardValue("wave_state.wave_intensity", tremor::script::Value(static_cast<double>(spawner.waveIntensity)), &ignoredError);
        interpreterHost->setBlackboardValue("wave_state.spawn_interval", tremor::script::Value(static_cast<double>(spawner.spawnInterval)), &ignoredError);

        if (defaultEnemyCount >= 0) {
            interpreterHost->setBlackboardValue("wave_state.default_enemy_count", tremor::script::Value(static_cast<double>(defaultEnemyCount)), &ignoredError);
        }
        if (defaultSpawnInterval >= 0.0f) {
            interpreterHost->setBlackboardValue("wave_state.default_spawn_interval", tremor::script::Value(static_cast<double>(defaultSpawnInterval)), &ignoredError);
        }
        if (defaultWaveIntensity >= 0.0f) {
            interpreterHost->setBlackboardValue("wave_state.default_wave_intensity", tremor::script::Value(static_cast<double>(defaultWaveIntensity)), &ignoredError);
        }
    }

    void applyWaveDescriptorPolicy(
        WaveSpawner& spawner,
        int defaultEnemyCount,
        float defaultSpawnInterval,
        float defaultWaveIntensity
    ) {
        if (!interpreterHost) {
            return;
        }

        syncWaveStateToInterpreter(
            spawner,
            defaultEnemyCount,
            defaultSpawnInterval,
            defaultWaveIntensity
        );

        if (interpreterHost->hasBoundHostCallback("wave_descriptor_policy")) {
            std::string callbackError;
            std::optional<tremor::script::Value> callbackResult = interpreterHost->invokeHostCallback(
                "wave_descriptor_policy",
                {},
                &callbackError
            );
            if (!callbackResult) {
                Logger::get().warn("Interpreter wave_descriptor_policy failed: {}", callbackError);
            } else if (const tremor::script::ObjectValue* descriptor = callbackResult->asObject()) {
                const auto applyNumericField = [descriptor](std::string_view name) -> std::optional<double> {
                    const auto found = descriptor->fields.find(std::string(name));
                    if (found == descriptor->fields.end()) {
                        return std::nullopt;
                    }
                    return found->second.asNumber();
                };

                if (const std::optional<double> enemyCount = applyNumericField("enemy_count")) {
                    spawner.enemiesPerWave = std::max(1, static_cast<int>(std::lround(*enemyCount)));
                }
                if (const std::optional<double> spawnInterval = applyNumericField("spawn_interval")) {
                    spawner.spawnInterval = std::max(0.1f, static_cast<float>(*spawnInterval));
                }
                if (const std::optional<double> waveIntensity = applyNumericField("wave_intensity")) {
                    spawner.waveIntensity = std::max(0.1f, static_cast<float>(*waveIntensity));
                }

                Logger::get().info(
                    "Interpreter wave descriptor applied: wave={} enemies={} interval={} intensity={}",
                    spawner.currentWave,
                    spawner.enemiesPerWave,
                    spawner.spawnInterval,
                    spawner.waveIntensity
                );
            } else {
                Logger::get().warn(
                    "Interpreter wave_descriptor_policy returned non-object value {}",
                    callbackResult->debugString()
                );
            }

            syncWaveStateToInterpreter(
                spawner,
                defaultEnemyCount,
                defaultSpawnInterval,
                defaultWaveIntensity
            );
            return;
        }

        applyWaveEnemyCountPolicy(spawner, defaultEnemyCount);
        applyWaveSpawnIntervalPolicy(spawner, defaultSpawnInterval);
        syncWaveStateToInterpreter(
            spawner,
            defaultEnemyCount,
            defaultSpawnInterval,
            defaultWaveIntensity
        );
    }

    void primeInitialWaveFromScript() {
        world.each([this](flecs::entity, WaveSpawner& spawner) {
            applyWaveDescriptorPolicy(
                spawner,
                spawner.enemiesPerWave,
                spawner.spawnInterval,
                spawner.waveIntensity
            );
        });
    }

    void loadBuiltInInterpreterPrograms() {
        if (!interpreterHost) {
            return;
        }

        static constexpr std::string_view kBuiltInInterpreterScript = R"SCRIPT(
program dmc_bootstrap

rule host_online
on_load
action log interpreter_host_online

rule announce_game_start
on_event game_start
action log game_start_received
action command emit_ui_message gameplay package loaded

rule announce_wave_start
on_event wave_started
action log wave_started_received
action command emit_ui_message wave advanced
)SCRIPT";

        interpreterHost->loadProgramFromText(kBuiltInInterpreterScript, "builtin://dmc_bootstrap.tafscript");

        const std::filesystem::path gameplayPackage = std::filesystem::path("assets") / "gameplay.taf";
        if (std::filesystem::exists(gameplayPackage)) {
            interpreterHost->loadProgramsFromPackage(gameplayPackage);
        }
    }

    void setupComponents() {
        world.component<Position>();
        world.component<Velocity>();
        world.component<Rotation>();
        world.component<Scale>();
        world.component<Health>();
        world.component<CollisionRadius>();
        world.component<CombatStats>();
        world.component<ComboState>();
        world.component<StyleMeter>();
        world.component<MovementState>();
        world.component<JumpState>();
        world.component<AttackState>();
        world.component<LaunchState>();
        world.component<WeaponSlot>();
        world.component<GunState>();
        world.component<EnemyAI>();
        world.component<Experience>();
        world.component<RedOrb>();
        world.component<WaveSpawner>();
        world.component<MeshRenderer>();
        world.component<ParticleEffect>();
        world.component<PhysicsBody>();
        world.component<Player>();
        world.component<Enemy>();
        world.component<Boss>();
        world.component<Projectile>();
        world.component<InputCommand>();
    }

    void setupSystems() {
        // Movement system - applies movement forces while preserving physics velocity
        world.system<const Velocity, const PhysicsBody>("PhysicsMovementSystem")
            .each([this](flecs::entity e, const Velocity& vel, const PhysicsBody& physicsBody) {
                if (physicsWorld && !physicsBody.bodyId.IsInvalid()) {
                    // Get current physics velocity
                    glm::vec3 currentVel = physicsWorld->GetBodyVelocity(physicsBody.bodyId);

                    // Only override X and Z (horizontal movement), preserve Y (gravity/jumping)
                    glm::vec3 newVel = currentVel;
                    newVel.x = vel.value.x;
                    newVel.z = vel.value.z;

                    physicsWorld->SetBodyVelocity(physicsBody.bodyId, newVel);
                }
            });

        // Physics sync system - sync ECS positions FROM physics bodies AFTER physics update
        world.system<Position, const PhysicsBody>("PhysicsSyncSystem")
            .each([this](flecs::entity e, Position& pos, const PhysicsBody& physicsBody) {
                if (physicsWorld && !physicsBody.bodyId.IsInvalid()) {
                    // Get position from physics body (physics is authoritative)
                    glm::vec3 physicsPos = physicsWorld->GetBodyPosition(physicsBody.bodyId);
                    pos.setFloat(physicsPos);
                }
            });

        // Jump state system - physics handles gravity automatically
        world.system<const Position, JumpState, const PhysicsBody>("JumpStateSystem")
            .each([this](flecs::entity e, const Position& pos, JumpState& jump, const PhysicsBody& physicsBody) {
                float currentY = pos.getFloat().y;

                // Check if grounded based on physics body position
                if (currentY <= jump.groundY + 0.1f && !jump.isGrounded) {
                    jump.isGrounded = true;
                    jump.jumpsRemaining = jump.maxJumps;
                } else if (currentY > jump.groundY + 0.1f && jump.isGrounded) {
                    jump.isGrounded = false;
                }
            });


        // Dash system
        world.system<Position, Velocity, MovementState>("DashSystem")
            .each([](flecs::entity e, Position& pos, Velocity& vel, MovementState& movement) {
                float dt = e.world().delta_time();

                // Update dash cooldown
                if (movement.dashCooldown > 0) {
                    movement.dashCooldown -= dt;
                }

                // Handle dashing
                if (movement.isDashing) {
                    movement.dashTimer += dt;
                    vel.value = movement.dashDirection * movement.dashSpeed;

                    if (movement.dashTimer >= movement.dashDuration) {
                        movement.isDashing = false;
                        movement.dashTimer = 0;
                    }
                }
            });

        // Combat system
        world.system<AttackState, CombatStats>("CombatSystem")
            .each([this](flecs::entity e, AttackState& attack, const CombatStats& stats) {
                float dt = e.world().delta_time();

                if (attack.isAttacking) {
                    attack.attackTimer += dt;

                    if (attack.attackTimer >= attack.attackDuration) {
                        attack.isAttacking = false;
                        attack.attackTimer = 0;
                        attack.attackCooldown = 0.1f; // Small cooldown between attacks
                    }
                }

                if (attack.attackCooldown > 0) {
                    attack.attackCooldown -= dt;
                }
            });

        // Combo system
        world.system<ComboState, StyleMeter>("ComboSystem")
            .each([](flecs::entity e, ComboState& combo, StyleMeter& style) {
                float dt = e.world().delta_time();

                // Update combo timer
                if (combo.hitCount > 0) {
                    combo.timer += dt;

                    if (combo.timer >= combo.maxTime) {
                        // Combo dropped
                        combo.hitCount = 0;
                        combo.timer = 0;
                        combo.currentCombo = "";
                        style.points *= 0.5f; // Penalty for dropping combo
                    }
                }

                // Update style meter
                if (style.points > 0) {
                    style.points -= style.decayRate * dt;
                    if (style.points < 0) style.points = 0;
                }

                // Calculate style rank
                if (style.points >= 5000) style.rank = SMOKIN_SEXY_STYLE;
                else if (style.points >= 3000) style.rank = SICK_SKILLS;
                else if (style.points >= 2000) style.rank = SAVAGE;
                else if (style.points >= 1000) style.rank = APOCALYPTIC;
                else if (style.points >= 500) style.rank = BADASS;
                else if (style.points >= 100) style.rank = CRAZY;
                else style.rank = DISMAL;

                style.multiplier = 1.0f + static_cast<float>(style.rank) * 0.5f;
            });

        // Enemy AI system
        world.system<Position, Velocity, const EnemyAI, const Enemy>("EnemyAISystem")
            .each([](flecs::entity e, Position& pos, Velocity& vel, const EnemyAI& ai, const Enemy&) {
                if (ai.stunDuration > 0) {
                    vel.value = glm::vec3(0.0f);
                    return;
                }

                if (ai.target) {
                    auto targetPos = ai.target.get<Position>();
                    if (targetPos) {
                        glm::vec3 direction = targetPos->getFloat() - pos.getFloat();
                        float distance = glm::length(direction);

                        if (distance > ai.attackRange && distance < ai.aggroRange) {
                            direction.y = 0; // Keep on ground
                            direction = glm::normalize(direction);
                            vel.value = direction * 3.0f; // Enemy speed
                        } else if (distance <= ai.attackRange) {
                            vel.value = glm::vec3(0.0f);
                            // Attack logic would go here
                        }
                    }
                }
            });

        // Launch/Juggle system
        world.system<Position, Velocity, LaunchState>("LaunchSystem")
            .each([](flecs::entity e, Position& pos, Velocity& vel, LaunchState& launch) {
                if (launch.isLaunched) {
                    launch.launchTime += e.world().delta_time();

                    if (launch.launchTime < 0.3f) {
                        // Upward launch
                        vel.value.y = 10.0f;
                    }

                    if (launch.launchTime >= launch.juggleWindow) {
                        launch.isLaunched = false;
                        launch.launchTime = 0;
                    }
                }
            });

        // Red Orb magnet system
        world.system<Position, Velocity, RedOrb>("OrbMagnetSystem")
            .each([this](flecs::entity e, Position& pos, Velocity& vel, RedOrb& orb) {
                auto playerPos = player.get<Position>();

                if (playerPos) {
                    float dist = glm::length(playerPos->getFloat() - pos.getFloat());

                    if (dist < orb.magnetRange || orb.magnetized) {
                        orb.magnetized = true;
                        glm::vec3 direction = playerPos->getFloat() - pos.getFloat();
                        if (glm::length(direction) > 0.01f) {
                            direction = glm::normalize(direction);
                            vel.value = direction * 15.0f;
                        }
                    }
                }
            });

        // Collision system
        setupCollisionSystem();

        // Wave spawning system
        world.system<WaveSpawner>("WaveSpawnSystem")
            .each([this](flecs::entity e, WaveSpawner& spawner) {
                spawner.timeSinceLastSpawn += e.world().delta_time();

                if (spawner.timeSinceLastSpawn >= spawner.spawnInterval) {
                    spawner.timeSinceLastSpawn = 0.0f;

                    if (spawner.enemiesSpawned < spawner.enemiesPerWave) {
                        spawnEnemy(spawner.currentWave, spawner.waveIntensity);
                        spawner.enemiesSpawned++;
                        syncWaveStateToInterpreter(spawner);
                    } else {
                        // Wave complete
                        syncWaveStateToInterpreter(spawner);
                        if (interpreterHost) {
                            interpreterHost->emitEvent({
                                "wave_completed",
                                {
                                    {"wave", std::to_string(spawner.currentWave)},
                                    {"enemies_spawned", std::to_string(spawner.enemiesSpawned)},
                                    {"enemies_per_wave", std::to_string(spawner.enemiesPerWave)},
                                    {"spawn_interval", std::to_string(spawner.spawnInterval)},
                                    {"wave_intensity", std::to_string(spawner.waveIntensity)}
                                }
                            });
                        }

                        spawner.currentWave++;
                        spawner.enemiesSpawned = 0;
                        const int defaultEnemyCount = 5 + spawner.currentWave * 2;
                        spawner.enemiesPerWave = defaultEnemyCount;
                        const float defaultWaveIntensity = spawner.waveIntensity * 1.15f;
                        spawner.waveIntensity = defaultWaveIntensity;
                        const float defaultSpawnInterval = std::max(1.0f, 3.0f - spawner.currentWave * 0.1f);
                        spawner.spawnInterval = defaultSpawnInterval;
                        applyWaveDescriptorPolicy(
                            spawner,
                            defaultEnemyCount,
                            defaultSpawnInterval,
                            defaultWaveIntensity
                        );

                        if (interpreterHost) {
                            interpreterHost->emitEvent({
                                "wave_started",
                                {
                                    {"wave", std::to_string(spawner.currentWave)},
                                    {"enemies_spawned", std::to_string(spawner.enemiesSpawned)},
                                    {"enemies_per_wave", std::to_string(spawner.enemiesPerWave)},
                                    {"spawn_interval", std::to_string(spawner.spawnInterval)},
                                    {"wave_intensity", std::to_string(spawner.waveIntensity)},
                                    {"default_enemy_count", std::to_string(defaultEnemyCount)},
                                    {"default_spawn_interval", std::to_string(defaultSpawnInterval)},
                                    {"default_wave_intensity", std::to_string(defaultWaveIntensity)}
                                }
                            });
                        }
                    }
                }
            });
    }

    void setupCollisionSystem() {
        world.system<const Position, const CollisionRadius>("CollisionSystem")
            .each([this](flecs::entity e1, const Position& pos1, const CollisionRadius& rad1) {
                world.each([&](flecs::entity e2, const Position& pos2, const CollisionRadius& rad2) {
                    if (e1 == e2) return;

                    float dist = glm::length(pos1.getFloat() - pos2.getFloat());
                    if (dist < rad1.value + rad2.value) {
                        handleCollision(e1, e2);
                    }
                });
            });
    }

    void createPlayer() {
        glm::vec3 startPos(0.0f, 1.0f, 0.0f); // Start slightly above ground

        // Create physics body for player
        BodyID playerPhysicsBody;
        if (physicsWorld) {
            playerPhysicsBody = physicsWorld->CreateDynamicBody(
                startPos,
                0.5f,   // radius
                1.8f,   // height (humanoid)
                DMCSurvivors::Layers::PLAYER
            );
        }

        player = world.entity("Player")
            .set<Position>(Position(startPos))
            .set<Velocity>({glm::vec3(0.0f)})
            .set<Rotation>({glm::quat(1.0f, 0.0f, 0.0f, 0.0f)})
            .set<Scale>({glm::vec3(1.0f)})
            .set<Health>({100.0f, 100.0f, 1.0f})
            .set<CollisionRadius>({0.5f})
            .set<CombatStats>({20.0f, 1.5f, 0.15f, 2.5f})
            .set<ComboState>({})
            .set<StyleMeter>({})
            .set<MovementState>({})
            .set<JumpState>({})
            .set<AttackState>({})
            .set<WeaponSlot>({})
            .set<Experience>({0.0f, 100.0f, 1})
            .set<PhysicsBody>({playerPhysicsBody, false})
            .set<Player>({})
            .set<MeshRenderer>({0, glm::vec4(0.2f, 0.5f, 1.0f, 1.0f)});

        Logger::get().info("🎯 Created player with physics body");
    }

    void createWaveSpawner() {
        WaveSpawner spawner{};
        syncWaveStateToInterpreter(
            spawner,
            spawner.enemiesPerWave,
            spawner.spawnInterval,
            spawner.waveIntensity
        );

        world.entity("WaveSpawner")
            .set<WaveSpawner>(spawner);
    }

    void spawnEnemy(int wave, float intensity) {
        float angle = angleDist(rng);
        float radius = radiusDist(rng);

        auto playerPos = player.get<Position>();
        glm::vec3 spawnPos = playerPos ?
            playerPos->getFloat() + glm::vec3(cos(angle) * radius, 1.0f, sin(angle) * radius) :
            glm::vec3(cos(angle) * radius, 1.0f, sin(angle) * radius);

        float healthMult = intensity;
        float damageMult = 1.0f + wave * 0.1f;

        // Create physics body for enemy
        BodyID enemyPhysicsBody;
        if (physicsWorld) {
            enemyPhysicsBody = physicsWorld->CreateDynamicBody(
                spawnPos,
                0.4f,   // radius
                1.6f,   // height (slightly smaller than player)
                DMCSurvivors::Layers::ENEMY
            );
        }

        auto enemy = world.entity()
            .set<Position>({spawnPos})
            .set<Velocity>({glm::vec3(0.0f)})
            .set<Rotation>({glm::quat(1.0f, 0.0f, 0.0f, 0.0f)})
            .set<Health>({50.0f * healthMult, 50.0f * healthMult})
            .set<CollisionRadius>({0.4f})
            .set<CombatStats>({10.0f * damageMult, 0.8f, 0.05f, 1.5f})
            .set<LaunchState>({})
            .set<PhysicsBody>({enemyPhysicsBody, false})
            .set<Enemy>({})
            .set<EnemyAI>({player, 30.0f, 2.0f})
            .set<MeshRenderer>({1, glm::vec4(0.8f, 0.2f, 0.2f, 1.0f)});

        if (interpreterHost) {
            interpreterHost->emitEvent({
                "enemy_spawned",
                {
                    {"wave", std::to_string(wave)},
                    {"intensity", std::to_string(intensity)},
                    {"entity_id", std::to_string(static_cast<uint64_t>(enemy.id()))}
                }
            });
        }
    }

    void spawnRedOrb(const glm::vec3& position, float value) {
        world.entity()
            .set<Position>({position})
            .set<Velocity>({glm::vec3(0.0f)})
            .set<CollisionRadius>({0.3f})
            .set<RedOrb>({value})
            .set<MeshRenderer>({2, glm::vec4(1.0f, 0.2f, 0.2f, 1.0f)});
    }

    void performJump() {
        auto jump = player.get_mut<JumpState>();
        auto physicsBody = player.get<PhysicsBody>();

        if (jump && physicsBody && physicsWorld) {
            // Apply upward impulse to physics body
            glm::vec3 jumpImpulse(0.0f, jump->jumpForce, 0.0f);
            physicsWorld->AddImpulse(physicsBody->bodyId, jumpImpulse);

            jump->isGrounded = false;
            jump->isJumping = true;
            jump->jumpsRemaining--;

            // Add style points for air combos
            auto style = player.get_mut<StyleMeter>();
            if (style && jump->jumpsRemaining == 0) {
                style->points += 50; // Double jump bonus
            }

            Logger::get().debug("🎯 Player jumped with impulse force: {}", jump->jumpForce);
        }
    }

    void startDash(const glm::vec3& direction) {
        auto movement = player.get_mut<MovementState>();
        auto jump = player.get_mut<JumpState>();

        if (movement) {
            movement->isDashing = true;
            movement->dashTimer = 0;
            movement->dashCooldown = movement->dashCooldownMax;

            // Use input direction or forward if no input
            if (glm::length(direction) > 0.1f) {
                movement->dashDirection = glm::normalize(direction);
            } else {
                auto rot = player.get<Rotation>();
                if (rot) {
                    movement->dashDirection = glm::rotate(rot->value, glm::vec3(0, 0, -1));
                }
            }

            // Air dash consumes air dash count
            if (jump && !jump->isGrounded) {
                if (movement->airDashesRemaining > 0) {
                    movement->airDashesRemaining--;
                } else {
                    movement->isDashing = false; // Can't air dash
                }
            }
        }
    }

    void performAttack(AttackType type) {
        auto attack = player.get_mut<AttackState>();
        auto combo = player.get_mut<ComboState>();
        auto style = player.get_mut<StyleMeter>();

        if (attack && !attack->isAttacking && attack->attackCooldown <= 0) {
            attack->isAttacking = true;
            attack->currentAttack = type;
            attack->attackTimer = 0;

            // Set attack duration based on type
            switch (type) {
                case LIGHT_ATTACK:
                    attack->attackDuration = 0.3f;
                    if (combo) combo->hitCount++;
                    if (style) style->points += 10 * style->multiplier;
                    break;
                case HEAVY_ATTACK:
                    attack->attackDuration = 0.6f;
                    if (combo) combo->hitCount++;
                    if (style) style->points += 20 * style->multiplier;
                    break;
                case LAUNCHER:
                    attack->attackDuration = 0.5f;
                    if (combo) combo->hitCount++;
                    if (style) style->points += 30 * style->multiplier;
                    break;
                default:
                    attack->attackDuration = 0.4f;
            }

            // Reset combo timer on hit
            if (combo) {
                combo->timer = 0;
            }

            // Deal damage to nearby enemies
            dealMeleeDamage(type);
        }
    }

    void dealMeleeDamage(AttackType type) {
        auto playerPos = player.get<Position>();
        auto stats = player.get<CombatStats>();
        auto style = player.get_mut<StyleMeter>();

        if (!playerPos || !stats) return;

        float range = 3.0f;
        float damage = stats->damage;

        // Modify damage based on attack type
        switch (type) {
            case HEAVY_ATTACK: damage *= 1.5f; break;
            case LAUNCHER: damage *= 1.2f; break;
            default: break;
        }

        // Apply style multiplier
        if (style) {
            damage *= style->multiplier;
        }

        // Check for enemies in range and collect entities to delete
        std::vector<flecs::entity> enemiesToDelete;
        world.each([&](flecs::entity enemy, Position& enemyPos, Health& health, const Enemy&) {
            float dist = glm::length(enemyPos.getFloat() - playerPos->getFloat());

            if (dist < range) {
                health.current -= damage;

                // Launch enemy on launcher attack
                if (type == LAUNCHER) {
                    auto launch = enemy.get_mut<LaunchState>();
                    if (launch) {
                        launch->isLaunched = true;
                        launch->launchTime = 0;
                    }

                    auto vel = enemy.get_mut<Velocity>();
                    if (vel) {
                        vel->value.y = 10.0f;
                    }
                }

                // Mark enemy for deletion if defeated
                if (health.current <= 0) {
                    spawnRedOrb(enemyPos.getFloat(), 10.0f * (1.0f + gameTime / 1000.0f));
                    enemiesToDelete.push_back(enemy);

                    // Style bonus for kills
                    if (style) {
                        style->points += 100 * style->multiplier;
                    }
                }
            }
        });

        // Delete enemies after iteration is complete
        for (auto& enemy : enemiesToDelete) {
            enemy.destruct();
        }
    }

    void toggleLockOn() {
        if (currentTarget) {
            currentTarget = flecs::entity();
        } else {
            // Find nearest enemy
            auto playerPos = player.get<Position>();
            if (!playerPos) return;

            flecs::entity nearest;
            float nearestDist = 20.0f; // Max lock-on range

            world.each([&](flecs::entity enemy, const Position& enemyPos, const Enemy&) {
                float dist = glm::length(enemyPos.getFloat() - playerPos->getFloat());
                if (dist < nearestDist) {
                    nearestDist = dist;
                    nearest = enemy;
                }
            });

            currentTarget = nearest;
        }
    }

    void handleCollision(flecs::entity e1, flecs::entity e2) {
        // Player-Enemy collision
        if (e1.has<Player>() && e2.has<Enemy>()) {
            auto health = e1.get_mut<Health>();
            auto enemyStats = e2.get<CombatStats>();
            auto enemyAI = e2.get_mut<EnemyAI>();

            if (health && enemyStats && enemyAI) {
                if (enemyAI->attackCooldown <= 0) {
                    health->current -= enemyStats->damage;
                    enemyAI->attackCooldown = 1.0f / enemyStats->attackSpeed;

                    // Reset style on hit
                    auto style = e1.get_mut<StyleMeter>();
                    if (style) {
                        style->points *= 0.8f;
                    }
                }
            }
        }

        // Player-RedOrb collision
        if (e1.has<Player>() && e2.has<RedOrb>()) {
            auto exp = e1.get_mut<Experience>();
            auto orb = e2.get<RedOrb>();

            if (exp && orb) {
                exp->current += orb->value;

                // Level up check
                while (exp->current >= exp->toNextLevel) {
                    exp->current -= exp->toNextLevel;
                    exp->level++;
                    exp->toNextLevel *= 1.2f;

                    // Heal on level up
                    auto health = e1.get_mut<Health>();
                    if (health) {
                        health->current = health->max;
                    }
                }

                entitiesMarkedForDeletion.push_back(e2);
            }
        }
    }

    void initializePhysics() {
        Logger::get().info("🎯 Initializing Jolt Physics for DMC Survivors...");

        tremor::physics::JoltPhysicsSettings settings;
        if (!physicsLayerConfig.empty()) {
            settings.layers = physicsLayerConfig.build();
        }

        physicsWorld = std::make_unique<DMCSurvivors::PhysicsWorld>(std::move(settings));
        if (!physicsWorld->Initialize()) {
            Logger::get().error("Failed to initialize Jolt Physics!");
            physicsWorld.reset();
        } else {
            Logger::get().info("✅ Jolt Physics initialized successfully!");

            // Create ground plane
            createGroundPlane();
        }
    }

    void createGroundPlane() {
        if (physicsWorld) {
            // Create a smaller ground plane for testing falling mechanics
            BodyID groundBody = physicsWorld->CreateStaticBody(
                glm::vec3(0.0f, 10.0f, 0.0f),      // position (1 unit below origin)
                glm::vec3(50.0f, 1.0f, 50.0f)      // half extents (100x2x100 world units)
            );
            Logger::get().info("🌍 Created ground plane (100x2x100 units) for physics world");

            // Create a second platform for testing
            BodyID platformBody = physicsWorld->CreateStaticBody(
                glm::vec3(120.0f, 10.0f, 0.0f),    // position at X=120
                glm::vec3(20.0f, 1.0f, 20.0f)      // half extents (40x2x40 world units)
            );
            Logger::get().info("🌍 Created test platform at X=120");
        }
    }

    // Get physics world for direct access if needed
    DMCSurvivors::PhysicsWorld* getPhysicsWorld() { return physicsWorld.get(); }
};

} // namespace DMCSurvivors

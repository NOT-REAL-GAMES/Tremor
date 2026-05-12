#include "vk_overlay_bridge.h"

#include "vk.h"
#include "logger.h"
#include "include/taffy_audio_tools.h"

#include <filesystem>
#include <iostream>

namespace tremor::gfx {

    void VulkanOverlayBridge::initialize(VulkanBackend& backend) {
        std::cout << "🎨 Initializing Taffy Overlay System..." << std::endl;
        backend.last_overlay_check_ = std::chrono::steady_clock::now();
        std::cout << "✅ Overlay system initialized!" << std::endl;
    }

    void VulkanOverlayBridge::initializeWorkflow(VulkanBackend& backend) {
        initialize(backend);
    }

    void VulkanOverlayBridge::createDevelopmentAssets(VulkanBackend&) {
        bool canCreateAssets = false;
        try {
            if (std::filesystem::exists("assets") || std::filesystem::create_directories("assets")) {
                canCreateAssets = true;
            }
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::get().warning("Could not create assets directory: {}. Skipping asset creation.", e.what());
            Logger::get().warning("To fix this, run the application from its intended directory (usually the 'bin' folder)");
            return;
        }

        if (!canCreateAssets) {
            return;
        }

        try {
            std::filesystem::create_directories("assets/overlays");
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::get().warning("Could not create overlays directory: {}", e.what());
        }

        try {
            std::filesystem::create_directories("assets/audio");
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::get().warning("Could not create audio directory: {}", e.what());
        }

        try {
            tremor::taffy::tools::createSineWaveAudioAsset("assets/audio/sine_440hz.taf", 440.0f, 2.0f);
            tremor::taffy::tools::createSineWaveAudioAsset("assets/audio/sine_220hz.taf", 220.0f, 2.0f);
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::get().warning("Could not create audio assets: {}", e.what());
        } catch (const std::exception& e) {
            Logger::get().warning("Error creating audio assets: {}", e.what());
        }

        try {
            std::filesystem::create_directories("assets/fonts");
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::get().warning("Could not create fonts directory: {}", e.what());
        }

        Logger::get().info("Development overlay initialization completed (some assets may not have been created if directories were inaccessible)");
    }

    void VulkanOverlayBridge::loadDefaultAssets(VulkanBackend& backend) {
        if (!backend.m_overlayManager) {
            return;
        }

        backend.m_overlayManager->reloadAsset("assets/cube.taf");
        backend.m_overlayManager->load_master_asset("assets/cube.taf");

        backend.m_overlayManager->reloadAsset("assets/sphere.taf");
        backend.m_overlayManager->load_master_asset("assets/sphere.taf");
    }

    void VulkanOverlayBridge::loadTestAssetWithOverlays(VulkanBackend& backend) {
        std::cout << "🎮 Loading test assets with overlays..." << std::endl;

        std::vector<MeshVertex> testVertices;

        MeshVertex v1;
        v1.position = glm::vec3(-0.5f, -0.5f, 0.0f);
        v1.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        v1.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
        v1.texCoord = glm::vec2(0.0f, 0.0f);
        testVertices.push_back(v1);

        MeshVertex v2;
        v2.position = glm::vec3(0.5f, -0.5f, 0.0f);
        v2.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        v2.color = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
        v2.texCoord = glm::vec2(1.0f, 0.0f);
        testVertices.push_back(v2);

        MeshVertex v3;
        v3.position = glm::vec3(0.0f, 0.5f, 0.0f);
        v3.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        v3.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        v3.texCoord = glm::vec2(0.5f, 1.0f);
        testVertices.push_back(v3);

        std::vector<uint32_t> testIndices = { 0, 1, 2 };

        std::cout << "📐 Creating test triangle with Vec3Q positions..." << std::endl;
        uint32_t testMeshId = backend.m_clusteredRenderer->loadMesh(testVertices, testIndices, "test_vec3q_triangle");

        if (testMeshId != UINT32_MAX) {
            std::cout << "✅ Test triangle created with mesh ID: " << testMeshId << std::endl;
        } else {
            std::cout << "❌ Failed to create test triangle" << std::endl;
        }
    }

    void VulkanOverlayBridge::update(VulkanBackend& backend) {
        const auto now = std::chrono::steady_clock::now();
        if (now - backend.last_overlay_check_ > backend.overlay_check_interval_) {
            backend.last_overlay_check_ = now;
        }
    }

} // namespace tremor::gfx

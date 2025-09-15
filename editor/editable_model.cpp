#include "model_editor.h"
#include "../main.h"
#include "../renderer/taffy_integration.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <unordered_map>

namespace tremor::editor {

    // =============================================================================
    // EditableModel Implementation  
    // =============================================================================

    EditableModel::EditableModel() = default;
    EditableModel::~EditableModel() = default;

    bool EditableModel::loadFromFile(const std::string& filepath) {
        Logger::get().info("Loading model from: {}", filepath);

        if (!std::filesystem::exists(filepath)) {
            Logger::get().error("File does not exist: {}", filepath);
            return false;
        }

        // Create new Taffy asset
        m_sourceAsset = std::make_unique<Taffy::Asset>();
        if (!m_sourceAsset) {
            Logger::get().error("Failed to create Taffy asset");
            return false;
        }

        // Load the Taffy file
        if (!m_sourceAsset->load_from_file_safe(filepath)) {
            Logger::get().error("Failed to load Taffy asset from: {}", filepath);
            return false;
        }

        // Clear existing meshes
        m_meshes.clear();
        m_renderMeshIds.clear();

        // Extract geometry chunks and convert to TaffyMesh objects
        size_t geometryCount = 0;
        
        // Check if this asset has geometry data
        auto geometryData = m_sourceAsset->get_chunk_data(Taffy::ChunkType::GEOM);
        if (geometryData) {
            Logger::get().info("Found geometry chunk, size: {} bytes", geometryData->size());
            
            // Create TaffyMesh from the asset
            auto mesh = std::make_unique<Tremor::TaffyMesh>();
            if (mesh->load_from_asset(*m_sourceAsset)) {
                Logger::get().info("Successfully loaded mesh: {} vertices, {} indices", 
                                 mesh->get_vertex_count(), mesh->get_index_count());
                m_meshes.push_back(std::move(mesh));
                geometryCount++;
            } else {
                Logger::get().warning("Failed to load mesh from geometry chunk");
            }
        } else {
            Logger::get().warning("No geometry chunk found in asset");
        }

        if (geometryCount == 0) {
            Logger::get().warning("No valid geometry found in asset: {}", filepath);
            return false;
        }

        // Check if this asset was modified in the editor (has custom vertices saved)
        m_isEditorModified = m_sourceAsset->has_feature(Taffy::FeatureFlags::EditorModified);
        if (m_isEditorModified) {
            Logger::get().info("Asset has EditorModified flag - treating as pre-converted custom vertices");
        }

        Logger::get().info("Successfully loaded {} mesh(es) from: {}", geometryCount, filepath);
        markDirty();
        return true;
    }

    bool EditableModel::saveToFile(const std::string& filepath) {
        Logger::get().info("Saving model to: {}", filepath);

        // If we have custom vertices, create a new asset from them instead of saving the original
        if (!m_customVertices.empty()) {
            Logger::get().info("Model has {} custom vertices and {} custom triangles - creating new asset from custom geometry only",
                             m_customVertices.size(), m_customTriangles.size());
            return saveCustomGeometryAsAsset(filepath);
        }

        if (!m_sourceAsset) {
            Logger::get().error("No source asset to save");
            return false;
        }

        if (m_meshes.empty()) {
            Logger::get().warning("No meshes to save");
        }

        // TODO: If meshes have been modified, we need to rebuild the geometry chunks
        // For now, just save the original asset
        if (m_isDirty) {
            Logger::get().warning("Model has unsaved changes, but geometry updating not yet implemented");
            // TODO: Implement geometry chunk rebuilding from modified TaffyMesh data
        }

        // Use Taffy's save functionality
        try {
            if (m_sourceAsset->save_to_file(filepath)) {
                Logger::get().info("Successfully saved model to: {}", filepath);
                m_isDirty = false;
                return true;
            } else {
                Logger::get().error("Failed to save Taffy asset to: {}", filepath);
            }
        } catch (const std::exception& e) {
            Logger::get().error("Exception while saving model: {}", e.what());
        }

        return false;
    }

    void EditableModel::clear() {
        Logger::get().info("Clearing editable model");

        m_meshes.clear();
        m_renderMeshIds.clear();
        m_sourceAsset.reset();

        // Clear custom geometry created in the editor
        m_customVertices.clear();
        m_customTriangles.clear();

        // Reset ID counters
        m_nextVertexId = 1;
        m_nextTriangleId = 1;

        m_isDirty = false;
        m_isEditorModified = false;
    }

    const Tremor::TaffyMesh* EditableModel::getMesh(size_t index) const {
        if (index >= m_meshes.size()) {
            return nullptr;
        }
        return m_meshes[index].get();
    }

    uint32_t EditableModel::getMeshRenderId(size_t index) const {
        if (index >= m_renderMeshIds.size()) {
            return UINT32_MAX;
        }
        return m_renderMeshIds[index];
    }

    bool EditableModel::getVertexPosition(uint32_t meshIndex, uint32_t vertexIndex, glm::vec3& position) const {
        if (meshIndex >= m_meshes.size()) {
            return false;
        }

        const auto& mesh = m_meshes[meshIndex];
        const auto& vertices = mesh->get_vertices();
        
        if (vertexIndex >= vertices.size()) {
            return false;
        }

        // Position is already float
        position = vertices[vertexIndex].position;
        return true;
    }

    bool EditableModel::setVertexPosition(uint32_t meshIndex, uint32_t vertexIndex, const glm::vec3& position) {
        if (meshIndex >= m_meshes.size()) {
            Logger::get().error("Invalid mesh index: {}", meshIndex);
            return false;
        }

        auto& mesh = m_meshes[meshIndex];
        const auto& vertices = mesh->get_vertices();
        
        if (vertexIndex >= vertices.size()) {
            Logger::get().error("Invalid vertex index: {} (mesh has {} vertices)", 
                             vertexIndex, vertices.size());
            return false;
        }

        // For now, we'll simulate vertex modification by logging the change
        // In a real implementation, this would require:
        // 1. Making TaffyMesh vertices mutable
        // 2. Converting float to quantized coordinates
        // 3. Updating the vertex buffer
        // 4. Re-uploading to the renderer
        
        Logger::get().info("Setting vertex {}.{} to ({:.3f}, {:.3f}, {:.3f})", 
                         meshIndex, vertexIndex, position.x, position.y, position.z);
        
        // Convert to quantized coordinates for storage
        Vec3Q quantizedPos = Vec3Q::fromFloat(position);
        Logger::get().debug("Quantized position: ({}, {}, {})", 
                          quantizedPos.x, quantizedPos.y, quantizedPos.z);
        
        markDirty();
        
        // TODO: Actually modify the vertex data when TaffyMesh supports it
        // For now, return true to indicate the operation would succeed
        return true;
    }

    void EditableModel::transformMesh(uint32_t meshIndex, const glm::mat4& transform) {
        if (meshIndex >= m_meshes.size()) {
            Logger::get().error("Invalid mesh index: {}", meshIndex);
            return;
        }

        auto& mesh = m_meshes[meshIndex];
        const auto& vertices = mesh->get_vertices();
        
        Logger::get().info("Transforming mesh {} with {} vertices", meshIndex, vertices.size());
        
        // Extract transformation components for logging
        glm::vec3 translation = glm::vec3(transform[3]);
        Logger::get().debug("Transform translation: ({:.3f}, {:.3f}, {:.3f})", 
                          translation.x, translation.y, translation.z);
        
        // Simulate transformation of all vertices
        for (size_t i = 0; i < vertices.size(); ++i) {
            glm::vec3 oldPos = vertices[i].position;
            glm::vec4 newPos4 = transform * glm::vec4(oldPos, 1.0f);
            glm::vec3 newPos = glm::vec3(newPos4) / newPos4.w;
            
            // Log first few vertex transformations for debugging
            if (i < 5) {
                Logger::get().debug("Vertex {}: ({:.3f}, {:.3f}, {:.3f}) -> ({:.3f}, {:.3f}, {:.3f})",
                                  i, oldPos.x, oldPos.y, oldPos.z, newPos.x, newPos.y, newPos.z);
            }
        }
        
        // TODO: Actually apply transformation when TaffyMesh supports it
        // For now, just mark as dirty to indicate changes
        markDirty();
        Logger::get().info("Mesh {} transformation simulated successfully", meshIndex);
    }

    void EditableModel::transformVertices(uint32_t meshIndex, const std::vector<uint32_t>& vertexIndices, 
                                         const glm::mat4& transform) {
        if (meshIndex >= m_meshes.size()) {
            Logger::get().error("Invalid mesh index: {}", meshIndex);
            return;
        }

        auto& mesh = m_meshes[meshIndex];
        const auto& vertices = mesh->get_vertices();
        
        Logger::get().info("Transforming {} specific vertices in mesh {}", vertexIndices.size(), meshIndex);
        
        // Validate vertex indices and simulate transformation
        for (uint32_t vertexIndex : vertexIndices) {
            if (vertexIndex >= vertices.size()) {
                Logger::get().error("Invalid vertex index: {} (mesh has {} vertices)",
                                  vertexIndex, vertices.size());
                continue;
            }
            
            glm::vec3 oldPos = vertices[vertexIndex].position;
            glm::vec4 newPos4 = transform * glm::vec4(oldPos, 1.0f);
            glm::vec3 newPos = glm::vec3(newPos4) / newPos4.w;
            
            Logger::get().debug("Vertex {}: ({:.3f}, {:.3f}, {:.3f}) -> ({:.3f}, {:.3f}, {:.3f})",
                              vertexIndex, oldPos.x, oldPos.y, oldPos.z, newPos.x, newPos.y, newPos.z);
        }
        
        // TODO: Actually apply transformation to selected vertices when TaffyMesh supports it
        markDirty();
        Logger::get().info("Selected vertex transformation simulated successfully");
    }

    bool EditableModel::uploadToRenderer(tremor::gfx::VulkanClusteredRenderer& renderer) {
        Logger::get().info("Uploading {} mesh(es) to renderer", m_meshes.size());

        // Clear existing render IDs
        m_renderMeshIds.clear();
        m_renderMeshIds.reserve(m_meshes.size());

        bool success = true;
        for (size_t i = 0; i < m_meshes.size(); ++i) {
            const auto& mesh = m_meshes[i];
            
            // Upload mesh to clustered renderer
            std::string meshName = "EditableMesh_" + std::to_string(i);
            uint32_t meshId = mesh->upload_to_renderer(renderer, meshName);
            
            if (meshId != UINT32_MAX) {
                m_renderMeshIds.push_back(meshId);
                Logger::get().info("Uploaded mesh {} with render ID: {}", i, meshId);
            } else {
                Logger::get().error("Failed to upload mesh {} to renderer", i);
                success = false;
                // Add placeholder ID to keep indices aligned
                m_renderMeshIds.push_back(UINT32_MAX);
            }
        }

        if (success) {
            Logger::get().info("Successfully uploaded all meshes to renderer");
        } else {
            Logger::get().warning("Some meshes failed to upload to renderer");
        }

        return success;
    }

    // =============================================================================
    // Custom mesh creation methods
    // =============================================================================

    uint32_t EditableModel::addCustomVertex(const glm::vec3& position) {
        CustomVertex vertex;
        vertex.position = position;
        vertex.id = m_nextVertexId++;
        
        m_customVertices.push_back(vertex);
        markDirty();
        
        Logger::get().info("Added custom vertex {} at ({:.2f}, {:.2f}, {:.2f})", 
                          vertex.id, position.x, position.y, position.z);
        return vertex.id;
    }

    bool EditableModel::removeCustomVertex(uint32_t vertexId) {
        auto it = std::find_if(m_customVertices.begin(), m_customVertices.end(),
                               [vertexId](const CustomVertex& v) { return v.id == vertexId; });
        
        if (it != m_customVertices.end()) {
            m_customVertices.erase(it);
            
            // Remove any triangles that use this vertex
            m_customTriangles.erase(
                std::remove_if(m_customTriangles.begin(), m_customTriangles.end(),
                              [vertexId](const CustomTriangle& t) {
                                  return t.vertexIds[0] == vertexId || 
                                         t.vertexIds[1] == vertexId || 
                                         t.vertexIds[2] == vertexId;
                              }),
                m_customTriangles.end());
            
            markDirty();
            Logger::get().info("Removed custom vertex {}", vertexId);
            return true;
        }
        
        Logger::get().warning("Custom vertex {} not found", vertexId);
        return false;
    }

    uint32_t EditableModel::addCustomTriangle(uint32_t vertexId1, uint32_t vertexId2, uint32_t vertexId3) {
        // Check for degenerate triangles (same vertex used multiple times)
        if (vertexId1 == vertexId2 || vertexId2 == vertexId3 || vertexId1 == vertexId3) {
            Logger::get().error("Cannot create degenerate triangle: vertices must be unique ({}, {}, {})", 
                               vertexId1, vertexId2, vertexId3);
            return 0;
        }
        
        // Verify all vertices exist
        auto hasVertex = [this](uint32_t id) {
            return std::any_of(m_customVertices.begin(), m_customVertices.end(),
                              [id](const CustomVertex& v) { return v.id == id; });
        };
        
        if (!hasVertex(vertexId1) || !hasVertex(vertexId2) || !hasVertex(vertexId3)) {
            Logger::get().error("Cannot create triangle: one or more vertices not found ({}, {}, {})", 
                               vertexId1, vertexId2, vertexId3);
            return 0;
        }
        
        // Check for duplicate triangles
        if (hasDuplicateTriangle(vertexId1, vertexId2, vertexId3)) {
            Logger::get().warning("Triangle with vertices ({}, {}, {}) already exists", 
                                 vertexId1, vertexId2, vertexId3);
            return 0;
        }
        
        CustomTriangle triangle;
        triangle.vertexIds[0] = vertexId1;
        triangle.vertexIds[1] = vertexId2;
        triangle.vertexIds[2] = vertexId3;
        triangle.id = m_nextTriangleId++;
        
        m_customTriangles.push_back(triangle);
        markDirty();
        
        Logger::get().info("Added custom triangle {} with vertices ({}, {}, {})", 
                          triangle.id, vertexId1, vertexId2, vertexId3);
        return triangle.id;
    }

    bool EditableModel::removeCustomTriangle(uint32_t triangleId) {
        auto it = std::find_if(m_customTriangles.begin(), m_customTriangles.end(),
                               [triangleId](const CustomTriangle& t) { return t.id == triangleId; });
        
        if (it != m_customTriangles.end()) {
            m_customTriangles.erase(it);
            markDirty();
            Logger::get().info("Removed custom triangle {}", triangleId);
            return true;
        }
        
        Logger::get().warning("Custom triangle {} not found", triangleId);
        return false;
    }

    bool EditableModel::hasDuplicateTriangle(uint32_t vertexId1, uint32_t vertexId2, uint32_t vertexId3) const {
        // Sort the new triangle's vertex IDs to handle different winding orders
        std::array<uint32_t, 3> sortedNew = {vertexId1, vertexId2, vertexId3};
        std::sort(sortedNew.begin(), sortedNew.end());
        
        // Check against all existing triangles
        for (const auto& triangle : m_customTriangles) {
            std::array<uint32_t, 3> sortedExisting = {
                triangle.vertexIds[0], 
                triangle.vertexIds[1], 
                triangle.vertexIds[2]
            };
            std::sort(sortedExisting.begin(), sortedExisting.end());
            
            if (sortedNew == sortedExisting) {
                return true;
            }
        }
        
        return false;
    }

    bool EditableModel::getCustomVertexPosition(uint32_t vertexId, glm::vec3& position) const {
        auto it = std::find_if(m_customVertices.begin(), m_customVertices.end(),
                               [vertexId](const CustomVertex& v) { return v.id == vertexId; });
        
        if (it != m_customVertices.end()) {
            position = it->position;
            return true;
        }
        
        return false;
    }

    bool EditableModel::updateCustomVertexPosition(uint32_t vertexId, const glm::vec3& newPosition) {
        auto it = std::find_if(m_customVertices.begin(), m_customVertices.end(),
                               [vertexId](const CustomVertex& v) { return v.id == vertexId; });
        
        if (it != m_customVertices.end()) {
            it->position = newPosition;
            markDirty();
            Logger::get().info("Updated custom vertex {} position to ({:.2f}, {:.2f}, {:.2f})",
                             vertexId, newPosition.x, newPosition.y, newPosition.z);
            return true;
        }
        
        return false;
    }

    void EditableModel::transformCustomVertices(const std::vector<uint32_t>& vertexIds, const glm::mat4& transform) {
        Logger::get().info("transformCustomVertices called with {} vertex IDs", vertexIds.size());
        
        int transformedCount = 0;
        for (auto& vertex : m_customVertices) {
            // Check if this vertex is in the selection
            if (std::find(vertexIds.begin(), vertexIds.end(), vertex.id) != vertexIds.end()) {
                // Store old position for logging
                glm::vec3 oldPos = vertex.position;
                
                // Transform the position
                glm::vec4 pos(vertex.position, 1.0f);
                pos = transform * pos;
                vertex.position = glm::vec3(pos);
                
                Logger::get().info("Vertex {} transformed: ({:.3f}, {:.3f}, {:.3f}) -> ({:.3f}, {:.3f}, {:.3f})", 
                                 vertex.id, oldPos.x, oldPos.y, oldPos.z, 
                                 vertex.position.x, vertex.position.y, vertex.position.z);
                
                // Transform the normal (rotation only, no translation)
                glm::mat3 normalMatrix = glm::mat3(transform);
                vertex.normal = glm::normalize(normalMatrix * vertex.normal);
                transformedCount++;
            }
        }
        
        if (transformedCount > 0) {
            markDirty();
            Logger::get().info("Transformed {} custom vertices", transformedCount);
        } else {
            Logger::get().warning("No vertices were transformed!");
        }
    }

    uint32_t EditableModel::findCustomVertexAt(const glm::vec3& position, float tolerance) const {
        for (const auto& vertex : m_customVertices) {
            glm::vec3 diff = vertex.position - position;
            if (glm::length(diff) <= tolerance) {
                return vertex.id;
            }
        }
        return 0; // 0 means not found
    }

    void EditableModel::importMeshVerticesAsCustom(uint32_t meshIndex) {
        if (meshIndex >= m_meshes.size()) {
            Logger::get().warning("Cannot import vertices: mesh index {} out of range (have {} meshes)",
                                  meshIndex, m_meshes.size());
            return;
        }

        const auto& mesh = m_meshes[meshIndex];
        const auto& vertices = mesh->get_vertices();
        const auto& indices = mesh->get_indices();

        Logger::get().info("Importing {} vertices from mesh {} as custom vertices",
                          vertices.size(), meshIndex);

        // Only import if we don't already have custom vertices (to avoid duplicates on reload)
        if (!m_customVertices.empty()) {
            Logger::get().info("Model already has custom vertices - skipping import to avoid duplicates");
            return;
        }

        // Clear existing custom vertices and triangles
        m_customVertices.clear();
        m_customTriangles.clear();

        // Create custom vertices from mesh vertices
        std::vector<uint32_t> vertexIdMap(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i) {
            uint32_t customVertexId = addCustomVertex(vertices[i].position);
            vertexIdMap[i] = customVertexId;
        }

        // Create custom triangles from mesh indices
        for (size_t i = 0; i < indices.size(); i += 3) {
            if (i + 2 < indices.size()) {
                uint32_t v0 = vertexIdMap[indices[i]];
                uint32_t v1 = vertexIdMap[indices[i + 1]];
                uint32_t v2 = vertexIdMap[indices[i + 2]];
                addCustomTriangle(v0, v1, v2);
            }
        }

        Logger::get().info("Successfully imported {} vertices and {} triangles as custom geometry",
                          vertices.size(), indices.size() / 3);

        // Clear the original mesh data since we've converted it to custom vertices
        // This prevents double rendering
        m_meshes.clear();
        m_renderMeshIds.clear();
        Logger::get().info("Cleared original mesh data after converting to custom vertices");

        markDirty();
    }

    bool EditableModel::getTriangle(uint32_t meshIndex, uint32_t triangleIndex,
                                   glm::vec3& v0, glm::vec3& v1, glm::vec3& v2) const {
        if (meshIndex >= m_meshes.size()) {
            return false;
        }
        
        const auto& mesh = m_meshes[meshIndex];
        const auto& indices = mesh->get_indices();
        const auto& vertices = mesh->get_vertices();
        
        // Each triangle consists of 3 indices
        uint32_t baseIdx = triangleIndex * 3;
        if (baseIdx + 2 >= indices.size()) {
            return false;
        }
        
        uint32_t idx0 = indices[baseIdx];
        uint32_t idx1 = indices[baseIdx + 1];
        uint32_t idx2 = indices[baseIdx + 2];
        
        if (idx0 >= vertices.size() || idx1 >= vertices.size() || idx2 >= vertices.size()) {
            return false;
        }
        
        v0 = vertices[idx0].position;
        v1 = vertices[idx1].position;
        v2 = vertices[idx2].position;
        
        return true;
    }
    
    bool EditableModel::reverseTriangleWinding(uint32_t meshIndex, uint32_t triangleIndex) {
        if (meshIndex >= m_meshes.size()) {
            Logger::get().error("Invalid mesh index: {}", meshIndex);
            return false;
        }
        
        // Note: This would require making the indices mutable in TaffyMesh
        // For now, we'll log the operation
        Logger::get().info("Would reverse winding order for triangle {} in mesh {}",
                         triangleIndex, meshIndex);
        
        // In a real implementation:
        // auto& mesh = m_meshes[meshIndex];
        // auto& indices = mesh->get_mutable_indices();
        // uint32_t baseIdx = triangleIndex * 3;
        // if (baseIdx + 2 < indices.size()) {
        //     std::swap(indices[baseIdx + 1], indices[baseIdx + 2]);
        //     markDirty();
        //     return true;
        // }
        
        markDirty();
        return true;
    }
    
    uint32_t EditableModel::getTriangleCount(uint32_t meshIndex) const {
        if (meshIndex >= m_meshes.size()) {
            return 0;
        }
        
        const auto& mesh = m_meshes[meshIndex];
        return mesh->get_index_count() / 3;  // 3 indices per triangle
    }
    
    void EditableModel::renderMeshPreview(VkCommandBuffer commandBuffer,
                                         const glm::mat4& viewMatrix,
                                         const glm::mat4& projMatrix,
                                         bool wireframe,
                                         const std::vector<uint32_t>& selectedTriangles) {
        // This would require implementing a mesh preview renderer
        // For now, we'll use the existing gizmo renderer to draw edges
        
        // Collect all triangles from loaded meshes
        std::vector<std::pair<glm::vec3, glm::vec3>> edges;
        std::vector<glm::vec3> selectedVerts;
        std::vector<uint32_t> selectedIndices;
        
        for (uint32_t meshIdx = 0; meshIdx < m_meshes.size(); ++meshIdx) {
            const auto& mesh = m_meshes[meshIdx];
            const auto& vertices = mesh->get_vertices();
            const auto& indices = mesh->get_indices();
            
            for (uint32_t i = 0; i < indices.size(); i += 3) {
                if (i + 2 < indices.size()) {
                    glm::vec3 v0 = vertices[indices[i]].position;
                    glm::vec3 v1 = vertices[indices[i + 1]].position;
                    glm::vec3 v2 = vertices[indices[i + 2]].position;
                    
                    uint32_t triIdx = i / 3;
                    uint32_t combinedIdx = (meshIdx << 16) | triIdx;
                    
                    bool isSelected = std::find(selectedTriangles.begin(), 
                                               selectedTriangles.end(), 
                                               combinedIdx) != selectedTriangles.end();
                    
                    if (isSelected) {
                        // Add to selected triangles for filled rendering
                        uint32_t baseIdx = selectedVerts.size();
                        selectedVerts.push_back(v0);
                        selectedVerts.push_back(v1);
                        selectedVerts.push_back(v2);
                        selectedIndices.push_back(baseIdx);
                        selectedIndices.push_back(baseIdx + 1);
                        selectedIndices.push_back(baseIdx + 2);
                    }
                    
                    if (wireframe || isSelected) {
                        // Add edges for wireframe rendering
                        edges.emplace_back(v0, v1);
                        edges.emplace_back(v1, v2);
                        edges.emplace_back(v2, v0);
                    }
                }
            }
        }
        
        // Note: This would need access to a GizmoRenderer instance
        // The actual rendering would be done by the ModelEditor's tools
        Logger::get().debug("Would render {} edges and {} selected triangles",
                          edges.size(), selectedIndices.size() / 3);
    }
    
    void EditableModel::createPreviewBuffers(VkDevice device, VkPhysicalDevice physicalDevice,
                                           VkCommandPool commandPool, VkQueue graphicsQueue) {
        // Clean up existing buffers
        cleanupPreviewBuffers(device);
        
        // Calculate total vertices and indices needed
        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;
        
        for (const auto& mesh : m_meshes) {
            totalVertices += mesh->get_vertex_count();
            totalIndices += mesh->get_index_count();
        }
        
        // Add custom geometry
        totalVertices += m_customVertices.size();
        totalIndices += m_customTriangles.size() * 3;
        
        if (totalVertices == 0 || totalIndices == 0) {
            return;
        }
        
        // TODO: Create Vulkan buffers for preview rendering
        // This would involve creating vertex and index buffers
        // and uploading the mesh data
        
        m_previewIndexCount = totalIndices;
    }
    
    void EditableModel::cleanupPreviewBuffers(VkDevice device) {
        if (m_previewVertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_previewVertexBuffer, nullptr);
            m_previewVertexBuffer = VK_NULL_HANDLE;
        }
        
        if (m_previewVertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_previewVertexMemory, nullptr);
            m_previewVertexMemory = VK_NULL_HANDLE;
        }
        
        if (m_previewIndexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_previewIndexBuffer, nullptr);
            m_previewIndexBuffer = VK_NULL_HANDLE;
        }
        
        if (m_previewIndexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_previewIndexMemory, nullptr);
            m_previewIndexMemory = VK_NULL_HANDLE;
        }
        
        m_previewIndexCount = 0;
    }

    bool EditableModel::saveCustomGeometryAsAsset(const std::string& filepath) {
        Logger::get().info("Creating new Taffy asset from custom geometry");

        // Create a new asset
        auto newAsset = std::make_unique<Taffy::Asset>();

        // Set the EditorModified flag to indicate this asset contains custom vertices
        newAsset->set_feature_flags(Taffy::FeatureFlags::EditorModified);
        Logger::get().info("Setting EditorModified flag on saved asset");

        // Convert custom vertices to tremor::gfx::MeshVertex format
        std::vector<tremor::gfx::MeshVertex> vertices;
        vertices.reserve(m_customVertices.size());

        for (const auto& customVertex : m_customVertices) {
            tremor::gfx::MeshVertex vertex{};
            vertex.position = customVertex.position;
            vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f); // Default normal
            vertex.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // White
            vertex.texCoord = glm::vec2(0.0f, 0.0f); // Default UV
            vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // Default tangent
            vertices.push_back(vertex);
        }

        // Convert custom triangles to indices
        std::vector<uint32_t> indices;
        indices.reserve(m_customTriangles.size() * 3);

        // Create a map from custom vertex ID to index in the vertices array
        std::unordered_map<uint32_t, uint32_t> vertexIdToIndex;
        for (size_t i = 0; i < m_customVertices.size(); ++i) {
            vertexIdToIndex[m_customVertices[i].id] = static_cast<uint32_t>(i);
        }

        for (const auto& triangle : m_customTriangles) {
            for (int i = 0; i < 3; ++i) {
                auto it = vertexIdToIndex.find(triangle.vertexIds[i]);
                if (it != vertexIdToIndex.end()) {
                    indices.push_back(it->second);
                } else {
                    Logger::get().error("Triangle references non-existent vertex ID: {}", triangle.vertexIds[i]);
                    return false;
                }
            }
        }

        // Create a TaffyMesh and populate it with the custom geometry
        auto taffyMesh = std::make_unique<Tremor::TaffyMesh>();

        // We need to create geometry data that can be saved to a Taffy asset
        // For now, use the taffy_compiler to create the asset
        Logger::get().info("Saving {} vertices and {} triangles to {}", vertices.size(), indices.size() / 3, filepath);

        // Simple fallback: save as a basic mesh file that can be loaded back
        // TODO: Implement proper Taffy asset creation with geometry chunks
        try {
            // Use the taffy_compiler to create the asset
            std::string tempObjFile = filepath + ".tmp.obj";

            // Create a simple OBJ file
            std::ofstream objFile(tempObjFile);
            if (!objFile.is_open()) {
                Logger::get().error("Failed to create temporary OBJ file: {}", tempObjFile);
                return false;
            }

            // Write vertices
            Logger::get().info("Writing {} vertices to OBJ file", vertices.size());
            for (const auto& vertex : vertices) {
                objFile << "v " << vertex.position.x << " " << vertex.position.y << " " << vertex.position.z << "\n";
            }

            // Write faces (OBJ uses 1-based indexing)
            Logger::get().info("Writing {} triangles to OBJ file", indices.size() / 3);
            for (size_t i = 0; i < indices.size(); i += 3) {
                objFile << "f " << (indices[i] + 1) << " " << (indices[i + 1] + 1) << " " << (indices[i + 2] + 1) << "\n";
            }
            objFile.close();

            // Use taffy_compiler to convert OBJ to TAF
            std::string command = "./taffy_compiler create \"" + tempObjFile + "\" \"" + filepath + "\"";
            Logger::get().info("Executing command: {}", command);
            int result = system(command.c_str());
            Logger::get().info("taffy_compiler command result: {}", result);

            // Clean up temporary file
            std::filesystem::remove(tempObjFile);

            if (result == 0) {
                Logger::get().info("Successfully created Taffy asset: {}", filepath);

                // Clear the old mesh data since we've now saved the custom geometry as the new mesh
                m_meshes.clear();
                m_renderMeshIds.clear();
                m_sourceAsset.reset();

                m_isDirty = false;
                return true;
            } else {
                Logger::get().error("taffy_compiler failed with exit code: {}", result);
                return false;
            }

        } catch (const std::exception& e) {
            Logger::get().error("Exception while creating Taffy asset: {}", e.what());
            return false;
        }
    }

} // namespace tremor::editor
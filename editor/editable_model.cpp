#include "model_editor.h"
#include "../main.h"
#include "../renderer/taffy_integration.h"
#include <filesystem>
#include <fstream>
#include <algorithm>

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

        Logger::get().info("Successfully loaded {} mesh(es) from: {}", geometryCount, filepath);
        markDirty();
        return true;
    }

    bool EditableModel::saveToFile(const std::string& filepath) {
        Logger::get().info("Saving model to: {}", filepath);

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
        m_isDirty = false;
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

        // Convert quantized position to float
        position = vertices[vertexIndex].position.toFloat();
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
            glm::vec3 oldPos = vertices[i].position.toFloat();
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
            
            glm::vec3 oldPos = vertices[vertexIndex].position.toFloat();
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

    bool EditableModel::getCustomVertexPosition(uint32_t vertexId, glm::vec3& position) const {
        auto it = std::find_if(m_customVertices.begin(), m_customVertices.end(),
                               [vertexId](const CustomVertex& v) { return v.id == vertexId; });
        
        if (it != m_customVertices.end()) {
            position = it->position;
            return true;
        }
        
        return false;
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

} // namespace tremor::editor
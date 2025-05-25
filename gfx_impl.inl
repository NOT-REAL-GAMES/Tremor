// gfx_impl.inl - Template implementations for gfx.h

namespace tremor::gfx {

    // OctreeNode template implementations
    template<typename T>
    void OctreeNode<T>::insert(const T& object, const AABBQ& objectBounds) {
        if (!m_bounds.intersects(objectBounds)) {
            return; // Object is outside this node's bounds
        }

        if (m_isLeaf && m_objects.size() < m_maxObjects) {
            m_objects.push_back(object);
            m_objectBounds.push_back(objectBounds);
            return;
        }

        if (m_isLeaf) {
            if (m_depth >= m_maxDepth) {
                m_objects.push_back(object);
                m_objectBounds.push_back(objectBounds);
                return;
            }
            split();
        }

        int bestChildIndex = getChildIndex(objectBounds);
        if (bestChildIndex != -1) {
            if (m_children[bestChildIndex]) {
                m_children[bestChildIndex]->insert(object, objectBounds);
            }
        }
        else {
            m_objects.push_back(object);
            m_objectBounds.push_back(objectBounds);
        }
    }

    template<typename T>
    bool OctreeNode<T>::remove(const T& object, const AABBQ& objectBounds) {
        if (!m_bounds.intersects(objectBounds)) {
            return false;
        }

        if (m_isLeaf) {
            for (size_t i = 0; i < m_objects.size(); i++) {
                if (m_objects[i].instanceID == object.instanceID) {
                    m_objects.erase(m_objects.begin() + i);
                    m_objectBounds.erase(m_objectBounds.begin() + i);
                    return true;
                }
            }
            return false;
        }
        else {
            for (int i = 0; i < 8; i++) {
                if (m_children[i] && m_children[i]->remove(object, objectBounds)) {
                    return true;
                }
            }
            return false;
        }
    }

    template<typename T>
    void OctreeNode<T>::query(const AABBQ& queryBounds, std::vector<T>& results) const {
        if (!m_bounds.intersects(queryBounds)) {
            return;
        }

        for (size_t i = 0; i < m_objects.size(); i++) {
            if (m_objectBounds[i].intersects(queryBounds)) {
                results.push_back(m_objects[i]);
            }
        }

        if (!m_isLeaf) {
            for (int i = 0; i < 8; i++) {
                if (m_children[i]) {
                    m_children[i]->query(queryBounds, results);
                }
            }
        }
    }

    template<typename T>
    void OctreeNode<T>::query(const Frustum& frustum, std::vector<T>& results) const {
        AABBF nodeBoundsF = m_bounds.toFloat();
        if (!frustum.containsAABB(nodeBoundsF.min, nodeBoundsF.max)) {
            return;
        }

        for (size_t i = 0; i < m_objects.size(); i++) {
            AABBF objBoundsF = m_objectBounds[i].toFloat();
            if (frustum.containsAABB(objBoundsF.min, objBoundsF.max)) {
                results.push_back(m_objects[i]);
            }
        }

        if (!m_isLeaf) {
            for (int i = 0; i < 8; i++) {
                if (m_children[i]) {
                    m_children[i]->query(frustum, results);
                }
            }
        }
    }

    template<typename T>
    void OctreeNode<T>::getAllObjects(std::vector<T>& results) const {
        results.insert(results.end(), m_objects.begin(), m_objects.end());

        if (!m_isLeaf) {
            for (int i = 0; i < 8; i++) {
                if (m_children[i]) {
                    m_children[i]->getAllObjects(results);
                }
            }
        }
    }

    template<typename T>
    void OctreeNode<T>::split() {
        if (!m_isLeaf) return;

        m_isLeaf = false;
        Vec3Q center = m_bounds.getCenter();

        for (int i = 0; i < 8; i++) {
            Vec3Q min, max;
            min.x = (i & 1) ? center.x : m_bounds.min.x;
            min.y = (i & 2) ? center.y : m_bounds.min.y;
            min.z = (i & 4) ? center.z : m_bounds.min.z;
            max.x = (i & 1) ? m_bounds.max.x : center.x;
            max.y = (i & 2) ? m_bounds.max.y : center.y;
            max.z = (i & 4) ? m_bounds.max.z : center.z;

            AABBQ childBounds(min, max);
            m_children[i] = std::make_unique<OctreeNode<T>>(childBounds, m_depth + 1, m_maxDepth, m_maxObjects);
        }

        std::vector<T> tempObjects = std::move(m_objects);
        std::vector<AABBQ> tempBounds = std::move(m_objectBounds);
        m_objects.clear();
        m_objectBounds.clear();

        for (size_t i = 0; i < tempObjects.size(); i++) {
            int bestChildIndex = getChildIndex(tempBounds[i]);
            if (bestChildIndex != -1) {
                m_children[bestChildIndex]->insert(tempObjects[i], tempBounds[i]);
            }
            else {
                m_objects.push_back(tempObjects[i]);
                m_objectBounds.push_back(tempBounds[i]);
            }
        }
    }

    template<typename T>
    int OctreeNode<T>::getChildIndex(const AABBQ& objectBounds) const {
        if (m_isLeaf) return -1;

        Vec3Q center = m_bounds.getCenter();

        bool inPositiveX = objectBounds.min.x >= center.x;
        bool inNegativeX = objectBounds.max.x < center.x;
        bool inPositiveY = objectBounds.min.y >= center.y;
        bool inNegativeY = objectBounds.max.y < center.y;
        bool inPositiveZ = objectBounds.min.z >= center.z;
        bool inNegativeZ = objectBounds.max.z < center.z;

        if ((inPositiveX || inNegativeX) && (inPositiveY || inNegativeY) && (inPositiveZ || inNegativeZ)) {
            int index = 0;
            if (inPositiveX) index |= 1;
            if (inPositiveY) index |= 2;
            if (inPositiveZ) index |= 4;
            return index;
        }

        return -1;
    }

    // Octree template implementations
    template<typename T>
    std::vector<T> Octree<T>::query(const AABBQ& bounds) const {
        std::vector<T> results;
        m_root->query(bounds, results);
        return results;
    }

    template<typename T>
    std::vector<T> Octree<T>::query(const Frustum& frustum) const {
        std::vector<T> results;
        m_root->query(frustum, results);
        return results;
    }

    template<typename T>
    std::vector<T> Octree<T>::getAllObjects() const {
        std::vector<T> results;
        if (m_root) {
            m_root->getAllObjects(results);
        }
        return results;
    }

} // namespace tremor::gfx
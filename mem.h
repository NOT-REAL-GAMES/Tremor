#pragma once

#include "main.h"

namespace tremor::mem {

    // Memory tracking and allocation system
    class MemoryManager {
    public:
        static MemoryManager& instance() {
            static MemoryManager s_instance;
            return s_instance;
        }

        // Memory allocation stats
        struct Stats {
            std::atomic<size_t> totalAllocated{ 0 };
            std::atomic<size_t> totalFreed{ 0 };
            std::atomic<size_t> peakUsage{ 0 };
            std::atomic<size_t> currentUsage{ 0 };
            std::atomic<size_t> allocCount{ 0 };
            std::atomic<size_t> freeCount{ 0 };

            // No need to synchronize this as it's only accessed with the mutex held
            std::unordered_map<size_t, size_t> allocationSizeHistogram;
        };

        // Explicit memory allocations (use sparingly)
        void* allocate(size_t size, const char* tag = nullptr) {
            if (size == 0) return nullptr;

            // Allocate memory with header for tracking
            constexpr size_t headerSize = sizeof(AllocationHeader);
            size_t totalSize = size + headerSize;

            void* rawMemory = std::malloc(totalSize);
            if (!rawMemory) {
                std::cerr << "MemoryManager: Failed to allocate " << size << " bytes" << std::endl;
                return nullptr;
            }

            // Setup header
            AllocationHeader* header = static_cast<AllocationHeader*>(rawMemory);
            header->size = size;
            header->magic = ALLOCATION_MAGIC;

            if (tag) {
                std::string_view src(tag);
                auto copyLen = std::min(src.length(), sizeof(header->tag) - 1);
                std::copy_n(src.begin(), copyLen, header->tag);
                header->tag[copyLen] = '\0';
            }
            else {
                header->tag[0] = '\0';
            }

            // Update statistics
            stats.totalAllocated += size;
            stats.currentUsage += size;
            stats.allocCount++;

            size_t current = stats.currentUsage.load();
            size_t peak = stats.peakUsage.load();
            while (current > peak) {
                if (stats.peakUsage.compare_exchange_weak(peak, current)) {
                    break;
                }
                current = stats.currentUsage.load();
                peak = stats.peakUsage.load();
            }

            // Track allocation size distribution for debugging
            {
                std::lock_guard<std::mutex> lock(allocationMutex);
                stats.allocationSizeHistogram[size]++;

                // Track detailed allocation info if debugging is enabled
                if (trackAllocations) {
                    AllocationInfo info;
                    info.size = size;
                    info.tag = tag ? tag : "";

                    // Capture stack trace if available
#ifdef TREMOR_CAPTURE_STACK_TRACES
                    info.stackTraceSize = captureStackTrace(info.stackTrace, 20);
#else
                    info.stackTraceSize = 0;
#endif

                    allocations[static_cast<uint8_t*>(rawMemory) + headerSize] = info;
                }
            }

            // Return pointer past the header
            return static_cast<uint8_t*>(rawMemory) + headerSize;
        }

        void* reallocate(void* ptr, size_t newSize, const char* tag = nullptr) {
            if (!ptr) {
                return allocate(newSize, tag);
            }

            if (newSize == 0) {
                free(ptr);
                return nullptr;
            }

            // Get the header
            AllocationHeader* header = getAllocationHeader(ptr);
            if (!header || header->magic != ALLOCATION_MAGIC) {
                std::cerr << "MemoryManager: Invalid pointer passed to reallocate" << std::endl;
                return nullptr;
            }

            // Update statistics for the old allocation
            size_t oldSize = header->size;
            stats.totalFreed += oldSize;
            stats.currentUsage -= oldSize;
            stats.freeCount++;

            {
                std::lock_guard<std::mutex> lock(allocationMutex);
                // Decrement the old size in the histogram
                auto it = stats.allocationSizeHistogram.find(oldSize);
                if (it != stats.allocationSizeHistogram.end()) {
                    if (it->second > 1) {
                        it->second--;
                    }
                    else {
                        stats.allocationSizeHistogram.erase(it);
                    }
                }

                // Remove from detailed tracking
                if (trackAllocations) {
                    allocations.erase(ptr);
                }
            }

            // Perform reallocation
            constexpr size_t headerSize = sizeof(AllocationHeader);
            size_t totalSize = newSize + headerSize;

            void* rawMemory = std::realloc(header, totalSize);
            if (!rawMemory) {
                std::cerr << "MemoryManager: Failed to reallocate " << newSize << " bytes" << std::endl;
                return nullptr;
            }

            // Update header
            header = static_cast<AllocationHeader*>(rawMemory);
            header->size = newSize;

            if (tag) {
                std::string_view src(tag);
                auto copyLen = std::min(src.length(), sizeof(header->tag) - 1);
                std::copy_n(src.begin(), copyLen, header->tag);
                header->tag[sizeof(header->tag) - 1] = '\0';
            }

            // Update statistics for the new allocation
            stats.totalAllocated += newSize;
            stats.currentUsage += newSize;
            stats.allocCount++;

            size_t current = stats.currentUsage.load();
            size_t peak = stats.peakUsage.load();
            while (current > peak) {
                if (stats.peakUsage.compare_exchange_weak(peak, current)) {
                    break;
                }
                current = stats.currentUsage.load();
                peak = stats.peakUsage.load();
            }

            {
                std::lock_guard<std::mutex> lock(allocationMutex);
                // Increment the new size in the histogram
                stats.allocationSizeHistogram[newSize]++;

                // Track the new allocation
                if (trackAllocations) {
                    AllocationInfo info;
                    info.size = newSize;
                    info.tag = tag ? tag : "";

#ifdef TREMOR_CAPTURE_STACK_TRACES
                    info.stackTraceSize = captureStackTrace(info.stackTrace, 20);
#else
                    info.stackTraceSize = 0;
#endif

                    allocations[static_cast<uint8_t*>(rawMemory) + headerSize] = info;
                }
            }

            // Return pointer past the header
            return static_cast<uint8_t*>(rawMemory) + headerSize;
        }

        void free(void* ptr) {
            if (!ptr) return;

            // Get the header
            AllocationHeader* header = getAllocationHeader(ptr);
            if (!header || header->magic != ALLOCATION_MAGIC) {
                std::cerr << "MemoryManager: Invalid pointer passed to free" << std::endl;
                return;
            }

            // Update statistics
            size_t size = header->size;
            stats.totalFreed += size;
            stats.currentUsage -= size;
            stats.freeCount++;

            {
                std::lock_guard<std::mutex> lock(allocationMutex);
                // Update the size histogram
                auto it = stats.allocationSizeHistogram.find(size);
                if (it != stats.allocationSizeHistogram.end()) {
                    if (it->second > 1) {
                        it->second--;
                    }
                    else {
                        stats.allocationSizeHistogram.erase(it);
                    }
                }

                // Remove from detailed tracking
                if (trackAllocations) {
                    allocations.erase(ptr);
                }
            }

            // Invalidate the header to catch double-frees
            header->magic = 0;

            // Free the actual memory
            std::free(header);
        }

        // Create objects with memory tracking
        template<typename T, typename... Args>
        T* createObject(Args&&... args) {
            void* memory = allocate(sizeof(T), typeid(T).name());
            if (!memory) return nullptr;

            try {
                return new(memory) T(std::forward<Args>(args)...);
            }
            catch (...) {
                free(memory);
                throw;
            }
        }

        // Destroy tracked objects
        template<typename T>
        void destroyObject(T* obj) {
            if (obj) {
                obj->~T();
                free(obj);
            }
        }

        // Get memory stats
        const Stats& getStats() const { return stats; }

        // Reset stats (for level changes, etc)
        void resetStats() {
            stats.totalAllocated = 0;
            stats.totalFreed = 0;
            stats.peakUsage = 0;
            stats.currentUsage = 0;
            stats.allocCount = 0;
            stats.freeCount = 0;

            std::lock_guard<std::mutex> lock(allocationMutex);
            stats.allocationSizeHistogram.clear();
        }

        // Enable/disable detailed allocation tracking
        void setTrackAllocations(bool enable) {
            std::lock_guard<std::mutex> lock(allocationMutex);
            trackAllocations = enable;
            if (!enable) {
                allocations.clear();
            }
        }

        // Dump memory leaks - call before shutdown
        void dumpLeaks(std::ostream& out = std::cerr) {
            std::lock_guard<std::mutex> lock(allocationMutex);

            if (!trackAllocations || allocations.empty()) {
                out << "No memory leaks detected or tracking disabled." << std::endl;
                return;
            }

            out << "Memory leaks detected: " << allocations.size() << " allocations not freed" << std::endl;
            out << "Current memory usage: " << stats.currentUsage << " bytes" << std::endl;

            size_t totalLeaked = 0;
            for (const auto& [ptr, info] : allocations) {
                totalLeaked += info.size;
                out << "  Leak: " << info.size << " bytes";
                if (!info.tag.empty()) {
                    out << " [" << info.tag << "]";
                }
                out << std::endl;

#ifdef TREMOR_CAPTURE_STACK_TRACES
                if (info.stackTraceSize > 0) {
                    out << "    Allocation stack trace:" << std::endl;
                    printStackTrace(out, info.stackTrace, info.stackTraceSize);
                }
#endif
            }

            out << "Total leaked memory: " << totalLeaked << " bytes" << std::endl;
        }

    private:
        static constexpr uint32_t ALLOCATION_MAGIC = 0xDEADBEEF;

        // Header stored before each allocation
        struct AllocationHeader {
            size_t size;          // Size of the allocation (excluding header)
            uint32_t magic;       // Magic number to detect invalid frees
            char tag[32];         // Optional tag for debugging
        };

        // Get the header from a user pointer
        AllocationHeader* getAllocationHeader(void* ptr) {
            if (!ptr) return nullptr;
            return reinterpret_cast<AllocationHeader*>(
                static_cast<uint8_t*>(ptr) - sizeof(AllocationHeader)
                );
        }

        Stats stats;
        std::mutex allocationMutex;
        bool trackAllocations = true;  // Enable by default

        // Track allocations for leak detection
        struct AllocationInfo {
            size_t size;
            std::string tag;
            void* stackTrace[20];
            int stackTraceSize;
        };
        std::unordered_map<void*, AllocationInfo> allocations;

#ifdef TREMOR_CAPTURE_STACK_TRACES
        // Platform-specific stack trace capture
        int captureStackTrace(void** trace, int maxDepth) {
            // This would be implemented differently per platform
            // For now, a stub implementation
            return 0;
        }

        void printStackTrace(std::ostream& out, void** trace, int size) {
            // This would be implemented differently per platform
            out << "    Stack trace not available on this platform" << std::endl;
        }
#endif
    };

    // RAII wrapper for temporary memory allocations
    template<typename T>
    class ScopedAlloc {
    public:
        explicit ScopedAlloc(size_t count = 1) {
            data = static_cast<T*>(MemoryManager::instance().allocate(sizeof(T) * count, "ScopedAlloc"));
            elementCount = count;

            // Default initialize the memory if it's not trivially constructible
            if constexpr (!std::is_trivially_constructible_v<T>) {
                for (size_t i = 0; i < count; ++i) {
                    new(&data[i]) T();
                }
            }
        }

        ~ScopedAlloc() {
            if (data) {
                // Call destructors if needed
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    for (size_t i = 0; i < elementCount; ++i) {
                        data[i].~T();
                    }
                }

                MemoryManager::instance().free(data);
            }
        }

        // No copying
        ScopedAlloc(const ScopedAlloc&) = delete;
        ScopedAlloc& operator=(const ScopedAlloc&) = delete;

        // Allow moving
        ScopedAlloc(ScopedAlloc&& other) noexcept : data(other.data), elementCount(other.elementCount) {
            other.data = nullptr;
            other.elementCount = 0;
        }

        ScopedAlloc& operator=(ScopedAlloc&& other) noexcept {
            if (this != &other) {
                // Clean up existing data
                if (data) {
                    // Call destructors if needed
                    if constexpr (!std::is_trivially_destructible_v<T>) {
                        for (size_t i = 0; i < elementCount; ++i) {
                            data[i].~T();
                        }
                    }

                    MemoryManager::instance().free(data);
                }

                // Take ownership of other's data
                data = other.data;
                elementCount = other.elementCount;
                other.data = nullptr;
                other.elementCount = 0;
            }
            return *this;
        }

        // Access operators
        T* get() const { return data; }
        T& operator[](size_t index) {
            if (index >= elementCount) {
                throw std::out_of_range("ScopedAlloc index out of range");
            }
            return data[index];
        }
        const T& operator[](size_t index) const {
            if (index >= elementCount) {
                throw std::out_of_range("ScopedAlloc index out of range");
            }
            return data[index];
        }

        // Size info
        size_t size() const { return elementCount; }

        // Iterator support
        T* begin() { return data; }
        T* end() { return data + elementCount; }
        const T* begin() const { return data; }
        const T* end() const { return data + elementCount; }

        // Pointer arithmetic operators
        T* operator+(size_t offset) const {
            if (offset >= elementCount) {
                throw std::out_of_range("ScopedAlloc offset out of range");
            }
            return data + offset;
        }

    private:
        T* data = nullptr;
        size_t elementCount = 0;
    };

    // Convenience allocation functions
    template<typename T, typename... Args>
    T* createObject(Args&&... args) {
        return MemoryManager::instance().createObject<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    void destroyObject(T* obj) {
        MemoryManager::instance().destroyObject(obj);
    }

    // Wrappers for raw allocation functions
    inline void* allocate(size_t size, const char* tag = nullptr) {
        return MemoryManager::instance().allocate(size, tag);
    }

    inline void* reallocate(void* ptr, size_t newSize, const char* tag = nullptr) {
        return MemoryManager::instance().reallocate(ptr, newSize, tag);
    }

    inline void free(void* ptr) {
        MemoryManager::instance().free(ptr);
    }

    // Dynamic array with memory tracking
    template<typename T>
    class DynamicArray {
    public:
        DynamicArray() = default;

        explicit DynamicArray(size_t initialCapacity) {
            reserve(initialCapacity);
        }

        DynamicArray(const DynamicArray& other) {
            if (other.m_size > 0) {
                reserve(other.m_size);
                m_size = other.m_size;

                for (size_t i = 0; i < m_size; ++i) {
                    new(&m_data[i]) T(other.m_data[i]);
                }
            }
        }

        DynamicArray(DynamicArray&& other) noexcept
            : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity) {
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }

        ~DynamicArray() {
            clear();
            if (m_data) {
                free(m_data);
                m_data = nullptr;
            }
        }

        DynamicArray& operator=(const DynamicArray& other) {
            if (this != &other) {
                clear();

                if (other.m_size > 0) {
                    reserve(other.m_size);
                    m_size = other.m_size;

                    for (size_t i = 0; i < m_size; ++i) {
                        new(&m_data[i]) T(other.m_data[i]);
                    }
                }
            }
            return *this;
        }

        DynamicArray& operator=(DynamicArray&& other) noexcept {
            if (this != &other) {
                clear();
                if (m_data) {
                    free(m_data);
                }

                m_data = other.m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;

                other.m_data = nullptr;
                other.m_size = 0;
                other.m_capacity = 0;
            }
            return *this;
        }

        void push_back(const T& value) {
            if (m_size >= m_capacity) {
                size_t newCapacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(newCapacity);
            }

            new(&m_data[m_size]) T(value);
            ++m_size;
        }

        void push_back(T&& value) {
            if (m_size >= m_capacity) {
                size_t newCapacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(newCapacity);
            }

            new(&m_data[m_size]) T(std::move(value));
            ++m_size;
        }

        template<typename... Args>
        T& emplace_back(Args&&... args) {
            if (m_size >= m_capacity) {
                size_t newCapacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(newCapacity);
            }

            T* element = new(&m_data[m_size]) T(std::forward<Args>(args)...);
            ++m_size;
            return *element;
        }

        void pop_back() {
            if (m_size > 0) {
                --m_size;
                m_data[m_size].~T();
            }
        }

        void clear() {
            for (size_t i = 0; i < m_size; ++i) {
                m_data[i].~T();
            }
            m_size = 0;
        }

        void reserve(size_t newCapacity) {
            if (newCapacity <= m_capacity) return;

            T* newData = static_cast<T*>(allocate(sizeof(T) * newCapacity, "DynamicArray"));

            // Move existing elements
            for (size_t i = 0; i < m_size; ++i) {
                new(&newData[i]) T(std::move(m_data[i]));
                m_data[i].~T();
            }

            // Free old buffer and update to new one
            if (m_data) {
                free(m_data);
            }

            m_data = newData;
            m_capacity = newCapacity;
        }

        void resize(size_t newSize) {
            if (newSize > m_capacity) {
                reserve(newSize);
            }

            // If growing, default-construct new elements
            if (newSize > m_size) {
                for (size_t i = m_size; i < newSize; ++i) {
                    new(&m_data[i]) T();
                }
            }
            // If shrinking, destroy excess elements
            else if (newSize < m_size) {
                for (size_t i = newSize; i < m_size; ++i) {
                    m_data[i].~T();
                }
            }

            m_size = newSize;
        }

        // Element access
        T& operator[](size_t index) {
            if (index >= m_size) {
                throw std::out_of_range("DynamicArray index out of range");
            }
            return m_data[index];
        }

        const T& operator[](size_t index) const {
            if (index >= m_size) {
                throw std::out_of_range("DynamicArray index out of range");
            }
            return m_data[index];
        }

        T& at(size_t index) {
            if (index >= m_size) {
                throw std::out_of_range("DynamicArray index out of range");
            }
            return m_data[index];
        }

        const T& at(size_t index) const {
            if (index >= m_size) {
                throw std::out_of_range("DynamicArray index out of range");
            }
            return m_data[index];
        }

        T& front() {
            if (m_size == 0) {
                throw std::out_of_range("DynamicArray is empty");
            }
            return m_data[0];
        }

        const T& front() const {
            if (m_size == 0) {
                throw std::out_of_range("DynamicArray is empty");
            }
            return m_data[0];
        }

        T& back() {
            if (m_size == 0) {
                throw std::out_of_range("DynamicArray is empty");
            }
            return m_data[m_size - 1];
        }

        const T& back() const {
            if (m_size == 0) {
                throw std::out_of_range("DynamicArray is empty");
            }
            return m_data[m_size - 1];
        }

        // Capacity
        bool empty() const { return m_size == 0; }
        size_t size() const { return m_size; }
        size_t capacity() const { return m_capacity; }

        // Iterators
        T* begin() { return m_data; }
        T* end() { return m_data + m_size; }
        const T* begin() const { return m_data; }
        const T* end() const { return m_data + m_size; }

    private:
        T* m_data = nullptr;
        size_t m_size = 0;
        size_t m_capacity = 0;
    };

}

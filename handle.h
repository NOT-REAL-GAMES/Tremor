#pragma once
#include "main.h"

namespace tremor::gfx {

    // Forward declarations for resource types
    class Texture;
    class Buffer;
    class Shader;
    class Pipeline;

    // Base resource class for reference counting
    class Resource {
    public:
        virtual ~Resource() = default;

        void addRef() { refCount++; }
        void release() {
            if (--refCount == 0) {
                delete this;
            }
        }

        // Factory registration could be added here

    protected:
        Resource() : refCount(0) {}

    private:
        std::atomic<int> refCount;
    };

    // Type-safe handle template
    template<typename T>
    class Handle {
    public:
        // Add unique identifier
        uint32_t id = 0;

        // Default constructor creates a null handle
        Handle() : resource(nullptr), id(0) {}

        // Construct from a raw resource pointer
        explicit Handle(T* res, uint32_t resourceId = 0) : resource(res), id(resourceId) {
            if (resource) resource->addRef();
        }

        // Create a handle with just an ID (for ID-based lookup systems)
        static Handle fromId(uint32_t resourceId) {
            Handle handle;
            handle.id = resourceId;
            return handle;
        }

        // Copy constructor
        Handle(const Handle& other) : resource(other.resource), id(other.id) {
            if (resource) resource->addRef();
        }

        // Move constructor
        Handle(Handle&& other) noexcept : resource(other.resource), id(other.id) {
            other.resource = nullptr;
            other.id = 0;
        }

        // Destructor
        ~Handle() {
            if (resource) resource->release();
        }

        // Copy assignment
        Handle& operator=(const Handle& other) {
            if (this != &other) {
                if (resource) resource->release();
                resource = other.resource;
                id = other.id;
                if (resource) resource->addRef();
            }
            return *this;
        }

        // Move assignment
        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                if (resource) resource->release();
                resource = other.resource;
                id = other.id;
                other.resource = nullptr;
                other.id = 0;
            }
            return *this;
        }

        // Access operators
        T* operator->() const { return resource; }
        T& operator*() const {
            assert(resource && "Trying to dereference a null handle");
            return *resource;
        }

        // Comparison operators - compare by ID if available, otherwise by pointer
        bool operator==(const Handle& other) const {
            return (id != 0 && other.id != 0) ? (id == other.id) : (resource == other.resource);
        }

        bool operator!=(const Handle& other) const {
            return !(*this == other);
        }

        // Check if handle is valid - can be valid if it has a resource pointer OR a valid ID
        explicit operator bool() const { return resource != nullptr || id != 0; }
        bool isValid() const { return resource != nullptr || id != 0; }

        // Check if resource is loaded (not just an ID)
        bool isLoaded() const { return resource != nullptr; }

        // Get raw pointer (use with caution)
        T* get() const { return resource; }

        // Reset handle
        void reset() {
            if (resource) {
                resource->release();
                resource = nullptr;
            }
            id = 0;
        }

        // Cast to another handle type (if resources are related)
        template<typename U>
        Handle<U> as() const {
            // Safe downcast with dynamic_cast
            U* casted = dynamic_cast<U*>(resource);
            return Handle<U>(casted, id);
        }

    private:
        T* resource;
    };

} // namespace tremor::gfx
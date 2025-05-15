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
        // Default constructor creates a null handle
        Handle() : resource(nullptr) {}

        // Construct from a raw resource pointer
        explicit Handle(T* res) : resource(res) {
            if (resource) resource->addRef();
        }

        // Copy constructor
        Handle(const Handle& other) : resource(other.resource) {
            if (resource) resource->addRef();
        }

        // Move constructor
        Handle(Handle&& other) noexcept : resource(other.resource) {
            other.resource = nullptr;
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
                if (resource) resource->addRef();
            }
            return *this;
        }

        // Move assignment
        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                if (resource) resource->release();
                resource = other.resource;
                other.resource = nullptr;
            }
            return *this;
        }

        // Access operators
        T* operator->() const { return resource; }
        T& operator*() const {
            assert(resource && "Trying to dereference a null handle");
            return *resource;
        }

        // Comparison operators
        bool operator==(const Handle& other) const { return resource == other.resource; }
        bool operator!=(const Handle& other) const { return resource != other.resource; }

        // Check if handle is valid
        explicit operator bool() const { return resource != nullptr; }
        bool isValid() const { return resource != nullptr; }

        // Get raw pointer (use with caution)
        T* get() const { return resource; }

        // Reset handle
        void reset() {
            if (resource) {
                resource->release();
                resource = nullptr;
            }
        }

        // Cast to another handle type (if resources are related)
        template<typename U>
        Handle<U> as() const {
            // Safe downcast with dynamic_cast
            U* casted = dynamic_cast<U*>(resource);
            return Handle<U>(casted);
        }

    private:
        T* resource;
    };

} // namespace tremor::gfx
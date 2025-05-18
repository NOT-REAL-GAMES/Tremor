#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <expected>
#include <vector>
#include <deque>
#include <execution>        // C++23 improved parallel algorithms
#include "vm.hpp"           // For VMError

namespace tremor::vm {

    // Memory management for the VM
    class VMMemory {
    public:
        VMMemory(std::size_t dataSize, std::size_t stackSize);
        ~VMMemory() = default;

        // Memory access with std::expected
        std::expected<std::span<std::byte>, VMError> getMemorySpan(std::size_t offset, std::size_t size);

        // Stack management
        std::expected<void, VMError> pushStack(intptr_t value);
        std::expected<intptr_t, VMError> popStack();

        // Get direct pointers for internal VM use (unsafe!)
        std::byte* getDataBase() { return m_dataSegment.data(); }
        intptr_t* getStackBase() { return m_stack.data(); }

        // Helper to read values with C++23 improvements
        template<typename T>
        std::expected<T, VMError> readMemory(std::size_t offset) {
            auto span = getMemorySpan(offset, sizeof(T));

            // C++23 monadic operations with std::expected
            return span.and_then([](auto bytes) -> std::expected<T, VMError> {
                if (bytes.size() < sizeof(T)) {
                    return std::unexpected(VMError::SegmentationFault);
                }

                T value;
                std::memcpy(&value, bytes.data(), sizeof(T));
                return value;
                });
        }

        // Write values with C++23 improvements
        template<typename T>
        std::expected<void, VMError> writeMemory(std::size_t offset, const T& value) {
            auto span = getMemorySpan(offset, sizeof(T));

            // C++23 monadic operations
            return span.and_then([&value](auto bytes) -> std::expected<void, VMError> {
                if (bytes.size() < sizeof(T)) {
                    return std::unexpected(VMError::SegmentationFault);
                }

                std::memcpy(bytes.data(), &value, sizeof(T));
                return {};
                });
        }

        // Block copy with C++23 parallel algorithms
        std::expected<void, VMError> blockCopy(std::size_t destOffset, std::size_t srcOffset, std::size_t size);

    private:
        std::vector<std::byte> m_dataSegment;
        std::vector<intptr_t> m_stack;
        std::size_t m_stackPointer = 0;
    };

    // Implementation of block copy using C++23 parallel algorithms
    inline std::expected<void, VMError> VMMemory::blockCopy(
        std::size_t destOffset, std::size_t srcOffset, std::size_t size) {

        auto srcSpan = getMemorySpan(srcOffset, size);
        auto destSpan = getMemorySpan(destOffset, size);

        // Using C++23 monadic operations for cleaner error handling
        return srcSpan.and_then([&](auto src) {
            return destSpan.and_then([&](auto dest) -> std::expected<void, VMError> {
                // Using C++23 parallel algorithms for large copies
                if (size > 1024) {
                    std::copy(std::execution::par_unseq, src.begin(), src.end(), dest.begin());
                }
                else {
                    std::copy(src.begin(), src.end(), dest.begin());
                }
                return {};
                });
            });
    }

} // namespace tremor::vm
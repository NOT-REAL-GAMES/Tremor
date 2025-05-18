#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <span>
#include <expected>         // C++23 native std::expected
#include <optional>
#include <filesystem>
#include <concepts>
#include <stacktrace>       // C++23 stacktrace for better debugging

namespace tremor::vm {

    // Error handling with std::expected
    enum class VMError {
        None,
        FileNotFound,
        InvalidBytecode,
        StackOverflow,
        InvalidInstruction,
        SystemCallError,
        OutOfMemory,
        SegmentationFault,
        DivisionByZero,
        UnknownError
    };

    // String conversion for VMError - C++23 allows for more constexpr string operations
    constexpr std::string_view to_string(VMError error) {
        switch (error) {
        case VMError::None: return "No error";
        case VMError::FileNotFound: return "File not found";
        case VMError::InvalidBytecode: return "Invalid bytecode";
        case VMError::StackOverflow: return "Stack overflow";
        case VMError::InvalidInstruction: return "Invalid instruction";
        case VMError::SystemCallError: return "System call error";
        case VMError::OutOfMemory: return "Out of memory";
        case VMError::SegmentationFault: return "Segmentation fault";
        case VMError::DivisionByZero: return "Division by zero";
        case VMError::UnknownError: return "Unknown error";
        }
        return "Unknown error";
    }

    // Type-safe system call handler concept - improved C++23 concepts
    template<typename T>
    concept SystemCallHandler = requires(T handler, std::span<intptr_t> args) {
        { handler(args) } -> std::convertible_to<intptr_t>;
        { handler.is_available(int{}) } -> std::convertible_to<bool>;
    };

    // VM execution context
    class VMContext {
    public:
        // Disallow copying to prevent duplicate VMs
        VMContext(const VMContext&) = delete;
        VMContext& operator=(const VMContext&) = delete;

        // Allow moving for ownership transfer
        VMContext(VMContext&&) noexcept = default;
        VMContext& operator=(VMContext&&) noexcept = default;

        // Static factory method with std::expected
        static std::expected<std::unique_ptr<VMContext>, VMError> create(
            std::string_view name,
            std::filesystem::path bytecodeFile,
            std::function<intptr_t(std::span<intptr_t>)> systemCallHandler
        );

        // Call a VM function by index
        std::expected<intptr_t, VMError> callFunction(int functionIndex, std::span<intptr_t> args = {});

        // Call a VM function by name
        std::expected<intptr_t, VMError> callFunction(std::string_view functionName, std::span<intptr_t> args = {});

        // Check if a function exists
        bool hasFunction(std::string_view functionName) const;

        // Get VM statistics
        struct Statistics {
            std::size_t memoryUsage;
            std::size_t instructionsExecuted;
            double executionTimeMs;
            std::size_t systemCallsInvoked;

            // C++23 spaceship operator for easy comparison
            auto operator<=>(const Statistics&) const = default;
        };

        Statistics getStatistics() const;

        // Get a stacktrace - C++23 feature for better debugging
        std::stacktrace getCurrentStacktrace() const;

        // Cleanup
        ~VMContext();

    private:
        // Private constructor - use factory method instead
        VMContext(std::string_view name);

        // Implementation details
        class Implementation;
        std::unique_ptr<Implementation> m_impl;
    };

    // Helper function for easier VM creation
    std::expected<std::unique_ptr<VMContext>, VMError> createVM(
        std::string_view name,
        std::filesystem::path bytecodeFile,
        std::function<intptr_t(std::span<intptr_t>)> systemCallHandler
    ) {
        return VMContext::create(name, std::move(bytecodeFile), std::move(systemCallHandler));
    }

} // namespace tremor::vm
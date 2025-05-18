// vm.cpp
#include "vm.hpp"

namespace tremor::vm {

    // Minimal Implementation class to make the code compile
    class VMContext::Implementation {
    public:
        Implementation(std::string_view name) : m_name(name) {}

        // Stub methods (to be implemented)
        bool hasFunction(std::string_view) const { return false; }
        int getFunctionIndex(std::string_view) const { return -1; }
        Statistics getStatistics() const { return {}; }

    private:
        std::string m_name;
    };

    // Then update the constructor in vm.cpp
    VMContext::VMContext(std::string_view name)
        : m_impl(std::make_unique<Implementation>(name)) {
    }


    // Implementation of VMContext factory method
    std::expected<std::unique_ptr<VMContext>, VMError> VMContext::create(
        std::string_view name,
        std::filesystem::path bytecodeFile,
        std::function<intptr_t(std::span<intptr_t>)> systemCallHandler
    ) {
        // Create the VM context
        auto vmContext = std::unique_ptr<VMContext>(new VMContext(name));

        // Implementation will be added as you build out the VM
        // For now, return a basic implementation that works

        return vmContext;
    }

    // Implementation of function calls
    std::expected<intptr_t, VMError> VMContext::callFunction(int functionIndex, std::span<intptr_t> args) {
        // To be implemented
        return 0; // Placeholder
    }

    std::expected<intptr_t, VMError> VMContext::callFunction(std::string_view functionName, std::span<intptr_t> args) {
        // To be implemented
        return 0; // Placeholder
    }

    bool VMContext::hasFunction(std::string_view functionName) const {
        // To be implemented
        return false; // Placeholder
    }

    VMContext::Statistics VMContext::getStatistics() const {
        // To be implemented
        return {}; // Placeholder
    }

    std::stacktrace VMContext::getCurrentStacktrace() const {
        // To be implemented
        return std::stacktrace::current();
    }

    VMContext::~VMContext() = default;

} // namespace tremor::vm
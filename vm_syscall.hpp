#pragma once

#include <cstdint>
#include <span>
#include <functional>
#include <unordered_map>
#include <expected>
#include <any>              // Improved C++23 any support
#include <format>
#include <print>
#include "vm.hpp"

namespace tremor::vm {

    // System call interface with C++23 improvements
    class SystemCallInterface {
    public:
        using HandlerFunc = std::function<intptr_t(std::span<intptr_t>)>;

        // Default constructor registers standard handlers
        SystemCallInterface();

        // Register a system call handler
        void registerHandler(int syscallNum, HandlerFunc handler) {
            m_handlers[syscallNum] = std::move(handler);
        }

        // Check if a system call is available
        bool is_available(int syscallNum) const {
            return m_handlers.contains(syscallNum);  // C++23 contains
        }

        // Dispatch a system call
        intptr_t dispatch(std::span<intptr_t> args) {
            if (args.empty()) {
                std::print(stderr, "Empty system call arguments\n");  // C++23 print
                return -1;
            }

            int syscallNum = args[0];

            // C++23 if with initializer
            if (auto it = m_handlers.find(syscallNum); it != m_handlers.end()) {
                try {
                    return it->second(args);
                }
                catch (const std::exception& e) {
                    std::print(stderr, "Exception in system call {}: {}\n",
                        syscallNum, e.what());  // C++23 print
                    return -1;
                }
            }

            std::print(stderr, "Unknown system call: {}\n", syscallNum);  // C++23 print
            return -1;
        }

        // Add a custom data provider for system calls
        template<typename T>
        void setDataProvider(std::string_view name, T* provider) {
            m_dataProviders[std::string(name)] = provider;
        }

        // Get a data provider
        template<typename T>
        T* getDataProvider(std::string_view name) {
            auto it = m_dataProviders.find(std::string(name));
            if (it != m_dataProviders.end()) {
                return std::any_cast<T*>(it->second);
            }
            return nullptr;
        }

    private:
        std::unordered_map<int, HandlerFunc> m_handlers;
        std::unordered_map<std::string, std::any> m_dataProviders;

        // Standard system call handlers
        intptr_t handlePrint(std::span<intptr_t> args);
        intptr_t handleError(std::span<intptr_t> args);
        intptr_t handleMilliseconds(std::span<intptr_t> args);
        intptr_t handleFileOpen(std::span<intptr_t> args);
        intptr_t handleFileRead(std::span<intptr_t> args);
        intptr_t handleFileWrite(std::span<intptr_t> args);
        intptr_t handleFileClose(std::span<intptr_t> args);
    };

} // namespace tremor::vm
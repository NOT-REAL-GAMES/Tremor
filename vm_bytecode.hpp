#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <filesystem>
#include <format>
#include <unordered_map>
#include <memory>

namespace tremor::vm {

    // Error handling
    enum class VMError {
        None,
        FileNotFound,
        InvalidBytecode,
        InvalidMagic,
        ReadError,
        OutOfMemory,
        InvalidFunction,
        SegmentationFault,
        UnknownError
    };

    // Q3VM bytecode header
    struct VMHeader {
        int32_t magic;           // Magic number identifying Q3VM format (0x12721444)
        int32_t instructionCount;// Number of instructions
        int32_t codeOffset;      // File offset to code segment
        int32_t codeLength;      // Length of code segment
        int32_t dataOffset;      // File offset to data segment
        int32_t dataLength;      // Length of data segment
        int32_t litOffset;       // File offset to string literal segment
        int32_t litLength;       // Length of string literal segment
        int32_t bssOffset;       // File offset to BSS segment (runtime only)
        int32_t bssLength;       // Length needed for BSS segment (zero-initialized)

        // C++23 format to string
        std::string toString() const {
            return std::format(
                "Q3VM Header:\n"
                "  Magic: 0x{:08X}\n"
                "  Instructions: {}\n"
                "  Code: offset={}, size={}\n"
                "  Data: offset={}, size={}\n"
                "  Lit: offset={}, size={}\n"
                "  BSS: offset={}, size={}\n",
                magic, instructionCount,
                codeOffset, codeLength,
                dataOffset, dataLength,
                litOffset, litLength,
                bssOffset, bssLength
            );
        }
    };

    // Function metadata extracted from bytecode
    struct VMFunction {
        std::string name;       // Function name
        int32_t codeOffset;     // Offset in code segment
        int32_t parameters;     // Number of parameters

        // C++23 spaceship operator
        auto operator<=>(const VMFunction&) const = default;
    };

    // Bytecode parser class
    class BytecodeParser {
    public:
        // Factory method to create parser from file
        static std::expected<std::unique_ptr<BytecodeParser>, VMError> fromFile(
            const std::filesystem::path& path);

        // Get header information
        const VMHeader& getHeader() const { return m_header; }

        // Get bytecode segments
        std::span<const std::byte> getCodeSegment() const { return m_codeSegment; }
        std::span<const std::byte> getDataSegment() const { return m_dataSegment; }
        std::span<const std::byte> getLitSegment() const { return m_litSegment; }

        // Get BSS size (for allocating zero-initialized memory)
        size_t getBssSize() const { return m_header.bssLength; }

        // Get total memory needed for all segments
        size_t getTotalMemorySize() const {
            return m_header.dataLength + m_header.litLength + m_header.bssLength;
        }

        // Get all functions
        std::span<const VMFunction> getFunctions() const { return m_functions; }

        // Find function by name with C++23 monadic operations
        std::expected<const VMFunction*, VMError> findFunction(std::string_view name) const;

        // Find function by code offset
        std::expected<const VMFunction*, VMError> findFunctionByOffset(int32_t offset) const;

        // Get string from lit segment (for debugging)
        std::expected<std::string_view, VMError> getString(int32_t offset) const;

        // Validity check
        bool isValid() const { return m_valid; }

        // Debug dump
        void dumpInfo() const;

        // Private constructor - use factory method
        BytecodeParser() : m_valid(false) {}

        // Parse file content
        std::expected<void, VMError> parseFile(const std::filesystem::path& path);

        // Parse function table from data segment
        std::expected<void, VMError> parseFunctionTable();

        VMHeader m_header;                  // Bytecode header
        std::vector<std::byte> m_codeSegment;   // Code segment
        std::vector<std::byte> m_dataSegment;   // Data segment  
        std::vector<std::byte> m_litSegment;    // String literals
        std::vector<VMFunction> m_functions;    // Extracted functions
        bool m_valid;                       // Validity flag
    };

} // namespace tremor::vm
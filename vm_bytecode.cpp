#include "vm_bytecode.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <ranges>
#include <print>

namespace tremor::vm {

    // Magic number for Q3VM format
    constexpr int32_t VM_MAGIC = 0x12721444;

    // Helper to read from a binary file
    template<typename T>
    std::expected<T, VMError> readBinary(std::ifstream& file) {
        T value;
        if (!file.read(reinterpret_cast<char*>(&value), sizeof(T))) {
            return std::unexpected(VMError::ReadError);
        }
        return value;
    }

    // Static factory method implementation
    std::expected<std::unique_ptr<BytecodeParser>, VMError> BytecodeParser::fromFile(
        const std::filesystem::path& path) {

        auto parser = std::make_unique<BytecodeParser>();
        auto result = parser->parseFile(path);

        if (!result) {
            return std::unexpected(result.error());
        }

        parser->m_valid = true;
        return parser;
    }

    // Parse bytecode file
    std::expected<void, VMError> BytecodeParser::parseFile(const std::filesystem::path& path) {
        // Open file
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::unexpected(VMError::FileNotFound);
        }

        // Read header
        auto headerResult = readBinary<VMHeader>(file);
        if (!headerResult) {
            return std::unexpected(headerResult.error());
        }

        m_header = headerResult.value();

        // Verify magic number
        if (m_header.magic != VM_MAGIC) {
            return std::unexpected(VMError::InvalidMagic);
        }

        // Sanity check sizes
        if (m_header.codeLength <= 0 || m_header.codeLength > 1024 * 1024 * 10) {
            return std::unexpected(VMError::InvalidBytecode);
        }

        // Allocate memory for segments
        try {
            m_codeSegment.resize(m_header.codeLength);
            m_dataSegment.resize(m_header.dataLength);
            m_litSegment.resize(m_header.litLength);
        }
        catch (const std::bad_alloc&) {
            return std::unexpected(VMError::OutOfMemory);
        }

        // Read code segment
        file.seekg(m_header.codeOffset);
        if (!file.read(reinterpret_cast<char*>(m_codeSegment.data()), m_header.codeLength)) {
            return std::unexpected(VMError::ReadError);
        }

        // Read data segment
        file.seekg(m_header.dataOffset);
        if (!file.read(reinterpret_cast<char*>(m_dataSegment.data()), m_header.dataLength)) {
            return std::unexpected(VMError::ReadError);
        }

        // Read lit segment
        file.seekg(m_header.litOffset);
        if (!file.read(reinterpret_cast<char*>(m_litSegment.data()), m_header.litLength)) {
            return std::unexpected(VMError::ReadError);
        }

        // Parse function table
        auto funcResult = parseFunctionTable();
        if (!funcResult) {
            return std::unexpected(funcResult.error());
        }

        return {};
    }

    // Read a null-terminated string from a segment
    std::expected<std::string, VMError> readString(std::span<const std::byte> segment, int32_t offset) {
        if (offset < 0 || offset >= segment.size()) {
            return std::unexpected(VMError::SegmentationFault);
        }

        std::string result;
        while (offset < segment.size()) {
            char c = static_cast<char>(segment[offset++]);
            if (c == '\0') break;
            result.push_back(c);
        }

        return result;
    }

    // Parse function table from data segment
    std::expected<void, VMError> BytecodeParser::parseFunctionTable() {
        // Function entry in the table consists of name offset and code offset
        struct FunctionEntry {
            int32_t nameOffset;   // Offset to name string in lit segment
            int32_t codeOffset;   // Offset to function code in code segment
        };

        // Check if data segment is too small
        if (m_dataSegment.size() < sizeof(FunctionEntry)) {
            return std::unexpected(VMError::InvalidBytecode);
        }

        // Parse function table at the start of data segment
        size_t offset = 0;
        while (offset + sizeof(FunctionEntry) <= m_dataSegment.size()) {
            // Read function entry
            FunctionEntry entry;
            std::memcpy(&entry, m_dataSegment.data() + offset, sizeof(FunctionEntry));
            offset += sizeof(FunctionEntry);

            // End of function table is marked by -1 name offset
            if (entry.nameOffset == -1) {
                break;
            }

            // Read function name from lit segment
            auto nameResult = readString(m_litSegment, entry.nameOffset);
            if (!nameResult) {
                std::print(stderr, "Warning: Couldn't read function name at offset {}\n", entry.nameOffset);
                continue;
            }

            // Create function entry
            VMFunction func;
            func.name = nameResult.value();
            func.codeOffset = entry.codeOffset;
            func.parameters = 0;  // Would need to analyze code to determine this

            m_functions.push_back(func);
        }

        // Sort functions by code offset for binary search
        std::ranges::sort(m_functions, {}, &VMFunction::codeOffset);

        return {};
    }

    // Find function by name
    std::expected<const VMFunction*, VMError> BytecodeParser::findFunction(std::string_view name) const {
        // Using C++23 ranges to find the function
        auto it = std::ranges::find_if(m_functions, [name](const auto& func) {
            return func.name == name;
            });

        if (it != m_functions.end()) {
            return &(*it);
        }

        return std::unexpected(VMError::InvalidFunction);
    }

    // Find function by code offset
    std::expected<const VMFunction*, VMError> BytecodeParser::findFunctionByOffset(int32_t offset) const {
        // Binary search for function containing this offset
        auto it = std::ranges::lower_bound(m_functions, offset, {}, &VMFunction::codeOffset);

        // If we found a function or this is the last function, return it
        if (it != m_functions.end()) {
            if (it->codeOffset == offset) {
                return &(*it);
            }

            // If this isn't the first function, the offset might be in the previous function
            if (it != m_functions.begin()) {
                --it;
                // Calculate end of this function (start of next function)
                int32_t nextOffset = (it + 1 != m_functions.end()) ? (it + 1)->codeOffset : m_header.codeLength;

                // Check if offset is within this function
                if (offset >= it->codeOffset && offset < nextOffset) {
                    return &(*it);
                }
            }
        }

        return std::unexpected(VMError::InvalidFunction);
    }

    // Get string from lit segment
    std::expected<std::string_view, VMError> BytecodeParser::getString(int32_t offset) const {
        if (offset < 0 || offset >= m_litSegment.size()) {
            return std::unexpected(VMError::SegmentationFault);
        }

        // Find string length (null terminated)
        const char* start = reinterpret_cast<const char*>(m_litSegment.data() + offset);
        const char* end = start;
        while (end < reinterpret_cast<const char*>(m_litSegment.data() + m_litSegment.size()) && *end) {
            ++end;
        }

        return std::string_view(start, end - start);
    }

    // Debug info dump
    void BytecodeParser::dumpInfo() const {
        std::print("{}\n", m_header.toString());

        std::print("Functions ({}):\n", m_functions.size());
        for (const auto& func : m_functions) {
            std::print("  {} at offset 0x{:x}\n", func.name, func.codeOffset);
        }
    }

} // namespace tremor::vm
#pragma once

#include <cstdint>
#include <array>
#include <expected>
#include <span>
#include <chrono>
#include <stacktrace>
#include <print>
#include "vm.hpp"
#include "vm_memory.hpp"

namespace tremor::vm {

    // VM instruction opcodes with improved enum support
    enum class OpCode : uint8_t {
        UNDEF,
        IGNORE,
        BREAK,
        ENTER,
        LEAVE,
        CALL,
        PUSH,
        POP,
        CONST,
        LOCAL,
        JUMP,
        EQ,
        NE,
        LTI,
        LEI,
        GTI,
        GEI,
        LTU,
        LEU,
        GTU,
        GEU,
        EQF,
        NEF,
        LTF,
        LEF,
        GTF,
        GEF,
        LOAD1,
        LOAD2,
        LOAD4,
        STORE1,
        STORE2,
        STORE4,
        ARG,
        BLOCK_COPY,
        SEX8,
        SEX16,
        NEGI,
        ADD,
        SUB,
        DIVI,
        DIVU,
        MODI,
        MODU,
        MULI,
        MULU,
        BAND,
        BOR,
        BXOR,
        BCOM,
        LSH,
        RSHI,
        RSHU,
        NEGF,
        ADDF,
        SUBF,
        DIVF,
        MULF,
        CVIF,
        CVFI
    };

    // Get string representation of OpCode (C++23 improved constexpr)
    constexpr std::string_view to_string(OpCode op) {
        switch (op) {
        case OpCode::UNDEF: return "UNDEF";
        case OpCode::IGNORE: return "IGNORE";
        case OpCode::BREAK: return "BREAK";
        case OpCode::ENTER: return "ENTER";
        case OpCode::LEAVE: return "LEAVE";
        case OpCode::CALL: return "CALL";
        case OpCode::PUSH: return "PUSH";
        case OpCode::POP: return "POP";
        case OpCode::CONST: return "CONST";
        case OpCode::LOCAL: return "LOCAL";
        case OpCode::JUMP: return "JUMP";
            // ... other opcodes ...
        default: return "UNKNOWN";
        }
    }

    // VM instruction structure
    struct VMInstruction {
        OpCode opcode;

        struct {
            uint8_t reg;
            uint8_t sreg1;
            uint8_t sreg2;
            int32_t value;
        } operands;

        // C++23 improved string formatting
        std::string toString() const {
            return std::format("{} r{}={} v={}",
                to_string(opcode),
                operands.reg,
                operands.sreg1,
                operands.value);
        }
    };

    // VM instruction decoder with C++23 improvements
    class VMInstructionDecoder {
    public:
        VMInstructionDecoder(std::span<const std::byte> codeSegment)
            : m_codeSegment(codeSegment) {
        }

        // Decode the next instruction
        std::expected<VMInstruction, VMError> decodeNext();

        // Get/set program counter
        std::size_t getProgramCounter() const { return m_programCounter; }
        void setProgramCounter(std::size_t pc) { m_programCounter = pc; }

    private:
        std::span<const std::byte> m_codeSegment;
        std::size_t m_programCounter = 0;
    };

    // VM execution context
    class VMExecutionContext {
    public:
        VMExecutionContext(
            VMMemory& memory,
            std::span<const std::byte> codeSegment,
            std::function<intptr_t(std::span<intptr_t>)> systemCallHandler
        );

        // Execute a function
        std::expected<intptr_t, VMError> executeFunction(
            int32_t codeOffset,
            std::span<intptr_t> args
        );

        // Get statistics
        VMContext::Statistics getStatistics() const { return m_statistics; }

        // C++23 improved debugging
        std::stacktrace getCurrentStacktrace() const { return m_stacktrace; }

    private:
        VMMemory& m_memory;
        VMInstructionDecoder m_decoder;
        std::function<intptr_t(std::span<intptr_t>)> m_systemCallHandler;

        // Execution state
        std::array<intptr_t, 16> m_registers = {};
        int32_t m_programCounter = 0;
        bool m_halted = false;

        // For debugging
        std::stacktrace m_stacktrace;

        // Statistics
        VMContext::Statistics m_statistics = {};

        // Execute a single instruction
        std::expected<void, VMError> executeInstruction(const VMInstruction& inst);
    };

} // namespace tremor::vm
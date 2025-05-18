#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <format>
#include "vm_bytecode.hpp"

#undef IGNORE
#undef CONST

namespace tremor::vm {

    // VM instruction opcodes
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

    // Get string representation of OpCode
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
        case OpCode::EQ: return "EQ";
        case OpCode::NE: return "NE";
        case OpCode::LTI: return "LTI";
        case OpCode::LEI: return "LEI";
        case OpCode::GTI: return "GTI";
        case OpCode::GEI: return "GEI";
        case OpCode::LTU: return "LTU";
        case OpCode::LEU: return "LEU";
        case OpCode::GTU: return "GTU";
        case OpCode::GEU: return "GEU";
        case OpCode::EQF: return "EQF";
        case OpCode::NEF: return "NEF";
        case OpCode::LTF: return "LTF";
        case OpCode::LEF: return "LEF";
        case OpCode::GTF: return "GTF";
        case OpCode::GEF: return "GEF";
        case OpCode::LOAD1: return "LOAD1";
        case OpCode::LOAD2: return "LOAD2";
        case OpCode::LOAD4: return "LOAD4";
        case OpCode::STORE1: return "STORE1";
        case OpCode::STORE2: return "STORE2";
        case OpCode::STORE4: return "STORE4";
        case OpCode::ARG: return "ARG";
        case OpCode::BLOCK_COPY: return "BLOCK_COPY";
        case OpCode::SEX8: return "SEX8";
        case OpCode::SEX16: return "SEX16";
        case OpCode::NEGI: return "NEGI";
        case OpCode::ADD: return "ADD";
        case OpCode::SUB: return "SUB";
        case OpCode::DIVI: return "DIVI";
        case OpCode::DIVU: return "DIVU";
        case OpCode::MODI: return "MODI";
        case OpCode::MODU: return "MODU";
        case OpCode::MULI: return "MULI";
        case OpCode::MULU: return "MULU";
        case OpCode::BAND: return "BAND";
        case OpCode::BOR: return "BOR";
        case OpCode::BXOR: return "BXOR";
        case OpCode::BCOM: return "BCOM";
        case OpCode::LSH: return "LSH";
        case OpCode::RSHI: return "RSHI";
        case OpCode::RSHU: return "RSHU";
        case OpCode::NEGF: return "NEGF";
        case OpCode::ADDF: return "ADDF";
        case OpCode::SUBF: return "SUBF";
        case OpCode::DIVF: return "DIVF";
        case OpCode::MULF: return "MULF";
        case OpCode::CVIF: return "CVIF";
        case OpCode::CVFI: return "CVFI";
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

        // Format instruction as string
        std::string toString() const {
            switch (opcode) {
                // Instructions with no operands
            case OpCode::UNDEF:
            case OpCode::BREAK:
            case OpCode::LEAVE:
            case OpCode::POP:
            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::DIVI:
            case OpCode::DIVU:
            case OpCode::MODI:
            case OpCode::MODU:
            case OpCode::MULI:
            case OpCode::MULU:
            case OpCode::BAND:
            case OpCode::BOR:
            case OpCode::BXOR:
            case OpCode::BCOM:
            case OpCode::NEGI:
            case OpCode::NEGF:
            case OpCode::ADDF:
            case OpCode::SUBF:
            case OpCode::DIVF:
            case OpCode::MULF:
            case OpCode::CVIF:
            case OpCode::CVFI:
                return std::format("{}", to_string(opcode));

                // Instructions with one register
            case OpCode::PUSH:
                return std::format("{} r{}", to_string(opcode), operands.reg);

                // Instructions with one immediate value
            case OpCode::CONST:
            case OpCode::LOCAL:
            case OpCode::JUMP:
            case OpCode::ENTER:
                return std::format("{} {}", to_string(opcode), operands.value);

                // Instructions with reg + offset
            case OpCode::LOAD1:
            case OpCode::LOAD2:
            case OpCode::LOAD4:
            case OpCode::STORE1:
            case OpCode::STORE2:
            case OpCode::STORE4:
                return std::format("{} r{}, r{}+{}",
                    to_string(opcode),
                    operands.reg,
                    operands.sreg1,
                    operands.value);

                // Instructions with reg, reg (sign extension)
            case OpCode::SEX8:
            case OpCode::SEX16:
                return std::format("{} r{}, r{}",
                    to_string(opcode),
                    operands.reg,
                    operands.sreg1);

                // Conditional jumps
            case OpCode::EQ:
            case OpCode::NE:
            case OpCode::LTI:
            case OpCode::LEI:
            case OpCode::GTI:
            case OpCode::GEI:
            case OpCode::LTU:
            case OpCode::LEU:
            case OpCode::GTU:
            case OpCode::GEU:
            case OpCode::EQF:
            case OpCode::NEF:
            case OpCode::LTF:
            case OpCode::LEF:
            case OpCode::GTF:
            case OpCode::GEF:
                return std::format("{} r{}, r{}, {}",
                    to_string(opcode),
                    operands.reg,
                    operands.sreg1,
                    operands.value);

                // Call instruction (function or syscall)
            case OpCode::CALL:
                if (operands.value < 0) {
                    return std::format("{} syscall #{}",
                        to_string(opcode),
                        -operands.value);
                }
                else {
                    return std::format("{} {}",
                        to_string(opcode),
                        operands.value);
                }

                // Other instructions
            default:
                return std::format("{} r{}={} v={}",
                    to_string(opcode),
                    operands.reg,
                    operands.sreg1,
                    operands.value);
            }
        }
    };

    // VM instruction decoder
    class InstructionDecoder {
    public:
        InstructionDecoder(std::span<const std::byte> codeSegment)
            : m_codeSegment(codeSegment), m_programCounter(0) {
        }

        // Reset program counter
        void reset(size_t pc = 0) {
            m_programCounter = pc;
        }

        // Decode the next instruction
        std::expected<VMInstruction, VMError> decode() {
            // Bounds checking
            if (m_programCounter >= m_codeSegment.size()) {
                return std::unexpected(VMError::SegmentationFault);
            }

            // Read opcode
            OpCode opcode = static_cast<OpCode>(m_codeSegment[m_programCounter++]);

            // Initialize instruction
            VMInstruction inst;
            inst.opcode = opcode;
            inst.operands = {};

            // Decode operands based on opcode
            switch (opcode) {
                // Instructions with no operands
            case OpCode::UNDEF:
            case OpCode::BREAK:
            case OpCode::LEAVE:
            case OpCode::POP:
            case OpCode::NEGI:
            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::DIVI:
            case OpCode::DIVU:
            case OpCode::MODI:
            case OpCode::MODU:
            case OpCode::MULI:
            case OpCode::MULU:
            case OpCode::BAND:
            case OpCode::BOR:
            case OpCode::BXOR:
            case OpCode::BCOM:
            case OpCode::LSH:
            case OpCode::RSHI:
            case OpCode::RSHU:
            case OpCode::NEGF:
            case OpCode::ADDF:
            case OpCode::SUBF:
            case OpCode::DIVF:
            case OpCode::MULF:
            case OpCode::CVIF:
            case OpCode::CVFI:
                break;

                // Instructions with one operand (8-bit)
            case OpCode::IGNORE:
            case OpCode::ARG:
            case OpCode::PUSH:
                if (m_programCounter + 1 > m_codeSegment.size()) {
                    return std::unexpected(VMError::SegmentationFault);
                }
                inst.operands.reg = static_cast<uint8_t>(m_codeSegment[m_programCounter++]);
                break;

                // Instructions with one operand (32-bit)
            case OpCode::ENTER:
            case OpCode::CONST:
            case OpCode::LOCAL:
            case OpCode::JUMP:
            case OpCode::CALL:
                if (m_programCounter + 4 > m_codeSegment.size()) {
                    return std::unexpected(VMError::SegmentationFault);
                }
                std::memcpy(&inst.operands.value, &m_codeSegment[m_programCounter], 4);
                m_programCounter += 4;
                break;

                // Conditional jumps (register + value)
            case OpCode::EQ:
            case OpCode::NE:
            case OpCode::LTI:
            case OpCode::LEI:
            case OpCode::GTI:
            case OpCode::GEI:
            case OpCode::LTU:
            case OpCode::LEU:
            case OpCode::GTU:
            case OpCode::GEU:
            case OpCode::EQF:
            case OpCode::NEF:
            case OpCode::LTF:
            case OpCode::LEF:
            case OpCode::GTF:
            case OpCode::GEF:
                if (m_programCounter + 6 > m_codeSegment.size()) {
                    return std::unexpected(VMError::SegmentationFault);
                }
                inst.operands.reg = static_cast<uint8_t>(m_codeSegment[m_programCounter++]);
                inst.operands.sreg1 = static_cast<uint8_t>(m_codeSegment[m_programCounter++]);
                std::memcpy(&inst.operands.value, &m_codeSegment[m_programCounter], 4);
                m_programCounter += 4;
                break;

                // Load/store instructions (register + offset)
            case OpCode::LOAD1:
            case OpCode::LOAD2:
            case OpCode::LOAD4:
            case OpCode::STORE1:
            case OpCode::STORE2:
            case OpCode::STORE4:
                if (m_programCounter + 6 > m_codeSegment.size()) {
                    return std::unexpected(VMError::SegmentationFault);
                }
                inst.operands.reg = static_cast<uint8_t>(m_codeSegment[m_programCounter++]);
                inst.operands.sreg1 = static_cast<uint8_t>(m_codeSegment[m_programCounter++]);
                std::memcpy(&inst.operands.value, &m_codeSegment[m_programCounter], 4);
                m_programCounter += 4;
                break;

                // Block copy instruction (3 values)
            case OpCode::BLOCK_COPY:
                if (m_programCounter + 12 > m_codeSegment.size()) {
                    return std::unexpected(VMError::SegmentationFault);
                }
                // Read size
                std::memcpy(&inst.operands.value, &m_codeSegment[m_programCounter], 4);
                m_programCounter += 4;

                // Read source address (stored in reg)
                int32_t srcAddr;
                std::memcpy(&srcAddr, &m_codeSegment[m_programCounter], 4);
                inst.operands.reg = srcAddr; // Simplification
                m_programCounter += 4;

                // Read destination address (stored in sreg1)
                int32_t destAddr;
                std::memcpy(&destAddr, &m_codeSegment[m_programCounter], 4);
                inst.operands.sreg1 = destAddr; // Simplification
                m_programCounter += 4;
                break;

                // Sign extension instructions
            case OpCode::SEX8:
            case OpCode::SEX16:
                if (m_programCounter + 2 > m_codeSegment.size()) {
                    return std::unexpected(VMError::SegmentationFault);
                }
                inst.operands.reg = static_cast<uint8_t>(m_codeSegment[m_programCounter++]);
                inst.operands.sreg1 = static_cast<uint8_t>(m_codeSegment[m_programCounter++]);
                break;

            default:
                return std::unexpected(VMError::InvalidBytecode);
            }

            return inst;
        }

        // Get current position
        size_t getPosition() const { return m_programCounter; }

        // Set position
        void setPosition(size_t pos) { m_programCounter = pos; }

        // Disassemble a range of instructions
        std::vector<std::string> disassemble(
            size_t start,
            size_t count,
            const BytecodeParser* parser = nullptr) {

            std::vector<std::string> result;

            size_t savedPC = m_programCounter;
            m_programCounter = start;

            for (size_t i = 0; i < count; i++) {
                size_t currentPC = m_programCounter;

                // Try to get function name from parser
                std::string prefix;
                if (parser) {
                    auto funcResult = parser->findFunctionByOffset(currentPC);
                    if (funcResult) {
                        prefix = std::format("\n{}:", funcResult.value()->name);
                        result.push_back(prefix);
                    }
                }

                // Decode instruction
                auto instResult = decode();
                if (!instResult) {
                    result.push_back(std::format("{:08x}: <invalid>", currentPC));
                    break;
                }

                // Add to result
                result.push_back(std::format("{:08x}: {}", currentPC, instResult.value().toString()));

                // Break if end of code segment
                if (m_programCounter >= m_codeSegment.size()) {
                    break;
                }
            }

            // Restore position
            m_programCounter = savedPC;
            return result;
        }

    private:
        std::span<const std::byte> m_codeSegment;
        size_t m_programCounter;
    };

} // namespace tremor::vm
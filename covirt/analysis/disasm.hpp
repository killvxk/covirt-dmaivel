#pragma once

#include "basic_block.hpp"
#include "zasm/x86/mnemonic.hpp"

#include <utils/log.hpp>

#include <span>
#include <functional>
#include <variant>

#include <zasm/zasm.hpp>

namespace covirt {
    using zydis_reg = ZydisDecodedOperandReg_;
    using zydis_imm = ZydisDecodedOperandImm_;
    using zydis_mem = ZydisDecodedOperandMem_;

    struct zydis_operand {
        zydis_operand() {}
        zydis_operand(ZydisDecodedOperand &op);

        std::variant<std::monostate, zydis_reg, zydis_imm, zydis_mem> value;
        size_t size;

        std::optional<basic_block*> references_bb;
        std::optional<uintptr_t> references_rva;

        void assign(ZydisDecodedOperand &op);

        constexpr bool is_register() {  return std::holds_alternative<zydis_reg>(value); }
        constexpr bool is_immediate() { return std::holds_alternative<zydis_imm>(value); }
        constexpr bool is_memory() {    return std::holds_alternative<zydis_mem>(value); }
        constexpr bool is_empty() {     return std::holds_alternative<std::monostate>(value); }

        constexpr auto as_register() {  return std::get<zydis_reg>(value); }
        constexpr auto as_immediate() { return std::get<zydis_imm>(value); }
        constexpr auto as_memory() {    return std::get<zydis_mem>(value); }

        int register_index(bool use_index = false);
        
        constexpr int64_t immediate()
        {
            out::assertion(is_immediate(), "zydis_operand is not an immediate");
            return as_immediate().value.s;
        }
    };

    void disasm(std::span<const uint8_t> content, uint64_t base_address, std::function<void(uint64_t, ZydisDisassembledInstruction)> callback);

    constexpr bool is_jump(ZydisDisassembledInstruction &ins) { return ins.info.mnemonic >= zasm::x86::Mnemonic::Jb && ins.info.mnemonic <= zasm::x86::Mnemonic::Jz; }
    constexpr bool is_jcc(ZydisDisassembledInstruction &ins) { return is_jump(ins) && ins.info.mnemonic != zasm::x86::Mnemonic::Jmp; }
}

#pragma once

#include "generic_emitter.hpp"
#include "generic_vm.hpp"

#include <analysis/basic_block.hpp>
#include <analysis/disasm.hpp>

#include <functional>
#include <map>
#include <string>

namespace covirt {
    using fn_instruction_translator_t = std::function<bool(covirt::zydis_operand&, covirt::zydis_operand&)>;
    using dump_index_table_t = std::map<int, std::string>;
    using fn_dump_t = std::function<void(std::vector<uint8_t>&, dump_index_table_t&)>;

    struct lift_result {
        std::vector<uint8_t> bytes;
        dump_index_table_t dump_index_table;
    };

    class generic_lifter {
    protected:
        struct fill_in_data {
            // basic block that we need to jump to
            //
            basic_block *bb;

            // offset into the lifted bytecode that we need to write
            // this value into
            //
            size_t offset_write_into_lift;

            // sizeof basically
            // 
            size_t size;
        };

        std::vector<fill_in_data> fill_in_gaps;
    public:
        virtual std::map<ZydisMnemonic, fn_instruction_translator_t>& get_translation_table() = 0;
        virtual void vm_exit(uint16_t bytes_to_skip) = 0;
        virtual void native(uint8_t *ins_bytes, size_t length) = 0;
        virtual generic_emitter& get_emitter() = 0;

        auto get_fill_in_gaps() { return fill_in_gaps; }
    };

    lift_result lift(std::vector<covirt::subroutine> &routines, covirt::generic_lifter &lifter, covirt::generic_vm &vm);
}
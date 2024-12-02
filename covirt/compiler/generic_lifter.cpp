#include "generic_lifter.hpp"
#include "analysis/basic_block.hpp"

#include <utils/log.hpp>
#include <covirt_stub.h>

// clear??
//
static inline covirt::basic_block *get_bb_which_address_resides_in(covirt::subroutine &routine, uintptr_t addr)
{
    for (auto bb = routine.basic_blocks; bb != nullptr; bb = bb->next) {
        if (addr >= bb->start_va && addr < bb->end_va)
            return bb;
    }

    return nullptr;
}

covirt::lift_result covirt::lift(std::vector<covirt::subroutine> &routines, generic_lifter &lifter, generic_vm &vm)
{
    auto table = lifter.get_translation_table();
    dump_index_table_t dump_index_table;

    auto vm_entry_length = vm.get_vm_enter().get_length();

    for (auto & bb_routine : routines) {
        auto bb_length = bb_routine.length() + __covirt_vm_stub_length + __covirt_vm_stub_length;
        auto bb_unused_length = bb_length - vm_entry_length;

        bb_routine.offset_into_lift = lifter.get_emitter().get().size();

        for (auto bb = bb_routine.basic_blocks; bb != nullptr; bb = bb->next) {
            bb->offset_into_lift = lifter.get_emitter().get().size();

            int skip = 0;
            for (auto& [bytes, ins] : *bb) {
                auto fn_translate = table[ins.info.mnemonic];
                auto retaddr = bb_routine.start_va - __covirt_vm_stub_length + vm_entry_length;

                bool liftable_and_lifted = fn_translate != nullptr;

                dump_index_table[lifter.get_emitter().get_count()] = ins.text;

                if (liftable_and_lifted) {
                    zydis_operand dst(ins.operands[0]);
                    zydis_operand src(ins.operands[1]);

                    if (is_jump(ins)) {
                        dst.references_bb = get_bb_which_address_resides_in(bb_routine, dst.immediate() + ins.runtime_address + ins.info.length);
                        out::assertion(dst.references_bb.value() != nullptr, "attempted to jump out of the protected region");
                    }
                    else if (ins.info.mnemonic == ZYDIS_MNEMONIC_LEA && src.is_memory()) {
                        // assumes 'base' is rip, because if it isn't fn_translate will fail
                        //
                        // we want to translate the rva from:
                        // [rva relative to current rip] => [rva relative to retaddr]
                        //
                        // (old_addr+old_disp)−(new_addr)
                        // + ins.info.length so that we don't have to calc in the vm

                        dst.references_rva = (ins.runtime_address + src.as_memory().disp.value) - retaddr + ins.info.length;
                    }
                    else if (ins.info.mnemonic == ZYDIS_MNEMONIC_CALL) {
                        // same concept as before, we need an rva relative to the retaddr
                        //
                        dst.references_rva = ((ins.runtime_address + dst.immediate()) - retaddr + ins.info.length);
                    }

                    if (!fn_translate(dst, src))
                        liftable_and_lifted = false;
                }

                if (!liftable_and_lifted) {
                    lifter.native(bytes, ins.info.length);
                    out::warn("instruction '{}' has no defined vm handler, will execute natively", out::name(ins.text));
                }

                skip += ins.info.length;
            }
        }

        lifter.vm_exit(bb_unused_length);
    }

    // fill in jumps inside of the lifted bytecode
    //
    auto& bytecode = lifter.get_emitter().get();
    for (auto& fill_in : lifter.get_fill_in_gaps()) {
        size_t bb_offset = fill_in.bb->offset_into_lift;
        // std::memcpy(&bytecode[fill_in.offset_write_into_lift], &bb_offset, fill_in.size);
        *(uint16_t*)&bytecode[fill_in.offset_write_into_lift] = fill_in.bb->offset_into_lift;
    }

    out::info("generated {} total vm instructions", out::value(lifter.get_emitter().get_count()));

    // to-do: the copy here is sub-optimal, pass around as reference instead without crashing?
    //
    return covirt::lift_result{ lifter.get_emitter().get(), dump_index_table };
}
#include "basic_block.hpp"
#include "disasm.hpp"

#include <utils/log.hpp>

#include <queue>
#include <set>

covirt::subroutine covirt::decompose_bb(basic_block &bb)
{
    std::priority_queue<uintptr_t, std::vector<uintptr_t>, std::greater<uintptr_t>> visit;
    std::set<uintptr_t> addresses;

    for (auto& [bytes, ins] : bb)
        if (is_jump(ins)) {
            addresses.insert(zydis_operand(ins.operands[0]).immediate() + ins.runtime_address + ins.info.length);
            if (ins.info.mnemonic != zasm::x86::Mnemonic::Jmp)
                addresses.insert(ins.runtime_address + ins.info.length);
        }

    for (auto &a : addresses)
        visit.push(a);
    visit.push(bb.end_va);

    auto size = visit.size();

    covirt::subroutine result(bb);
    auto current = result.basic_blocks;

    // we need to clear because we copied 'bb', so the first
    // basic block contains every
    //
    current->clear();

    auto start_address = bb.start_va;
    for (int i = 0; i < size; i++) {
        auto end_address = visit.top(); 
        visit.pop();

        current->start_va = start_address;
        current->end_va = end_address;

        // to-do: find more effecient method
        //
        for (auto& [bytes, ins] : bb) {
            if (ins.runtime_address >= start_address && ins.runtime_address < end_address)
                current->push_back({bytes, ins});
        }

        current->next = new covirt::basic_block;
        current = current->next;

        start_address = end_address;

        // we emit one last basic block, which acts as the vm_exit block
        //
        if (visit.empty()) {
            current->start_va = start_address;
            current->end_va = end_address + 1;
        }
    }

    return result;
}
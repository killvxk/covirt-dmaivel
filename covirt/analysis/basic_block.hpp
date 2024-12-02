#pragma once

#include <Zydis/Zydis.h>
#include <vector>

namespace covirt {
    class basic_block : public std::vector<std::pair<uint8_t*, ZydisDisassembledInstruction>> {
    public:
        uintptr_t start_va;
        uintptr_t end_va;
        uint32_t offset_into_lift;

        basic_block *next = nullptr;
    };

    class subroutine {
    public:
        subroutine() { }
        subroutine(basic_block &bb) :
            start_va(bb.start_va), end_va(bb.end_va)
        {
            basic_blocks = new basic_block;
            basic_blocks[0] = bb;
        }

        uintptr_t start_va;
        uintptr_t end_va;

        auto length() const { return end_va - start_va; }

        uint32_t offset_into_lift = 0;
        basic_block *basic_blocks = nullptr;
    };

    subroutine decompose_bb(basic_block &bb);
}

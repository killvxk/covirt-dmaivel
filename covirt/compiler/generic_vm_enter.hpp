#pragma once

#include <zasm/zasm.hpp>

#include <memory>

namespace covirt {
    class generic_vm_enter {
    public:
        void set_call_offset(uintptr_t base_of_call, uintptr_t base_of_vm, uintptr_t offset_of_call)
        {
            call_offset = base_of_vm - (base_of_call + offset_of_call) - get_length();
        }

        void set_vm_bytecode_offset(uintptr_t offset_into_lift)
        {
            lift_offset = offset_into_lift;
        }

        virtual std::unique_ptr<uint8_t> get_bytes() = 0;
        virtual size_t get_length() = 0;

        // this is used to assemble everything that comes before `push, call`
        //
        virtual void assemble_effects(zasm::x86::Assembler &a) = 0;

        // if the vm_enter modified any registers before jumping to vm_entry,
        // this function should provide the instructions to revert such changes
        //
        virtual void revert_effects(zasm::x86::Assembler &a) = 0;
    protected:
        uintptr_t call_offset = 0;
        uintptr_t lift_offset = 0;
    };
}
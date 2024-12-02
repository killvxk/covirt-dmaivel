#pragma once 

#include "generic_vm_enter.hpp"

namespace covirt {
    class default_vm_enter : public generic_vm_enter {
    public:
        std::unique_ptr<uint8_t> get_bytes() override;
        size_t get_length() override;
        void assemble_effects(zasm::x86::Assembler &a) override;
        void revert_effects(zasm::x86::Assembler &a) override;
    private:
        static uint8_t vm_enter[];
    };
}
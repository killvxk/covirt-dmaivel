#pragma once 

#include "../generic_transform.hpp"

namespace covirt {
    class mba_pass final : public generic_transform_pass {
    public:
        void pass(zasm::Program &p, zasm::x86::Assembler &a);
    };
}
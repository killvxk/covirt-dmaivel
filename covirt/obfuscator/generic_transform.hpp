#pragma once

#include <zasm/zasm.hpp>

namespace covirt {
    class generic_transform_pass {
    public:
        virtual void pass(zasm::Program &p, zasm::x86::Assembler &a) = 0;
    };
}
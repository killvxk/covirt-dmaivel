#pragma once

#include "generic_vm_enter.hpp"

#include <utils/rand.hpp>
#include <obfuscator/generic_transform.hpp>

#include <functional>
#include <map>
#include <print>
#include <zasm/zasm.hpp>

namespace covirt {
    // to-do: make vm_handler_t return this
    //
    struct vm_handler_metadata {
        std::vector<zasm::x86::Reg> available_registers;

        // to-do: add more bloat here
        //
    };
    
    using fn_vm_handler_t = std::function<void(zasm::x86::Assembler &)>;
    
    // to-do: support opcodes beyond uint8_t?
    //
    class generic_vm {
    public:
        // first function that gets called; label creation
        //
        virtual void initialize(zasm::x86::Assembler &a) = 0;

        // middle function; output handlers
        // 
        virtual std::map<uint8_t, fn_vm_handler_t>& get_handlers() = 0;

        // last function that gets called; data setup
        // 
        virtual void finalize(zasm::x86::Assembler &a) = 0;

        // get the vm_enter emitter
        //
        virtual generic_vm_enter& get_vm_enter() = 0;

        // set size of code "section"
        //
        virtual void set_code_size(size_t size) = 0;

        // set size of stack "section"
        //
        virtual void set_stack_size(size_t size) = 0;

        // assemble
        //
        std::pair<std::vector<uint8_t>, std::size_t> assemble(std::vector<generic_transform_pass*> &transform_passes);
    };
}
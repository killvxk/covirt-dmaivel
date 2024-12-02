#include "default_vm_enter.hpp"
#include <cstring>
#include <memory>

// sub rsp, 0x200
// push <offset_into_lifted_bytes>
// call <vm_entry>
//
uint8_t covirt::default_vm_enter::vm_enter[] = { 0x48, 0x81, 0xEC, 0x00, 0x02, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00, 0x00 };

std::unique_ptr<uint8_t> covirt::default_vm_enter::get_bytes()
{
    auto result_vm_enter = std::make_unique<uint8_t>(get_length());
    
    *(uint32_t*)&vm_enter[8] = lift_offset;
    *(uint32_t*)&vm_enter[13] = call_offset;

    std::memcpy(result_vm_enter.get(), vm_enter, get_length());
    return std::move(result_vm_enter);
}

size_t covirt::default_vm_enter::get_length()
{
    return sizeof(vm_enter);
}

void covirt::default_vm_enter::assemble_effects(zasm::x86::Assembler &a)
{
    a.sub(zasm::x86::rsp, 0x200);
}

void covirt::default_vm_enter::revert_effects(zasm::x86::Assembler &a)
{
    a.add(zasm::x86::rsp, 0x200);
}
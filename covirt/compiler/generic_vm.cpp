#include "generic_vm.hpp"
#include <utils/log.hpp>

constexpr std::size_t page_size = 4096;
#define PAGE_ROUND_DOWN(x) (x & (~(page_size-1)))
#define PAGE_ROUND_UP(x) ((x + page_size-1) & (~(page_size-1))) 

std::pair<std::vector<uint8_t>, std::size_t> covirt::generic_vm::assemble(std::vector<generic_transform_pass*> &passes)
{
    zasm::Program program(zasm::MachineMode::AMD64);
    zasm::x86::Assembler assembler(program);
    zasm::Serializer serializer{};

    initialize(assembler);
    for (auto && [opcode, handler] : get_handlers())
        handler(assembler);
    finalize(assembler);

    for (auto &apply_transform : passes)
        apply_transform->pass(program, assembler);

    auto res = serializer.serialize(program, 0);
    out::assertion(res == zasm::ErrorCode::None, "failed to serialize vm: {}:{}", res.getErrorName(), res.getErrorMessage());

    size_t size = 0;
    for (int i = 0; i < serializer.getSectionCount(); i++)
        size += serializer.getSectionInfo(i)->virtualSize;

    std::vector<uint8_t> bytes;
    auto v0 = std::vector<uint8_t>(serializer.getCode() + serializer.getSectionInfo(0)->offset, serializer.getCode() + serializer.getSectionInfo(0)->offset + serializer.getSectionInfo(0)->physicalSize);
    auto v1 = std::vector<uint8_t>(serializer.getCode() + serializer.getSectionInfo(1)->offset, serializer.getCode() + serializer.getSectionInfo(1)->offset + serializer.getSectionInfo(1)->physicalSize);
    bytes.insert(bytes.end(), v0.begin(), v0.end());

    size_t data_start = PAGE_ROUND_UP(bytes.size());
    for (auto i = bytes.size(); i < data_start; i++)
        bytes.push_back(covirt::rand<uint8_t>());
    bytes.insert(bytes.end(), v1.begin(), v1.end());
    
    return { bytes, data_start };
}
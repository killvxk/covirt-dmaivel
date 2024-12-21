#include <analysis/binary.hpp>
#include <obfuscator/passes/smc_pass.hpp>
#include <obfuscator/passes/mba_pass.hpp>
#include <utils/log.hpp>
#include <vm/v0.hpp>

#include <utils/argparse.hpp>

#include <filesystem>
#include "version.h"

int main(int argc, char **argv)
{
    int stack_size = 0;
    int code_size = 0;

    argparse::ArgumentParser program("covirt", COVIRT_VERSION);
    program.add_argument("file_input").help("path to input binary to virtualize").metavar("INPUT_PATH");
    program.add_argument("-o", "--output")
           .help("specify the output file [default: INPUT_PATH.covirt]")
           .metavar("OUTPUT_PATH")
           .nargs(1);
    program.add_argument("-vcode", "--vm_code_size")
           .default_value(int(2048))
           .help("specify the maximum allowed total lifted bytes")
           .metavar("MAX")
           .nargs(1)
           .store_into(code_size);
    program.add_argument("-vstack", "--vm_stack_size")
           .default_value(int(2048))
           .help("specify the size of the virtual stack")
           .metavar("SIZE")
           .nargs(1)
           .store_into(stack_size);
    program.add_argument("-no_smc", "--no_self_modifying_code")
           .default_value(false)
           .implicit_value(true)
           .help("disable smc pass");
    program.add_argument("-no_mba", "--no_mixed_boolean_arith")
           .default_value(false)
           .implicit_value(true)
           .help("disable mba pass");
    program.add_argument("-d", "--show_dump_table")
           .default_value(false)
           .implicit_value(true)
           .help("show disassembly of the vm instructions");
    program.add_description("Code virtualizer for x86-64 ELF & PE binaries");

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& err) {
        out::fail("{}\n\n{}", err.what(), program.help().str());
        return 0;
    }

    auto input_file_path = program.get("file_input");

    if (!std::filesystem::exists(input_file_path) || !std::filesystem::is_regular_file(input_file_path)) {
        std::println("{} no such file exists, or is a directory: '{}'", out::bad("fatal error:"), input_file_path);
        return 0;
    }

    covirt::binary file(input_file_path);
    try { file.set_out_path(program.get("-o")); } catch (...) { }

    covirt::vm::v0_vm x;
    x.set_code_size(uint32_t(code_size));
    x.set_stack_size(uint32_t(stack_size));

    std::vector<covirt::generic_transform_pass*> passes;

    if (!program.get<bool>("-no_smc")) passes.push_back(new covirt::smc_pass());
    if (!program.get<bool>("-no_mba")) passes.push_back(new covirt::mba_pass());

    if (!passes.empty()) out::info("obfuscating virtual machine...");
    else out::warn("assembling vm without any obfuscation passes");

    auto [bytes, data_start] = x.assemble(passes);
    file.add_section(".covirt0", bytes, true, true);

    std::vector<covirt::basic_block> basic_blocks;
    for (auto& section : file.sections()) {
        if (file.is_section_executable(section)) {
            auto content = section.content();
            auto runtime_address = file.imagebase() + section.virtual_address();
            auto bb = covirt::basic_block{};
            
            covirt::disasm(content, runtime_address, [&](uint64_t address, ZydisDisassembledInstruction ins) {
                auto offset = address - runtime_address; 

                if (std::memcmp(&content[0] + offset - 16, __covirt_vm_start_bytes, 16) == 0)
                    bb.start_va = address;
                if (std::memcmp(&content[0] + offset, __covirt_vm_end_bytes, 16) == 0)
                    bb.end_va = address;

                if (bb.end_va) {
                    out::assertion(bb.start_va, "binary appears to be missing marker '__covirt_vm_start()'");
                    basic_blocks.push_back(bb);
                    bb = {};
                }

                if (bb.start_va)
                    bb.push_back({(uint8_t*)content.data() + offset, ins});
            });

            out::assertion(!(bb.start_va && !bb.end_va), "binary appears to be missing marker '__covirt_vm_end()'");
        }
    }

    out::assertion(!basic_blocks.empty(), "found no code markers in binary");

    std::vector<covirt::subroutine> routines;
    for (auto& bb : basic_blocks)
        routines.push_back(decompose_bb(bb));
    
    size_t count = 0;
    for (auto& r : routines)
        for (auto bb = r.basic_blocks; bb != nullptr; bb = bb->next)
            count++;

    out::info("found {} regions which decomposed into {} total basic blocks", out::value(basic_blocks.size()), out::value(count));

    covirt::vm::v0_lifter lifter;
    auto lifted = lift(routines, lifter, x);

    out::assertion(lifted.bytes.size() < code_size, "ran out of code space, try using '-vcode {}'", lifted.bytes.size() + 1);

    if (program.get<bool>("-d"))
        covirt::vm::debug::dump_v0(lifted);

    file.write_vm_entries(routines, x.get_vm_enter());
    file.write_vm_bytecode(lifted.bytes, bytes, data_start, code_size);

    out::ok("virtualization complete");
}

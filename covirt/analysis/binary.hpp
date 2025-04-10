#pragma once

#include <LIEF/Abstract/Binary.hpp>
#include <LIEF/LIEF.hpp>
#include <LIEF/ELF/Binary.hpp>
#include <LIEF/ELF/Section.hpp>
#include <LIEF/PE/Binary.hpp>
#include <LIEF/PE/Section.hpp>

#include <memory>
#include <variant>
#include <print>

#include <covirt_stub.h>

#include "basic_block.hpp"

#include <utils/rand.hpp>
#include <compiler/generic_vm_enter.hpp>

/*
 * stupid lief wrapper to look nice and abstraction
 */

using lief_elf_flags_t = LIEF::ELF::Section::FLAGS;
using lief_pe_flags_t = LIEF::PE::Section::CHARACTERISTICS;
using lief_sections_iterator_t = LIEF::Binary::it_sections;
using lief_section = LIEF::Section;

namespace covirt {
    class binary {
    public:
        std::string suffix = ".covirt";

        binary(const std::string &file);

        void set_out_path(const std::string &out)
        {
            out_path = out;
        }

        void add_section(const std::string &string, std::vector<uint8_t> content, bool ex, bool wr);
        lief_sections_iterator_t sections();
        bool is_section_executable(lief_section &section);
        uint64_t imagebase();
        lief_section *get_section(const std::string &name);
        lief_section *get_section(uint64_t address);
        void update();
        void write_vm_entries(std::vector<covirt::subroutine> &routines, covirt::generic_vm_enter &vm_enter);
        void write_vm_bytecode(std::vector<uint8_t> &lifted_bytes, std::vector<uint8_t> &vm_section_bytes, size_t data_start, size_t vcode_size);
        
    private:
        std::string out_path;

        // generic can't go out of scope, or else silly seg faults
        //
        std::unique_ptr<LIEF::Binary> generic;
        std::variant<LIEF::ELF::Binary*, LIEF::PE::Binary*> specific;

        LIEF::ELF::Section* as_elf(LIEF::Section &section) { return dynamic_cast<LIEF::ELF::Section*>(&section); }
        LIEF::PE::Section* as_pe(LIEF::Section &section) { return dynamic_cast<LIEF::PE::Section*>(&section); }
    };
}
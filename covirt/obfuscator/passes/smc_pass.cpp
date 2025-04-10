#include "smc_pass.hpp"

#include <utils/log.hpp>
#include <utils/rand.hpp>
#include <utils/indicators.hpp>

using namespace indicators;

void covirt::smc_pass::pass(zasm::Program &p, zasm::x86::Assembler &a)
{
    uintptr_t start_addr = uintptr_t(p.getHead());
    uintptr_t end_addr = uintptr_t(p.getTail());

    ProgressBar bar{
        option::BarWidth{50},
        option::Start{"["},
        option::Fill{"="},
        option::Lead{">"},
        option::Remainder{" "},
        option::End{"]"},
        option::PrefixText{std::format("[{}] [{}] {}: ", out::get_timestamp(), out::yellow("wait"), "smc_pass")},
        option::ForegroundColor{Color::yellow},
        option::ShowElapsedTime{true},
        option::ShowRemainingTime{true},
        option::FontStyles{std::vector<FontStyle>{FontStyle::bold}}
    };

    size_t total_count = p.size();
    size_t curnt_count = 0;

    size_t n_total = 0;
    size_t n_trans = 0;

    zasm::Program mini_program(zasm::MachineMode::AMD64);
    zasm::x86::Assembler mini_assembler(mini_program);
    zasm::Serializer serializer{};
    
    auto x = p.getHead();
    for (auto node = p.getHead(); node != p.getTail(); node = node->getNext()) {
        node->visit([&](auto &&x) {
            bar.set_progress(size_t(float(float(curnt_count++) / total_count) * 100));

            if constexpr (!std::is_same_v<std::decay_t<decltype(x)>, zasm::Instruction>)
                return;

            n_total++;

            zasm::Instruction &instruction = node->get<zasm::Instruction>();

            // this isn't ideal, but if pop becomes smc'd and we apply mba to it, we end up
            // modifying some previously popped into registers, which messes it all up
            //
            // to-do: add ability to mark sections of code as to keep safe from obfuscation
            //        covirt::no_transform_start();
            //        covirt::no_transform_end(); 
            // 
            if (instruction.getMnemonic() == zasm::x86::Mnemonic::Pop)
                return;
            
            // we need these to stick around because they get overwritten when we do native execution
            //
            if (instruction.getMnemonic() == zasm::x86::Mnemonic::Nop)
                return;

            mini_program.clear();
            mini_assembler.emit(instruction);
                
            auto res = serializer.serialize(mini_program, 0);
            if (res != zasm::ErrorCode::None)
                return;

            auto code = serializer.getCode();
            auto length = serializer.getCodeSize();

            if (length != 1 && length != 2 && length != 4)
                return;

            a.setCursor(node);

            a.pushfq();

            auto lap = a.createLabel();
            switch (length) {
            case 1: a.mov(zasm::x86::byte_ptr(zasm::x86::rip, lap), zasm::Imm8(code[0])); break;
            case 2: a.mov(zasm::x86::word_ptr(zasm::x86::rip, lap), zasm::Imm16(*(uint16_t*)code)); break;
            case 4: a.mov(zasm::x86::dword_ptr(zasm::x86::rip, lap), zasm::Imm32(*(uint32_t*)code)); break;
            }

            a.popfq();

            a.bind(lap);
            for (int i = 0; i < length; i++) {
                auto r = covirt::rand<uint8_t>();
                a.embed(&r, 1);
            }

            auto end = a.getCursor();
            p.destroy(node);
            node = end;

            n_trans++;
        });
    }

    out::clear();
    out::ok("smc_pass: transformed {} ({:.1f}%) instructions", out::value(n_trans), float(n_trans) / n_total * 100.f);
}

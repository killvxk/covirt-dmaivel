#include "mba_pass.hpp"

#include <obfuscator/expression.hpp>
#include <utils/log.hpp>
#include <utils/indicators.hpp>

using namespace indicators;

expression transform_by_bitsize(zasm::Imm &imm, int depth = 1)
{
    auto bit_size = zasm::getBitSize(imm.getBitSize());
    switch (bit_size) {
    case 8: return expression::transform_constant(imm.value<int8_t>(), depth);
    case 16: return expression::transform_constant(imm.value<int16_t>(), depth);
    case 32: return expression::transform_constant(imm.value<int32_t>(), depth);
    case 64: out::assertion(false, "can't transform a 64-bit value"); return {};
    }
    out::assertion(false, "can't transform an unknown bit-sized value");
    return {};
}

void covirt::mba_pass::pass(zasm::Program &p, zasm::x86::Assembler &a)
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
        option::PrefixText{std::format("[{}] [{}] {}: ", out::get_timestamp(), out::yellow("wait"), "mba_pass")},
        option::ForegroundColor{Color::yellow},
        option::ShowElapsedTime{true},
        option::ShowRemainingTime{true},
        option::FontStyles{std::vector<FontStyle>{FontStyle::bold}}
    };

    size_t total_count = p.size();
    size_t curnt_count = 0;

    size_t n_total = 0;
    size_t n_trans = 0;
    size_t n_new_total = 0;

    auto x = p.getHead();
    for (auto node = p.getHead(); node != p.getTail(); node = node->getNext()) {
        node->visit([&](auto &&x) {
            bar.set_progress(size_t(float(float(curnt_count++) / total_count) * 100));

            if constexpr (!std::is_same_v<std::decay_t<decltype(x)>, zasm::Instruction>)
                return;

            n_total++;

            zasm::Instruction &instruction = node->get<zasm::Instruction>();
            expression exp{};

            if (instruction.getOperandCount() != 2)
                return;

            auto op0 = instruction.getOperand(0);
            auto op1 = instruction.getOperand(1);
            auto mnemonic = instruction.getMnemonic().value();

            switch (mnemonic) {
            case zasm::x86::Mnemonic::Mov: if (!op1.holds<zasm::Imm>()) return; exp = transform_by_bitsize(op1.get<zasm::Imm>(), 6); break;
            case zasm::x86::Mnemonic::Add: exp = A + B; break;
            case zasm::x86::Mnemonic::Sub: exp = A - B; break;
            case zasm::x86::Mnemonic::Xor: exp = A ^ B; break;
            case zasm::x86::Mnemonic::And: exp = A & B; break;
            case zasm::x86::Mnemonic::Or: exp = A | B; break;
            case zasm::x86::Mnemonic::Not: exp = ~A; break;
            case zasm::x86::Mnemonic::Neg: exp = -A; break;
            default:
                return;
            };

            // bc we modify rsp in vm op 'call', this will mess up the registers we were
            // supposed to be reserving/restoring
            //
            // to-do: refer to to-do in smc_pass about protecting sections of code from obfuscation
            // or simply define what registers are available at what time? (i like this idea more)
            //
            if (op0.holds<zasm::Reg>()) {
                if (op0.get<zasm::Reg>().as<zasm::x86::Gp>() == zasm::x86::rsp)
                    return;
            }

            if (mnemonic != zasm::x86::Mnemonic::Mov) {
                // to-do: investigate why combining `transform_constant` with variables
                // results in such stupid undefined behavior, only *half* of the time...
                //
                // i think this issue has some relation to the issue described with preserving
                // rsp and certain registers being modified after push/pop?
                //
                std::vector<std::pair<expression, expression>> mba_transformations = {
                    { (A^B), (A|B)-(A&B) },
                    { (A+B), (A&B)+(A|B) },
                    { (A-B), (A^-B)+(A&-B)+(A&-B) },
                    { (A&B), (A+B)-(A|B) },
                    { (A|B), (A+B)+(~A|~B)+/*expression::transform_constant<uint8_t>*/(1) },
                    { (~A),  (-A+/*expression::transform_constant<uint8_t>*/(-1)) },
                    { (-A),  (~A|A)-A+/*expression::transform_constant<uint8_t>*/(1) }
                };

                for (int i = 0; i < 3; i++)
                    for (auto& [match, transform] : mba_transformations)
                        exp = exp.transform(match, transform);
            }

            std::vector<zasm::x86::Gp> avail = { zasm::x86::r15, zasm::x86::r14, zasm::x86::r13, zasm::x86::r12, zasm::x86::r8, zasm::x86::rdi, zasm::x86::rbx };
            if (instruction.getOperandCount() == 2) {
                a.setCursor(node);

                auto size = zasm::getBitSize(std::max(op0.getBitSize(zasm::MachineMode::AMD64), op1.getBitSize(zasm::MachineMode::AMD64)));
                // for (auto &x : avail)
                //     a.xor_(x, x);
                auto [end, count] = exp.assemble_steps(a, op0, op1, avail, size);

                n_trans++;
                n_new_total += count;

                p.destroy(node);

                //node = end->getPrev();
                node = end;
            }
        });
    }

    out::clear();
    out::ok("mba_pass: transformed {} ({:.1f}%) instructions into {}", out::value(n_trans), float(n_trans) / n_total * 100.f, out::value(n_new_total));
}
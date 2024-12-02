#include <compiler/generic_lifter.hpp>
#include <compiler/generic_emitter.hpp>
#include <compiler/generic_vm.hpp>
#include <compiler/default_vm_enter.hpp>

#include <optional>

#include <zasm/zasm.hpp>

namespace covirt::vm {
    enum class v0_op : uint8_t {
        vm_enter, vm_exit, push_imm, push_reg, pop, read, write, add, sub, bxor, band, bor, cmp, jmp, jz, jnz, jb, jnb, jbe, jnbe, jl, jle, jnl, jnle, call, lea, execute_native
    };

    class v0_emitter : public generic_emitter {
    public:
    #define LAZY_EMIT(x) \
        template <typename S, typename... Tx> \
        auto& x(S encode_size, Tx&&... args) \
        { \
            emplace(opcode(v0_op::x, int(encode_size)), std::forward<Tx>(args)...); \
            return *this; \
        }

        LAZY_EMIT(push_reg);
        LAZY_EMIT(push_imm);
        LAZY_EMIT(pop);
        LAZY_EMIT(read);
        LAZY_EMIT(write);
        LAZY_EMIT(add);
        LAZY_EMIT(sub);
        LAZY_EMIT(bxor);
        LAZY_EMIT(band);
        LAZY_EMIT(bor);
        LAZY_EMIT(cmp);
        LAZY_EMIT(lea);
        LAZY_EMIT(call);
    #undef LAZY_EMIT
    };

    class v0_lifter : public generic_lifter {
    public:
        v0_emitter e;

        std::map<ZydisMnemonic, fn_instruction_translator_t>& get_translation_table() override
        {
            return lift_impl;
        }

        generic_emitter& get_emitter() override 
        {
            return e;
        }

        void vm_exit(uint16_t bytes_to_skip) override
        {
            e >> e.opcode(v0_op::vm_exit, 1) >> uint16_t(bytes_to_skip);
        }

        void native(uint8_t *ins_bytes, size_t length) override
        {
            e >> e.opcode(v0_op::execute_native, 1) >> uint8_t(length);
            for (int i = 0; i < length; i++) e >> ins_bytes[i];
        } 

    private:
        static constexpr uint8_t tmp_reg_idx = 14;

        void push_address(covirt::zydis_operand &operand);
        void push_operand(covirt::zydis_operand &operand, std::optional<int> override_size = {});
        void pop_operand(covirt::zydis_operand &operand, std::optional<int> override_size = {}, std::optional<covirt::zydis_operand> src = {});

        std::map<ZydisMnemonic, fn_instruction_translator_t> lift_impl = {
            {
                ZYDIS_MNEMONIC_MOV, [&](covirt::zydis_operand &dst, covirt::zydis_operand &src) {
                    push_operand(src, dst.size);
                    pop_operand(dst, {}, src);
                    return true;
                }
            },

#define LAZY_ARITH(mnemonic, op) \
            { \
                mnemonic, [&](covirt::zydis_operand &dst, covirt::zydis_operand &src) { \
                    push_operand(dst); \
                    push_operand(src, dst.size); \
                    e.op(dst.size); \
                    pop_operand(dst); \
                    return true; \
                } \
            }

            LAZY_ARITH(ZYDIS_MNEMONIC_ADD, add),
            LAZY_ARITH(ZYDIS_MNEMONIC_SUB, sub),
            LAZY_ARITH(ZYDIS_MNEMONIC_XOR, bxor),
            LAZY_ARITH(ZYDIS_MNEMONIC_AND, band),
            LAZY_ARITH(ZYDIS_MNEMONIC_OR, bor),
#undef LAZY_ARITH

            {
                ZYDIS_MNEMONIC_CMP, [&](covirt::zydis_operand &dst, covirt::zydis_operand &src) {
                    push_operand(dst);
                    push_operand(src, dst.size);
                    e.cmp(dst.size);
                    return true; \
                }
            },

#define LAZY_JUMP(mnemonic, op) \
            { \
                mnemonic, [&](covirt::zydis_operand &dst, covirt::zydis_operand &src) { \
                    e >> e.opcode(op, 1) >> uint16_t(0); \
                    fill_in_gaps.push_back({ dst.references_bb.value(), e.get().size() - sizeof(uint16_t), sizeof(uint16_t) }); \
                    return true; \
                } \
            }

            LAZY_JUMP(ZYDIS_MNEMONIC_JMP, v0_op::jmp),
            LAZY_JUMP(ZYDIS_MNEMONIC_JZ, v0_op::jz),
            LAZY_JUMP(ZYDIS_MNEMONIC_JNZ, v0_op::jnz),
            LAZY_JUMP(ZYDIS_MNEMONIC_JB, v0_op::jb),
            LAZY_JUMP(ZYDIS_MNEMONIC_JNB, v0_op::jnb),
            LAZY_JUMP(ZYDIS_MNEMONIC_JBE, v0_op::jbe),
            LAZY_JUMP(ZYDIS_MNEMONIC_JNBE, v0_op::jnbe),
            LAZY_JUMP(ZYDIS_MNEMONIC_JL, v0_op::jl),
            LAZY_JUMP(ZYDIS_MNEMONIC_JLE, v0_op::jle),
            LAZY_JUMP(ZYDIS_MNEMONIC_JNL, v0_op::jnl),
            LAZY_JUMP(ZYDIS_MNEMONIC_JNLE, v0_op::jnle),
#undef LAZY_JUMP

            {
                ZYDIS_MNEMONIC_LEA, [&](covirt::zydis_operand &dst, covirt::zydis_operand &src) {
                    if (src.as_memory().base != ZYDIS_REGISTER_RIP)
                        return false;

                    e.lea(dst.size, int32_t(dst.references_rva.value()));
                    pop_operand(dst);
                    return true;
                }
            },
            {
                ZYDIS_MNEMONIC_CALL, [&](covirt::zydis_operand &dst, covirt::zydis_operand &src) {
                    e.call(1, int32_t(dst.references_rva.value()));
                    return true;
                }
            },
        };
    };

    class v0_vm : public generic_vm {
    public:
        void initialize(zasm::x86::Assembler &a) override;
        void finalize(zasm::x86::Assembler& a) override;

        std::map<uint8_t, fn_vm_handler_t>& get_handlers() override { return vm_impl; }
        generic_vm_enter& get_vm_enter() override { return vm_enter_emitter; }

        void set_code_size(size_t size) override { code_size = size; };
        void set_stack_size(size_t size) override { stack_size = size; };

    private:
        zasm::x86::Gp64 vip, vsp;

        size_t code_size = 0;
        size_t stack_size = 0;

        std::map<std::string, zasm::Label> global_labels = {
            {"saved_rsp", {}},
            {"_vsp", {}},
            {"_vip", {}},
            {"vstack", {}},
            {"vcode", {}},
            {"vtable", {}},
            {"retaddr", {}},
            {"venter", {}},
            {"vexit", {}},
            {"vpush_imm", {}},
            {"vpush_reg", {}},
            {"vpop", {}},
            {"vread", {}},
            {"vwrite", {}},
            {"vadd", {}},
            {"vsub", {}},
            {"vxor", {}},
            {"vand", {}},
            {"vor", {}},
            {"vcmp", {}},
            {"vjmp", {}},
            {"vjz", {}},
            {"vjnz", {}},
            {"vjb", {}},
            {"vjnb", {}},
            {"vjbe", {}},
            {"vjnbe", {}},
            {"vjl", {}},
            {"vjle", {}},
            {"vjnl", {}},
            {"vjnle", {}},
            {"vcall", {}},
            {"vlea", {}},
            {"vexenative", {}}
        };

        default_vm_enter vm_enter_emitter;

        // to-do: look into `embedLabelRel` instead of runtime creation? idk
        //
        template <typename T, typename... Tx>
        void create_jump_table_once(zasm::x86::Assembler& a, T table, Tx&&... entries)
        {
            auto pass = a.createLabel();

            a.cmp(zasm::x86::qword_ptr(zasm::x86::rip, table), 0);
            a.jnz(pass);

            a.lea(zasm::x86::r9, zasm::x86::qword_ptr(zasm::x86::rip, table));
            (([&](auto &&label) { 
                a.lea(zasm::x86::r10, zasm::x86::qword_ptr(zasm::x86::rip, label));
                a.mov(zasm::x86::qword_ptr(zasm::x86::r9), zasm::x86::r10);
                a.add(zasm::x86::r9, 8);
            })(entries), ...);

            a.bind(pass);
        }

        void vm_next_instruction(zasm::x86::Assembler& a, std::optional<zasm::Label> label = {});
        void jump_using_table(zasm::x86::Assembler& a, zasm::Label &paths);
        void get_size_from_opcode(zasm::x86::Assembler& a, zasm::Label &start);
        void get_vreg_address(zasm::x86::Assembler& a);
        void get_vreg_value(zasm::x86::Assembler& a);

        std::map<uint8_t, fn_vm_handler_t> vm_impl = {
            {
                uint8_t(v0_op::vm_enter), [&](zasm::x86::Assembler& a) {
                    a.bind(global_labels["venter"]);

                    a.pop(zasm::x86::r11);
                    a.mov(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["retaddr"]), zasm::x86::r11);
                    a.pop(zasm::x86::r11);
                    a.mov(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["saved_rsp"]), zasm::x86::rsp);

                    // to-do: save r9, r10 before?
                    // to-do: not ideal, this is repetitive

                    create_jump_table_once(a, global_labels["vtable"], global_labels["venter"], global_labels["vexit"], 
                            global_labels["vpush_imm"], global_labels["vpush_reg"], global_labels["vpop"], global_labels["vread"], global_labels["vwrite"], 
                            global_labels["vadd"], global_labels["vsub"], global_labels["vxor"], global_labels["vand"], global_labels["vor"], 
                            global_labels["vcmp"], global_labels["vjmp"], global_labels["vjz"], global_labels["vjnz"], global_labels["vjb"], 
                            global_labels["vjnb"], global_labels["vjbe"], global_labels["vjnbe"], global_labels["vjl"], global_labels["vjle"],
                            global_labels["vjnl"], global_labels["vjnle"], global_labels["vcall"], global_labels["vlea"], global_labels["vexenative"]);

                    a.push(zasm::x86::r15); // -8
                    a.push(zasm::x86::r14); // -16
                    a.push(zasm::x86::r13); // -24
                    a.push(zasm::x86::r12); // -32
                    a.push(zasm::x86::r11); // -40
                    a.push(zasm::x86::r10); // -48
                    a.push(zasm::x86::r9); // -56
                    a.push(zasm::x86::r8); // -64
                    a.push(zasm::x86::rdi); // -72
                    a.push(zasm::x86::rsi); // -80
                    a.push(zasm::x86::rbp); // -88
                    a.push(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["saved_rsp"])); // -96
                    a.push(zasm::x86::rbx); // -104
                    a.push(zasm::x86::rdx); // -112
                    a.push(zasm::x86::rcx); // -120
                    a.push(zasm::x86::rax); // -128
                    a.pushfq(); // -136

                    a.lea(vsp, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vstack"]));
                    a.add(vsp, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["_vsp"]));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::r11);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::vm_exit), [&](zasm::x86::Assembler& a) {
                    a.bind(global_labels["vexit"]);

                    a.inc(vip);
                    a.mov(zasm::x86::r10w, zasm::x86::word_ptr(vip));
                    a.add(zasm::x86::word_ptr(zasm::x86::rip, global_labels["retaddr"]), zasm::x86::r10w);

                    a.popfq();
                    a.pop(zasm::x86::rax);
                    a.pop(zasm::x86::rcx);
                    a.pop(zasm::x86::rdx);
                    a.pop(zasm::x86::rbx);
                    a.pop(zasm::x86::rbp); // rsp
                    a.pop(zasm::x86::rbp);
                    a.pop(zasm::x86::rsi);
                    a.pop(zasm::x86::rdi);
                    a.pop(zasm::x86::r8);
                    a.pop(zasm::x86::r9);
                    a.pop(zasm::x86::r10);
                    a.pop(zasm::x86::r11);
                    a.pop(zasm::x86::r12);
                    a.pop(zasm::x86::r13);
                    a.pop(zasm::x86::r14);
                    a.pop(zasm::x86::r15);

                    vm_enter_emitter.revert_effects(a);

                    a.jmp(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["retaddr"]));
                }
            },
            {
                uint8_t(v0_op::push_imm), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto vpushz = [&]<typename T>(int size, zasm::x86::Gp v, T ptr) {
                        a.bind(labels[1 + size]);
                        a.sub(vsp, 1 << size);
                        a.mov(v, ptr(std::forward<zasm::x86::Gp64>(vip)));
                        a.mov(ptr(std::forward<zasm::x86::Gp64>(vsp)), v);
                        a.add(vip, 1 << size);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vpush_imm"]);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    vpushz(0b00, zasm::x86::cl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    vpushz(0b01, zasm::x86::cx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    vpushz(0b10, zasm::x86::ecx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    vpushz(0b11, zasm::x86::rcx, zasm::x86::qword_ptr<zasm::x86::Gp64>);
                    
                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::push_reg), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto vpushz = [&]<typename T>(int size, zasm::x86::Gp v, T ptr) {
                        a.bind(labels[1 + size]);
                        a.sub(vsp, 1 << size);
                        a.mov(ptr(std::forward<zasm::x86::Gp64>(vsp)), v);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vpush_reg"]);
                    get_vreg_value(a);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    vpushz(0b00, zasm::x86::dl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    vpushz(0b01, zasm::x86::dx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    vpushz(0b10, zasm::x86::edx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    vpushz(0b11, zasm::x86::rdx, zasm::x86::qword_ptr<zasm::x86::Gp64>);
                    
                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::pop), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto vpopz = [&]<typename T>(int size, zasm::x86::Gp v, T ptr) {
                        a.bind(labels[1 + size]);
                        a.mov(v, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.add(vsp, 1 << size);
                        a.mov(ptr(const_cast<zasm::x86::Gp64&&>(zasm::x86::rdx)), v);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vpop"]);
                    get_vreg_address(a);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    vpopz(0b00, zasm::x86::cl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    vpopz(0b01, zasm::x86::cx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    vpopz(0b10, zasm::x86::ecx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    vpopz(0b11, zasm::x86::rcx, zasm::x86::qword_ptr<zasm::x86::Gp64>);
                    
                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::read), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto vreadz = [&]<typename T>(int size, zasm::x86::Gp v, T ptr) {
                        a.bind(labels[1 + size]);
                        a.mov(zasm::x86::rdx, zasm::x86::qword_ptr(vsp));
                        a.add(vsp, 8 - (1 << size));
                        a.mov(v, ptr(const_cast<zasm::x86::Gp64&&>(zasm::x86::rdx)));
                        a.mov(ptr(std::forward<zasm::x86::Gp64>(vsp)), v);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vread"]);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    vreadz(0b00, zasm::x86::cl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    vreadz(0b01, zasm::x86::cx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    vreadz(0b10, zasm::x86::ecx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    vreadz(0b11, zasm::x86::rcx, zasm::x86::qword_ptr<zasm::x86::Gp64>);

                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::write), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto vwritez = [&]<typename T>(int size, zasm::x86::Gp v, T ptr) {
                        a.bind(labels[1 + size]);
                        a.mov(zasm::x86::r10, zasm::x86::qword_ptr(vsp));
                        a.mov(ptr(const_cast<zasm::x86::Gp64&&>(zasm::x86::r10)), v);
                        a.add(vsp, 8);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vwrite"]);
                    get_vreg_value(a);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    vwritez(0b00, zasm::x86::dl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    vwritez(0b01, zasm::x86::dx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    vwritez(0b10, zasm::x86::edx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    vwritez(0b11, zasm::x86::rdx, zasm::x86::qword_ptr<zasm::x86::Gp64>);

                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::add), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto varith = [&]<typename T>(int size, zasm::x86::Gp v0, zasm::x86::Gp v1, T ptr) {
                        a.bind(labels[1 + size]);
                        a.mov(v0, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.add(vsp, 1 << size);
                        a.mov(v1, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.add(v0, v1);
                        a.mov(ptr(std::forward<zasm::x86::Gp64>(vsp)), v0);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vadd"]);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    varith(0b00, zasm::x86::cl, zasm::x86::dl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    varith(0b01, zasm::x86::cx, zasm::x86::dx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    varith(0b10, zasm::x86::ecx, zasm::x86::edx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    varith(0b11, zasm::x86::rcx, zasm::x86::rdx, zasm::x86::qword_ptr<zasm::x86::Gp64>);

                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::sub), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto varith = [&]<typename T>(int size, zasm::x86::Gp v0, zasm::x86::Gp v1, T ptr) {
                        a.bind(labels[1 + size]);
                        a.mov(v0, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.add(vsp, 1 << size);
                        a.mov(v1, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.sub(v1, v0);
                        a.mov(ptr(std::forward<zasm::x86::Gp64>(vsp)), v1);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vsub"]);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    varith(0b00, zasm::x86::cl, zasm::x86::dl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    varith(0b01, zasm::x86::cx, zasm::x86::dx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    varith(0b10, zasm::x86::ecx, zasm::x86::edx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    varith(0b11, zasm::x86::rcx, zasm::x86::rdx, zasm::x86::qword_ptr<zasm::x86::Gp64>);

                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::bxor), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto varith = [&]<typename T>(int size, zasm::x86::Gp v0, zasm::x86::Gp v1, T ptr) {
                        a.bind(labels[1 + size]);
                        a.mov(v0, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.add(vsp, 1 << size);
                        a.mov(v1, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.xor_(v1, v0);
                        a.mov(ptr(std::forward<zasm::x86::Gp64>(vsp)), v1);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vxor"]);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    varith(0b00, zasm::x86::cl, zasm::x86::dl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    varith(0b01, zasm::x86::cx, zasm::x86::dx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    varith(0b10, zasm::x86::ecx, zasm::x86::edx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    varith(0b11, zasm::x86::rcx, zasm::x86::rdx, zasm::x86::qword_ptr<zasm::x86::Gp64>);

                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::band), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto varith = [&]<typename T>(int size, zasm::x86::Gp v0, zasm::x86::Gp v1, T ptr) {
                        a.bind(labels[1 + size]);
                        a.mov(v0, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.add(vsp, 1 << size);
                        a.mov(v1, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.and_(v1, v0);
                        a.mov(ptr(std::forward<zasm::x86::Gp64>(vsp)), v1);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vand"]);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    varith(0b00, zasm::x86::cl, zasm::x86::dl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    varith(0b01, zasm::x86::cx, zasm::x86::dx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    varith(0b10, zasm::x86::ecx, zasm::x86::edx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    varith(0b11, zasm::x86::rcx, zasm::x86::rdx, zasm::x86::qword_ptr<zasm::x86::Gp64>);

                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::bor), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto varith = [&]<typename T>(int size, zasm::x86::Gp v0, zasm::x86::Gp v1, T ptr) {
                        a.bind(labels[1 + size]);
                        a.mov(v0, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.add(vsp, 1 << size);
                        a.mov(v1, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.or_(v1, v0);
                        a.mov(ptr(std::forward<zasm::x86::Gp64>(vsp)), v1);
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vor"]);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    varith(0b00, zasm::x86::cl, zasm::x86::dl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    varith(0b01, zasm::x86::cx, zasm::x86::dx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    varith(0b10, zasm::x86::ecx, zasm::x86::edx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    varith(0b11, zasm::x86::rcx, zasm::x86::rdx, zasm::x86::qword_ptr<zasm::x86::Gp64>);

                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::cmp), [&](zasm::x86::Assembler& a) {
                    auto labels = [&]{ std::array<zasm::Label, 6> res; for (auto&x:res) x = a.createLabel(); return res; }();

                    auto varith = [&]<typename T>(int size, zasm::x86::Gp v0, zasm::x86::Gp v1, T ptr) {
                        a.bind(labels[1 + size]);
                        a.mov(v0, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.add(vsp, 1 << size);
                        a.mov(v1, ptr(std::forward<zasm::x86::Gp64>(vsp)));
                        a.cmp(v1, v0);
                        a.pushfq();
                        a.pop(v0.r64());
                        a.add(vsp, 1 << size);
                        a.sub(vsp, 2);
                        a.mov(zasm::x86::word_ptr(std::forward<zasm::x86::Gp64>(vsp)), v0.r16());
                        a.jmp(labels[5]);
                    };

                    get_size_from_opcode(a, global_labels["vcmp"]);

                    create_jump_table_once(a, labels[0], labels[1], labels[2], labels[3], labels[4]);
                    jump_using_table(a, labels[0]);

                    varith(0b00, zasm::x86::cl, zasm::x86::dl, zasm::x86::byte_ptr<zasm::x86::Gp64>);
                    varith(0b01, zasm::x86::cx, zasm::x86::dx, zasm::x86::word_ptr<zasm::x86::Gp64>);
                    varith(0b10, zasm::x86::ecx, zasm::x86::edx, zasm::x86::dword_ptr<zasm::x86::Gp64>);
                    varith(0b11, zasm::x86::rcx, zasm::x86::rdx, zasm::x86::qword_ptr<zasm::x86::Gp64>);

                    vm_next_instruction(a, labels[5]);
                }
            },
            {
                uint8_t(v0_op::jmp), [&](zasm::x86::Assembler& a) {
                    a.bind(global_labels["vjmp"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jz), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel();

                    a.bind(global_labels["vjz"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // ZF = 1?
                    a.and_(zasm::x86::rdx, 0x0040);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jnz(truth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jnz), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel();

                    a.bind(global_labels["vjnz"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // ZF = 0?
                    a.and_(zasm::x86::rdx, 0x0040);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jz(truth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jb), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel();

                    a.bind(global_labels["vjb"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // CF = 1?
                    a.and_(zasm::x86::rdx, 0x0001);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jnz(truth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jnb), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel();

                    a.bind(global_labels["vjnb"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // CF = 0?
                    a.and_(zasm::x86::rdx, 0x0001);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jz(truth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jbe), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel();

                    a.bind(global_labels["vjbe"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // CF = 1?
                    a.and_(zasm::x86::rdx, 0x0001);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jnz(truth);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // or ZF = 1?
                    a.and_(zasm::x86::rdx, 0x0040);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jnz(truth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jnbe), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel(), ntruth = a.createLabel();

                    a.bind(global_labels["vjnbe"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // CF = 0?
                    a.and_(zasm::x86::rdx, 0x0001);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jnz(ntruth);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // and ZF = 0?
                    a.and_(zasm::x86::rdx, 0x0040);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jnz(ntruth);
                    a.jmp(truth);
                    a.bind(ntruth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jl), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel();

                    a.bind(global_labels["vjl"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // or SF != OF?
                    a.and_(zasm::x86::rdx, 0x0880);
                    a.popcnt(zasm::x86::rdx, zasm::x86::rdx);
                    a.cmp(zasm::x86::rdx, 1);
                    a.jz(truth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jle), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel();

                    a.bind(global_labels["vjle"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // ZF = 1?
                    a.and_(zasm::x86::rdx, 0x0040);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jnz(truth);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // or SF != OF?
                    a.and_(zasm::x86::rdx, 0x0880);
                    a.popcnt(zasm::x86::rdx, zasm::x86::rdx);
                    a.cmp(zasm::x86::rdx, 1);
                    a.jz(truth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jnl), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel();

                    a.bind(global_labels["vjnl"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // or SF == OF?
                    a.and_(zasm::x86::rdx, 0x0880);
                    a.popcnt(zasm::x86::rdx, zasm::x86::rdx);
                    a.cmp(zasm::x86::rdx, 1);
                    a.jnz(truth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::jnle), [&](zasm::x86::Assembler& a) {
                    auto vnext = a.createLabel(), truth = a.createLabel(), ntruth = a.createLabel();

                    a.bind(global_labels["vjnle"]);
                    a.inc(vip);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // ZF = 0?
                    a.and_(zasm::x86::rdx, 0x0040);
                    a.test(zasm::x86::rdx, zasm::x86::rdx);
                    a.jnz(ntruth);
                    a.movzx(zasm::x86::rdx, zasm::x86::word_ptr(vsp)); // and SF == OF?
                    a.and_(zasm::x86::rdx, 0x0880);
                    a.popcnt(zasm::x86::rdx, zasm::x86::rdx);
                    a.cmp(zasm::x86::rdx, 1);
                    a.jz(ntruth);
                    a.jmp(truth);
                    a.bind(ntruth);
                    a.add(vip, 2);
                    a.jmp(vnext);
                    
                    a.bind(truth);
                    a.movzx(zasm::x86::rcx, zasm::x86::word_ptr(vip));
                    a.lea(vip, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vcode"]));
                    a.add(vip, zasm::x86::rcx);
                    a.bind(vnext);
                    a.add(vsp, 2);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::call), [&](zasm::x86::Assembler& a) {
                    a.bind(global_labels["vcall"]);
                    a.inc(vip);
                    a.mov(zasm::x86::r11, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["retaddr"]));
                    a.movsxd(zasm::x86::r9, zasm::x86::dword_ptr(vip));
                    a.add(zasm::x86::r11, zasm::x86::r9);

                    // push original global_labels["retaddr"] to global_labels["vstack"], incase we vmenter somewhere else
                    a.sub(vsp, 8);
                    a.mov(zasm::x86::r9, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["retaddr"]));
                    a.mov(zasm::x86::qword_ptr(vsp), zasm::x86::r9);

                    // push current vip to global_labels["vstack"], in case we vmenter somewhere else
                    a.add(vip, 4);
                    a.sub(vsp, 8);
                    a.mov(zasm::x86::qword_ptr(vsp), vip);

                    // set vsp offset in case we vmenter somewhere else
                    a.mov(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["_vsp"]), vsp);
                    a.lea(zasm::x86::r9, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vstack"]));
                    a.sub(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["_vsp"]), zasm::x86::r9);

                    // vmexit proc
                    a.popfq();
                    a.pop(zasm::x86::rax);
                    a.pop(zasm::x86::rcx);
                    a.pop(zasm::x86::rdx);
                    a.pop(zasm::x86::rbx);
                    a.pop(zasm::x86::rbp); // rsp
                    a.pop(zasm::x86::rbp);
                    a.pop(zasm::x86::rsi);
                    a.pop(zasm::x86::rdi);
                    a.pop(zasm::x86::r8);
                    a.pop(zasm::x86::r9);
                    a.pop(zasm::x86::r10);
                    a.pop(zasm::x86::r12); // r11
                    a.pop(zasm::x86::r12);
                    a.pop(zasm::x86::r13);
                    a.pop(zasm::x86::r14);
                    a.pop(zasm::x86::r15);

                    vm_enter_emitter.revert_effects(a);

                    a.call(zasm::x86::r11);

                    // vmenter proc
                    vm_enter_emitter.assemble_effects(a);

                    a.push(zasm::x86::r15); // -8
                    a.push(zasm::x86::r14); // -16
                    a.push(zasm::x86::r13); // -24
                    a.push(zasm::x86::r12); // -32
                    a.push(zasm::x86::r11); // -40
                    a.push(zasm::x86::r10); // -48
                    a.push(zasm::x86::r9); // -56
                    a.push(zasm::x86::r8); // -64
                    a.push(zasm::x86::rdi); // -72
                    a.push(zasm::x86::rsi); // -80
                    a.push(zasm::x86::rbp); // -88
                    a.push(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["saved_rsp"])); // -96
                    a.push(zasm::x86::rbx); // -104
                    a.push(zasm::x86::rdx); // -112
                    a.push(zasm::x86::rcx); // -120
                    a.push(zasm::x86::rax); // -128
                    a.pushfq(); // -136

                    a.lea(vsp, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vstack"]));
                    a.add(vsp, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["_vsp"]));

                    a.mov(vip, zasm::x86::qword_ptr(vsp));
                    a.add(vsp, 8);
                    a.mov(zasm::x86::r11, zasm::x86::qword_ptr(vsp));
                    a.mov(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["retaddr"]), zasm::x86::r11);

                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::lea), [&](zasm::x86::Assembler& a) {
                    a.bind(global_labels["vlea"]);
                    a.inc(vip);
                    a.movsxd(zasm::x86::ecx, zasm::x86::dword_ptr(vip));
                    a.add(vip, 4);
                    a.add(zasm::x86::rcx, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["retaddr"]));
                    a.sub(vsp, 8);
                    a.mov(zasm::x86::qword_ptr(vsp), zasm::x86::rcx);
                    vm_next_instruction(a);
                }
            },
            {
                uint8_t(v0_op::execute_native), [&](zasm::x86::Assembler& a) {
                    a.bind(global_labels["vexenative"]);
                    
                    auto loop = a.createLabel();
                    auto native_code_section = a.createLabel();
                    auto done = a.createLabel();
                    
                    a.inc(vip);
                    a.movzx(zasm::x86::rcx, zasm::x86::byte_ptr(vip));
                    a.lea(zasm::x86::rdx, zasm::x86::qword_ptr(zasm::x86::rip, native_code_section));

                    a.inc(vip);

                    // to-do: use rep movsb? (bc currently mba is hardcoded to use rdi)
                    //
                    a.bind(loop);
                    a.test(zasm::x86::rcx, zasm::x86::rcx);
                    a.jz(done);
                    a.mov(zasm::x86::r9b, zasm::x86::byte_ptr(vip));
                    a.mov(zasm::x86::byte_ptr(zasm::x86::rdx), zasm::x86::r9b);
                    a.inc(zasm::x86::rdx);
                    a.inc(vip);
                    a.dec(zasm::x86::rcx);
                    a.jmp(loop);
                    a.bind(done);

                    // push current vip to global_labels["vstack"], in case we vmenter somewhere else
                    a.sub(vsp, 8);
                    a.mov(zasm::x86::qword_ptr(vsp), vip);

                    // set vsp offset in case we vmenter somewhere else
                    a.mov(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["_vsp"]), vsp);
                    a.lea(zasm::x86::r9, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vstack"]));
                    a.sub(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["_vsp"]), zasm::x86::r9);

                    a.popfq();
                    a.pop(zasm::x86::rax);
                    a.pop(zasm::x86::rcx);
                    a.pop(zasm::x86::rdx);
                    a.pop(zasm::x86::rbx);
                    a.pop(zasm::x86::rbp); // rsp
                    a.pop(zasm::x86::rbp);
                    a.pop(zasm::x86::rsi);
                    a.pop(zasm::x86::rdi);
                    a.pop(zasm::x86::r8);
                    a.pop(zasm::x86::r9);
                    a.pop(zasm::x86::r10);
                    a.pop(zasm::x86::r12); // r11
                    a.pop(zasm::x86::r12);
                    a.pop(zasm::x86::r13);
                    a.pop(zasm::x86::r14);
                    a.pop(zasm::x86::r15);

                    vm_enter_emitter.revert_effects(a);

                    a.bind(native_code_section);
                    for (int i = 0; i < 16; i++)
                        a.nop();

                    vm_enter_emitter.assemble_effects(a);

                    a.push(zasm::x86::r15); // -8
                    a.push(zasm::x86::r14); // -16
                    a.push(zasm::x86::r13); // -24
                    a.push(zasm::x86::r12); // -32
                    a.push(zasm::x86::r11); // -40
                    a.push(zasm::x86::r10); // -48
                    a.push(zasm::x86::r9); // -56
                    a.push(zasm::x86::r8); // -64
                    a.push(zasm::x86::rdi); // -72
                    a.push(zasm::x86::rsi); // -80
                    a.push(zasm::x86::rbp); // -88
                    a.push(zasm::x86::qword_ptr(zasm::x86::rip, global_labels["saved_rsp"])); // -96
                    a.push(zasm::x86::rbx); // -104
                    a.push(zasm::x86::rdx); // -112
                    a.push(zasm::x86::rcx); // -120
                    a.push(zasm::x86::rax); // -128
                    a.pushfq(); // -136

                    a.lea(vsp, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vstack"]));
                    a.add(vsp, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["_vsp"]));

                    a.mov(vip, zasm::x86::qword_ptr(vsp));
                    a.add(vsp, 8);

                    a.lea(zasm::x86::rdx, zasm::x86::qword_ptr(zasm::x86::rip, native_code_section));
                    a.mov(zasm::x86::dword_ptr(zasm::x86::rdx), 0x90909090);
                    a.mov(zasm::x86::dword_ptr(zasm::x86::rdx, 4), 0x90909090);
                    a.mov(zasm::x86::dword_ptr(zasm::x86::rdx, 8), 0x90909090);
                    a.mov(zasm::x86::dword_ptr(zasm::x86::rdx, 12), 0x90909090);

                    vm_next_instruction(a);
                }
            }
        };
    };

    namespace debug {
        void dump_v0(lift_result &result);
    }
}
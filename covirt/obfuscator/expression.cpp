#include "expression.hpp"

std::pair<zasm::Node*, size_t> expression::assemble_steps(zasm::x86::Assembler &a, zasm::Operand &A, zasm::Operand &B, std::vector<zasm::x86::Gp> &avail_registers, size_t size, std::optional<zasm::Label> label)
{
    std::vector<asm_step> steps;
    assemble_steps_state state(avail_registers);

    auto result = generate_asm_steps(A, B, steps, state, size);

    constexpr auto binop_safe_to_rotate = [](char op) -> bool {
        switch (op) {
        case '+': 
        case '-':
            return false;
        case '^': 
        case '&': 
        case '|': 
        case '~':
            return true;
        default:
            return false;
        }
    };

    for (auto &step : steps)
        std::visit([&](auto &&x) {
            using T = std::decay_t<decltype(x)>;

            auto rot = covirt::rand<uint8_t>();
            auto rot_op = zasm::Operand(zasm::Imm(rot));
            auto rot_op_x86 = to_x86_type(rot_op, 8);

            if constexpr (std::is_same_v<T, binary_asm>) {
                auto dst = to_x86_type(x.dst, size);
                auto lhs = to_x86_type(x.lhs, size);
                auto rhs = to_x86_type(x.rhs, size);

                safe_asm(a, [](auto &a, auto &dst, auto &src) { a.mov(dst, src); }, dst, lhs);

                if (binop_safe_to_rotate(x.op)) {
                    if (std::holds_alternative<zasm::x86::Gp>(lhs) && std::holds_alternative<zasm::x86::Gp>(rhs)) {
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.rol(dst, src); }, dst, rot_op_x86);
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.rol(dst, src); }, rhs, rot_op_x86);
                    }
                } else {
                    // if its not safe to rotate, we just put in a dummy rotation. this still manages to mess up ida
                    //
                    if (std::holds_alternative<zasm::x86::Gp>(lhs) && std::holds_alternative<zasm::x86::Gp>(rhs)) {
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.rol(dst, src); }, dst, rot_op_x86);
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.rol(dst, src); }, rhs, rot_op_x86);
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.ror(dst, src); }, dst, rot_op_x86);
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.ror(dst, src); }, rhs, rot_op_x86);
                    }
                }

                switch (x.op) {
                case '+': safe_asm(a, [](auto &a, auto &dst, auto &src) { a.add(dst, src); }, dst, rhs); break;
                case '-': safe_asm(a, [](auto &a, auto &dst, auto &src) { a.sub(dst, src); }, dst, rhs); break;
                case '^': safe_asm(a, [](auto &a, auto &dst, auto &src) { a.xor_(dst, src); }, dst, rhs); break;
                case '&': safe_asm(a, [](auto &a, auto &dst, auto &src) { a.and_(dst, src); }, dst, rhs); break;
                case '|': safe_asm(a, [](auto &a, auto &dst, auto &src) { a.or_(dst, src); }, dst, rhs); break;
                }

                if (binop_safe_to_rotate(x.op)) {
                    if (std::holds_alternative<zasm::x86::Gp>(lhs) && std::holds_alternative<zasm::x86::Gp>(rhs) ) {
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.ror(dst, src); }, dst, rot_op_x86);
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.ror(dst, src); }, rhs, rot_op_x86);
                    }
                } else {
                    if (std::holds_alternative<zasm::x86::Gp>(lhs) && std::holds_alternative<zasm::x86::Gp>(rhs)) {
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.rol(dst, src); }, dst, rot_op_x86);
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.rol(dst, src); }, rhs, rot_op_x86);
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.ror(dst, src); }, dst, rot_op_x86);
                        safe_rot(a, [](auto &a, auto &dst, auto &src) { a.ror(dst, src); }, rhs, rot_op_x86);
                    }
                }
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(x)>, unary_asm>) {
                auto dst = to_x86_type(x.dst, size);
                auto src = to_x86_type(x.operand, size);

                safe_asm(a, [](auto &a, auto &dst, auto &src) { a.mov(dst, src); }, dst, src);
                
                if (binop_safe_to_rotate(x.op) && std::holds_alternative<zasm::x86::Gp>(dst))
                    safe_rot(a, [](auto &a, auto &dst, auto &src) { a.rol(dst, src); }, dst, rot_op_x86);
                else {
                    safe_rot(a, [](auto &a, auto &dst, auto &src) { a.rol(dst, src); }, dst, rot_op_x86);
                    safe_rot(a, [](auto &a, auto &dst, auto &src) { a.ror(dst, src); }, dst, rot_op_x86);
                }

                switch (x.op) {
                case '-': safe_asm(a, [](auto &a, auto &dst, auto &src) { a.neg(dst); }, dst, dst); break;
                case '~': safe_asm(a, [](auto &a, auto &dst, auto &src) { a.not_(dst); }, dst, dst); break;
                }

                if (binop_safe_to_rotate(x.op) && std::holds_alternative<zasm::x86::Gp>(dst))
                    safe_rot(a, [](auto &a, auto &dst, auto &src) { a.ror(dst, src); }, dst, rot_op_x86);
                else {
                    safe_rot(a, [](auto &a, auto &dst, auto &src) { a.rol(dst, src); }, dst, rot_op_x86);
                    safe_rot(a, [](auto &a, auto &dst, auto &src) { a.ror(dst, src); }, dst, rot_op_x86);
                }
            }
        }, step);

    auto x = to_x86_type(A, size);
    auto y = to_x86_type(result, size);
    safe_asm(a, [label](auto &a, auto &dst, auto &src) { a.mov(dst, src); }, x, y);
    if (label.has_value())
        a.bind(label.value());

    auto saved_count = asm_count;
    asm_count = 0;

    return { a.getCursor(), saved_count };
}

std::string expression::to_string() const 
{
    if (std::holds_alternative<std::string>(data)) {
        return std::get<std::string>(data);
    } else if (std::holds_alternative<size_t>(data)) {
        return std::to_string(std::get<size_t>(data));
    } else if (std::holds_alternative<binary_op>(data)) {
        const auto& op = std::get<binary_op>(data);
        return "(" + op.lhs->to_string() + op.op + op.rhs->to_string() + ")";
    } else {
        const auto& op = std::get<unary_op>(data);
        return std::string() + op.op + "(" + op.operand->to_string() + ")";
    }
}

std::variant<zasm::Imm, zasm::x86::Gp, zasm::x86::Mem> expression::to_x86_type(zasm::Operand &op, size_t size)
{
    if (op.holds<zasm::Imm>()) {
        return op.get<zasm::Imm>();
    } else if (op.holds<zasm::Reg>()) {
        auto reg = op.get<zasm::Reg>().as<zasm::x86::Reg>().as<zasm::x86::Gp>();
        switch (size) {
        case 8: return reg.r8lo();
        case 16: return reg.r16();
        case 32: return reg.r32();
        case 64: return reg.r64();
        }
    } else if (op.holds<zasm::Mem>()) {
        return op.get<zasm::x86::Mem>();
    } else {
        out::assertion(false, "unsupported operand type");
    }
    return {};
}

zasm::Operand expression::generate_asm_steps(zasm::Operand &A, zasm::Operand &B, std::vector<asm_step>& steps, assemble_steps_state &state, size_t size) const 
{
    if (std::holds_alternative<std::string>(data)) {
        // to-do: don't hardcode "A"
        //
        return std::get<std::string>(data) == "A" ? A : B;
    } else if (std::holds_alternative<size_t>(data)) {
        auto value = std::get<size_t>(data);
        switch (size) {
        case 8: return zasm::Imm8(int8_t(value));
        case 16: return zasm::Imm16(int16_t(value));
        case 32: 
        case 64: return zasm::Imm32(int32_t(value));
        }
    } else if (std::holds_alternative<binary_op>(data)) {
        const auto& op = std::get<binary_op>(data);
        auto lhs = op.lhs->generate_asm_steps(A, B, steps, state, size);
        auto rhs = op.rhs->generate_asm_steps(A, B, steps, state, size);
        
        state.throw_away_register(lhs);
        auto result_reg = state.collect_register();
        steps.push_back(binary_asm{op.op, result_reg, lhs, rhs});

        state.throw_away_register(rhs);

        return result_reg;
    } else {
        const auto& op = std::get<unary_op>(data);
        auto operand = op.operand->generate_asm_steps(A, B, steps, state, size);

        state.throw_away_register(operand);
        auto result_reg = state.collect_register();
        steps.push_back(unary_asm{op.op, result_reg, operand});

        return result_reg;
    }

    out::assertion(false, "unexpectedly reached unreachable code path");
    return {};
}

expression expression::transform_impl(const expression& match, const expression& new_expr) const 
{
    if (equals(match))
        return new_expr;

    if (std::holds_alternative<binary_op>(data)) {
        const auto& op = std::get<binary_op>(data);
        auto new_lhs = op.lhs->transform_impl(match, new_expr);
        auto new_rhs = op.rhs->transform_impl(match, new_expr);
        return expression(op.op, new_lhs, new_rhs);
    } else if (std::holds_alternative<unary_op>(data)) {
        const auto& op = std::get<unary_op>(data);
        auto new_operand = op.operand->transform_impl(match, new_expr);
        return expression(op.op, new_operand);
    } else {
        return *this;
    }
}
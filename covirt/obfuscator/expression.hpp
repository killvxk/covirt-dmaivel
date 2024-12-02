#pragma once 

#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <map>
#include <expected>

#include <zasm/zasm.hpp>
#include <utils/log.hpp>
#include <utils/rand.hpp>

class expression {
public:
    expression() {}

    expression(const std::string& name) : data(name) {}

    expression(size_t constant) : data(constant) {}

    expression(char op, const expression& lhs, const expression& rhs)
        : data(binary_op{op, std::make_shared<expression>(lhs), std::make_shared<expression>(rhs)}) {}

    expression(char op, const expression& operand)
        : data(unary_op{op, std::make_shared<expression>(operand)}) {}

    expression operator+(const expression& other) const { return expression('+', *this, other); }
    expression operator-(const expression& other) const { return expression('-', *this, other); }
    expression operator*(const expression& other) const { return expression('*', *this, other); }
    expression operator&(const expression& other) const { return expression('&', *this, other); }
    expression operator|(const expression& other) const { return expression('|', *this, other); }
    expression operator^(const expression& other) const { return expression('^', *this, other); }

    expression operator~() const { return expression('~', *this); }
    expression operator-() const { return expression('-', *this); }

    expression transform(const expression& match, const expression& new_expr) const { return transform_impl(match, new_expr); }

    template <typename T>
    static expression transform_constant(T target, int depth = 1) 
    {
        if (depth == 0)
            return expression(target);

        auto left = transform_constant<T>(covirt::rand<T>(), depth - 1);
        auto right = transform_constant<T>(covirt::rand<T>(), depth - 1);

        std::vector<std::function<expression(const expression&, const expression&)>> operations = {
            [](const expression& l, const expression& r) { return l + r; },
            [](const expression& l, const expression& r) { return l - r; },
            [](const expression& l, const expression& r) { return l ^ r; },
            [](const expression& l, const expression& r) { return l & r; },
            [](const expression& l, const expression& r) { return l | r; }
        };

        auto expr = operations[covirt::rand<int>() % operations.size()](left, right);

        T correction = target - expr.template evaluate<T>();
        auto correction_expr = expr + expression(correction);

        return correction_expr;
    }

    std::pair<zasm::Node*, size_t> assemble_steps(zasm::x86::Assembler &a, zasm::Operand &A, zasm::Operand &B, std::vector<zasm::x86::Gp> &avail_registers, size_t size, std::optional<zasm::Label> label = {});

private:
    struct binary_op {
        char op;
        std::shared_ptr<expression> lhs;
        std::shared_ptr<expression> rhs;
    };

    struct unary_op {
        char op;
        std::shared_ptr<expression> operand;
    };

    struct binary_asm {
        char op;
        zasm::Operand dst;
        zasm::Operand lhs;
        zasm::Operand rhs;
    };

    struct unary_asm {
        char op;
        zasm::Operand dst;
        zasm::Operand operand;
    };

    struct assemble_steps_state {
        assemble_steps_state(std::vector<zasm::x86::Gp> &avail_registers) 
        {
            for (auto &r : avail_registers)
                avail[r] = true;
        }

        zasm::Operand collect_register()
        {
            for (auto& [reg, state] : avail)
                if (state) {
                    state = false;
                    return reg;
                }

            out::assertion(false, "cant collect register, ran out of registers");
            return {};
        }

        void throw_away_register(zasm::Operand &reg)
        {
            if (reg.holds<zasm::Reg>())
                if (avail.contains(reg.get<zasm::Reg>().as<zasm::x86::Gp>()))
                    avail[reg.get<zasm::Reg>().as<zasm::x86::Gp>()] = true;
        }

        std::map<zasm::x86::Gp, bool> avail;
    };

    using asm_step = std::variant<binary_asm, unary_asm>;

    size_t asm_count = 0;
    std::variant<std::string, size_t, binary_op, unary_op> data;

    bool equals(const expression& other) const { return to_string() == other.to_string(); }

    template <typename Op>
    void safe_asm(zasm::x86::Assembler &a, Op operation, std::variant<zasm::Imm, zasm::x86::Gp, zasm::x86::Mem> &dst, std::variant<zasm::Imm, zasm::x86::Gp, zasm::x86::Mem> &src) 
    {
        std::visit([&a, operation](auto &&dst, auto &&src) {
            using dst_t = std::decay_t<decltype(dst)>;
            using src_t = std::decay_t<decltype(src)>;

            if constexpr ((std::is_same_v<dst_t, zasm::x86::Gp> && (std::is_same_v<src_t, zasm::x86::Gp> || std::is_same_v<src_t, zasm::Imm> || std::is_same_v<src_t, zasm::x86::Mem>))
                       || (std::is_same_v<dst_t, zasm::x86::Mem> && (std::is_same_v<src_t, zasm::x86::Gp> || std::is_same_v<src_t, zasm::Imm>))) {
                operation(a, dst, src);
            }
            else {
                out::fail("safe_asm did not emit an instruction!");
            }
        }, dst, src);

        asm_count++;
    }

    template <typename Op>
    void safe_rot(zasm::x86::Assembler &a, Op operation, std::variant<zasm::Imm, zasm::x86::Gp, zasm::x86::Mem> &dst, std::variant<zasm::Imm, zasm::x86::Gp, zasm::x86::Mem> &src) 
    {
        std::visit([&a, operation](auto &&dst, auto &&src) {
            using dst_t = std::decay_t<decltype(dst)>;
            using src_t = std::decay_t<decltype(src)>;

            if constexpr ((std::is_same_v<dst_t, zasm::x86::Gp> && (std::is_same_v<src_t, zasm::x86::Gp> || std::is_same_v<src_t, zasm::Imm>))
                       || (std::is_same_v<dst_t, zasm::x86::Mem> && (std::is_same_v<src_t, zasm::x86::Gp> || std::is_same_v<src_t, zasm::Imm>))) {
                operation(a, dst, src);
            }
            else {
                out::fail("safe_asm did not emit an instruction!");
            }
        }, dst, src);

        asm_count++;
    }

    // to-do: remove this function; currently its used to match patterns, however
    // it is more of a remnant from when i was debugging this class and needed
    // to print out the expression
    //
    std::string to_string() const;
    
    std::variant<zasm::Imm, zasm::x86::Gp, zasm::x86::Mem> to_x86_type(zasm::Operand &op, size_t size);
    
    zasm::Operand generate_asm_steps(zasm::Operand &A, zasm::Operand &B, std::vector<asm_step>& steps, assemble_steps_state &state, size_t size) const;
    
    expression transform_impl(const expression& match, const expression& new_expr) const;

    template <typename T>
    T evaluate() const 
    {
        out::assertion(!std::holds_alternative<std::string>(data), "can't evaluate a variable");
        
        if (std::holds_alternative<size_t>(data)) {
            return T(std::get<size_t>(data));
        } else if (std::holds_alternative<binary_op>(data)) {
            const auto& op = std::get<binary_op>(data);
            T lhs = op.lhs->evaluate<T>();
            T rhs = op.rhs->evaluate<T>();
            switch (op.op) {
            case '+': return lhs + rhs;
            case '-': return lhs - rhs;
            case '^': return lhs ^ rhs;
            case '&': return lhs & rhs;
            case '|': return lhs | rhs;
            default: 
                out::assertion(false, "unknown binary operator ({})", op.op);
            }
        } else {
            const auto& op = std::get<unary_op>(data);
            T operand = op.operand->evaluate<T>();
            switch (op.op) {
            case '~': return ~operand;
            case '-': return -operand;
            default: 
                out::assertion(false, "unknown unary operator ({})", op.op);
            }
        }

        out::assertion(false, "hit unexpected codepath");
        return {};
    }
};

static expression A("A");
static expression B("B");
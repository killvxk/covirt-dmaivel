#include "disasm.hpp"

#include <utils/log.hpp>
#include <print>

covirt::zydis_operand::zydis_operand(ZydisDecodedOperand& op)
{
	assign(op);
}

void covirt::zydis_operand::assign(ZydisDecodedOperand& op)
{
	size = op.size / 8;
	switch (op.type) {
	case ZYDIS_OPERAND_TYPE_REGISTER: value = op.reg; break;
	case ZYDIS_OPERAND_TYPE_IMMEDIATE: value = op.imm; break;
	case ZYDIS_OPERAND_TYPE_MEMORY: value = op.mem; break;
	default: value = std::monostate{}; break;
	}
}

int covirt::zydis_operand::register_index(bool use_index)
{
	auto reg = ZYDIS_REGISTER_NONE;

	if (is_register()) reg = as_register().value;
	if (is_memory()) reg = !use_index ? as_memory().base : as_memory().index;

	out::assertion(reg != ZYDIS_REGISTER_NONE, "zydis_operand isn't a register");

	if (reg >= ZYDIS_REGISTER_AL && reg <= ZYDIS_REGISTER_R15B) {
		return reg - ZYDIS_REGISTER_AL;
	}
	else if (reg >= ZYDIS_REGISTER_AX && reg <= ZYDIS_REGISTER_R15W) {
		return reg - ZYDIS_REGISTER_AX;
	}
	else if (reg >= ZYDIS_REGISTER_EAX && reg <= ZYDIS_REGISTER_R15D) {
		return reg - ZYDIS_REGISTER_EAX;
	}
	else if (reg >= ZYDIS_REGISTER_RAX && reg <= ZYDIS_REGISTER_R15) {
		return reg - ZYDIS_REGISTER_RAX;
	}
	else {
		return -1;
	}

}

void covirt::disasm(std::span<const uint8_t> content, uint64_t base_address, std::function<void(uint64_t, ZydisDisassembledInstruction)> callback)
{
	ZydisDisassembledInstruction ins;
	size_t offset = 0;

	while (ZYAN_SUCCESS(ZydisDisassembleIntel(
		ZYDIS_MACHINE_MODE_LONG_64,
		base_address,
		&content[0] + offset,
		content.size() - offset,
		&ins
	))) {
		callback(base_address, ins);

		base_address += ins.info.length;
		offset += ins.info.length;
	}
}

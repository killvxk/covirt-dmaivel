#include "v0.hpp"

#include <stack>
#include <utils/log.hpp>

void covirt::vm::v0_lifter::push_address(covirt::zydis_operand& operand)
{
	//[base+disp]<--done
	//[index*scale+disp]<--done
	//[base+index*scale]<--done
	//[base+index*scale+disp]<--done

	const auto mem = operand.as_memory();
	int add_argc = 0;
	if (mem.index != ZYDIS_REGISTER_NONE)
	{
		e.push_reg(8, static_cast<uint8_t>(operand.register_index(true)));
		for (auto i = 1;i < mem.scale;i++)
		{
			//1 2 4 8
			e.push_reg(8, static_cast<uint8_t>(operand.register_index(true)));
			e.add(8);

			if (add_argc) {
				e.pop(8, static_cast<uint8_t>(14));
				e.push_reg(8, static_cast<uint8_t>(14));
			}
			add_argc++;
		}
		add_argc++;
	}
	if (mem.base != ZYDIS_REGISTER_NONE)
	{
		if (add_argc > 1) {
			e.pop(8, static_cast<uint8_t>(14));
			e.push_reg(8, static_cast<uint8_t>(14));
		}
		e.push_reg(8, static_cast<uint8_t>(operand.register_index()));
		if (add_argc)
			e.add(8);
		add_argc++;
	}
	if (add_argc > 1) {
		e.pop(8, static_cast<uint8_t>(14));
		e.push_reg(8, static_cast<uint8_t>(14));
	}
	e.push_imm(8, static_cast<uint64_t>(operand.as_memory().disp.value));
	e.add(8);

	//e.push_reg(8, uint8_t(operand.register_index()))
	// .push_imm(8, uint64_t(operand.as_memory().disp.value))
	// .add(8);
}

void covirt::vm::v0_lifter::push_operand(covirt::zydis_operand& operand, std::optional<int> override_size)
{
	int size = override_size.value_or(operand.size);

	if (operand.is_register())
		e.push_reg(size, static_cast<uint8_t>(operand.register_index()));
	else if (operand.is_immediate())
		e.push_imm(size, e.cast(operand.immediate(), size));
	else /* if (operand.is_memory()) */ {
		push_address(operand);
		e.read(size);
	}
}

void covirt::vm::v0_lifter::pop_operand(covirt::zydis_operand& operand, std::optional<int> override_size, std::optional<covirt::zydis_operand> src)
{
	int size = override_size.value_or(operand.size);
	auto src_operand = src.value_or(covirt::zydis_operand());

	if (operand.is_register())
		e.pop(size, static_cast<uint8_t>(operand.register_index()));
	else /* if (operand.is_memory()) */ {
		if (src_operand.is_empty() || src_operand.is_immediate()) {
			e.pop(size, static_cast<uint8_t>(tmp_reg_idx));
			push_address(operand);
			e.write(size, static_cast<uint8_t>(tmp_reg_idx));
		}
		else {
			push_address(operand);
			e.write(size, static_cast<uint8_t>(src_operand.register_index()));
		}
	}
}

void covirt::vm::v0_vm::initialize(zasm::x86::Assembler& a)
{
	for (auto& [name, label] : global_labels)
		label = a.createLabel(name.c_str());

	vip = zasm::x86::rax;
	vsp = zasm::x86::rsi;

	a.section(".text");
}

void covirt::vm::v0_vm::finalize(zasm::x86::Assembler& a)
{
	a.section(".data", zasm::Section::Attribs::Data);

	a.bind(global_labels["vcode"]); a.db(0, code_size);
	a.bind(global_labels["saved_rsp"]); a.dq(0);
	a.bind(global_labels["_vsp"]); a.dq(stack_size);
	a.bind(global_labels["_vip"]); a.dq(0);
	a.bind(global_labels["vstack"]); a.db(0, stack_size);
	a.bind(global_labels["retaddr"]); a.dq(0);

	a.bind(global_labels["vtable"]);
	a.dq(0, 8);
}

void covirt::vm::v0_vm::vm_next_instruction(zasm::x86::Assembler& a, std::optional<zasm::Label> label)
{
	if (label.has_value())
		a.bind(label.value());

	a.movzx(zasm::x86::rcx, zasm::x86::byte_ptr(vip));
	a.and_(zasm::x86::cl, 0b00111111);
	a.lea(zasm::x86::r9, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["vtable"]));
	a.jmp(zasm::x86::qword_ptr(zasm::x86::r9, zasm::x86::rcx, 8));
}

void covirt::vm::v0_vm::jump_using_table(zasm::x86::Assembler& a, zasm::Label& paths)
{
	a.lea(zasm::x86::r9, zasm::x86::qword_ptr(zasm::x86::rip, paths));
	a.jmp(zasm::x86::qword_ptr(zasm::x86::r9, zasm::x86::rcx, 8));

	a.bind(paths);
	a.dq(0, 4);
}

void covirt::vm::v0_vm::get_size_from_opcode(zasm::x86::Assembler& a, zasm::Label& start)
{
	a.bind(start);
	a.movzx(zasm::x86::rcx, zasm::x86::byte_ptr(vip));
	a.shr(zasm::x86::cl, 6);
	a.add(vip, 1);
}

void covirt::vm::v0_vm::get_vreg_address(zasm::x86::Assembler& a)
{
	a.mov(zasm::x86::r9, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["saved_rsp"]));
	a.sub(zasm::x86::r9, 16 * 8);
	a.movzx(zasm::x86::r10, zasm::x86::byte_ptr(vip));
	a.lea(zasm::x86::rdx, zasm::x86::qword_ptr(zasm::x86::r9, zasm::x86::r10, 8));
	a.add(vip, 1);
}

void covirt::vm::v0_vm::get_vreg_value(zasm::x86::Assembler& a)
{
	a.mov(zasm::x86::r9, zasm::x86::qword_ptr(zasm::x86::rip, global_labels["saved_rsp"]));
	a.sub(zasm::x86::r9, 16 * 8);
	a.movzx(zasm::x86::r10, zasm::x86::byte_ptr(vip));
	a.mov(zasm::x86::rdx, zasm::x86::qword_ptr(zasm::x86::r9, zasm::x86::r10, 8));
	a.add(vip, 1);
}

// to-do: ugly code, refactor plz, this was rush job
//
void covirt::vm::debug::dump_v0(lift_result& result)
{
	auto bytes = result.bytes;
	auto equivs = result.dump_index_table;

	static char suffix[] = { 'b', 'w', 'd', 'q' };
	int x = 0;
	int r = 0;

	std::stack<std::string> expression_stack;

	std::println("");
	std::println("| off | idx | lifted from                          | vm instruction             | expression");
	std::println("|-----|-----|--------------------------------------|----------------------------|---------------------------");

	for (int i = 0; i < bytes.size();) {
		uint8_t opcode = bytes[i] & 0b00111111;
		uint8_t size = 1 << (bytes[i] >> 6);

		std::print("| {:>12} | {:>12} | {:<36} | ", out::red(i), out::red(x), equivs.contains(x) ? equivs[x] : "");
		x++;

		switch (opcode) {
			using enum v0_op;
			case int(vm_exit) :
				std::println("{:<26} | goto {} + {}", "vmexit", out::purple("retaddr"), out::value(*(uint16_t*)(&bytes[i + 1])));
				i += 2;
				break;
				case int(push_imm) :
					std::print("push{} ", suffix[bytes[i] >> 6]);
					switch (size) {
					case 1: std::println("{:<29} | ", out::value_hex(*(uint8_t*)(&bytes[i + 1]))); expression_stack.push(out::value(*(int8_t*)(&bytes[i + 1]))); break;
					case 2: std::println("{:<29} | ", out::value_hex(*(uint16_t*)(&bytes[i + 1]))); expression_stack.push(out::value(*(int16_t*)(&bytes[i + 1]))); break;
					case 4: std::println("{:<29} | ", out::value_hex(*(uint32_t*)(&bytes[i + 1]))); expression_stack.push(out::value(*(int32_t*)(&bytes[i + 1]))); break;
					case 8: std::println("{:<29} | ", out::value_hex(*(uint64_t*)(&bytes[i + 1]))); expression_stack.push(out::value(*(int64_t*)(&bytes[i + 1]))); break;
					}
					i += size;
					break;
					case int(push_reg) :
						std::println("{:<35} | ", std::format("push{} {}", suffix[bytes[i] >> 6], out::green(std::format("v{}", bytes[i + 1]))));
						if (bytes[i + 1] == 5) expression_stack.push(out::green(std::format("vbp", bytes[i + 1])));
						else expression_stack.push(out::green(std::format("v{}", bytes[i + 1])));
						i++;
						break;
						case int(pop) :
							std::print("{:<35} | ", std::format("pop{} {}", suffix[bytes[i] >> 6], out::green(std::format("v{}", bytes[i + 1]))));
						{
							auto a = expression_stack.top(); expression_stack.pop();
							std::println("{} = ({}){}", out::green(std::format("v{}", bytes[i + 1])), out::yellow(std::format("u{}", size * 8)), a);
						}
						i++;
						break;
						case int(read) :
							std::print("{:<26} | ", std::format("read{}", suffix[bytes[i] >> 6]));
						{
							auto a = expression_stack.top(); expression_stack.pop();
							std::println("{} = *({}*)({})", out::purple(std::format("t{}", r)), out::yellow(std::format("u{}", size * 8)), a);
						}
						expression_stack.push(out::purple(std::format("t{}", r++)));
						break;
						case int(write) :
							std::print("{:<35} | ", std::format("write{} {}", suffix[bytes[i] >> 6], out::green(std::format("v{}", bytes[i + 1]))));
						{
							auto a = expression_stack.top(); expression_stack.pop();
							std::println("*({}*)({}) = {}", out::yellow(std::format("u{}", size * 8)), a, out::green(std::format("v{}", bytes[i + 1])));
						}
						i++;
						break;
						case int(add) :
							std::print("{:<26} | ", std::format("add{}", suffix[bytes[i] >> 6]));
						{
							auto a = expression_stack.top(); expression_stack.pop();
							auto b = expression_stack.top(); expression_stack.pop();
							std::println("{} = {} + {}", out::purple(std::format("t{}", r)), b, a);
						}
						expression_stack.push(out::purple(std::format("t{}", r++)));
						break;
						case int(sub) :
							std::print("{:<26} | ", std::format("sub{}", suffix[bytes[i] >> 6]));
						{
							auto a = expression_stack.top(); expression_stack.pop();
							auto b = expression_stack.top(); expression_stack.pop();
							std::println("{} = {} - {}", out::purple(std::format("t{}", r)), b, a);
						}
						expression_stack.push(out::purple(std::format("t{}", r++)));
						break;
						case int(bxor) :
							std::print("{:<26} | ", std::format("xor{}", suffix[bytes[i] >> 6]));
						{
							auto a = expression_stack.top(); expression_stack.pop();
							auto b = expression_stack.top(); expression_stack.pop();
							std::println("{} = {} ^ {}", out::purple(std::format("t{}", r)), b, a);
						}
						expression_stack.push(out::purple(std::format("t{}", r++)));
						break;
						case int(cmp) :
							std::println("{:<26} | ", std::format("cmp{}", suffix[bytes[i] >> 6]));
							expression_stack.push(out::purple(std::format("flags", r++)));
							break;
							case int(jmp) :
							{
								std::println("{:<26} | goto {}", "jmp", out::red(*(uint16_t*)(&bytes[i + 1])));
								i += 2;
							}
							break;
							case int(call) :
							{
								std::println("{:<26} | goto {}", "call", out::red(*(uint16_t*)(&bytes[i + 1])));
								i += 4;
							}
							break;
							case int(lea) :
							{
								std::println("{:<26} | goto {}", "lea", out::red(*(uint16_t*)(&bytes[i + 1])));
								i += 4;
							}
							break;
							case int(execute_native) :
							{
								std::println("{:<26} | ", "exe_native");
								i += bytes[i + 1] + 1;
							}
							break;
							default:
								if (opcode >= int(jz) && opcode <= int(jnle))
								{
									auto a = expression_stack.top(); expression_stack.pop();
									std::println("{:<26} | using {} goto {}", "jcc", a, out::red(*(uint16_t*)(&bytes[i + 1])));
									i += 2;
									break;
								}
								std::println("{:<35} | ", std::format("(bad:{:x})", bytes[i]));
								break;
		}

		i++;
	}

	std::println("");
}

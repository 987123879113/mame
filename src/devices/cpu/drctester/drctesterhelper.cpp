#include "drctester.h"

constexpr double FLOAT_ALLOWED_DELTA = 0.0025f;
constexpr double DOUBLE_ALLOWED_DELTA = 0.0025;

constexpr int DISPLAY_RESULTS = 1;

static void cfunc_display_result(void *param)
{
	((drctester_cpu_device *)param)->func_display_result();
}

void drctester_cpu_device::generate_test_start(drcuml_block &block, uint32_t opcode, uint32_t opcode_size, uint32_t input_count, uint32_t output_count, uint32_t carry, uint32_t flags, const uint64_t *input_params, const uint64_t *output_params, const uint64_t *input_methods, const uint64_t *result_methods, const uint64_t *input_formats, const uint64_t *result_formats, uint32_t flag_combo, uint32_t initial_flags)
{
	UML_SETFLGS(block, initial_flags);

	if (carry == 0 || carry == 1)
		UML_CARRY(block, carry, 0); // set it first in case the flags are expected to be unchanged, so the flags

	UML_GETFLGS(block, I2, flag_combo);
	UML_MOV(block, uml::mem(&m_state->test_initial_status), I2);
	UML_MOV(block, uml::mem(&m_state->test_expected_status), flags);
	UML_MOV(block, uml::mem(&m_state->condition), 0);

	UML_MOV(block, uml::mem(&m_state->test_num), m_state->test_counter++);
	UML_MOV(block, uml::mem(&m_state->test_opcode), opcode);
	UML_MOV(block, uml::mem(&m_state->test_opcode_size), opcode_size);
	UML_MOV(block, uml::mem(&m_state->test_input_count), input_count);
	UML_MOV(block, uml::mem(&m_state->test_output_count), output_count);
	UML_MOV(block, uml::mem(&m_state->test_flag_combo), flag_combo);

	for (int i = 0; i < std::size(m_state->test_inputs); i++)
		m_state->test_inputs[i] = 0;

	for (int i = 0; i < std::size(m_state->test_expected_outputs); i++)
		m_state->test_expected_outputs[i] = 0;

	for (int i = 0; i < std::size(m_state->test_param_methods); i++)
		m_state->test_param_methods[i] = 0;

	for (int i = 0; i < std::size(m_state->test_result_methods); i++)
		m_state->test_result_methods[i] = 0;

	for (int i = 0; i < std::size(m_state->test_param_formats); i++)
		m_state->test_param_formats[i] = 0;

	for (int i = 0; i < std::size(m_state->test_result_formats); i++)
		m_state->test_result_formats[i] = 0;

	for (int i = 0; i < std::size(m_state->test_result_outputs); i++)
	{
		m_state->test_result_outputs[i] = 0;
		UML_DMOV(block, uml::mem(&m_state->test_result_outputs[i]), 0);
	}

	for (int i = 0; i < input_count && input_params != nullptr; i++)
		UML_DMOV(block, uml::mem(&m_state->test_inputs[i]), input_params[i]);

	for (int i = 0; i < output_count && output_params != nullptr; i++)
		UML_DMOV(block, uml::mem(&m_state->test_expected_outputs[i]), output_params[i]);

	for (int i = 0; i < input_count && input_methods != nullptr; i++)
		UML_DMOV(block, uml::mem(&m_state->test_param_methods[i]), input_methods[i]);

	for (int i = 0; i < output_count && result_methods != nullptr; i++)
		UML_DMOV(block, uml::mem(&m_state->test_result_methods[i]), result_methods[i]);

	for (int i = 0; i < input_count && input_formats != nullptr; i++)
		UML_DMOV(block, uml::mem(&m_state->test_param_formats[i]), input_formats[i]);

	for (int i = 0; i < output_count && result_formats != nullptr; i++)
		UML_DMOV(block, uml::mem(&m_state->test_result_formats[i]), result_formats[i]);
}

void drctester_cpu_device::generate_test_end(drcuml_block &block, uint32_t flag_combo)
{
	UML_GETFLGS(block, I2, flag_combo);
	UML_MOV(block, uml::mem(&m_state->test_result_status), I2);
	UML_LABEL(block, m_labelnum++);
	UML_CALLC(block, cfunc_display_result, this);
}

void drctester_cpu_device::func_display_result()
{
	bool valid = true;

	if (!DISPLAY_RESULTS)
		return;

	printf("Test #%d\n", m_state->test_num);
	printf("\tOpcode: %d %s condition[%s]\n", m_state->test_opcode_size, uml::instruction::get_name(m_state->test_opcode), m_state->condition ? condition_strings[m_state->condition - uml::COND_Z] : "ALWAYS");
	if (m_state->test_input_count > 0) {
		printf("\tInputs:\n");
		for (int i = 0; i < m_state->test_input_count; i++) {
			if (m_state->test_param_formats[i] == uml::SIZE_SHORT)
			{
				printf("\t\tmethod[%s] %08x %f\n", method_str(m_state->test_param_methods[i]), (uint32_t)m_state->test_inputs[i], u2f(m_state->test_inputs[i]));
			}
			else if (m_state->test_param_formats[i] == uml::SIZE_DOUBLE)
			{
				printf("\t\tmethod[%s] %016llx %lf\n", method_str(m_state->test_param_methods[i]), m_state->test_inputs[i], u2d(m_state->test_inputs[i]));
			}
			else
			{
				if (m_state->test_opcode_size == 8)
					printf("\t\tmethod[%s] 0x%016llx\n", method_str(m_state->test_param_methods[i]), m_state->test_inputs[i]);
				else
					printf("\t\tmethod[%s] 0x%08x\n", method_str(m_state->test_param_methods[i]), (uint32_t)m_state->test_inputs[i]);
			}
		}
	}

	bool is_default_pattern = m_state->test_output_count > 0;
	if (m_state->test_output_count > 0) {
		printf("\tOutputs:\n");
		for (int i = 0; i < m_state->test_output_count; i++) {
			uint64_t default_pattern_mask = m_state->test_result_formats[i] == uml::SIZE_SHORT || m_state->test_opcode_size == 4 ? 0xffffffff : 0xffffffffffffffff;

			if (m_state->test_result_outputs[i] != DEFAULT_PATTERN && m_state->test_result_outputs[i] != (DEFAULT_PATTERN & default_pattern_mask))
				is_default_pattern = false;

			if (m_state->test_result_formats[i] == uml::SIZE_SHORT)
			{
				m_state->test_expected_outputs[i] &= 0xffffffff;
				m_state->test_result_outputs[i] &= 0xffffffff;

				uint32_t normalized_expected = normalize_nan_inf(m_state->test_expected_outputs[i], uml::SIZE_SHORT);
				uint32_t normalized_result = normalize_nan_inf(m_state->test_result_outputs[i], uml::SIZE_SHORT);

				uint64_t normalized_exected_low = u2f(normalized_expected) < 0.0 ? f2u(u2f(normalized_expected) + FLOAT_ALLOWED_DELTA) : f2u(u2f(normalized_expected) - FLOAT_ALLOWED_DELTA);
				uint64_t normalized_exected_high = u2f(normalized_expected) < 0.0 ? f2u(u2f(normalized_expected) - FLOAT_ALLOWED_DELTA) : f2u(u2f(normalized_expected) + FLOAT_ALLOWED_DELTA);

				bool is_valid = (normalized_result >= normalized_exected_low && normalized_result <= normalized_exected_high) || (normalized_expected == normalized_result);

				printf("\t\tmethod[%s] expected[%08x %f] result[%08x %f] valid[%d]\n",
					method_str(m_state->test_result_methods[i]),
					(uint32_t)m_state->test_expected_outputs[i], u2f(m_state->test_expected_outputs[i]),
					(uint32_t)m_state->test_result_outputs[i], u2f(m_state->test_result_outputs[i]),
					is_valid
				);
				valid = valid && is_valid;
			}
			else if (m_state->test_result_formats[i] == uml::SIZE_DOUBLE)
			{
				uint64_t normalized_expected = normalize_nan_inf(m_state->test_expected_outputs[i], uml::SIZE_DOUBLE);
				uint64_t normalized_result = normalize_nan_inf(m_state->test_result_outputs[i], uml::SIZE_DOUBLE);

				uint64_t normalized_exected_low = u2d(normalized_expected) < 0 ? d2u(u2d(normalized_expected) + DOUBLE_ALLOWED_DELTA) : d2u(u2d(normalized_expected) - DOUBLE_ALLOWED_DELTA);
				uint64_t normalized_exected_high = u2d(normalized_expected) < 0 ? d2u(u2d(normalized_expected) - DOUBLE_ALLOWED_DELTA) : d2u(u2d(normalized_expected) + DOUBLE_ALLOWED_DELTA);

				bool is_valid = (normalized_result >= normalized_exected_low && normalized_result <= normalized_exected_high) || (normalized_expected == normalized_result);

				printf("\t\tmethod[%s] expected[%016llx %lf] result[%016llx %lf] valid[%d]\n",
					method_str(m_state->test_result_methods[i]),
					m_state->test_expected_outputs[i], u2d(m_state->test_expected_outputs[i]),
					m_state->test_result_outputs[i], u2d(m_state->test_result_outputs[i]),
					is_valid
				);
				valid = valid && is_valid;
			}
			else
			{
				if (m_state->test_opcode_size == 8)
				{
					printf("\t\tmethod[%s] expected[0x%016llx] result[0x%016llx] valid[%d]\n", method_str(m_state->test_result_methods[i]), m_state->test_expected_outputs[i], m_state->test_result_outputs[i], m_state->test_expected_outputs[i] == m_state->test_result_outputs[i]);
					valid = valid && m_state->test_expected_outputs[i] == m_state->test_result_outputs[i];
				}
				else
				{
					m_state->test_expected_outputs[i] &= 0xffffffff;
					m_state->test_result_outputs[i] &= 0xffffffff;

					printf("\t\tmethod[%s] expected[0x%08x] result[0x%08x] valid[%d]\n", method_str(m_state->test_result_methods[i]), (uint32_t)m_state->test_expected_outputs[i], (uint32_t)m_state->test_result_outputs[i], (uint32_t)m_state->test_expected_outputs[i] == (uint32_t)m_state->test_result_outputs[i]);
					valid = valid && (uint32_t)m_state->test_expected_outputs[i] == (uint32_t)m_state->test_result_outputs[i];
				}
			}
		}
	}

	const auto outflags = uml::instruction::get_outflags(m_state->test_opcode);
	const auto modflags = uml::instruction::get_modflags(m_state->test_opcode);
	const auto is_conditional = uml::instruction::get_conditional(m_state->test_opcode);

	printf("\tStatus:\n");
	printf("\t\toutflags[%02x] modflags[%02x] tested[%02x] conditional[%d]\n", outflags, modflags, m_state->test_flag_combo, is_conditional);
	uint32_t expected_flags, modified_flags;

	if (m_state->test_expected_status == FLAGS_UNCHANGED)
	{
		expected_flags = m_state->test_initial_status;
		modified_flags = m_state->test_result_status;
	}
	else if (m_state->test_expected_status & FLAGS_UNDEFINED_OTHER)
	{
		expected_flags = m_state->test_expected_status & ~FLAGS_UNDEFINED_OTHER;
		modified_flags = m_state->test_result_status & m_state->test_expected_status & ~FLAGS_UNDEFINED_OTHER;
	}
	else
	{
		expected_flags = modflags == uml::OPFLAGS_NONE || is_conditional ? m_state->test_initial_status : m_state->test_expected_status;
		modified_flags = modflags == uml::OPFLAGS_NONE || is_conditional ? m_state->test_result_status : m_state->test_result_status & outflags;
	}

	if (!is_conditional)
		expected_flags &= m_state->test_flag_combo;

	bool modified_flags_valid = expected_flags == modified_flags;

	bool conditions_met = false;
	switch (m_state->condition)
	{
		case uml::COND_ALWAYS:
			conditions_met = true;
			break;

		case uml::COND_Z:
			conditions_met = (m_state->test_initial_status & uml::FLAG_Z) != 0;
			break;

		case uml::COND_NZ:
			conditions_met = (m_state->test_initial_status & uml::FLAG_Z) == 0;
			break;

		case uml::COND_S:
			conditions_met = (m_state->test_initial_status & uml::FLAG_S) != 0;
			break;

		case uml::COND_NS:
			conditions_met = (m_state->test_initial_status & uml::FLAG_S) == 0;
			break;

		case uml::COND_C:
			conditions_met = (m_state->test_initial_status & uml::FLAG_C) != 0;
			break;

		case uml::COND_NC:
			conditions_met = (m_state->test_initial_status & uml::FLAG_C) == 0;
			break;

		case uml::COND_V:
			conditions_met = (m_state->test_initial_status & uml::FLAG_V) != 0;
			break;

		case uml::COND_NV:
			conditions_met = (m_state->test_initial_status & uml::FLAG_V) == 0;
			break;

		case uml::COND_U:
			conditions_met = (m_state->test_initial_status & uml::FLAG_U) != 0;
			break;

		case uml::COND_NU:
			conditions_met = (m_state->test_initial_status & uml::FLAG_U) == 0;
			break;

		case uml::COND_A:
			conditions_met = (m_state->test_initial_status & (uml::FLAG_C | uml::FLAG_Z)) == 0;
			break;

		case uml::COND_BE:
			conditions_met = (m_state->test_initial_status & (uml::FLAG_C | uml::FLAG_Z)) != 0;
			break;

		case uml::COND_G:
			conditions_met = (!!(m_state->test_initial_status & uml::FLAG_S) == !!(m_state->test_initial_status & uml::FLAG_V)) && !(m_state->test_initial_status & uml::FLAG_Z);
			break;

		case uml::COND_LE:
			conditions_met = (!!(m_state->test_initial_status & uml::FLAG_S) != !!(m_state->test_initial_status & uml::FLAG_V)) || (m_state->test_initial_status & uml::FLAG_Z);
			break;

		case uml::COND_L:
			conditions_met = !!(m_state->test_initial_status & uml::FLAG_S) != !!(m_state->test_initial_status & uml::FLAG_V);
			break;

		case uml::COND_GE:
			conditions_met = !!(m_state->test_initial_status & uml::FLAG_S) == !!(m_state->test_initial_status & uml::FLAG_V);
			break;
	}

	if (conditions_met)
		valid = valid && !is_default_pattern;
	else
		valid = is_default_pattern;

	printf("\t\tinitial[%02x] expected[%02x (%02x)] result[%02x (%02x)] valid[%d]\n", m_state->test_initial_status, expected_flags, m_state->test_expected_status, modified_flags, m_state->test_result_status, modified_flags_valid);

	valid = valid && modified_flags_valid;
	printf("%s\n\n", valid ? "PASSED" : "FAILED");
}

const char *drctester_cpu_device::method_str(uint32_t method)
{
	if (method == METHOD_MEM)
		return "mem";
	else if (method == METHOD_REG)
		return "reg";
	else if (method == METHOD_IMM)
		return "imm";
	else if (method == METHOD_MAPVAR)
		return "mapvar";
	return "unk";
}

uint64_t drctester_cpu_device::normalize_nan_inf(uint64_t value, uint64_t size)
{
	if (size == uml::SIZE_DOUBLE)
	{
		// double
		if (value == d2u(std::numeric_limits<double>::quiet_NaN()))
			return d2u(-std::numeric_limits<double>::quiet_NaN());
		else if (value == d2u(std::numeric_limits<double>::signaling_NaN()))
			return d2u(-std::numeric_limits<double>::quiet_NaN());
		else if (value == d2u(-std::numeric_limits<double>::signaling_NaN()))
			return d2u(-std::numeric_limits<double>::quiet_NaN());
		else if (value == d2u(std::numeric_limits<double>::infinity()))
			return d2u(-std::numeric_limits<double>::infinity());
	}
	else
	{
		// float
		if (value == f2u(std::numeric_limits<float>::quiet_NaN()))
			return f2u(-std::numeric_limits<float>::quiet_NaN());
		else if (value == f2u(std::numeric_limits<float>::signaling_NaN()))
			return f2u(-std::numeric_limits<float>::quiet_NaN());
		else if (value == f2u(-std::numeric_limits<float>::signaling_NaN()))
			return f2u(-std::numeric_limits<float>::quiet_NaN());
		else if (value == f2u(std::numeric_limits<float>::infinity()))
			return f2u(-std::numeric_limits<float>::infinity());
	}

	return value;
}

void drctester_cpu_device::TEST_ENTRY_1_NORET_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM,uint64_t  __CARRY, uint64_t __FLAGS, uint64_t __PARAM_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM};
	const uint64_t input_methods[] = {__PARAM_METHOD};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), 0, __CARRY, __FLAGS, inputs, nullptr, input_methods, nullptr, nullptr, nullptr, __FLAG_COMBO);
	if (__PARAM_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM); }
	else if (__PARAM_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM); }
	if (__PARAM_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_value[0])); }
	else if (__PARAM_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0); }
	else if (__PARAM_METHOD == METHOD_IMM) { block.append().configure(__OPCODE, __SIZE, __PARAM); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_2_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM};
	const uint64_t input_methods[] = {__PARAM_METHOD};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), __CARRY, __FLAGS, inputs, results, input_methods, result_methods, nullptr, nullptr, __FLAG_COMBO);
	if (__PARAM_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM); }
	else if (__PARAM_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM); }
	if (__PARAM_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0])); }
	else if (__PARAM_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0])); }
	else if (__PARAM_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0); }
	else if (__PARAM_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0); }
	else if (__PARAM_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM); }
	else if (__PARAM_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM); }
	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I0); }
	generate_test_end(block, __FLAG_COMBO);
}


void drctester_cpu_device::TEST_ENTRY_2_NORET_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2};
	const uint64_t input_methods[] = {__PARAM1_METHOD, __PARAM2_METHOD};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), 0, __CARRY, __FLAGS, inputs, nullptr, input_methods, nullptr, nullptr, nullptr, __FLAG_COMBO);
	if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
	else if (__PARAM1_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM1); }
	if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
	else if (__PARAM2_METHOD == METHOD_REG) { UML_DMOV(block, uml::I1, __PARAM2); }
	if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_value[0]), uml::I1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, __PARAM1, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM) { block.append().configure(__OPCODE, __SIZE, __PARAM1, __PARAM2); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_3_SEXT_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2 };
	const uint64_t input_methods[] = {__PARAM1_METHOD, METHOD_IMM};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), __CARRY, __FLAGS, inputs, results, input_methods, result_methods, nullptr, nullptr, __FLAG_COMBO);
	if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
	else if (__PARAM1_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM1); }
	if (__PARAM1_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, __PARAM2); }
	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I0); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_3_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2};
	const uint64_t input_methods[] = {__PARAM1_METHOD, __PARAM2_METHOD};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), __CARRY, __FLAGS, inputs, results, input_methods, result_methods, nullptr, nullptr, __FLAG_COMBO);
	if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
	else if (__PARAM1_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM1); }
	if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
	else if (__PARAM2_METHOD == METHOD_REG) { UML_DMOV(block, uml::I1, __PARAM2); }
	if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::I1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::I1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, __PARAM2); }
	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I0); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_4_SINGLE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2};
	const uint64_t input_methods[] = {__PARAM1_METHOD, __PARAM2_METHOD};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	uint32_t result_count = 0;
	if (__RESULT != UNDEFINED)
		result_count++;
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), result_count, __CARRY, __FLAGS, inputs, results, input_methods, result_methods, nullptr, nullptr, __FLAG_COMBO);
	if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
	else if (__PARAM1_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM1); }
	if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
	else if (__PARAM2_METHOD == METHOD_REG) { UML_DMOV(block, uml::I1, __PARAM2); }
	if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::I1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::I1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::I0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::I0, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, __PARAM1, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, __PARAM1, __PARAM2); }
	if (__RESULT != UNDEFINED) {
		if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
		else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I0); }
	}
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_4_DOUBLE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT1, uint64_t __RESULT2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __RESULT1_METHOD, uint64_t __RESULT2_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2};
	const uint64_t input_methods[] = {__PARAM1_METHOD, __PARAM2_METHOD};
	const uint64_t results[] = {__RESULT1, __RESULT2};
	const uint64_t result_methods[] = {__RESULT1_METHOD, __RESULT2_METHOD};
	uint32_t result_count = 0;
	if (__RESULT1 != UNDEFINED)
		result_count++;
	if (__RESULT2 != UNDEFINED)
		result_count++;
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), result_count, __CARRY, __FLAGS, inputs, results, input_methods, result_methods, nullptr, nullptr, __FLAG_COMBO);
	if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
	else if (__PARAM1_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM1); }
	if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
	else if (__PARAM2_METHOD == METHOD_REG) { UML_DMOV(block, uml::I1, __PARAM2); }
	if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[1]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I1, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_result_value[1]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[1]), uml::mem(&m_state->test_mem_value[0]), uml::I1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I1, uml::mem(&m_state->test_mem_value[0]), uml::I1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_result_value[1]), uml::mem(&m_state->test_mem_value[0]), uml::I1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1, uml::mem(&m_state->test_mem_value[0]), uml::I1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[1]), uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I1, uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_result_value[1]), uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1, uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[1]), uml::I0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I1, uml::I0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_result_value[1]), uml::I0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1, uml::I0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[1]), uml::I0, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I1, uml::I0, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_result_value[1]), uml::I0, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1, uml::I0, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[1]), uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I1, uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_result_value[1]), uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1, uml::I0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[1]), __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I1, __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_result_value[1]), __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1, __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[1]), __PARAM1, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I1, __PARAM1, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_result_value[1]), __PARAM1, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1, __PARAM1, uml::I1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_result_value[1]), __PARAM1, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_MEM && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I1, __PARAM1, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_result_value[1]), __PARAM1, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT1_METHOD == METHOD_REG && __RESULT2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I1, __PARAM1, __PARAM2); }
	if (__RESULT1 != UNDEFINED) {
		if (__RESULT1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
		else if (__RESULT1_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I0); }
	}
	if (__RESULT2 != UNDEFINED) {
		if (__RESULT2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[1]), uml::mem(&m_state->test_mem_result_value[1])); }
		else if (__RESULT2_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[1]), uml::I1); }
	}
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_4_TRIPLE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD,uint64_t  __PARAM2_METHOD, uint64_t __PARAM3_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2, __PARAM3};
	const uint64_t input_methods[] = {__PARAM1_METHOD, __PARAM2_METHOD, __PARAM3_METHOD};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), __CARRY, __FLAGS, inputs, results, input_methods, result_methods, nullptr, nullptr, __FLAG_COMBO);
	if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
	else if (__PARAM1_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM1); }
	if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
	else if (__PARAM2_METHOD == METHOD_REG) { UML_DMOV(block, uml::I1, __PARAM2); }
	if (__PARAM3_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[2]), __PARAM3); }
	else if (__PARAM3_METHOD == METHOD_REG) { UML_DMOV(block, uml::I2, __PARAM3); }
	if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::mem(&m_state->test_mem_value[0]), __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, uml::I0, __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __PARAM1, __PARAM2, __PARAM3); }
	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I0); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_4_QUAD_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __RESULT_IN, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __PARAM3_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2, __PARAM3, __RESULT_IN};
	const uint64_t input_methods[] = {__PARAM1_METHOD, __PARAM2_METHOD, __PARAM3_METHOD, __RESULT_METHOD};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), __CARRY, __FLAGS, inputs, results, input_methods, result_methods, nullptr, nullptr, __FLAG_COMBO);
	if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
	else if (__PARAM1_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM1); }
	if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
	else if (__PARAM2_METHOD == METHOD_REG) { UML_DMOV(block, uml::I1, __PARAM2); }
	if (__PARAM3_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[2]), __PARAM3); }
	else if (__PARAM3_METHOD == METHOD_REG) { UML_DMOV(block, uml::I2, __PARAM3); }
	if (__PARAM3_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_result_value[0]), __RESULT_IN); }
	else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::I3, __RESULT_IN); }
	if (__RESULT_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::mem(&m_state->test_mem_value[0]), uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::mem(&m_state->test_mem_value[0]), uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::mem(&m_state->test_mem_value[0]), uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::mem(&m_state->test_mem_value[0]), __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::mem(&m_state->test_mem_value[0]), __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::mem(&m_state->test_mem_value[0]), __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::I0, uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::I0, uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::I0, uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::I0, uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::I0, uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::I0, uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::I0, __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::I0, __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, uml::I0, __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, __PARAM1, uml::mem(&m_state->test_mem_value[1]), uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, __PARAM1, uml::mem(&m_state->test_mem_value[1]), uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, __PARAM1, uml::mem(&m_state->test_mem_value[1]), __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, __PARAM1, uml::I1, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, __PARAM1, uml::I1, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, __PARAM1, uml::I1, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, __PARAM1, __PARAM2, uml::mem(&m_state->test_mem_value[2])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, __PARAM1, __PARAM2, uml::I2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __PARAM3_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I3, __PARAM1, __PARAM2, __PARAM3); }
	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I3); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_FLOAT_2_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM_METHOD, uint64_t __RESULT_METHOD, uint64_t __PARAM_FORMAT, uint64_t __RESULT_FORMAT, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM};
	const uint64_t input_methods[] = {__PARAM_METHOD};
	const uint64_t input_formats[] = {__PARAM_FORMAT};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	const uint64_t output_formats[] = {__RESULT_FORMAT};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), __CARRY, __FLAGS, inputs, results, input_methods, result_methods, input_formats, output_formats, __FLAG_COMBO);
	if (__PARAM_FORMAT == uml::SIZE_DOUBLE) {
		if (__PARAM_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM); }
		else if (__PARAM_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM);
			UML_FDCOPYI(block, uml::F0, uml::I0);
		}
	} else {
		if (__PARAM_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM); }
		else if (__PARAM_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM);
			UML_FSCOPYI(block, uml::F0, uml::I0);
		}
	}
	if (__PARAM_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0])); }
	else if (__PARAM_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F1, uml::mem(&m_state->test_mem_value[0])); }
	else if (__PARAM_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::F0); }
	else if (__PARAM_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F1, uml::F0); }
	else if (__PARAM_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM); }
	else if (__PARAM_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F1, __PARAM); }

	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG && __RESULT_FORMAT == uml::SIZE_DOUBLE) { UML_ICOPYFD(block, uml::mem(&m_state->test_result_outputs[0]), uml::F1); }
	else if (__RESULT_METHOD == METHOD_REG && __RESULT_FORMAT == uml::SIZE_SHORT) { UML_ICOPYFS(block, uml::mem(&m_state->test_result_outputs[0]), uml::F1); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_FLOAT_CMP_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2};
	const uint64_t input_methods[] = {__PARAM1_METHOD, __PARAM2_METHOD};
	const uint64_t input_formats[] = {__PARAM1_FORMAT, __PARAM2_FORMAT};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), 0, __CARRY, __FLAGS, inputs, nullptr, input_methods, nullptr, input_formats, nullptr, __FLAG_COMBO);
	if (__PARAM1_FORMAT == uml::SIZE_DOUBLE) {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM1);
			UML_FDCOPYI(block, uml::F0, uml::I0);
		}
	} else {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM1);
			UML_FSCOPYI(block, uml::F0, uml::I0);
		}
	}
	if (__PARAM2_FORMAT == uml::SIZE_DOUBLE) {
		if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
		else if (__PARAM2_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I1, __PARAM2);
			UML_FDCOPYI(block, uml::F1, uml::I1);
		}
	} else {
		if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
		else if (__PARAM2_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I1, __PARAM2);
			UML_FSCOPYI(block, uml::F1, uml::I1);
		}
	}
	if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_value[0]), uml::F1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::F0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, uml::F1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM) { block.append().configure(__OPCODE, __SIZE, uml::F0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, __PARAM1, uml::F1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM) { block.append().configure(__OPCODE, __SIZE, __PARAM1, __PARAM2); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_FLOAT_3_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __RESULT_METHOD, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT, uint64_t __RESULT_FORMAT, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2};
	const uint64_t input_methods[] = {__PARAM1_METHOD, __PARAM2_METHOD};
	const uint64_t input_formats[] = {__PARAM1_FORMAT, __PARAM2_FORMAT};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	const uint64_t output_formats[] = {__RESULT_FORMAT};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), __CARRY, __FLAGS, inputs, results, input_methods, result_methods, input_formats, output_formats, __FLAG_COMBO);
	if (__PARAM1_FORMAT == uml::SIZE_DOUBLE) {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM1);
			UML_FDCOPYI(block, uml::F0, uml::I0);
		}
	} else {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM1);
			UML_FSCOPYI(block, uml::F0, uml::I0);
		}
	}
	if (__PARAM2_FORMAT == uml::SIZE_DOUBLE) {
		if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
		else if (__PARAM2_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I1, __PARAM2);
			UML_FDCOPYI(block, uml::F1, uml::I1);
		}
	} else {
		if (__PARAM2_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[1]), __PARAM2); }
		else if (__PARAM2_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I1, __PARAM2);
			UML_FSCOPYI(block, uml::F1, uml::I1);
		}
	}
	if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, uml::mem(&m_state->test_mem_value[0]), uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), uml::F1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, uml::mem(&m_state->test_mem_value[0]), uml::F1); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::F0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, uml::F0, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::F0, uml::F1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, uml::F0, uml::F1); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::F0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, uml::F0, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, __PARAM1, uml::mem(&m_state->test_mem_value[1])); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, uml::F1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, __PARAM1, uml::F1); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __PARAM2_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F0, __PARAM1, __PARAM2); }
	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG && __RESULT_FORMAT == uml::SIZE_DOUBLE) { UML_ICOPYFD(block, uml::mem(&m_state->test_result_outputs[0]), uml::F0); }
	else if (__RESULT_METHOD == METHOD_REG && __RESULT_FORMAT == uml::SIZE_SHORT) { UML_ICOPYFS(block, uml::mem(&m_state->test_result_outputs[0]), uml::F0); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_FLOAT_3_SIZE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __RESULT_METHOD, uint64_t __PARAM1_FORMAT, uint64_t __RESULT_FORMAT, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2};
	const uint64_t input_methods[] = {__PARAM1_METHOD, METHOD_IMM};
	const uint64_t input_formats[] = {__PARAM1_FORMAT, 0};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	const uint64_t output_formats[] = {__RESULT_FORMAT};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), __CARRY, __FLAGS, inputs, results, input_methods, result_methods, input_formats, output_formats, __FLAG_COMBO);
	if (__PARAM1_FORMAT == uml::SIZE_DOUBLE) {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM1);
			UML_FDCOPYI(block, uml::F0, uml::I0);
		}
	} else if (__PARAM1_FORMAT == uml::SIZE_SHORT) {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM1);
			UML_FSCOPYI(block, uml::F0, uml::I0);
		}
	} else {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM1); }
	}
	const auto param = __PARAM1_FORMAT == uml::SIZE_SHORT || __PARAM1_FORMAT == uml::SIZE_DOUBLE ? uml::F0 : uml::I0;
	if (__PARAM1_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F1, uml::mem(&m_state->test_mem_value[0]), __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), param, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F1, param, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2); }
	else if (__PARAM1_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F1, __PARAM1, __PARAM2); }
	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG && __RESULT_FORMAT == uml::SIZE_DOUBLE) { UML_ICOPYFD(block, uml::mem(&m_state->test_result_outputs[0]), uml::F1); }
	else if (__RESULT_METHOD == METHOD_REG && __RESULT_FORMAT == uml::SIZE_SHORT) { UML_ICOPYFS(block, uml::mem(&m_state->test_result_outputs[0]), uml::F1); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_FLOAT_4_SIZE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __RESULT_METHOD, uint64_t __PARAM1_FORMAT, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__PARAM1, __PARAM2, __PARAM3};
	const uint64_t input_methods[] = {__PARAM1_METHOD, METHOD_IMM, METHOD_IMM};
	const uint64_t input_formats[] = {__PARAM1_FORMAT, 0, 0};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), __CARRY, __FLAGS, inputs, results, input_methods, result_methods, input_formats, nullptr, __FLAG_COMBO);
	if (__PARAM1_FORMAT == uml::SIZE_DOUBLE) {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM1);
			UML_FDCOPYI(block, uml::F0, uml::I0);
		}
	} else if (__PARAM1_FORMAT == uml::SIZE_SHORT) {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM1);
			UML_FSCOPYI(block, uml::F0, uml::I0);
		}
	} else {
		if (__PARAM1_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM1); }
		else if (__PARAM1_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM1); }
	}
	const auto param = __PARAM1_FORMAT == uml::SIZE_SHORT || __PARAM1_FORMAT == uml::SIZE_DOUBLE ? uml::F0 : uml::I0;
	if (__PARAM1_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I1, uml::mem(&m_state->test_mem_value[0]), __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), param, __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I1, param, __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM1, __PARAM2, __PARAM3); }
	else if (__PARAM1_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I1, __PARAM1, __PARAM2, __PARAM3); }
	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I1); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_1_NORET(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_1_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __CARRY, __FLAGS, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_1_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __CARRY, __FLAGS, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_1_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __CARRY, __FLAGS, METHOD_IMM, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_2(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RETURN, uint64_t __PARAM, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_2_CMP(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 1; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_2_NORET(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_2_NORET_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_3_SEXT(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_3_SEXT_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_3_SEXT_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_SEXT_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_SEXT_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_SEXT_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_SEXT_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_3(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_4_SINGLE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_SINGLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_4_DOUBLE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT1, uint64_t __RESULT2, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_DOUBLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT1, __RESULT2, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_4_TRIPLE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_TRIPLE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
#endif
	}
}


void drctester_cpu_device::TEST_ENTRY_4_QUAD(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __RESULT_IN, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __CARRY, uint64_t __FLAGS)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_IMM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_IMM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_MEM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_REG, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_4_QUAD_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT_IN, __RESULT, __CARRY, __FLAGS, METHOD_IMM, METHOD_IMM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx]);
#endif
	}
}


void drctester_cpu_device::TEST_ENTRY_FLOAT_2(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RETURN, uint64_t __PARAM, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __RESULT_FORMAT, uint64_t __PARAM_FORMAT)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_FLOAT_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, __PARAM_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_FLOAT_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, __PARAM_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, __PARAM_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_2_GENERATOR(block, __OPCODE, __SIZE, __PARAM, __RETURN, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, __PARAM_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_FLOAT_CMP(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT)
{
	for (auto comboidx = 1; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_FLOAT_CMP_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, __PARAM1_FORMAT, __PARAM2_FORMAT, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_FLOAT_CMP_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, __PARAM1_FORMAT, __PARAM2_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_CMP_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, __PARAM1_FORMAT, __PARAM2_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_CMP_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, __PARAM1_FORMAT, __PARAM2_FORMAT, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_FLOAT_3(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __RESULT_FORMAT, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_FLOAT_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_REG, __PARAM1_FORMAT, __PARAM2_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_FLOAT_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_MEM, __PARAM1_FORMAT, __PARAM2_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, METHOD_REG, __PARAM1_FORMAT, __PARAM2_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_MEM, __PARAM1_FORMAT, __PARAM2_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, METHOD_REG, __PARAM1_FORMAT, __PARAM2_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_MEM, __PARAM1_FORMAT, __PARAM2_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, METHOD_REG, __PARAM1_FORMAT, __PARAM2_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_3_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, METHOD_MEM, __PARAM1_FORMAT, __PARAM2_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
#endif
	}
}

///////

void drctester_cpu_device::TEST_ENTRY_MOV_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __PARAM_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO, uml::condition_t __CONDITIONAL_FLAGS)
{
	const uint64_t inputs[] = {__PARAM};
	const uint64_t input_methods[] = {__PARAM_METHOD};
	const uint64_t outputs[] = {__PARAM};
	const uint64_t output_methods[] = {__RESULT_METHOD};

	// if (__CONDITIONAL_FLAGS == uml::COND_NZ && (__FLAG_COMBO & uml::FLAG_Z))
	//  UML_BREAK(block);

	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(outputs), !!(__FLAG_COMBO & uml::FLAG_C), 0, inputs, outputs, input_methods, output_methods, nullptr, nullptr, __FLAG_COMBO, __FLAG_COMBO);

	UML_MOV(block, uml::mem(&m_state->condition), uint32_t(__CONDITIONAL_FLAGS));

	UML_DMOV(block, uml::I1, DEFAULT_PATTERN);
	UML_DMOV(block, uml::mem(&m_state->test_mem_result_value[0]), DEFAULT_PATTERN);

	if (__PARAM_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM); }
	else if (__PARAM_METHOD == METHOD_REG) { UML_DMOV(block, uml::I0, __PARAM); }

	// Initialize the result field to something that isn't the initial value so we can be sure it actually did the move properly
	// if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_result_value[0]), DEFAULT_PATTERN); }
	// else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::I1, DEFAULT_PATTERN); }

	if (__PARAM_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)
		block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __CONDITIONAL_FLAGS);
	else if (__PARAM_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM)
		block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::I0, __CONDITIONAL_FLAGS);
	else if (__PARAM_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_MEM)
		block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __PARAM, __CONDITIONAL_FLAGS);
	else if (__PARAM_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG)
		block.append().configure(__OPCODE, __SIZE, uml::I1, uml::mem(&m_state->test_mem_value[0]), __CONDITIONAL_FLAGS);
	else if (__PARAM_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG)
		block.append().configure(__OPCODE, __SIZE, uml::I1, uml::I0, __CONDITIONAL_FLAGS);
	else if (__PARAM_METHOD == METHOD_IMM && __RESULT_METHOD == METHOD_REG)
		block.append().configure(__OPCODE, __SIZE, uml::I1, __PARAM, __CONDITIONAL_FLAGS);

	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I1); }

	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_MOV(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM)
{
	for (auto condidx = 0; condidx < std::size(conditions); condidx++)
	{
		for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
		{
			TEST_ENTRY_MOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx], conditions[condidx]);
#ifndef UML_BACKEND_TEST_SMALL
			TEST_ENTRY_MOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx], conditions[condidx]);
			TEST_ENTRY_MOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx], conditions[condidx]);
			TEST_ENTRY_MOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx], conditions[condidx]);
			TEST_ENTRY_MOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_IMM, METHOD_REG, FLAG_COMBOS[comboidx], conditions[condidx]);
			TEST_ENTRY_MOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_IMM, METHOD_MEM, FLAG_COMBOS[comboidx], conditions[condidx]);
#endif
		}
	}
}


///////

void drctester_cpu_device::TEST_ENTRY_FMOV_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __PARAM_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO, uml::condition_t __CONDITIONAL_FLAGS)
{
	const auto format = __SIZE == 4 ? uml::SIZE_SHORT : uml::SIZE_DOUBLE;

	const uint64_t inputs[] = {__PARAM};
	const uint64_t input_methods[] = {__PARAM_METHOD};
	const uint64_t input_formats[] = {format};
	const uint64_t results[] = {__PARAM};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	const uint64_t output_formats[] = {format};

	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), !!(__FLAG_COMBO & uml::FLAG_C), 0, inputs, results, input_methods, result_methods, input_formats, output_formats, __FLAG_COMBO, __FLAG_COMBO);

	UML_MOV(block, uml::mem(&m_state->condition), uint32_t(__CONDITIONAL_FLAGS));

	UML_DMOV(block, uml::I0, DEFAULT_PATTERN);
	UML_FDCOPYI(block, uml::F1, uml::I0);
	UML_DMOV(block, uml::mem(&m_state->test_mem_result_value[0]), DEFAULT_PATTERN);

	if (format == uml::SIZE_DOUBLE) {
		if (__PARAM_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM); }
		else if (__PARAM_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM);
			UML_FDCOPYI(block, uml::F0, uml::I0);
		}
	} else {
		if (__PARAM_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_mem_value[0]), __PARAM); }
		else if (__PARAM_METHOD == METHOD_REG) {
			UML_DMOV(block, uml::I0, __PARAM);
			UML_FSCOPYI(block, uml::F0, uml::I0);
		}
	}

	if (__PARAM_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::mem(&m_state->test_mem_value[0]), __CONDITIONAL_FLAGS); }
	else if (__PARAM_METHOD == METHOD_MEM && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F1, uml::mem(&m_state->test_mem_value[0]), __CONDITIONAL_FLAGS); }
	else if (__PARAM_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_MEM) { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), uml::F0, __CONDITIONAL_FLAGS); }
	else if (__PARAM_METHOD == METHOD_REG && __RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::F1, uml::F0, __CONDITIONAL_FLAGS); }

	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG && format == uml::SIZE_DOUBLE) { UML_ICOPYFD(block, uml::mem(&m_state->test_result_outputs[0]), uml::F1); }
	else if (__RESULT_METHOD == METHOD_REG && format == uml::SIZE_SHORT) { UML_ICOPYFS(block, uml::mem(&m_state->test_result_outputs[0]), uml::F1); }

	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_FMOV(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM)
{
	for (auto condidx = 0; condidx < std::size(conditions); condidx++)
	{
		for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
		{
			TEST_ENTRY_FMOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_REG, METHOD_REG, FLAG_COMBOS[comboidx], conditions[condidx]);
#ifndef UML_BACKEND_TEST_SMALL
			TEST_ENTRY_FMOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_REG, METHOD_MEM, FLAG_COMBOS[comboidx], conditions[condidx]);
			TEST_ENTRY_FMOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_MEM, METHOD_REG, FLAG_COMBOS[comboidx], conditions[condidx]);
			TEST_ENTRY_FMOV_GENERATOR(block, __OPCODE, __SIZE, __PARAM, METHOD_MEM, METHOD_MEM, FLAG_COMBOS[comboidx], conditions[condidx]);
#endif
		}
	}
}

void drctester_cpu_device::TEST_ENTRY_FLOAT_3_SIZE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __RESULT_FORMAT, uint64_t __PARAM1_FORMAT)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_FLOAT_3_SIZE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, __PARAM1_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_FLOAT_3_SIZE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, __PARAM1_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_3_SIZE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, __PARAM1_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_3_SIZE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, __PARAM1_FORMAT, __RESULT_FORMAT, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_FLOAT_4_SIZE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_FORMAT)
{
	for (auto comboidx = 0; comboidx < std::size(FLAG_COMBOS); comboidx++)
	{
		TEST_ENTRY_FLOAT_4_SIZE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_REG, __PARAM1_FORMAT, FLAG_COMBOS[comboidx]);
#ifndef UML_BACKEND_TEST_SMALL
		TEST_ENTRY_FLOAT_4_SIZE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_REG, METHOD_MEM, __PARAM1_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_4_SIZE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_REG, __PARAM1_FORMAT, FLAG_COMBOS[comboidx]);
		TEST_ENTRY_FLOAT_4_SIZE_GENERATOR(block, __OPCODE, __SIZE, __PARAM1, __PARAM2, __PARAM3, __RESULT, __CARRY, __FLAGS, METHOD_MEM, METHOD_MEM, __PARAM1_FORMAT, FLAG_COMBOS[comboidx]);
#endif
	}
}

void drctester_cpu_device::TEST_ENTRY_COND_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uml::condition_t __COND, uint64_t __RESULT, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO)
{
	const uint64_t inputs[] = {__COND};
	const uint64_t input_methods[] = {METHOD_IMM};
	const uint64_t results[] = {__RESULT};
	const uint64_t result_methods[] = {__RESULT_METHOD};
	generate_test_start(block, __OPCODE, __SIZE, std::size(inputs), std::size(results), -1, __FLAG_COMBO, inputs, results, input_methods, result_methods, nullptr, nullptr, __FLAG_COMBO, __FLAG_COMBO);
	if (__RESULT_METHOD == METHOD_MEM)      { block.append().configure(__OPCODE, __SIZE, uml::mem(&m_state->test_mem_result_value[0]), __COND); }
	else if (__RESULT_METHOD == METHOD_REG) { block.append().configure(__OPCODE, __SIZE, uml::I0, __COND); }
	if (__RESULT_METHOD == METHOD_MEM) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0])); }
	else if (__RESULT_METHOD == METHOD_REG) { UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::I0); }
	generate_test_end(block, __FLAG_COMBO);
}

void drctester_cpu_device::TEST_ENTRY_COND(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uml::condition_t __COND, uint64_t __RETURN, uint64_t __INITIAL_FLAGS)
{
	TEST_ENTRY_COND_GENERATOR(block, __OPCODE, __SIZE, __COND, __RETURN, METHOD_REG, __INITIAL_FLAGS & uml::OPFLAGS_ALL);
	TEST_ENTRY_COND_GENERATOR(block, __OPCODE, __SIZE, __COND, __RETURN, METHOD_MEM, __INITIAL_FLAGS & uml::OPFLAGS_ALL);
}

void drctester_cpu_device::TEST_MAPVAR_CONSTANT(drcuml_block &block, uml::parameter mapvar, uint32_t value)
{
	const uint64_t inputs[] = {value};
	const uint64_t input_methods[] = {METHOD_MAPVAR};
	const uint64_t results[] = {value};
	const uint64_t result_methods[] = {METHOD_MAPVAR};

	generate_test_start(block, uml::OP_MAPVAR, 4, std::size(inputs), std::size(results), -1, 0, inputs, results, input_methods, result_methods, nullptr, nullptr, 0, 0);

	UML_MAPVAR(block, mapvar, value);
	UML_ADD(block, uml::I0, mapvar, 0);

	UML_DMOV(block, uml::mem(&m_state->test_mem_result_value[0]), uml::I0);
	UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0]));

	generate_test_end(block, 0);
}

void drctester_cpu_device::TEST_MAPVAR_RECOVER(drcuml_block &block, uml::parameter mapvar, uint32_t value, int step)
{
	const uint64_t inputs[] = {value};
	const uint64_t input_methods[] = {METHOD_MAPVAR};
	const uint64_t results[] = {value};
	const uint64_t result_methods[] = {METHOD_MAPVAR};

	generate_test_start(block, uml::OP_MAPVAR, 4, std::size(inputs), std::size(results), -1, 0, inputs, results, input_methods, result_methods, nullptr, nullptr, 0, 0);

	UML_MAPVAR(block, mapvar, value);

	UML_CALLH(block, *m_testhandle);

	UML_DMOV(block, uml::mem(&m_state->test_mem_result_value[0]), uml::I0);
	UML_DMOV(block, uml::mem(&m_state->test_result_outputs[0]), uml::mem(&m_state->test_mem_result_value[0]));

	generate_test_end(block, 0);
}

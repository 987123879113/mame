#ifndef __SRC_DEVICES_CPU_DRCTESTER__
#define __SRC_DEVICES_CPU_DRCTESTER__

#include "cpu/drcuml.h"
#include "cpu/drcumlsh.h"

class drctester_cpu_device : public cpu_device
{
public:
	drctester_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	void func_display_result();

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_stop() override ATTR_COLD;

	virtual void execute_run() override;

	virtual space_config_vector memory_space_config() const override;

protected:
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

private:
	static constexpr uint64_t UNDEFINED = 0xdeadbeefdeadbeef;
	static constexpr int64_t FLAGS_UNCHANGED = 0x12345678;
	static constexpr uint64_t FLAGS_UNDEFINED_OTHER = 0x1000;

	enum {
		METHOD_MEM = 1,
		METHOD_REG,
		METHOD_IMM,
		METHOD_MAPVAR,
	};

	static constexpr uml::condition_t conditions[] = {
		uml::COND_ALWAYS,
		uml::COND_Z,
		uml::COND_NZ,
		uml::COND_S,
		uml::COND_NS,
		uml::COND_C,
		uml::COND_NC,
		uml::COND_V,
		uml::COND_NV,
		uml::COND_U,
		uml::COND_NU,
		uml::COND_A,
		uml::COND_BE,
		uml::COND_G,
		uml::COND_LE,
		uml::COND_L,
		uml::COND_GE,
	};

	const char* condition_strings[16] = {
		"COND_Z",
		"COND_NZ",
		"COND_S",
		"COND_NS",
		"COND_C",
		"COND_NC",
		"COND_V",
		"COND_NV",
		"COND_U",
		"COND_NU",
		"COND_A",
		"COND_BE",
		"COND_G",
		"COND_LE",
		"COND_L",
		"COND_GE"
	};

	static constexpr uint64_t DEFAULT_PATTERN = 0x5731573113751375;

	static constexpr uint32_t FLAG_COMBOS[] = {
		0,
		uml::FLAG_C,
		uml::FLAG_S,
		uml::FLAG_U,
		uml::FLAG_V,
		uml::FLAG_Z,
		uml::FLAG_C | uml::FLAG_S,
		uml::FLAG_C | uml::FLAG_U,
		uml::FLAG_C | uml::FLAG_V,
		uml::FLAG_C | uml::FLAG_Z,
		uml::FLAG_S | uml::FLAG_U,
		uml::FLAG_V | uml::FLAG_S,
		uml::FLAG_V | uml::FLAG_U,
		uml::FLAG_V | uml::FLAG_Z,
		uml::FLAG_Z | uml::FLAG_S,
		uml::FLAG_Z | uml::FLAG_U,
		uml::FLAG_C | uml::FLAG_V | uml::FLAG_U,
		uml::FLAG_C | uml::FLAG_V | uml::FLAG_Z,
		uml::FLAG_C | uml::FLAG_S | uml::FLAG_U,
		uml::FLAG_C | uml::FLAG_V | uml::FLAG_S,
		uml::FLAG_C | uml::FLAG_Z | uml::FLAG_S,
		uml::FLAG_C | uml::FLAG_Z | uml::FLAG_U,
		uml::FLAG_V | uml::FLAG_S | uml::FLAG_U,
		uml::FLAG_V | uml::FLAG_Z | uml::FLAG_S,
		uml::FLAG_V | uml::FLAG_Z | uml::FLAG_U,
		uml::FLAG_Z | uml::FLAG_S | uml::FLAG_U,
		uml::FLAG_C | uml::FLAG_V | uml::FLAG_S | uml::FLAG_U,
		uml::FLAG_C | uml::FLAG_V | uml::FLAG_Z | uml::FLAG_S,
		uml::FLAG_C | uml::FLAG_V | uml::FLAG_Z | uml::FLAG_U,
		uml::FLAG_C | uml::FLAG_Z | uml::FLAG_S | uml::FLAG_U,
		uml::FLAG_V | uml::FLAG_Z | uml::FLAG_S | uml::FLAG_U,
		uml::FLAG_C | uml::FLAG_V | uml::FLAG_Z | uml::FLAG_S | uml::FLAG_U
	};

	// Data that needs to be stored close to the generated DRC code
	struct internal_drc_state
	{
		int m_icount;

		uint32_t pc;
		uint32_t test_step;

		uint32_t test_num;
		uint32_t test_opcode;
		uint32_t test_opcode_size;
		uint32_t test_input_count;
		uint32_t test_output_count;
		uint32_t test_flag_combo;

		uint32_t test_initial_status;
		uint32_t test_expected_status;
		uint32_t test_result_status;
		uint32_t test_counter;

		uint32_t condition;

		uint64_t test_inputs[4];
		uint64_t test_expected_outputs[4];
		uint64_t test_result_outputs[4];
		uint64_t test_mem_value[4];
		uint64_t test_mem_result_value[4];
		uint64_t test_param_methods[4];
		uint64_t test_result_methods[4];
		uint64_t test_param_formats[4];
		uint64_t test_result_formats[4];

		uint64_t testval;

		uint64_t value, value2;
	};

	enum : int
	{
		EXECUTE_OUT_OF_CYCLES       = 0,
		EXECUTE_MISSING_CODE        = 1,
		EXECUTE_UNMAPPED_CODE       = 2,
		EXECUTE_RESET_CACHE         = 3
	};

	address_space_config m_program_config;
	address_space *m_program;

	drc_cache m_drccache;
	internal_drc_state *m_state;

	std::unique_ptr<drcuml_state> m_drcuml;

	uml::code_handle *m_entry;
	uml::code_handle *m_nocode;
	uml::code_handle *m_out_of_cycles;
	uml::code_handle *m_testhandle;

	uint32_t m_labelnum;
	bool m_cache_dirty;

	void internal_map(address_map &map) ATTR_COLD;

	void code_flush_cache();
	void code_compile_block(uint32_t pc);

	void static_generate_entry_point();
	void static_generate_nocode_handler();
	void static_generate_out_of_cycles();

	void generate_tests(drcuml_block &block, int step);
	void generate_test_start(drcuml_block &block, uint32_t opcode, uint32_t opcode_size, uint32_t input_count, uint32_t output_count, uint32_t carry, uint32_t flags, const uint64_t *input_params, const uint64_t *output_params, const uint64_t *input_methods, const uint64_t *result_methods, const uint64_t *input_formats, const uint64_t *result_formats, uint32_t flag_combo, uint32_t initial_flags = uml::OPFLAGS_ALL);
	void generate_test_end(drcuml_block &block, uint32_t flag_combo);

	const char *method_str(uint32_t method);
	uint64_t normalize_nan_inf(uint64_t value, uint64_t size);

	void TEST_ENTRY_1_NORET_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM,uint64_t  __CARRY, uint64_t __FLAGS, uint64_t __PARAM_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_2_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_2_NORET_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_3_SEXT_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_3_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_4_SINGLE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_4_DOUBLE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT1, uint64_t __RESULT2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __RESULT1_METHOD, uint64_t __RESULT2_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_4_TRIPLE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD,uint64_t  __PARAM2_METHOD, uint64_t __PARAM3_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_4_QUAD_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __RESULT_IN, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __PARAM3_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_FLOAT_2_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM_METHOD, uint64_t __RESULT_METHOD, uint64_t __PARAM_FORMAT, uint64_t __RESULT_FORMAT, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_FLOAT_2_NORET_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_FLOAT_CMP_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_FLOAT_3_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __PARAM2_METHOD, uint64_t __RESULT_METHOD, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT, uint64_t __RESULT_FORMAT, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_FLOAT_3_SIZE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __RESULT_METHOD, uint64_t __PARAM1_FORMAT, uint64_t __RESULT_FORMAT, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_FLOAT_4_SIZE_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __RESULT, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_METHOD, uint64_t __RESULT_METHOD, uint64_t __PARAM1_FORMAT, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_1_NORET(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_2(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RETURN, uint64_t __PARAM, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_2_CMP(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_2_NORET(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_3_SEXT(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_3(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_4_SINGLE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_4_DOUBLE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT1, uint64_t __RESULT2, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_4_TRIPLE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_4_QUAD(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __RESULT_IN, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __CARRY, uint64_t __FLAGS);
	void TEST_ENTRY_FLOAT_2(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RETURN, uint64_t __PARAM, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __RESULT_FORMAT, uint64_t __PARAM_FORMAT);
	void TEST_ENTRY_FLOAT_2_NORET(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT);
	void TEST_ENTRY_FLOAT_CMP(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT);
	void TEST_ENTRY_FLOAT_3(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __RESULT_FORMAT, uint64_t __PARAM1_FORMAT, uint64_t __PARAM2_FORMAT);

	void TEST_ENTRY_MOV_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __PARAM_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO, uml::condition_t __CONDITIONAL_FLAGS);
	void TEST_ENTRY_MOV(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM);

	void TEST_ENTRY_FMOV_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM, uint64_t __PARAM_METHOD, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO, uml::condition_t __CONDITIONAL_FLAGS);
	void TEST_ENTRY_FMOV(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __PARAM);

	void TEST_ENTRY_FLOAT_3_SIZE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __RESULT_FORMAT, uint64_t __PARAM1_FORMAT);
	void TEST_ENTRY_FLOAT_4_SIZE(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uint64_t __RESULT, uint64_t __PARAM1, uint64_t __PARAM2, uint64_t __PARAM3, uint64_t __CARRY, uint64_t __FLAGS, uint64_t __PARAM1_FORMAT);

	void TEST_ENTRY_COND_GENERATOR(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uml::condition_t __COND, uint64_t __RESULT, uint64_t __RESULT_METHOD, uint32_t __FLAG_COMBO);
	void TEST_ENTRY_COND(drcuml_block &block, uml::opcode_t __OPCODE, uint8_t __SIZE, uml::condition_t __COND, uint64_t __RETURN, uint64_t __INITIAL_FLAGS);

	void TEST_MAPVAR_CONSTANT(drcuml_block &block, uml::parameter mapvar, uint32_t value);
	void TEST_MAPVAR_RECOVER(drcuml_block &block, uml::parameter mapvar, uint32_t value, int step);
};

DECLARE_DEVICE_TYPE(DRCTESTER, drctester_cpu_device)

class drctester_cpu_disassembler : public util::disasm_interface
{
public:
	drctester_cpu_disassembler() {}

	virtual ~drctester_cpu_disassembler() = default;

	virtual u32 opcode_alignment() const override
	{
		return 1;
	}

	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) override
	{
		return pc;
	}
};

#endif

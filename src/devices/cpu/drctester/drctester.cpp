#include "emu.h"
#include "drctester.h"

#define UML_BACKEND_TEST
// #define UML_BACKEND_TEST_SMALL

constexpr uint64_t CACHE_SIZE = (64 * 1024 * 1024);

DEFINE_DEVICE_TYPE(DRCTESTER, drctester_cpu_device, "drctester", "DRC Tester CPU")

drctester_cpu_device::drctester_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: cpu_device(mconfig, DRCTESTER, tag, owner, clock)
	, m_program_config("program", ENDIANNESS_LITTLE, 32, 32, 0, address_map_constructor(FUNC(drctester_cpu_device::internal_map), this))
	, m_program(nullptr)
	, m_drccache(CACHE_SIZE + sizeof(drctester_cpu_device))
	, m_drcuml(nullptr)
	, m_entry(nullptr)
	, m_nocode(nullptr)
	, m_out_of_cycles(nullptr)
	, m_testhandle(nullptr)
	, m_cache_dirty(false)
{
}

void drctester_cpu_device::device_start()
{
	m_state = (internal_drc_state *)m_drccache.alloc_near(sizeof(internal_drc_state));
	memset(m_state, 0, sizeof(internal_drc_state));

	m_state->value = 0x1111111187654321;
	m_state->value2 = 0x4444444412345678;

	const uint32_t flags = 0;
	m_drcuml = std::make_unique<drcuml_state>(*this, m_drccache, flags, 1, 32, 1);

	set_icountptr(m_state->m_icount);

	m_drcuml->symbol_add(&m_state->test_step, sizeof(uint32_t), "STEP");
	m_drcuml->symbol_add(&m_state->m_icount, sizeof(m_state->m_icount), "icount");

	state_add(0, "STEP", m_state->test_step).formatstr("%1u");

	m_program = &space(AS_PROGRAM);
}

void drctester_cpu_device::device_reset()
{
	m_labelnum = 0;
	m_cache_dirty = true;

	m_state->pc = 0;
	m_state->test_step = 0;
	m_state->test_num = 0;
	m_state->test_opcode = 0;
	m_state->test_opcode_size = 0;
	m_state->test_input_count = 0;
	m_state->test_output_count = 0;
	m_state->test_flag_combo = 0;
	m_state->test_initial_status = 0;
	m_state->test_expected_status = 0;
	m_state->test_result_status = 0;
	m_state->test_counter = 0;

	std::fill(std::begin(m_state->test_inputs), std::end(m_state->test_inputs), 0);
	std::fill(std::begin(m_state->test_expected_outputs), std::end(m_state->test_expected_outputs), 0);
	std::fill(std::begin(m_state->test_result_outputs), std::end(m_state->test_result_outputs), 0);
	std::fill(std::begin(m_state->test_mem_value), std::end(m_state->test_mem_value), 0);
	std::fill(std::begin(m_state->test_mem_result_value), std::end(m_state->test_mem_result_value), 0);
	std::fill(std::begin(m_state->test_param_methods), std::end(m_state->test_param_methods), 0);
	std::fill(std::begin(m_state->test_result_methods), std::end(m_state->test_result_methods), 0);
	std::fill(std::begin(m_state->test_param_formats), std::end(m_state->test_param_formats), 0);
	std::fill(std::begin(m_state->test_result_formats), std::end(m_state->test_result_formats), 0);
}

void drctester_cpu_device::device_stop()
{
	if (m_drcuml != nullptr)
		m_drcuml = nullptr;
}

device_memory_interface::space_config_vector drctester_cpu_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_PROGRAM, &m_program_config)
	};
}

std::unique_ptr<util::disasm_interface> drctester_cpu_device::create_disassembler()
{
	return std::make_unique<drctester_cpu_disassembler>();
}

void drctester_cpu_device::internal_map(address_map &map)
{
	map(0x0000, 0x00ff).ram();
}

void drctester_cpu_device::execute_run()
{
	int execute_result;

	/* reset the cache if dirty */
	if (m_cache_dirty)
	{
		code_flush_cache();
		m_cache_dirty = false;
	}

	/* execute */
	do
	{
		/* run as much as we can */
		execute_result = m_drcuml->execute(*m_entry);

		/* if we need to recompile, do it */
		if (execute_result == EXECUTE_MISSING_CODE)
		{
			code_flush_cache();
			code_compile_block(m_state->pc);
		}
		else if (execute_result == EXECUTE_UNMAPPED_CODE)
		{
			fatalerror("Attempted to execute unmapped code at step %d\n", m_state->test_step);
		}
		else if (execute_result == EXECUTE_RESET_CACHE)
		{
			code_flush_cache();
		}
	} while (execute_result != EXECUTE_OUT_OF_CYCLES);
}

void drctester_cpu_device::code_flush_cache()
{
	/* empty the transient cache contents */
	m_drcuml->reset();

	try
	{
		/* generate the entry point and out-of-cycles handlers */
		static_generate_entry_point();
		static_generate_nocode_handler();
		static_generate_out_of_cycles();
	}

	catch (drcuml_block::abort_compilation &)
	{
		fatalerror("Unable to generate drctester code\n");
		fflush(stdout);
	}
}

void drctester_cpu_device::code_compile_block(uint32_t pc)
{
	drcuml_block &block(m_drcuml->begin_block(4096*8*std::size(FLAG_COMBOS)));

	UML_HASH(block, 0, pc);
	generate_tests(block, m_state->test_step++);
	UML_HASHJMP(block, 0, pc+0x10, *m_nocode);

	block.end();
}

static inline void alloc_handle(drcuml_state &drcuml, uml::code_handle *&handleptr, const char *name)
{
	if (!handleptr)
		handleptr = drcuml.handle_alloc(name);
}

void drctester_cpu_device::static_generate_entry_point()
{
	drcuml_block &block(m_drcuml->begin_block(32));

	alloc_handle(*m_drcuml, m_testhandle, "testhandle");
	UML_HANDLE(block, *m_testhandle);
	UML_RECOVER(block, uml::I0, uml::M1);
	UML_RET(block);

	alloc_handle(*m_drcuml, m_nocode, "nocode");
	alloc_handle(*m_drcuml, m_entry, "entry");
	UML_HANDLE(block, *m_entry);

	/* load fast integer registers */
	//load_fast_iregs(block);

	UML_HASHJMP(block, 0, mem(&m_state->pc), *m_nocode);

	block.end();
}

void drctester_cpu_device::static_generate_nocode_handler()
{
	/* begin generating */
	drcuml_block &block(m_drcuml->begin_block(10));

	/* generate a hash jump via the current mode and PC */
	alloc_handle(*m_drcuml, m_nocode, "nocode");
	UML_HANDLE(block, *m_nocode);
	UML_GETEXP(block, I0);

	UML_MOV(block, mem(&m_state->pc), I0);
	//save_fast_iregs(block);
	UML_EXIT(block, EXECUTE_MISSING_CODE);

	block.end();
}

void drctester_cpu_device::static_generate_out_of_cycles()
{
	/* begin generating */
	drcuml_block &block(m_drcuml->begin_block(10));

	/* generate a hash jump via the current mode and PC */
	alloc_handle(*m_drcuml, m_out_of_cycles, "out_of_cycles");
	UML_HANDLE(block, *m_out_of_cycles);
	//save_fast_iregs(block);
	UML_EXIT(block, EXECUTE_OUT_OF_CYCLES);

	block.end();
}

static void cfunc_exit(void *param)
{
	exit(1);
}

static void cfunc_print_val32(void *param)
{
	uint32_t *val = (uint32_t*)param;
	printf("cfunc_print_val %08x\n", *val);
}

static void cfunc_print_val64(void *param)
{
	uint64_t *val = (uint64_t*)param;
	printf("cfunc_print_val %016llx\n", *val);
}

void cfunc_dump_machine_state(void *param)
{
	drcuml_machine_state *input = (drcuml_machine_state*)param;

	printf("drcuml_machine_state dump:\n");
	printf("\t%02x %02x %08x\n", input->flags, input->fmod, input->exp);

	for (int i = 0; i < std::size(input->r); i++)
		printf("\treg  %d %08llx\n", i, input->r[i].d);

	for (int i = 0; i < std::size(input->f); i++)
		printf("\tfreg %d %08llx %lf\n", i, d2u(input->f[i].d), input->f[i].d);

	printf("\n");
}

// TODO: Generate mapvar tests
void drctester_cpu_device::generate_tests(drcuml_block &block, int step)
{
	fprintf(stderr, "generating tests for step %d\n", step);

	if (step == 0)
	{

TEST_ENTRY_2_CMP(block, uml::OP_CMP, 4, 0x7fffffff, 0x6dcba987, 0, 0);
TEST_ENTRY_2_CMP(block, uml::OP_CMP, 4, 0x80000000, 0x6dcba988, 0, uml::FLAG_V);
TEST_ENTRY_2_CMP(block, uml::OP_CMP, 4, 0xffffffff, 0x6dcba987, 0, uml::FLAG_S);
TEST_ENTRY_2_CMP(block, uml::OP_CMP, 4, 0x00000000, 0x6dcba988, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_2_CMP(block, uml::OP_CMP, 4, 0x12345678, 0x12345678, 0, uml::FLAG_Z);

	}
	else if (step == 1)
	{
TEST_ENTRY_2_CMP(block, uml::OP_CMP, 8, 0x7fffffffffffffff, 0x7edcba9876543210, 0, 0);
TEST_ENTRY_2_CMP(block, uml::OP_CMP, 8, 0x8000000000000000, 0x7edcba9876543211, 0, uml::FLAG_V);
TEST_ENTRY_2_CMP(block, uml::OP_CMP, 8, 0xffffffffffffffff, 0x7edcba9876543210, 0, uml::FLAG_S);
TEST_ENTRY_2_CMP(block, uml::OP_CMP, 8, 0x0000000000000000, 0x7edcba9876543211, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_2_CMP(block, uml::OP_CMP, 8, 0x0123456789abcdef, 0x0123456789abcdef, 0, uml::FLAG_Z);

	}
	else if (step == 2)
	{

TEST_ENTRY_3(block, uml::OP_ADD, 4, 0x7fffffff, 0x12345678, 0x6dcba987, 0, 0);
TEST_ENTRY_3(block, uml::OP_ADD, 4, 0x80000000, 0x12345678, 0x6dcba988, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADD, 4, 0xffffffff, 0x92345678, 0x6dcba987, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADD, 4, 0x00000000, 0x92345678, 0x6dcba988, 0, uml::FLAG_C | uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ADD, 4, 0x00000000, 0, 0, 0, uml::FLAG_Z);

	}
	else if (step == 3)
	{

TEST_ENTRY_3(block, uml::OP_ADD, 8, 0x7fffffffffffffff, 0x0123456789abcdef, 0x7edcba9876543210, 0, 0);
TEST_ENTRY_3(block, uml::OP_ADD, 8, 0x8000000000000000, 0x0123456789abcdef, 0x7edcba9876543211, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADD, 8, 0xffffffffffffffff, 0x8123456789abcdef, 0x7edcba9876543210, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADD, 8, 0x0000000000000000, 0x8123456789abcdef, 0x7edcba9876543211, 0, uml::FLAG_C | uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ADD, 8, 0x0000000000000000, 0, 0, 0, uml::FLAG_Z);

	}
	else if (step == 4)
	{

TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0x7fffffff, 0x12345678, 0x6dcba987, 0,      0);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0x7fffffff, 0x12345678, 0x6dcba986, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0x80000000, 0x12345678, 0x6dcba988, 0,   uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0x80000000, 0x12345678, 0x6dcba987, uml::FLAG_C, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0xffffffff, 0x92345678, 0x6dcba987, 0,   uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0xffffffff, 0x92345678, 0x6dcba986, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0x00000000, 0x92345678, 0x6dcba988, 0,   uml::FLAG_C | uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0x00000000, 0x92345678, 0x6dcba987, uml::FLAG_C, uml::FLAG_C | uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0x12345678, 0x12345678, 0xffffffff, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0x00000001, 0, 0, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ADDC, 4, 0x00000000, 0, 0, 0, uml::FLAG_Z);

	}
	else if (step == 5)
	{

TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0x7fffffffffffffff, 0x0123456789abcdef, 0x7edcba9876543210, 0,      0);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0x7fffffffffffffff, 0x0123456789abcdef, 0x7edcba987654320f, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0x8000000000000000, 0x0123456789abcdef, 0x7edcba9876543211, 0,      uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0x8000000000000000, 0x0123456789abcdef, 0x7edcba9876543210, uml::FLAG_C, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0xffffffffffffffff, 0x8123456789abcdef, 0x7edcba9876543210, 0,      uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0xffffffffffffffff, 0x8123456789abcdef, 0x7edcba987654320f, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0x0000000000000000, 0x8123456789abcdef, 0x7edcba9876543211, 0,      uml::FLAG_C | uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0x0000000000000000, 0x8123456789abcdef, 0x7edcba9876543210, uml::FLAG_C, uml::FLAG_C | uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0x123456789abcdef0, 0x123456789abcdef0, 0xffffffffffffffff, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0x0000000000000001, 0, 0, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ADDC, 8, 0x0000000000000000, 0, 0, 0, uml::FLAG_Z);

	}
	else if (step == 6)
	{

TEST_ENTRY_3(block, uml::OP_SUB, 4, 0x12345678, 0x7fffffff, 0x6dcba987, 0, 0);
TEST_ENTRY_3(block, uml::OP_SUB, 4, 0x12345678, 0x80000000, 0x6dcba988, 0, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_SUB, 4, 0x92345678, 0xffffffff, 0x6dcba987, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUB, 4, 0x92345678, 0x00000000, 0x6dcba988, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUB, 4, 0x00000000, 0x12345678, 0x12345678, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUB, 4, 0x00000000, 0, 0, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUB, 4, 0xffffffff, 0, 1, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUB, 4, 0x00000001, 1, 0, 0, 0);

	}
	else if (step == 7)
	{

TEST_ENTRY_3(block, uml::OP_SUB, 8, 0x0123456789abcdef, 0x7fffffffffffffff, 0x7edcba9876543210, 0, 0);
TEST_ENTRY_3(block, uml::OP_SUB, 8, 0x0123456789abcdef, 0x8000000000000000, 0x7edcba9876543211, 0, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_SUB, 8, 0x8123456789abcdef, 0xffffffffffffffff, 0x7edcba9876543210, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUB, 8, 0x8123456789abcdef, 0x0000000000000000, 0x7edcba9876543211, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUB, 8, 0x0000000000000000, 0x0123456789abcdef, 0x0123456789abcdef, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUB, 8, 0x0000000000000000, 0, 0, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUB, 8, 0xffffffffffffffff, 0, 1, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUB, 8, 0x0000000000000001, 1, 0, 0, 0);

	}
	else if (step == 8)
	{

TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x12345678, 0x7fffffff, 0x6dcba987, 0,   0);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x12345678, 0x7fffffff, 0x6dcba986, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x12345678, 0x80000000, 0x6dcba988, 0,   uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x12345678, 0x80000000, 0x6dcba987, uml::FLAG_C, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x92345678, 0xffffffff, 0x6dcba987, 0,   uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x92345678, 0xffffffff, 0x6dcba986, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x92345678, 0x00000000, 0x6dcba988, 0,   uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x92345678, 0x00000000, 0x6dcba987, uml::FLAG_C, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x12345678, 0x12345678, 0xffffffff, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x00000000, 0x12345678, 0x12345677, uml::FLAG_C, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0xffffffff, 0x00000000, 0x00000000, uml::FLAG_C, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x00000000, 0x00000000, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x00000000, 0xffffffff, 0xfffffffe, uml::FLAG_C, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0xffffffff, 0xffffffff, 0xffffffff, uml::FLAG_C, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x7fffffff, 0xffffffff, 0x7fffffff, uml::FLAG_C, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x7fffffff, 0x7fffffff, 0xffffffff, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x7fffffff, 0x7fffffff, 0xffffffff, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SUBB, 4, 0x80000000, 0x7fffffff, 0xffffffff, 0, uml::FLAG_C | uml::FLAG_V | uml::FLAG_S);

	}
	else if (step == 9)
	{

TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x0123456789abcdef, 0x7fffffffffffffff, 0x7edcba9876543210, 0,      0);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x0123456789abcdef, 0x7fffffffffffffff, 0x7edcba987654320f, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x0123456789abcdef, 0x8000000000000000, 0x7edcba9876543211, 0,      uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x0123456789abcdef, 0x8000000000000000, 0x7edcba9876543210, uml::FLAG_C, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x8123456789abcdef, 0xffffffffffffffff, 0x7edcba9876543210, 0,      uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x8123456789abcdef, 0xffffffffffffffff, 0x7edcba987654320f, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x8123456789abcdef, 0x0000000000000000, 0x7edcba9876543211, 0,      uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x8123456789abcdef, 0x0000000000000000, 0x7edcba9876543210, uml::FLAG_C, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x123456789abcdef0, 0x123456789abcdef0, 0xffffffffffffffff, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x0000000000000000, 0x123456789abcdef0, 0x123456789abcdeef, uml::FLAG_C, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0xffffffffffffffff, 0x0000000000000000, 0x0000000000000000, uml::FLAG_C, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x0000000000000000, 0xffffffffffffffff, 0xfffffffffffffffe, uml::FLAG_C, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, uml::FLAG_C, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x7fffffffffffffff, 0xffffffffffffffff, 0x7fffffffffffffff, uml::FLAG_C, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x7fffffffffffffff, 0x7fffffffffffffff, 0xffffffffffffffff, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SUBB, 8, 0x8000000000000000, 0x7fffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_C | uml::FLAG_V | uml::FLAG_S);

	}
	else if (step == 10)
	{

TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 4, 0x77777777, 0x00000000, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 4, 0xffffffff, 0x00000000, 0x11111111, 0x0000000f, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 4, 0x00000000, 0x00000000, 0x11111111, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 4, 0xea61d951, 0x37c048d0, 0x77777777, 0x77777777, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 4, 0x32323233, 0xcdcdcdcc, 0xcdcdcdcd, 0xffffffff, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 4, 0x00000000, 0x00000000, 0, 0, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 4, 0x00000000, 0x00000000, 1, 0, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 4, 0x000005b8, 0x00000b70, 0xffffffffaaaaaaab, 0x00001128, 0, uml::FLAG_V);

	}
	else if (step == 11)
	{

TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 8, 0x7777777777777777, 0x0000000000000000, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 8, 0xffffffffffffffff, 0x0000000000000000, 0x1111111111111111, 0x000000000000000f, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 8, 0x0000000000000000, 0x0000000000000000, 0x1111111111111111, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 8, 0x0c83fb72ea61d951, 0x37c048d159e26af3, 0x7777777777777777, 0x7777777777777777, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 8, 0x3232323232323233, 0xcdcdcdcdcdcdcdcc, 0xcdcdcdcdcdcdcdcd, 0xffffffffffffffff, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 8, 0x0000000000000000, 0x0000000000000000, 0, 0, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULU, 8, 0x0000000000000000, 0x0000000000000000, 1, 0, 0, uml::FLAG_Z);

	}
	else if (step == 12)
	{

TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 4, 0x77777777, 0x00000000, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 4, 0xffffffff, 0x00000000, 0x11111111, 0x0000000f, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 4, 0x00000000, 0x00000000, 0x11111111, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 4, 0x9e26af38, 0xc83fb72e, 0x77777777, 0x88888888, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 4, 0x32323233, 0x00000000, 0xcdcdcdcd, 0xffffffff, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 4, 0x00000000, 0x00000000, 0, 0, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 4, 0x00000000, 0x00000000, 1, 0, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 4, 0x000005b8, 0xfffffa48, 0xffffffffaaaaaaab, 0x00001128, 0, uml::FLAG_S | uml::FLAG_V);

	}
	else if (step == 13)
	{


TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 8, 0x7777777777777777, 0x0000000000000000, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 8, 0xffffffffffffffff, 0x0000000000000000, 0x1111111111111111, 0x000000000000000f, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 8, 0x0000000000000000, 0x0000000000000000, 0x1111111111111111, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 8, 0x7c048d159e26af38, 0xc83fb72ea61d950c, 0x7777777777777777, 0x8888888888888888, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 8, 0x3232323232323233, 0x0000000000000000, 0xcdcdcdcdcdcdcdcd, 0xffffffffffffffff, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 8, 0x0000000000000000, 0x0000000000000000, 0, 0, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_MULS, 8, 0x0000000000000000, 0x0000000000000000, 1, 0, 0, uml::FLAG_Z);

	}
	else if (step == 14)
	{

TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 4, 0x02702702, 0x00000003, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 4, 0x00000000, 0x11111111, 0x11111111, 0x11111112, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 4, 0x7fffffff, 0x00000000, 0xfffffffe, 0x00000002, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 4, 0xfffffffe, 0x00000000, 0xfffffffe, 0x00000001, 0, uml::FLAG_S);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 4, UNDEFINED, UNDEFINED, 0xffffffff, 0x00000000, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 4, UNDEFINED, UNDEFINED, 0x00000000, 0x00000000, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 4, 0x00000000, 0x00000000, 0x00000000, 0x11111112, 0, uml::FLAG_Z);

	}
	else if (step == 15)
	{

TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 8, 0x0270270270270270, 0x0000000000000001, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 8, 0x0000000000000000, 0x1111111111111111, 0x1111111111111111, 0x1111111111111112, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 8, 0x7fffffffffffffff, 0x0000000000000000, 0xfffffffffffffffe, 0x0000000000000002, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 8, 0xfffffffffffffffe, 0x0000000000000000, 0xfffffffffffffffe, 0x0000000000000001, 0, uml::FLAG_S);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 8, UNDEFINED, UNDEFINED, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 8, UNDEFINED, UNDEFINED, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVU, 8, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x1111111111111112, 0, uml::FLAG_Z);

	}
	else if (step == 16)
	{

TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 4, 0x02702702, 0x00000003, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 4, 0x00000000, 0x11111111, 0x11111111, 0x11111112, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 4, 0xffffffff, 0x00000000, 0xfffffffe, 0x00000002, 0, uml::FLAG_S);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 4, UNDEFINED, UNDEFINED, 0xffffffff, 0x00000000, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 4, UNDEFINED, UNDEFINED, 0x00000000, 0x00000000, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 4, 0x00000000, 0x00000000, 0x00000000, 0x11111112, 0, uml::FLAG_Z);

	}
	else if (step == 17)
	{

TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 8, 0x0270270270270270, 0x0000000000000001, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 8, 0x0000000000000000, 0x1111111111111111, 0x1111111111111111, 0x1111111111111112, 0, uml::FLAG_Z);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 8, 0xffffffffffffffff, 0x0000000000000000, 0xfffffffffffffffe, 0x0000000000000002, 0, uml::FLAG_S);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 8, UNDEFINED, UNDEFINED, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 8, UNDEFINED, UNDEFINED, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_V);
TEST_ENTRY_4_DOUBLE(block, uml::OP_DIVS, 8, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x1111111111111112, 0, uml::FLAG_Z);

	}
	else if (step == 18)
	{

TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 4, 0x77777777, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 4, 0xffffffff, 0x11111111, 0x0000000f, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 4, 0x00000000, 0x11111111, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 4, 0xea61d951, 0x77777777, 0x77777777, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 4, 0x32323233, 0xcdcdcdcd, 0xffffffff, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 4, 0x00000000, 0x00000000, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 4, 0x00000000, 0xffffffff, 0x00000000, 0, uml::FLAG_Z);

	}
	else if (step == 19)
	{

TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 8, 0x7777777777777777, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 8, 0xffffffffffffffff, 0x1111111111111111, 0x000000000000000f, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 8, 0x0000000000000000, 0x1111111111111111, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 8, 0x0c83fb72ea61d951, 0x7777777777777777, 0x7777777777777777, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 8, 0x3232323232323233, 0xcdcdcdcdcdcdcdcd, 0xffffffffffffffff, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 8, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULU, 8, 0x0000000000000000, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_Z);

	}
	else if (step == 20)
	{

TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 4, 0x77777777, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 4, 0xffffffff, 0x11111111, 0x0000000f, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 4, 0x00000000, 0x11111111, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 4, 0x9e26af38, 0x77777777, 0x88888888, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 4, 0x32323233, 0xcdcdcdcd, 0xffffffff, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 4, 0x00000000, 0x00000000, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 4, 0x00000000, 0xffffffff, 0x00000000, 0, uml::FLAG_Z);

	}
	else if (step == 21)
	{

TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 8, 0x7777777777777777, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 8, 0xffffffffffffffff, 0x1111111111111111, 0x000000000000000f, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 8, 0x0000000000000000, 0x1111111111111111, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 8, 0x7c048d159e26af38, 0x7777777777777777, 0x8888888888888888, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 8, 0x3232323232323233, 0xcdcdcdcdcdcdcdcd, 0xffffffffffffffff, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 8, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_MULS, 8, 0x0000000000000000, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_Z);

	}
	else if (step == 22)
	{

TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 4, 0x02702702, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 4, 0x00000000, 0x11111111, 0x11111112, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 4, 0x7fffffff, 0xfffffffe, 0x00000002, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 4, 0xfffffffe, 0xfffffffe, 0x00000001, 0, uml::FLAG_S);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 4, UNDEFINED, 0xffffffff, 0x00000000, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 4, UNDEFINED, 0x00000000, 0x00000000, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 4, 0x00000000, 0x00000000, 0x11111112, 0, uml::FLAG_Z);

	}
	else if (step == 23)
	{

TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 8, 0x0270270270270270, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 8, 0x0000000000000000, 0x1111111111111111, 0x1111111111111112, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 8, 0x7fffffffffffffff, 0xfffffffffffffffe, 0x0000000000000002, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 8, 0xfffffffffffffffe, 0xfffffffffffffffe, 0x0000000000000001, 0, uml::FLAG_S);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 8, UNDEFINED, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 8, UNDEFINED, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVU, 8, 0x0000000000000000, 0x0000000000000000, 0x1111111111111112, 0, uml::FLAG_Z);

	}
	else if (step == 24)
	{

TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 4, 0x02702702, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 4, 0x00000000, 0x11111111, 0x11111112, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 4, 0xffffffff, 0xfffffffe, 0x00000002, 0, uml::FLAG_S);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 4, UNDEFINED, 0xffffffff, 0x00000000, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 4, UNDEFINED, 0x00000000, 0x00000000, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 4, 0x00000000, 0x00000000, 0x11111112, 0, uml::FLAG_Z);

	}
	else if (step == 25)
	{

TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 8, 0x0270270270270270, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 8, 0x0000000000000000, 0x1111111111111111, 0x1111111111111112, 0, uml::FLAG_Z);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 8, 0xffffffffffffffff, 0xfffffffffffffffe, 0x0000000000000002, 0, uml::FLAG_S);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 8, UNDEFINED, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 8, UNDEFINED, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_V);
TEST_ENTRY_4_SINGLE(block, uml::OP_DIVS, 8, 0x0000000000000000, 0x0000000000000000, 0x1111111111111112, 0, uml::FLAG_Z);


	}
	else if (step == 26)
	{

TEST_ENTRY_3(block, uml::OP_MULULW, 4, 0x77777777, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_3(block, uml::OP_MULULW, 4, 0xeeeeeeee, 0x77777777, 0x00000002, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_MULULW, 4, 0x00000000, 0x11111111, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_MULULW, 4, 0x32323233, 0xcdcdcdcd, 0xffffffff, 0, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_MULULW, 4, 0xea61d951, 0x77777777, 0x77777777, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_MULULW, 4, 0x00000000, 0x00000000, 0x00000000, 0, uml::FLAG_Z);

	}
	else if (step == 27)
	{

TEST_ENTRY_3(block, uml::OP_MULULW, 8, 0x7777777777777777, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_3(block, uml::OP_MULULW, 8, 0x8888888888888888, 0x1111111111111111, 0x0000000000000008, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_MULULW, 8, 0x0000000000000000, 0x1111111111111111, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_MULULW, 8, 0x3232323232323233, 0xcdcdcdcdcdcdcdcd, 0xffffffffffffffff, 0, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_MULULW, 8, 0xeb851eb851eb851f, 0x7777777777777777, 0x9999999999999999, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_MULULW, 8, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_Z);

	}
	else if (step == 28)
	{

TEST_ENTRY_3(block, uml::OP_MULSLW, 4, 0x77777777, 0x11111111, 0x00000007, 0, 0);
TEST_ENTRY_3(block, uml::OP_MULSLW, 4, 0x00000000, 0x11111111, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_MULSLW, 4, 0x18888889, 0x77777777, 0xefffffff, 0, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_MULSLW, 4, 0xffffffff, 0x11111111, 0x0000000f, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_MULSLW, 4, 0x32323233, 0xcdcdcdcd, 0xffffffff, 0, 0);
TEST_ENTRY_3(block, uml::OP_MULSLW, 4, 0x00000000, 0x00000000, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_MULSLW, 4, 0x00000000, 0x80000000, 0x80000000, 0, uml::FLAG_V | uml::FLAG_Z);

	}
	else if (step == 29)
	{

TEST_ENTRY_3(block, uml::OP_MULSLW, 8, 0x7777777777777777, 0x1111111111111111, 0x0000000000000007, 0, 0);
TEST_ENTRY_3(block, uml::OP_MULSLW, 8, 0x0000000000000000, 0x1111111111111111, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_MULSLW, 8, 0x7c048d159e26af38, 0x7777777777777777, 0x8888888888888888, 0, uml::FLAG_V);
TEST_ENTRY_3(block, uml::OP_MULSLW, 8, 0xffffffffffffffff, 0x1111111111111111, 0x000000000000000f, 0, uml::FLAG_V | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_MULSLW, 8, 0x3232323232323233, 0xcdcdcdcdcdcdcdcd, 0xffffffffffffffff, 0, 0);
TEST_ENTRY_3(block, uml::OP_MULSLW, 8, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_MULSLW, 8, 0x0000000000000000, 0x8000000000000000, 0x8000000000000000, 0, uml::FLAG_V | uml::FLAG_Z);

	}
	else if (step == 30)
	{

TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 4, 0, 0, 0, 0);
TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 4, 1, 0, 0, uml::FLAG_C);
TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 8, 0 << 5, 5, 0, 0);
TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 8, 1 << 5, 5, 0, uml::FLAG_C);
TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 4, 0, 0, 0, 0);
TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 4, 1, 0, 0, uml::FLAG_C);
TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 8, uint64_t(0) << 60, 60, 0, 0);
TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 8, uint64_t(1) << 60, 60, 0, uml::FLAG_C);
TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 8, 1, 64, 0, uml::FLAG_C);
TEST_ENTRY_2_NORET(block, uml::OP_CARRY, 8, 0, 64, 0, 0);

	}
	else if (step == 31)
	{

TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0x00000000, 0x00, uml::SIZE_BYTE, 0, uml::FLAG_Z);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0x0000007f, 0x7f, uml::SIZE_BYTE, 0, 0);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0xffffffff, 0xff, uml::SIZE_BYTE, 0, uml::FLAG_S);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0x00000000, 0x0000, uml::SIZE_WORD, 0, uml::FLAG_Z);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0x00007fff, 0x7fff, uml::SIZE_WORD, 0, 0);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0xffffffff, 0xffff, uml::SIZE_WORD, 0, uml::FLAG_S);
// TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0x00000000, 0x00000000, uml::SIZE_DWORD, 0, uml::FLAG_Z);
// TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0x7fffffff, 0x7fffffff, uml::SIZE_DWORD, 0, 0);
// TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0xffffffff, 0xffffffff, uml::SIZE_DWORD, 0, uml::FLAG_S);
// TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0x00000000, 0x0000000000000000, uml::SIZE_QWORD, 0, uml::FLAG_Z);
// TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0x7fffffff, 0x7fffffffffffffff, uml::SIZE_QWORD, 0, 0);
// TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 4, 0xffffffff, 0xffffffffffffffff, uml::SIZE_QWORD, 0, uml::FLAG_S);

	}
	else if (step == 32)
	{

TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0x0000000000000000, 0x00, uml::SIZE_BYTE, 0, uml::FLAG_Z);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0x000000000000007f, 0x7f, uml::SIZE_BYTE, 0, 0);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0xffffffffffffffff, 0xff, uml::SIZE_BYTE, 0, uml::FLAG_S);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0x0000000000000000, 0x0000, uml::SIZE_WORD, 0, uml::FLAG_Z);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0x0000000000007fff, 0x7fff, uml::SIZE_WORD, 0, 0);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0xffffffffffffffff, 0xffff, uml::SIZE_WORD, 0, uml::FLAG_S);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0x0000000000000000, 0x00000000, uml::SIZE_DWORD, 0, uml::FLAG_Z);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0x000000007fffffff, 0x7fffffff, uml::SIZE_DWORD, 0, 0);
TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0xffffffffffffffff, 0xffffffff, uml::SIZE_DWORD, 0, uml::FLAG_S);
// TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0x0000000000000000, 0x0000000000000000, uml::SIZE_QWORD, 0, uml::FLAG_Z);
// TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0x7fffffffffffffff, 0x7fffffffffffffff, uml::SIZE_QWORD, 0, 0);
// TEST_ENTRY_3_SEXT(block, uml::OP_SEXT, 8, 0xffffffffffffffff, 0xffffffffffffffff, uml::SIZE_QWORD, 0, uml::FLAG_S);

	}
	else if (step == 33)
	{

TEST_ENTRY_3(block, uml::OP_AND, 4, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0, 0);
TEST_ENTRY_3(block, uml::OP_AND, 4, 0x00000000, 0xffffffff, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_AND, 4, 0xffffffff, 0xffffffff, 0xffffffff, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_AND, 8, 0x7fffffffffffffff, 0x7fffffffffffffff, 0x7fffffffffffffff, 0, 0);
TEST_ENTRY_3(block, uml::OP_AND, 8, 0x0000000000000000, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_AND, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_S);

	}
	else if (step == 34)
	{

TEST_ENTRY_2_NORET(block, uml::OP_TEST, 4, 0x7fffffff, 0x7fffffff, 0, 0);
TEST_ENTRY_2_NORET(block, uml::OP_TEST, 4, 0xffffffff, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_2_NORET(block, uml::OP_TEST, 4, 0xffffffff, 0xffffffff, 0, uml::FLAG_S);
TEST_ENTRY_2_NORET(block, uml::OP_TEST, 8, 0x7fffffffffffffff, 0x7fffffffffffffff, 0, 0);
TEST_ENTRY_2_NORET(block, uml::OP_TEST, 8, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_2_NORET(block, uml::OP_TEST, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_S);

	}
	else if (step == 35)
	{

TEST_ENTRY_3(block, uml::OP_OR, 4, 0x7fffffff, 0x0fffffff, 0x70000000, 0, 0);
TEST_ENTRY_3(block, uml::OP_OR, 4, 0x00000000, 0x00000000, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_OR, 4, 0xffffffff, 0xffffffff, 0x00000000, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_OR, 8, 0x7fffffffffffffff, 0x0fffffffffffffff, 0x7000000000000000, 0, 0);
TEST_ENTRY_3(block, uml::OP_OR, 8, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_OR, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_S);

	}
	else if (step == 36)
	{

TEST_ENTRY_3(block, uml::OP_XOR, 4, 0x51150000, 0x12345678, 0x43215678, 0, 0);
TEST_ENTRY_3(block, uml::OP_XOR, 4, 0x00000000, 0xffffffff, 0xffffffff, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_XOR, 4, 0xffffffff, 0xffffffff, 0x00000000, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_XOR, 8, 0x4a557dd715aa661b, 0x1234567890abcdef, 0x58612baf8501abf4, 0, 0);
TEST_ENTRY_3(block, uml::OP_XOR, 8, 0x0000000000000000, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_XOR, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0x0000000000000000, 0, uml::FLAG_S);

	}
	else if (step == 37)
	{

TEST_ENTRY_3(block, uml::OP_SHL, 4, 0x2468acf0, 0x12345678, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHL, 4, 0x2468acf0, 0x12345678, 33, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHL, 4, 0x00000000, 0x00000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SHL, 4, 0x00000000, 0x80000000, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHL, 4, 0x00000000, 0x80000000, 33, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHL, 4, 0xfffffffe, 0x7fffffff, 1, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SHL, 4, 0xfffffffe, 0xffffffff, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHL, 4, 0xffffffff, 0xffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0x2468acf121579bde, 0x1234567890abcdef, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0x2468acf121579bde, 0x1234567890abcdef, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0x0000000000000000, 0x0000000000000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0x0000000000000000, 0x8000000000000000, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0x0000000000000000, 0x8000000000000000, 65, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0xfffffffffffffffe, 0x7fffffffffffffff, 1, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0xfffffffffffffffe, 0xffffffffffffffff, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0x1234567890abcdef, 0x1234567890abcdef, 64, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHL, 8, 0x1234567890abcdef, 0x1234567890abcdef, 64, uml::FLAG_C, 0);

	}
	else if (step == 38)
	{

TEST_ENTRY_3(block, uml::OP_SHR, 4, 0x091a2b3c, 0x12345678, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHR, 4, 0x091a2b3c, 0x12345678, 33, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHR, 4, 0x00000000, 0x00000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SHR, 4, 0x7fffffff, 0xffffffff, 1, 0, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHR, 4, 0x00000000, 0x00000001, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHR, 4, 0x00000000, 0x00000001, 33, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHR, 4, 0xffffffff, 0xffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SHR, 8, 0x091a2b3c4855e6f7, 0x1234567890abcdee, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHR, 8, 0x091a2b3c4855e6f7, 0x1234567890abcdee, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHR, 8, 0x0000000000000000, 0x0000000000000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SHR, 8, 0x7fffffffffffffff, 0xffffffffffffffff, 1, 0, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHR, 8, 0x0000000000000000, 0x0000000000000001, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SHR, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SHR, 8, 0x1234567890abcdee, 0x1234567890abcdee, 64, 0, 0);
TEST_ENTRY_3(block, uml::OP_SHR, 8, 0x1234567890abcdee, 0x1234567890abcdee, 64, uml::FLAG_C, 0);

	}
	else if (step == 39)
	{

TEST_ENTRY_3(block, uml::OP_SAR, 4, 0x091a2b3c, 0x12345678, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_SAR, 4, 0x091a2b3c, 0x12345678, 33, 0, 0);
TEST_ENTRY_3(block, uml::OP_SAR, 4, 0x00000000, 0x00000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SAR, 4, 0xffffffff, 0xffffffff, 1, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SAR, 4, 0xffffffff, 0xffffffff, 33, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SAR, 4, 0x00000000, 0x00000001, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SAR, 4, 0xffffffff, 0xffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SAR, 8, 0x091a2b3c4855e6f7, 0x1234567890abcdee, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_SAR, 8, 0x091a2b3c4855e6f7, 0x1234567890abcdee, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_SAR, 8, 0x0000000000000000, 0x0000000000000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_SAR, 8, 0xffffffffffffffff, 0xffffffffffffffff, 1, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SAR, 8, 0xffffffffffffffff, 0xffffffffffffffff, 65, 0, uml::FLAG_C | uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SAR, 8, 0x0000000000000000, 0x0000000000000001, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_SAR, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_SAR, 8, 0x1234567890abcdee, 0x1234567890abcdee, 64, 0, 0);
TEST_ENTRY_3(block, uml::OP_SAR, 8, 0x1234567890abcdee, 0x1234567890abcdee, 64, uml::FLAG_C, 0);

	}
	else if (step == 40)
	{

TEST_ENTRY_3(block, uml::OP_ROL, 4, 0x2468acf0, 0x12345678, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROL, 4, 0x2468acf0, 0x12345678, 33, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROL, 4, 0x12345678, 0x12345678, 32, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROL, 4, 0x12345678, 0x12345678, 32, uml::FLAG_S, 0);
TEST_ENTRY_3(block, uml::OP_ROL, 4, 0x00000000, 0x00000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ROL, 4, 0x00000001, 0x80000000, 1, 0, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROL, 4, 0xfffffffe, 0x7fffffff, 1, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROL, 4, 0xfffffffe, 0x7fffffff, 33, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROL, 4, 0xffffffff, 0xffffffff, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROL, 4, 0xffffffff, 0xffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0x2468acf121579bde, 0x1234567890abcdef, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0x2468acf121579bde, 0x1234567890abcdef, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0x1234567890abcdef, 0x1234567890abcdef, 64, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0x1234567890abcdef, 0x1234567890abcdef, 64, uml::FLAG_S, 0);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0x0000000000000000, 0x0000000000000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0x0000000000000001, 0x8000000000000000, 1, 0, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0xfffffffffffffffe, 0x7fffffffffffffff, 1, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0xfffffffffffffffe, 0x7fffffffffffffff, 65, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0xffffffffffffffff, 0xffffffffffffffff, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0x1234567890abcdef, 0x1234567890abcdef, 64, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROL, 8, 0x1234567890abcdef, 0x1234567890abcdef, 64, uml::FLAG_C, 0);

	}
	else if (step == 41)
	{

TEST_ENTRY_3(block, uml::OP_ROR, 4, 0x091a2b3c, 0x12345678, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROR, 4, 0x091a2b3c, 0x12345678, 33, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROR, 4, 0x00000000, 0x00000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ROR, 4, 0x80000000, 0x00000001, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROR, 4, 0x80000000, 0x00000001, 33, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROR, 4, 0xffffffff, 0xffffffff, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROR, 4, 0xffffffff, 0xffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROR, 8, 0x091a2b3c4855e6f7, 0x1234567890abcdee, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROR, 8, 0x091a2b3c4855e6f7, 0x1234567890abcdee, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROR, 8, 0x0000000000000000, 0x0000000000000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ROR, 8, 0x8000000000000000, 0x0000000000000001, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROR, 8, 0x8000000000000000, 0x0000000000000001, 65, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROR, 8, 0xffffffffffffffff, 0xffffffffffffffff, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROR, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROR, 8, 0x1234567890abcdee, 0x1234567890abcdee, 64, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROR, 8, 0x1234567890abcdee, 0x1234567890abcdee, 64, uml::FLAG_C, 0);

	}
	else if (step == 42)
	{

TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x2468acf0, 0x12345678, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x2468acf0, 0x12345678, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x00000000, 0x00000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x00000000, 0x00000000, 65, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x00000000, 0x80000000, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x00000000, 0x80000000, 65, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xfffffffe, 0x7fffffff, 1, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xfffffffe, 0x7fffffff, 65, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xfffffffe, 0xffffffff, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xfffffffe, 0xffffffff, 65, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x2468acf0, 0x12345678, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x2468acf0, 0x12345678, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x00000001, 0x00000000, 1, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x00000001, 0x00000000, 65, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x00000001, 0x80000000, 1, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0x00000001, 0x80000000, 65, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xffffffff, 0x7fffffff, 1, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xffffffff, 0x7fffffff, 65, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xffffffff, 0xffffffff, 1, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xffffffff, 0xffffffff, 65, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xffffffff, 0xffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 4, 0xffffffff, 0xffffffff, 64, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x2468acf121579bde, 0x1234567890abcdef, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x2468acf121579bde, 0x1234567890abcdef, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x0000000000000000, 0x0000000000000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x0000000000000000, 0x0000000000000000, 65, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x0000000000000000, 0x8000000000000000, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x0000000000000000, 0x8000000000000000, 65, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xfffffffffffffffe, 0x7fffffffffffffff, 1, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xfffffffffffffffe, 0x7fffffffffffffff, 65, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xfffffffffffffffe, 0xffffffffffffffff, 1, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xfffffffffffffffe, 0xffffffffffffffff, 65, 0, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x2468acf121579bdf, 0x1234567890abcdef, 1, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x2468acf121579bdf, 0x1234567890abcdef, 65, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x0000000000000001, 0x0000000000000000, 1, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x0000000000000001, 0x0000000000000000, 65, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x0000000000000001, 0x8000000000000000, 1, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x0000000000000001, 0x8000000000000000, 65, uml::FLAG_C, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xffffffffffffffff, 0x7fffffffffffffff, 1, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xffffffffffffffff, 0x7fffffffffffffff, 65, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 1, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 65, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 64, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x1234567890abcdef, 0x1234567890abcdef, 64, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 64, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_ROLC, 8, 0x1234567890abcdef, 0x1234567890abcdef, 64, 0, 0);

	}
	else if (step == 43)
	{

TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x091a2b3c, 0x12345678, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x091a2b3c, 0x12345678, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x00000000, 0x00000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x00000000, 0x00000000, 65, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x00000000, 0x00000001, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x00000000, 0x00000001, 65, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x7fffffff, 0xffffffff, 1, 0, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x7fffffff, 0xffffffff, 65, 0, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x891a2b3c, 0x12345678, 1, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x891a2b3c, 0x12345678, 65, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x80000000, 0x00000000, 1, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x80000000, 0x00000000, 65, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x80000000, 0x00000001, 1, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0x80000000, 0x00000001, 65, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0xffffffff, 0xffffffff, 1, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0xffffffff, 0xffffffff, 65, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0xffffffff, 0xffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 4, 0xffffffff, 0xffffffff, 64, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x091a2b3c4855e6f7, 0x1234567890abcdee, 1, 0, 0);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x091a2b3c4855e6f7, 0x1234567890abcdee, 65, 0, 0);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x0000000000000000, 0x0000000000000000, 1, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x0000000000000000, 0x0000000000000000, 65, 0, uml::FLAG_Z);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x0000000000000000, 0x0000000000000001, 1, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x0000000000000000, 0x0000000000000001, 65, 0, uml::FLAG_Z | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x7fffffffffffffff, 0xffffffffffffffff, 1, 0, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x7fffffffffffffff, 0xffffffffffffffff, 65, 0, uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x891a2b3c4855e6f7, 0x1234567890abcdee, 1, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x891a2b3c4855e6f7, 0x1234567890abcdee, 65, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x8000000000000000, 0x0000000000000000, 1, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x8000000000000000, 0x0000000000000000, 65, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x8000000000000000, 0x0000000000000001, 1, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x8000000000000000, 0x0000000000000001, 65, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 1, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 65, uml::FLAG_C, uml::FLAG_S | uml::FLAG_C);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 64, uml::FLAG_C, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x1234567890abcdee, 0x1234567890abcdee, 64, uml::FLAG_C, 0);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0xffffffffffffffff, 0xffffffffffffffff, 64, 0, uml::FLAG_S);
TEST_ENTRY_3(block, uml::OP_RORC, 8, 0x1234567890abcdee, 0x1234567890abcdee, 64, 0, 0);

	}
	else if (step == 44)
	{

TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 4, 0x12345678, 0x12345678, 0, 0xffffffff, 0, 0);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 4, 0x0000b3c0, 0x12345678, 3, 0x0000ffff, 0, 0);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 4, 0x00000004, 0x12345678, 16, 0x0000000f, 0, 0);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 4, 0x00000000, 0x12345678, 0, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 4, 0x91a20000, 0x12345678, 3, 0xffff0000, 0, uml::FLAG_S);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 4, 0x00000000, 0x12345678, 3, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 4, 0x00000000, 0x7fffffff, 3, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 4, 0x00000000, 0x12345678, 16, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 8, 0x1234567890abcdef, 0x1234567890abcdef, 0, 0xffffffffffffffff, 0, 0);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 8, 0x00000000855e6f78, 0x1234567890abcdef, 3, 0x00000000ffffffff, 0, 0);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 8, 0x0000000000000004, 0x1234567890abcdef, 16, 0x000000000000000f, 0, 0);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 8, 0x91a2b3c400000000, 0x1234567890abcdef, 3, 0xffffffff00000000, 0, uml::FLAG_S);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 8, 0x0000000000000000, 0x1234567890abcdef, 0, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 8, 0x0000000000000000, 0x1234567890abcdef, 3, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 8, 0x0000000000000000, 0x7fffffffffffffff, 3, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_4_TRIPLE(block, uml::OP_ROLAND, 8, 0x0000000000000000, 0x1234567890abcdef, 32, 0x0000000000000000, 0, uml::FLAG_Z);

	}
	else if (step == 45)
	{

TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 4, 0x12345678, 0xffffffff, 0x12345678, 0, 0xffffffff, 0, 0);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 4, 0x1111b3c0, 0x11111234, 0x12345678, 3, 0x0000ffff, 0, 0);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 4, 0x00000000, 0x91a21111, 0x00000000, 3, 0xffffffff, 0, uml::FLAG_Z);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 4, 0x91a21111, 0x91a21111, 0x12345678, 3, 0xffff0000, 0, uml::FLAG_S);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 4, 0x91a21114, 0x91a21111, 0x12345678, 16, 0x0000000f, 0, uml::FLAG_S);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 4, 0xffffffff, 0xffffffff, 0x12345678, 16, 0x00000000, 0, uml::FLAG_S);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 8, 0x1234567890abcdef, 0xffffffffffffffff, 0x1234567890abcdef, 0, 0xffffffffffffffff, 0, 0);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 8, 0x11111111855e6f78, 0x11111111ffffffff, 0x1234567890abcdef, 3, 0x00000000ffffffff, 0, 0);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 8, 0x11111111fffffff4, 0x11111111ffffffff, 0x1234567890abcdef, 16, 0x000000000000000f, 0, 0);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 8, 0x0000000000000000, 0x91a2b3c411111111, 0x0000000000000000, 3, 0xffffffffffffffff, 0, uml::FLAG_Z);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 8, 0x91a2b3c411111111, 0xffffffff11111111, 0x1234567890abcdef, 3, 0xffffffff00000000, 0, uml::FLAG_S);
TEST_ENTRY_4_QUAD(block, uml::OP_ROLINS, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0x1234567890abcdef, 16, 0x0000000000000000, 0, uml::FLAG_S);

	}
	else if (step == 46)
	{

TEST_ENTRY_2(block, uml::OP_LZCNT, 4, 32, 0x00000000, 0, 0);
TEST_ENTRY_2(block, uml::OP_LZCNT, 4, 31, 0x00000001, 0, 0);
TEST_ENTRY_2(block, uml::OP_LZCNT, 4, 0,  0xffffffff, 0, uml::FLAG_Z);
TEST_ENTRY_2(block, uml::OP_LZCNT, 4, 16, 0x0000ffff, 0, 0);
TEST_ENTRY_2(block, uml::OP_LZCNT, 8, 64, 0x0000000000000000, 0, 0);
TEST_ENTRY_2(block, uml::OP_LZCNT, 8, 63, 0x0000000000000001, 0, 0);
TEST_ENTRY_2(block, uml::OP_LZCNT, 8, 0,  0xffffffffffffffff, 0, uml::FLAG_Z);
TEST_ENTRY_2(block, uml::OP_LZCNT, 8, 32, 0x00000000ffffffff, 0, 0);

	}
	else if (step == 47)
	{

// Insanity to set Z based on the input
TEST_ENTRY_2(block, uml::OP_TZCNT, 4, 32, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_2(block, uml::OP_TZCNT, 4, 0,  0xffffffff, 0, 0);
TEST_ENTRY_2(block, uml::OP_TZCNT, 4, 16, 0xffff0000, 0, 0);
TEST_ENTRY_2(block, uml::OP_TZCNT, 4, 31,  0x80000000, 0, 0);
TEST_ENTRY_2(block, uml::OP_TZCNT, 8, 64, 0x000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_2(block, uml::OP_TZCNT, 8, 0,  0xffffffffffffffff, 0, 0);
TEST_ENTRY_2(block, uml::OP_TZCNT, 8, 32, 0xffffffff00000000, 0, 0);
TEST_ENTRY_2(block, uml::OP_TZCNT, 8, 63, 0x8000000000000000, 0, 0);

	}
	else if (step == 48)
	{

TEST_ENTRY_2(block, uml::OP_BSWAP, 4, 0x00000000, 0x00000000, 0, uml::FLAG_Z);
TEST_ENTRY_2(block, uml::OP_BSWAP, 4, 0xffffffff, 0xffffffff, 0, uml::FLAG_S);
TEST_ENTRY_2(block, uml::OP_BSWAP, 4, 0x0000ffff, 0xffff0000, 0, 0);
TEST_ENTRY_2(block, uml::OP_BSWAP, 4, 0xffff0000, 0x0000ffff, 0, uml::FLAG_S);
TEST_ENTRY_2(block, uml::OP_BSWAP, 8, 0x0000000000000000, 0x0000000000000000, 0, uml::FLAG_Z);
TEST_ENTRY_2(block, uml::OP_BSWAP, 8, 0xffffffffffffffff, 0xffffffffffffffff, 0, uml::FLAG_S);
TEST_ENTRY_2(block, uml::OP_BSWAP, 8, 0x00000000ffffffff, 0xffffffff00000000, 0, 0);
TEST_ENTRY_2(block, uml::OP_BSWAP, 8, 0xffffffff00000000, 0x00000000ffffffff, 0, uml::FLAG_S);

	}
	else if (step == 49)
	{

//// Floating point

// Convert double to float and then back to double
TEST_ENTRY_FLOAT_2(block, uml::OP_FRNDS, 8, d2u(123.4891f), d2u(123.4891), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRNDS, 8, d2u(-123.4891f), d2u(-123.4891), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRNDS, 8, d2u(1.0f), d2u(1), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRNDS, 8, d2u(0.0f), d2u(0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRNDS, 8, d2u(127839.9148f), d2u(127839.9148), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRNDS, 8, d2u(0.475f), d2u(0.475), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_2(block, uml::OP_FRNDS, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_2(block, uml::OP_FRNDS, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 50)
	{

TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(-123.4891f), f2u(123.4891f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(123.4891f), f2u(-123.4891f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(-1.0f), f2u(1.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(-0.0f), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(-127839.9148f), f2u(127839.9148f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(-0.475f), f2u(0.475f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(-std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 4, f2u(-std::numeric_limits<float>::infinity()), f2u(-std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(-123.4891), d2u(123.4891), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(123.4891), d2u(-123.4891), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(-1.0), d2u(1), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(-0.0), d2u(0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(-127839.9148), d2u(127839.9148), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(-0.475), d2u(0.475), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(-std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FNEG, 8, d2u(-std::numeric_limits<double>::infinity()), d2u(-std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 51)
	{

TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 4, f2u(123.4891f), f2u(123.4891f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 4, f2u(123.4891f), f2u(-123.4891f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 4, f2u(1.0f), f2u(1.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 4, f2u(0.0f), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 4, f2u(0.0f), f2u(-0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 4, f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 4, f2u(std::numeric_limits<float>::infinity()), f2u(-std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 8, d2u(123.4891), d2u(123.4891), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 8, d2u(123.4891), d2u(-123.4891), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 8, d2u(1), d2u(1), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 8, d2u(0), d2u(0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 8, d2u(0), d2u(-0.0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 8, d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FABS, 8, d2u(std::numeric_limits<double>::infinity()), d2u(-std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 52)
	{

TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, 0x3f8e37e3, f2u(1.2345f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, 0x3f9ee7dc, f2u(1.5412f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-1.25987f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, f2u(1.0f), f2u(1.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, f2u(0.0f), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, f2u(-0.0f), f2u(-0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, 0x3ff1c6fc6778730e, d2u(1.2345), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, 0x3ff3dcfb79ff3135, d2u(1.5412), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-1.25987), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, d2u(1), d2u(1), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, d2u(0), d2u(0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, d2u(-0.0), d2u(-0.0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FSQRT, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);


	}
	else if (step == 53)
	{

TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 4, f2u(1.0f/1.2345f), f2u(1.2345f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 4, f2u(1.0f/1.5412f), f2u(1.5412f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 4, f2u(1.0f/-1.25987f), f2u(-1.25987f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 4, f2u(1.0f/1.0f), f2u(1.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 4, f2u(std::numeric_limits<float>::infinity()), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 4, f2u(std::numeric_limits<float>::infinity()), f2u(-0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 8, d2u(1/1.2345), d2u(1.2345), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 8, d2u(1/1.5412), d2u(1.5412), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 8, d2u(1/-1.25987), d2u(-1.25987), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 8, d2u(1/1), d2u(1), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 8, d2u(std::numeric_limits<double>::infinity()), d2u(0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 8, d2u(std::numeric_limits<double>::infinity()), d2u(-0.0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRECIP, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 54)
	{

TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 4, 0x3f666806, f2u(1.2345f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 4, 0x3f4e35d9, f2u(1.5412f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-1.25987f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 4, f2u(1.0f), f2u(1.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 4, f2u(std::numeric_limits<float>::infinity()), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 4, f2u(std::numeric_limits<float>::infinity()), f2u(-0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 8, 0x3feccd00b4e738b1, d2u(1.2345), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 8, 0x3fe9c6bb35ab1a2f, d2u(1.5412), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-1.25987), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 8, d2u(1/1), d2u(1), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 8, d2u(std::numeric_limits<double>::infinity()), d2u(0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 8, d2u(std::numeric_limits<double>::infinity()), d2u(-0.0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_2(block, uml::OP_FRSQRT, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 55)
	{

TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(1.0f), f2u(1.0f), 0, uml::FLAG_Z, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(0.0f), f2u(0.0f), 0, uml::FLAG_Z, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(-0.0f), f2u(0.0f), 0, uml::FLAG_Z, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(-0.0f), f2u(-0.0f), 0, uml::FLAG_Z, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(0.0f), f2u(-0.0f), 0, uml::FLAG_Z, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(1.0f), f2u(2.0f), 0, uml::FLAG_C, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, uml::FLAG_Z, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(-std::numeric_limits<float>::infinity()), f2u(-std::numeric_limits<float>::infinity()), 0, uml::FLAG_Z, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(std::numeric_limits<float>::infinity()), f2u(-std::numeric_limits<float>::infinity()), 0, 0, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, uml::FLAG_U | FLAGS_UNDEFINED_OTHER, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), 0, uml::FLAG_U | FLAGS_UNDEFINED_OTHER, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 4, f2u(-std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), 0, uml::FLAG_U | FLAGS_UNDEFINED_OTHER, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(1.0), d2u(1.0), 0, uml::FLAG_Z, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(0.0), d2u(0.0), 0, uml::FLAG_Z, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(-0.0), d2u(0.0), 0, uml::FLAG_Z, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(-0.0), d2u(-0.0), 0, uml::FLAG_Z, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(0.0), d2u(-0.0), 0, uml::FLAG_Z, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(1.0), d2u(2.0), 0, uml::FLAG_C, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(std::numeric_limits<double>::infinity()), d2u(-std::numeric_limits<double>::infinity()), 0, 0, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(-std::numeric_limits<double>::infinity()), d2u(-std::numeric_limits<double>::infinity()), 0, uml::FLAG_Z, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, uml::FLAG_Z, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, uml::FLAG_U | FLAGS_UNDEFINED_OTHER, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), 0, uml::FLAG_U | FLAGS_UNDEFINED_OTHER, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_CMP(block, uml::OP_FCMP, 8, d2u(-std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), 0, uml::FLAG_U | FLAGS_UNDEFINED_OTHER, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 56)
	{

TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(3.0f), f2u(1.5f), f2u(1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(-3.0f), f2u(-1.5f), f2u(-1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(0.0f), f2u(-1.5f), f2u(1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(0.0f), f2u(0.0f), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::infinity()), f2u(-std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(3.0), d2u(1.5), d2u(1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(-3.0), d2u(-1.5), d2u(-1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(0.0), d2u(-1.5), d2u(1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(0.0), d2u(0.0), d2u(0.0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::infinity()), d2u(-std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FADD, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 57)
	{

TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 4, f2u(0.0f), f2u(1.5f), f2u(1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 4, f2u(0.0f), f2u(-1.5f), f2u(-1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 4, f2u(-3.0f), f2u(-1.5f), f2u(1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 4, f2u(0.0f), f2u(0.0f), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 4, f2u(std::numeric_limits<float>::infinity()), f2u(-std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 8, d2u(0.0), d2u(1.5), d2u(1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 8, d2u(0.0), d2u(-1.5), d2u(-1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 8, d2u(-3.0), d2u(-1.5), d2u(1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 8, d2u(0.0), d2u(0), d2u(0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 8, d2u(std::numeric_limits<double>::infinity()), d2u(-std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FSUB, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 58)
	{

TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(2.25f), f2u(1.5f), f2u(1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(2.25f), f2u(-1.5f), f2u(-1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(-2.25f), f2u(-1.5f), f2u(1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(-0.0f), f2u(-1.5f), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(0.0f), f2u(-1.5f), f2u(-0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(0.0f), f2u(0.0f), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(std::numeric_limits<float>::infinity()), f2u(-std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(2.25), d2u(1.5), d2u(1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(2.25), d2u(-1.5), d2u(-1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(-2.25), d2u(-1.5), d2u(1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(-0.0), d2u(-1.5), d2u(0.0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(0.0), d2u(-1.5), d2u(-0.0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(0.0), d2u(0), d2u(0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(std::numeric_limits<double>::infinity()), d2u(-std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FMUL, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 59)
	{

TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(1.0f), f2u(1.5f), f2u(1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(1.0f), f2u(-1.5f), f2u(-1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(-1.0f), f2u(-1.5f), f2u(1.5f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(std::numeric_limits<float>::infinity()), f2u(-1.5f), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(std::numeric_limits<float>::infinity()), f2u(-1.5f), f2u(-0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(0.0f), f2u(0.0f), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::infinity()), f2u(std::numeric_limits<float>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 4, f2u(std::numeric_limits<float>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_SHORT, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(1.0), d2u(1.5), d2u(1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(1.0), d2u(-1.5), d2u(-1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(-1.0), d2u(-1.5), d2u(1.5), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(std::numeric_limits<double>::infinity()), d2u(-1.5), d2u(0.0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(std::numeric_limits<double>::infinity()), d2u(-1.5), d2u(-0.0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(0), d2u(0), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::infinity()), d2u(std::numeric_limits<double>::infinity()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3(block, uml::OP_FDIV, 8, d2u(std::numeric_limits<double>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE, uml::SIZE_DOUBLE);

	}
	else if (step == 60)
	{

TEST_ENTRY_MOV(block, uml::OP_MOV, 4, 0x00000000);
TEST_ENTRY_MOV(block, uml::OP_MOV, 4, 0xffffffff);
TEST_ENTRY_MOV(block, uml::OP_MOV, 4, 0x12345678);
TEST_ENTRY_MOV(block, uml::OP_MOV, 8, 0x0000000000000000);
TEST_ENTRY_MOV(block, uml::OP_MOV, 8, 0xffffffffffffffff);
TEST_ENTRY_MOV(block, uml::OP_MOV, 8, 0x1234567890abcdef);

	}
	else if (step == 61)
	{

TEST_ENTRY_FMOV(block, uml::OP_FMOV, 4, f2u(0.0));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 4, f2u(1.0));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 4, f2u(123.45));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 4, f2u(std::numeric_limits<float>::infinity()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 4, f2u(-std::numeric_limits<float>::infinity()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 4, f2u(std::numeric_limits<float>::quiet_NaN()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 4, f2u(-std::numeric_limits<float>::quiet_NaN()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 4, f2u(std::numeric_limits<float>::signaling_NaN()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 4, f2u(-std::numeric_limits<float>::signaling_NaN()));

TEST_ENTRY_FMOV(block, uml::OP_FMOV, 8, d2u(0.0));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 8, d2u(1.0));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 8, d2u(123.45));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 8, d2u(std::numeric_limits<double>::infinity()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 8, d2u(-std::numeric_limits<double>::infinity()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 8, d2u(std::numeric_limits<double>::quiet_NaN()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 8, d2u(-std::numeric_limits<double>::quiet_NaN()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 8, d2u(std::numeric_limits<double>::signaling_NaN()));
TEST_ENTRY_FMOV(block, uml::OP_FMOV, 8, d2u(-std::numeric_limits<double>::signaling_NaN()));

	}
	else if (step == 62)
	{
	// OPINFO1(SET,     "!set",     4|8, true,  NONE, NONE, ALL,  PINFO(OUT, OP, IRM))

		struct condition_test {
			uml::condition_t condition;
			uint64_t result;
			uint64_t flags;
		};

		condition_test conditions[] = {
			{ uml::COND_C, 1, uint64_t(uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_U | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_U | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_C, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },

			{ uml::COND_NC, 1, uint64_t(0) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_V) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_Z) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_S) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_U) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_U | uml::FLAG_V) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_U | uml::FLAG_S) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_NC, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_NC, 0, uint64_t(uml::FLAG_C) },

			{ uml::COND_V, 1, uint64_t(uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_U | uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_U | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_V, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },

			{ uml::COND_NV, 1, uint64_t(0) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_C) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_Z) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_S) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_U) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_U | uml::FLAG_C) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_U | uml::FLAG_S) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_NV, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_NV, 0, uint64_t(uml::FLAG_V) },

			{ uml::COND_Z, 1, uint64_t(uml::FLAG_Z) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_Z, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },

			{ uml::COND_NZ, 1, uint64_t(0) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_C) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_V) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_S) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_U) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_U | uml::FLAG_C) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_U | uml::FLAG_V) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_U | uml::FLAG_S) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_U | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_NZ, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_NZ, 0, uint64_t(uml::FLAG_Z) },

			{ uml::COND_S, 1, uint64_t(uml::FLAG_S) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_U | uml::FLAG_S) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_S, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },

			{ uml::COND_NS, 1, uint64_t(0) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_C) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_V) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_Z) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_U) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_U | uml::FLAG_C) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_U | uml::FLAG_V) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_U | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_NS, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_NS, 0, uint64_t(uml::FLAG_S) },

			{ uml::COND_U, 1, uint64_t(uml::FLAG_U) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_C) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_V) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_S) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_U, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },

			{ uml::COND_NU, 1, uint64_t(0) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_C) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_V) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_Z) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_S) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_NU, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_NU, 0, uint64_t(uml::FLAG_U) },

			{ uml::COND_A, 1, uint64_t(0) },
			{ uml::COND_A, 1, uint64_t(uml::FLAG_V) },
			{ uml::COND_A, 1, uint64_t(uml::FLAG_S) },
			{ uml::COND_A, 1, uint64_t(uml::FLAG_U) },
			{ uml::COND_A, 1, uint64_t(uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_A, 1, uint64_t(uml::FLAG_U | uml::FLAG_V) },
			{ uml::COND_A, 1, uint64_t(uml::FLAG_U | uml::FLAG_S) },
			{ uml::COND_A, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_A, 0, uint64_t(uml::FLAG_C) },
			{ uml::COND_A, 0, uint64_t(uml::FLAG_Z) },

			{ uml::COND_BE, 1, uint64_t(uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_Z) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_BE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_BE, 0, uint64_t(0) },

			{ uml::COND_G, 1, uint64_t(0) },
			{ uml::COND_G, 1, uint64_t(uml::FLAG_C) },
			{ uml::COND_G, 1, uint64_t(uml::FLAG_U) },
			{ uml::COND_G, 1, uint64_t(uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_G, 1, uint64_t(uml::FLAG_U | uml::FLAG_C) },
			{ uml::COND_G, 1, uint64_t(uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_G, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_G, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_G, 0, uint64_t(uml::FLAG_V) },
			{ uml::COND_G, 0, uint64_t(uml::FLAG_Z) },
			{ uml::COND_G, 0, uint64_t(uml::FLAG_S) },

			{ uml::COND_LE, 1, uint64_t(uml::FLAG_V) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_Z) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_S) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_V) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_LE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_LE, 0, uint64_t(0) },
			{ uml::COND_LE, 0, uint64_t(uml::FLAG_C) },
			{ uml::COND_LE, 0, uint64_t(uml::FLAG_U) },

			{ uml::COND_L, 1, uint64_t(uml::FLAG_V) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_S) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_U | uml::FLAG_V) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_U | uml::FLAG_S) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_U | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C | uml::FLAG_V) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_C) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z) },
			{ uml::COND_L, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_L, 0, uint64_t(0) },
			{ uml::COND_L, 0, uint64_t(uml::FLAG_C) },
			{ uml::COND_L, 0, uint64_t(uml::FLAG_Z) },
			{ uml::COND_L, 0, uint64_t(uml::FLAG_U) },

			{ uml::COND_GE, 1, uint64_t(0) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_C) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_Z) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_U) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_U | uml::FLAG_C) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_U | uml::FLAG_Z | uml::FLAG_C) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V) },
			{ uml::COND_GE, 1, uint64_t(uml::FLAG_U | uml::FLAG_S | uml::FLAG_Z | uml::FLAG_V | uml::FLAG_C) },
			{ uml::COND_GE, 0, uint64_t(uml::FLAG_V) },
			{ uml::COND_GE, 0, uint64_t(uml::FLAG_S) },
		};

		for (auto idx = 0; idx < std::size(conditions); idx++)
		{
			TEST_ENTRY_COND(block, uml::OP_SET, 4, conditions[idx].condition, conditions[idx].result, conditions[idx].flags);
			TEST_ENTRY_COND(block, uml::OP_SET, 8, conditions[idx].condition, conditions[idx].result, conditions[idx].flags);
		}
	}
	else if (step == 63)
	{

	// OPINFO4(FTOINT,  "f#toint",  4|8, false, NONE, NONE, ALL,  PINFO(OUT, P3, IRM), PINFO(IN, OP, FANY), PINFO(IN, OP, SIZE), PINFO(IN, OP, ROUND))
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 1, f2u(1.60f), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.60f), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -123, f2u(-123.60f), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.60f), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.60f), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 2, f2u(1.60f), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.20f), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -124, f2u(-123.50f), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 124, f2u(123.50f), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.0f), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 2, f2u(1.60f), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 124, f2u(123.20f), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -123, f2u(-123.50f), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 124, f2u(123.50f), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.0f), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 1, f2u(1.60f), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.20f), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -124, f2u(-123.50f), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.50f), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.0f), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);

// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 1, f2u(1.60f), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.20f), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -123, f2u(-123.50f), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.50f), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.0f), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);


TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 1, f2u(1.60f), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.60f), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -123, f2u(-123.60f), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.60f), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.60f), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 2, f2u(1.60f), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.20f), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -124, f2u(-123.50f), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 124, f2u(123.50f), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.0f), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 2, f2u(1.60f), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 124, f2u(123.20f), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -123, f2u(-123.50f), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 124, f2u(123.50f), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.0f), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 1, f2u(1.60f), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.20f), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -124, f2u(-123.50f), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.50f), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.0f), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);

// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 1, f2u(1.60f), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.20f), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, -123, f2u(-123.50f), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 123, f2u(123.50f), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 4, 999999, f2u(999999.0f), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT);


TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 1, d2u(1.60), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.60), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, uint32_t(-123), d2u(-123.60), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.60), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.60), uml::SIZE_DWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 2, d2u(1.60), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.20), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, uint32_t(-124), d2u(-123.50), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 124, d2u(123.50), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.0), uml::SIZE_DWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 2, d2u(1.60), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 124, d2u(123.20), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, uint32_t(-123), d2u(-123.50), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 124, d2u(123.50), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.0), uml::SIZE_DWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 1, d2u(1.60), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.20), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, uint32_t(-124), d2u(-123.50), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.50), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.0), uml::SIZE_DWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);

// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 1, d2u(1.60), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.20), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, uint32_t(-123), d2u(-123.50), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.50), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.0), uml::SIZE_DWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);


TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 1, d2u(1.60), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.60), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, -123, d2u(-123.60), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.60), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.60), uml::SIZE_QWORD, uml::ROUND_TRUNC, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 2, d2u(1.60), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.20), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, -124, d2u(-123.50), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 124, d2u(123.50), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.0), uml::SIZE_QWORD, uml::ROUND_ROUND, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 2, d2u(1.60), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 124, d2u(123.20), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, -123, d2u(-123.50), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 124, d2u(123.50), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.0), uml::SIZE_QWORD, uml::ROUND_CEIL, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);

TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 1, d2u(1.60), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.20), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, -124, d2u(-123.50), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.50), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.0), uml::SIZE_QWORD, uml::ROUND_FLOOR, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);

// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 1, d2u(1.60), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.20), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, -123, d2u(-123.50), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 123, d2u(123.50), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);
// TEST_ENTRY_FLOAT_4_SIZE(block, uml::OP_FTOINT, 8, 999999, d2u(999999.0), uml::SIZE_QWORD, uml::ROUND_DEFAULT, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE);

	}
	else if (step == 64)
	{
	// OPINFO3(FFRINT,  "f#frint",  4|8, false, NONE, NONE, ALL,  PINFO(OUT, OP, FRM), PINFO(IN, P3, IANY), PINFO(IN, OP, SIZE))
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 4, f2u(1.0f), 1, uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 4, f2u(123.0f), 123, uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 4, f2u(-123.0f), -123, uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 4, f2u(999999.0f), 999999, uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 4, f2u(1.0f), 1, uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 4, f2u(123.0f), 123, uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 4, f2u(-123.0f), -123, uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 4, f2u(999999.0f), 999999, uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, 0);

TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 8, d2u(1.0), 1, uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 8, d2u(123), 123, uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 8, d2u(-123), -123, uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 8, d2u(999999), 999999, uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 8, d2u(1.0), 1, uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 8, d2u(123), 123, uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 8, d2u(-123), -123, uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 8, d2u(999999), 999999, uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, 0);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRINT, 8, d2u(999999), 999999, uml::SIZE_DOUBLE, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, 0);

	}
	else if (step == 65)
	{
	// OPINFO3(FFRFLT,  "f#frflt",  4|8, false, NONE, NONE, ALL,  PINFO(OUT, OP, FRM), PINFO(IN, P3, FANY), PINFO(IN, OP, SIZE))

TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 4, f2u(1.5f), d2u(1.5), uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 4, f2u(0.0f), d2u(0.0), uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 4, f2u(-123.45f), d2u(-123.45), uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 4, f2u(123.45f), d2u(123.45), uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 4, f2u(std::numeric_limits<float>::quiet_NaN()), d2u(std::numeric_limits<double>::quiet_NaN()), uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 4, f2u(-std::numeric_limits<float>::quiet_NaN()), d2u(-std::numeric_limits<double>::quiet_NaN()), uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 4, f2u(std::numeric_limits<float>::infinity()), d2u(std::numeric_limits<double>::infinity()), uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_DOUBLE);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 4, f2u(-std::numeric_limits<float>::infinity()), d2u(-std::numeric_limits<double>::infinity()), uml::SIZE_QWORD, 0, FLAGS_UNCHANGED, uml::SIZE_SHORT, uml::SIZE_DOUBLE);

TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 8, d2u(1.5), f2u(1.5f), uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 8, d2u(0.0), f2u(0.0f), uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 8, d2u(-123.45), f2u(-123.45f), uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 8, d2u(123.45), f2u(123.45f), uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 8, d2u(std::numeric_limits<double>::quiet_NaN()), f2u(std::numeric_limits<float>::quiet_NaN()), uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 8, d2u(-std::numeric_limits<double>::quiet_NaN()), f2u(-std::numeric_limits<float>::quiet_NaN()), uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 8, d2u(std::numeric_limits<double>::infinity()), f2u(std::numeric_limits<float>::infinity()), uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_SHORT);
TEST_ENTRY_FLOAT_3_SIZE(block, uml::OP_FFRFLT, 8, d2u(-std::numeric_limits<double>::infinity()), f2u(-std::numeric_limits<float>::infinity()), uml::SIZE_DWORD, 0, FLAGS_UNCHANGED, uml::SIZE_DOUBLE, uml::SIZE_SHORT);

	}
	else if (step == 66)
	{

TEST_MAPVAR_CONSTANT(block, uml::M2, 2345);

TEST_MAPVAR_RECOVER(block, uml::M1, 1234, 0);

	}
	else if (step == 67)
	{

TEST_MAPVAR_RECOVER(block, uml::M1, 1234, 1);

	}
	else if (step == 68)
	{
		drcuml_machine_state *output = (drcuml_machine_state*)calloc(1, sizeof(drcuml_machine_state));

		UML_DMOV(block, uml::I0, d2u(1.2345));
		UML_FDCOPYI(block, uml::F0, uml::I0);
		UML_DMOV(block, uml::I0, d2u(2.2345));
		UML_FDCOPYI(block, uml::F1, uml::I0);
		UML_DMOV(block, uml::I0, d2u(3.2345));
		UML_FDCOPYI(block, uml::F2, uml::I0);
		UML_DMOV(block, uml::I0, d2u(4.2345));
		UML_FDCOPYI(block, uml::F3, uml::I0);
		UML_DMOV(block, uml::I0, d2u(5.2345));
		UML_FDCOPYI(block, uml::F4, uml::I0);
		UML_DMOV(block, uml::I0, d2u(6.2345));
		UML_FDCOPYI(block, uml::F5, uml::I0);
		UML_DMOV(block, uml::I0, d2u(7.2345));
		UML_FDCOPYI(block, uml::F6, uml::I0);
		UML_DMOV(block, uml::I0, d2u(8.2345));
		UML_FDCOPYI(block, uml::F7, uml::I0);
		UML_DMOV(block, uml::I0, d2u(9.2345));
		UML_FDCOPYI(block, uml::F8, uml::I0);
		UML_DMOV(block, uml::I0, d2u(10.2345));
		UML_FDCOPYI(block, uml::F9, uml::I0);

		UML_MOV(block, uml::I0, 0x123);
		UML_MOV(block, uml::I1, 0x12443);
		UML_MOV(block, uml::I2, 0x234);
		UML_MOV(block, uml::I3, 0x345);
		UML_MOV(block, uml::I4, 0x456);
		UML_MOV(block, uml::I5, 0x1123);
		UML_MOV(block, uml::I6, 0x2234);
		UML_MOV(block, uml::I7, 0x3345);
		UML_MOV(block, uml::I8, 0x4456);
		UML_MOV(block, uml::I9, 0x5123);

		UML_SETFLGS(block, uml::FLAG_U | uml::FLAG_Z | uml::FLAG_S);
		UML_SETFMOD(block, 1);
		UML_SAVE(block, output);

		UML_CALLC(block, cfunc_dump_machine_state, output);
	}
	else if (step == 69)
	{
		static drcuml_machine_state input = {
			.r = {
				{ .d = 0x00000123 },
				{ .d = 0x00012443 },
				{ .d = 0x00000234 },
				{ .d = 0x00000345 },
				{ .d = 0x00000456 },
				{ .d = 0x00001123 },
				{ .d = 0x00002234 },
				{ .d = 0x00003345 },
				{ .d = 0x00004456 },
				{ .d = 0x00005123 },
			},
			.f = {
				{ .d = 1.234500 },
				{ .d = 2.234500 },
				{ .d = 3.234500 },
				{ .d = 4.234500 },
				{ .d = 5.234500 },
				{ .d = 6.234500 },
				{ .d = 7.234500 },
				{ .d = 8.234500 },
				{ .d = 9.234500 },
				{ .d = 10.234500 },
			},
			.exp = 0x12345678,
			.fmod = 3,
			.flags = 0x1f,
		};

		UML_SETFLGS(block, 0);
		UML_SETFMOD(block, 0);

		UML_RESTORE(block, &input);

		drcuml_machine_state *output = (drcuml_machine_state*)calloc(1, sizeof(drcuml_machine_state));
		UML_SAVE(block, output);

		UML_CALLC(block, cfunc_dump_machine_state, output);
	}
	else if (step == 70)
	{
		// UML_MOV(block, uml::I0, 123);
		// UML_DADD(block, uml::I0, uml::I0, 4);

		UML_DMOV(block, uml::I0, 0x2222222212348765);

		UML_DAND(block, mem(&m_state->value), mem(&m_state->value), 0xffffffff00000000U);
		UML_AND(block, uml::I0, mem(&m_state->value2), 0xffffffff);
		UML_DOR(block, mem(&m_state->value), mem(&m_state->value), uml::I0);

		UML_DMOV(block, mem(&m_state->testval), mem(&m_state->value));

		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
	}
	else if (step == 71)
	{
		static uint64_t readdata = 0x8f;
		static uint32_t datawrite32[5] = {0};
		static uint64_t datawrite64[5] = {0};
		m_state->testval = 0;

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_LOAD(block, mem(&m_state->testval), &readdata, 0, uml::SIZE_BYTE, uml::SCALE_x1);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
		UML_DLOAD(block, mem(&m_state->testval), &readdata, 0, uml::SIZE_BYTE, uml::SCALE_x1);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
		UML_STORE(block, &datawrite32[0], 0, uml::I0, uml::SIZE_BYTE, uml::SCALE_x4);
		UML_STORE(block, &datawrite32[0], 2, uml::I0, uml::SIZE_WORD, uml::SCALE_x4);
		UML_STORE(block, &datawrite32[0], 4, uml::I0, uml::SIZE_DWORD, uml::SCALE_x4);
		for (int i = 0; i < std::size(datawrite32); i++)
		{
			datawrite32[i] = 0;
			UML_CALLC(block, cfunc_print_val32, &datawrite32[i]);
		}

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_LOADS(block, mem(&m_state->testval), &readdata, 0, uml::SIZE_BYTE, uml::SCALE_x1);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
		UML_DLOADS(block, mem(&m_state->testval), &readdata, 0, uml::SIZE_BYTE, uml::SCALE_x1);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
		UML_DSTORE(block, &datawrite64[0], 0, uml::I0, uml::SIZE_BYTE, uml::SCALE_x8);
		UML_DSTORE(block, &datawrite64[0], 1, uml::I0, uml::SIZE_WORD, uml::SCALE_x8);
		UML_DSTORE(block, &datawrite64[0], 3, uml::I0, uml::SIZE_DWORD, uml::SCALE_x8);
		UML_DSTORE(block, &datawrite64[0], 4, uml::I0, uml::SIZE_QWORD, uml::SCALE_x8);
		for (int i = 0; i < std::size(datawrite64); i++)
		{
			datawrite64[i] = 0;
			UML_CALLC(block, cfunc_print_val64, &datawrite64[i]);
		}

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_LOAD(block, uml::I0, &readdata, 0, uml::SIZE_BYTE, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_LOADS(block, uml::I0, &readdata, 0, uml::SIZE_BYTE, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOAD(block, uml::I0, &readdata, 0, uml::SIZE_BYTE, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOADS(block, uml::I0, &readdata, 0, uml::SIZE_BYTE, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_LOAD(block, uml::I0, &readdata, 0, uml::SIZE_WORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_LOADS(block, uml::I0, &readdata, 0, uml::SIZE_WORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOAD(block, uml::I0, &readdata, 0, uml::SIZE_WORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOADS(block, uml::I0, &readdata, 0, uml::SIZE_WORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_LOAD(block, uml::I0, &readdata, 0, uml::SIZE_DWORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_LOADS(block, uml::I0, &readdata, 0, uml::SIZE_DWORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOAD(block, uml::I0, &readdata, 0, uml::SIZE_DWORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOADS(block, uml::I0, &readdata, 0, uml::SIZE_DWORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		// UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		// UML_LOAD(block, uml::I0, &readdata, 0, uml::SIZE_QWORD, uml::SCALE_x1);
		// UML_DMOV(block, mem(&m_state->testval), uml::I0);
		// UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		// UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		// UML_LOADS(block, uml::I0, &readdata, 0, uml::SIZE_QWORD, uml::SCALE_x1);
		// UML_DMOV(block, mem(&m_state->testval), uml::I0);
		// UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOAD(block, uml::I0, &readdata, 0, uml::SIZE_QWORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOADS(block, uml::I0, &readdata, 0, uml::SIZE_QWORD, uml::SCALE_x1);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		static uint64_t readarr[5] = {0x1234567890abcdef, 0x234567890abcdef1, 0x34567890abcdef12, 0x4567890abcdef123, 0x567890abcdef1234};
		// UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		// UML_LOAD(block, uml::I0, &readarr[0], 0, uml::SIZE_QWORD, uml::SCALE_x4);
		// UML_DMOV(block, mem(&m_state->testval), uml::I0);
		// UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		// UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		// UML_LOADS(block, uml::I0, &readarr[0], 4, uml::SIZE_QWORD, uml::SCALE_x4);
		// UML_DMOV(block, mem(&m_state->testval), uml::I0);
		// UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOAD(block, uml::I0, &readarr[0], 4, uml::SIZE_QWORD, uml::SCALE_x4);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOADS(block, uml::I0, &readarr[0], 4, uml::SIZE_QWORD, uml::SCALE_x4);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		// UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		// UML_LOAD(block, uml::I0, &readarr[0], 2, uml::SIZE_QWORD, uml::SCALE_x8);
		// UML_DMOV(block, mem(&m_state->testval), uml::I0);
		// UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		// UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		// UML_LOADS(block, uml::I0, &readarr[0], 2, uml::SIZE_QWORD, uml::SCALE_x8);
		// UML_DMOV(block, mem(&m_state->testval), uml::I0);
		// UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOAD(block, uml::I0, &readarr[0], 2, uml::SIZE_QWORD, uml::SCALE_x8);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, uml::I0, 0x1234567890abcdef);
		UML_DLOADS(block, uml::I0, &readarr[0], 2, uml::SIZE_QWORD, uml::SCALE_x8);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
	}
	else if (step == 72)
	{

		static uint64_t fdata = f2u(123.4f);
		static uint32_t fdatawrite[5] = {0};
		UML_FSLOAD(block, uml::F0, &fdata, 0);
		UML_FSSTORE(block, &fdatawrite[0], 0, uml::F0);
		UML_FSSTORE(block, &fdatawrite[0], 3, uml::F0);
		for (int i = 0; i < std::size(fdatawrite); i++)
			UML_CALLC(block, cfunc_print_val32, &fdatawrite[i]);

		static uint64_t ddata = d2u(123.4);
		static uint64_t ddatawrite[5] = {0};
		UML_FDLOAD(block, uml::F0, &ddata, 0);
		UML_FDSTORE(block, &ddatawrite[0], 0, uml::F0);
		UML_FDSTORE(block, &ddatawrite[0], 2, uml::F0);
		UML_FDSTORE(block, &ddatawrite[0], 4, uml::F0);
		for (int i = 0; i < std::size(fdatawrite); i++)
			UML_CALLC(block, cfunc_print_val64, &ddatawrite[i]);
	}
	else if (step == 73)
	{
		m_program->write_qword(0, 0x12345678);
		m_program->write_qword(8, 0x1234567890abcdef);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 0);
		UML_READ(block, uml::I0, uml::I0, SIZE_DWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_READ(block, uml::I0, 0, SIZE_DWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 0);
		UML_READ(block, mem(&m_state->testval), uml::I0, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_READ(block, mem(&m_state->testval), 0, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 8);
		UML_DREAD(block, uml::I0, uml::I0, SIZE_QWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DREAD(block, uml::I0, 8, SIZE_QWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 8);
		UML_DREAD(block, mem(&m_state->testval), uml::I0, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DREAD(block, mem(&m_state->testval), 8, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
	}
	else if (step == 74)
	{
		m_program->write_qword(0, 0x12345678);
		m_program->write_qword(8, 0x1234567890abcdef);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 0);
		UML_DMOV(block, uml::I1, 0xff00ff);
		UML_READM(block, uml::I0, uml::I0, uml::I1, SIZE_DWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_READM(block, uml::I0, 0, 0xff00ff, SIZE_DWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 0);
		UML_DMOV(block, uml::I1, 0xff00ff);
		UML_READM(block, mem(&m_state->testval), uml::I0, uml::I1, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_READM(block, mem(&m_state->testval), 0, 0xff00ff, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 8);
		UML_DMOV(block, uml::I1, 0xffffffffffffffff);
		UML_DREADM(block, uml::I0, uml::I0, uml::I1, SIZE_QWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DREADM(block, uml::I0, 8, 0xffffffffffffffff, SIZE_QWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 8);
		UML_DMOV(block, uml::I1, 0xffffffffffffffff);
		UML_DREADM(block, mem(&m_state->testval), uml::I0, uml::I1, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DREADM(block, mem(&m_state->testval), 8, 0xffffffffffffffff, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
	}
	else if (step == 75)
	{
		auto f = f2u(123.4f);
		auto d = d2u(123.4);

		m_program->write_qword(0, f);
		m_program->write_qword(8, d);

		printf("f: %08x\nd: %016llx\n", f, d);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 0);
		UML_FSREAD(block, uml::F0, uml::I0, SPACE_PROGRAM);
		UML_FSMOV(block, mem(&m_state->testval), uml::F0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_FSREAD(block, uml::F0, 0, SPACE_PROGRAM);
		UML_FSMOV(block, mem(&m_state->testval), uml::F0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 0);
		UML_FSREAD(block, mem(&m_state->testval), uml::I0, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_FSREAD(block, mem(&m_state->testval), 0, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 8);
		UML_FDREAD(block, uml::F0, uml::I0, SPACE_PROGRAM);
		UML_FDMOV(block, mem(&m_state->testval), uml::F0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_FDREAD(block, uml::F0, 8, SPACE_PROGRAM);
		UML_FDMOV(block, mem(&m_state->testval), uml::F0);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_DMOV(block, uml::I0, 8);
		UML_FDREAD(block, mem(&m_state->testval), uml::I0, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DMOV(block, mem(&m_state->testval), 0);
		UML_FDREAD(block, mem(&m_state->testval), 8, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
	}
	else if (step == 76)
	{
// OPINFO3(WRITE,   "!write",   4|8, false, NONE, NONE, ALL,  PINFO(IN, 4, IANY), PINFO(IN, OP, IANY), PINFO(IN, OP, SPSIZE))

		UML_DMOV(block, uml::I0, 0);
		UML_DMOV(block, uml::I1, 0x5577557755775511);
		UML_WRITE(block, uml::I0, uml::I1, SIZE_DWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);

		UML_WRITE(block, 8, 0x5577557755775522, SIZE_DWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);

		UML_DMOV(block, uml::I0, 16);
		UML_DMOV(block, uml::I1, 0x5577557755775533);
		UML_DWRITE(block, uml::I0, uml::I1, SIZE_QWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);

		UML_DWRITE(block, 24, 0x5577557755775544, SIZE_QWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);

		UML_READ(block, mem(&m_state->testval), 0, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_READ(block, mem(&m_state->testval), 8, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DREAD(block, mem(&m_state->testval), 16, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DREAD(block, mem(&m_state->testval), 24, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

	}
	else if (step == 77)
	{
// OPINFO4(WRITEM,  "!writem",  4|8, false, NONE, NONE, ALL,  PINFO(IN, 4, IANY), PINFO(IN, OP, IANY), PINFO(IN, OP, IANY), PINFO(IN, OP, SPSIZE))

		UML_DMOV(block, uml::I0, 0);
		UML_DMOV(block, uml::I1, 0x5577557755776611);
		UML_DMOV(block, uml::I2, 0xffffffff);
		UML_WRITEM(block, uml::I0, uml::I1, uml::I2, SIZE_DWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);

		UML_WRITEM(block, 8, 0x5577557755776622, 0xffffffff, SIZE_DWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);

		UML_DMOV(block, uml::I0, 16);
		UML_DMOV(block, uml::I1, 0x5577557755776633);
		UML_DMOV(block, uml::I2, 0xffffffffffffffff);
		UML_DWRITEM(block, uml::I0, uml::I1, uml::I2, SIZE_QWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);

		UML_DWRITEM(block, 24, 0x5577557755776644, 0xffffffffffffffff, SIZE_QWORD, SPACE_PROGRAM);
		UML_DMOV(block, mem(&m_state->testval), uml::I0);

		UML_READ(block, mem(&m_state->testval), 0, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_READ(block, mem(&m_state->testval), 8, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DREAD(block, mem(&m_state->testval), 16, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DREAD(block, mem(&m_state->testval), 24, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
	}
	else if (step == 78)
	{
// OPINFO3(FWRITE,  "f#write",  4|8, false, NONE, NONE, ALL,  PINFO(IN, 4, IANY), PINFO(IN, OP, FANY), PINFO(IN, OP, SPSIZE))

		uint64_t f = 0x42f6cccd;
		uint64_t d = 0x405ed9999999999a;

		UML_DMOV(block, uml::I0, 0);
		UML_DMOV(block, uml::I1, f);
		UML_FSCOPYI(block, uml::F0, uml::I1);
		UML_FSWRITE(block, uml::I0, uml::F0, SPACE_PROGRAM);

		UML_DMOV(block, uml::I1, f);
		UML_FSCOPYI(block, uml::F0, uml::I1);
		UML_FSWRITE(block, 4, uml::F0, SPACE_PROGRAM);

		UML_MOV(block, mem(&m_state->testval), f);
		UML_FSWRITE(block, 8, mem(&m_state->testval), SPACE_PROGRAM);

		UML_DREAD(block, mem(&m_state->testval), 0, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DREAD(block, mem(&m_state->testval), 4, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DREAD(block, mem(&m_state->testval), 8, SIZE_DWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);


		UML_DMOV(block, uml::I0, 0);
		UML_DMOV(block, uml::I1, d);
		UML_FDCOPYI(block, uml::F0, uml::I1);
		UML_FDWRITE(block, uml::I0, uml::F0, SPACE_PROGRAM);

		UML_FDCOPYI(block, uml::F0, d);
		UML_DMOV(block, uml::I1, d);
		UML_FDCOPYI(block, uml::F0, uml::I1);
		UML_FDWRITE(block, 8, uml::F0, SPACE_PROGRAM);

		UML_DMOV(block, mem(&m_state->testval), d);
		UML_FDWRITE(block, 16, mem(&m_state->testval), SPACE_PROGRAM);

		UML_DREAD(block, mem(&m_state->testval), 0, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DREAD(block, mem(&m_state->testval), 8, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);

		UML_DREAD(block, mem(&m_state->testval), 16, SIZE_QWORD, SPACE_PROGRAM);
		UML_CALLC(block, cfunc_print_val64, &m_state->testval);
	}
	else
	{

		UML_CALLC(block, cfunc_exit, nullptr);
		exit(1);

	}
}

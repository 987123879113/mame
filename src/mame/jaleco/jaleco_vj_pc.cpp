// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
    Hardware to run VJ Windows PC side software

    Motherboard is similar to EP-5BVPXB but says REV 1.3 which matches NMC-5VXC
    but the board used does not have the NMC markings like the NMC-5VXC.

    VIA 82C586B PCI ISA/IDE bridge
    VIA 82C585VPX
    Winbond W83877F IO Core Logic

    S3 Virge/DX Q5C2BB

    UPS is connected to COM1 port

    CD-ROM: Mitsumi CRMC-FX3210S(?)
    Floppy: Unidentified but machine has floppy drive
*/

#include "emu.h"

#include "machine/pci.h"
#include "machine/pci-ide.h"
#include "machine/i82439hx.h"
#include "machine/i82439tx.h"
#include "machine/i82371sb.h"
#include "video/virge_pci.h"
#include "bus/isa/isa_cards.h"
#include "machine/fdc37c93x.h"

#include "jaleco_vj_pc.h"
#include "jaleco_vj_qtaro.h"
#include "jaleco_vj_sound.h"
#include "jaleco_vj_ups.h"


jaleco_vj_pc_device::jaleco_vj_pc_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
    device_t(mconfig, JALECO_VJ_PC, tag, owner, clock),
    m_maincpu(*this, "maincpu")
{
}

void jaleco_vj_pc_device::device_start()
{
}

void jaleco_vj_pc_device::device_reset()
{
}

static void isa_internal_devices(device_slot_interface &device)
{
    device.option_add("fdc37c93x", FDC37C93X);
}

static void isa_com(device_slot_interface &device)
{
    // TODO: Stepping Stage uses a different UPS but will still boot unlike VJ did without a valid UPS attached
    device.option_add("jaleco_vj_ups", JALECO_VJ_UPS);
}

static void isa_cards(device_slot_interface &device)
{
    device.option_add("jaleco_vj_sound", JALECO_VJ_ISA16_SOUND);
}

void jaleco_vj_pc_device::superio_config(device_t *device)
{
    fdc37c93x_device &fdc = *downcast<fdc37c93x_device *>(device);
    fdc.set_sysopt_pin(1);
    fdc.gp20_reset().set_inputline(m_maincpu, INPUT_LINE_RESET);
    fdc.gp25_gatea20().set_inputline(m_maincpu, INPUT_LINE_A20);
    fdc.irq1().set(":jaleco_vj_pc:pci:07.0", FUNC(i82371sb_isa_device::pc_irq1_w));
    fdc.irq8().set(":jaleco_vj_pc:pci:07.0", FUNC(i82371sb_isa_device::pc_irq8n_w));
    fdc.txd1().set(":jaleco_vj_pc:serport0", FUNC(rs232_port_device::write_txd));
    fdc.ndtr1().set(":jaleco_vj_pc:serport0", FUNC(rs232_port_device::write_dtr));
    fdc.nrts1().set(":jaleco_vj_pc:serport0", FUNC(rs232_port_device::write_rts));
    fdc.txd2().set(":jaleco_vj_pc:serport1", FUNC(rs232_port_device::write_txd));
    fdc.ndtr2().set(":jaleco_vj_pc:serport1", FUNC(rs232_port_device::write_dtr));
    fdc.nrts2().set(":jaleco_vj_pc:serport1", FUNC(rs232_port_device::write_rts));
}

void jaleco_vj_pc_device::boot_state_w(uint8_t data)
{
    logerror("Boot state %02x\n", data);
}

void jaleco_vj_pc_device::device_add_mconfig(machine_config &config)
{
    PENTIUM(config, m_maincpu, 20000000); // Pentium 60/90mhz? Underclocked for performance reasons
    m_maincpu->set_irq_acknowledge_callback("pci:07.0:pic8259_master", FUNC(pic8259_device::inta_cb));
    m_maincpu->smiact().set("pci:00.0", FUNC(i82439hx_host_device::smi_act_w));

    PCI_ROOT(config, "pci", 0);
    I82439HX(config, "pci:00.0", 0, m_maincpu, 256*1024*1024); // 0x05851106 VIA

    i82371sb_isa_device &isa(I82371SB_ISA(config, "pci:07.0", 0, m_maincpu));
    // isa.set_ids(0x05861106, 0x23, 0x060100, 0x00000000); // VIA VT82C586B, PCI-to-ISA Bridge
    isa.boot_state_hook().set(FUNC(jaleco_vj_pc_device::boot_state_w));
    isa.smi().set_inputline(m_maincpu, INPUT_LINE_SMI);

    i82371sb_ide_device &ide(I82371SB_IDE(config, "pci:07.1", 0, m_maincpu));
    // ide.set_ids(0x05711106, 0x06, 0x01018a, 0x00000000); // VIA VT82C586B, IDE Controller
    ide.irq_pri().set("pci:07.0", FUNC(i82371sb_isa_device::pc_irq14_w));
    ide.irq_sec().set("pci:07.0", FUNC(i82371sb_isa_device::pc_mirq0_w));

    // pci:07.3 0x30401106

    VIRGEDX_PCI(config, "pci:10.0", 0);

    jaleco_vj_king_qtaro_device& king_qtaro(JALECO_VJ_KING_QTARO(config, "pci:08.0", 0));
    king_qtaro.set_bus_master_space(m_maincpu, AS_PROGRAM);

    ISA16_SLOT(config, "board4", 0, "pci:07.0:isabus", isa_internal_devices, "fdc37c93x", true).set_option_machine_config("fdc37c93x", [this](device_t *device) { superio_config(device); });
    ISA16_SLOT(config, "isa1", 0, "pci:07.0:isabus", isa_cards, "jaleco_vj_sound", true);
    ISA16_SLOT(config, "isa2", 0, "pci:07.0:isabus", isa_cards, nullptr, true);
    ISA16_SLOT(config, "isa3", 0, "pci:07.0:isabus", isa_cards, nullptr, true);

    rs232_port_device& serport0(RS232_PORT(config, "serport0", isa_com, "jaleco_vj_ups"));
    serport0.rxd_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::rxd1_w));
    serport0.dcd_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::ndcd1_w));
    serport0.dsr_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::ndsr1_w));
    serport0.ri_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::nri1_w));
    serport0.cts_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::ncts1_w));

    rs232_port_device &serport1(RS232_PORT(config, "serport1", isa_com, nullptr));
    serport1.rxd_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::rxd2_w));
    serport1.dcd_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::ndcd2_w));
    serport1.dsr_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::ndsr2_w));
    serport1.ri_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::nri2_w));
    serport1.cts_handler().set("board4:fdc37c93x", FUNC(fdc37c93x_device::ncts2_w));
}

ROM_START(jaleco_vj_pc)
    ROM_REGION32_LE(0x40000, "pci:07.0", 0) /* PC bios */
    ROM_SYSTEM_BIOS(0, "m55ns04", "m55ns04") // Micronics M55HI-Plus with no sound
    ROMX_LOAD("m55-04ns.rom", 0x20000, 0x20000, CRC(0116b2b0) SHA1(19b0203decfd4396695334517488d488aec3ccde), ROM_BIOS(0))
ROM_END

static INPUT_PORTS_START(jaleco_vj_pc)
INPUT_PORTS_END

const tiny_rom_entry* jaleco_vj_pc_device::device_rom_region() const
{
    return ROM_NAME(jaleco_vj_pc);
}

ioport_constructor jaleco_vj_pc_device::device_input_ports() const
{
    return INPUT_PORTS_NAME(jaleco_vj_pc);
}

DEFINE_DEVICE_TYPE(JALECO_VJ_PC, jaleco_vj_pc_device, "jaleco_vj_pc", "Jaleco VJ PC")

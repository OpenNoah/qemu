/*
 * QEMU IRIVER Dicple D88 board support
 *
 * Emulates a very simple machine model similar to the one used by the
 * proprietary MIPS emulator.
 *
 * Copyright (c) 2024 Norman Zhi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/datadir.h"
#include "hw/clock.h"
#include "hw/qdev-clock.h"
#include "hw/mips/mips.h"
#include "hw/char/serial.h"
#include "hw/isa/isa.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/mips/bios.h"
#include "hw/loader.h"
#include "hw/irq.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "qemu/log.h"

#include "hw/misc/ingenic_cgu.h"
#include "hw/block/ingenic_emc.h"
#include "hw/block/ingenic_bch.h"
#include "hw/gpio/ingenic_gpio.h"
#include "hw/char/ingenic_uart.h"

typedef struct ResetData {
    MIPSCPU *cpu;
    uint64_t vector;
} ResetData;

static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUMIPSState *env = &s->cpu->env;

    cpu_reset(CPU(s->cpu));
    env->active_tc.PC = s->vector & ~(target_ulong)1;
}

static MIPSCPU *jz4755_init(MachineState *machine)
{
    MIPSCPU *cpu;
    CPUMIPSState *env;

    /* Needs to have clocks first */
    IngenicCgu *cgu = INGENIC_CGU(qdev_new(TYPE_INGENIC_CGU));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(cgu), &error_fatal);

    /* Init CPUs. */
    printf("%s cpu clock\n", __func__);
    // machine->cpu_type = "XBurstR1-mips-cpu";
    cpu = mips_cpu_create_with_clock(machine->cpu_type, qdev_get_clock_out(DEVICE(cgu), "clk-cclk"));
    env = &cpu->env;

    MemoryRegion *ahb0 = g_new(MemoryRegion, 1);
    MemoryRegion *ahb1 = g_new(MemoryRegion, 1);
    MemoryRegion *apb  = g_new(MemoryRegion, 1);

    /* Register AHB0 IO space at 0x13000000. */
    memory_region_init_io(ahb0, NULL, NULL, NULL, "ahb0", 0x00090000);
    memory_region_add_subregion(get_system_memory(), 0x13000000, ahb0);

    // Register EMC on AHB0 bus
    IngenicEmc *emc = INGENIC_EMC(qdev_new(TYPE_INGENIC_EMC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(emc), &error_fatal);
    MemoryRegion *emc_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(emc), 0);
    memory_region_add_subregion(ahb0, 0x00010000, emc_mr);

    /* Register AHB1 IO space at 0x13090000. */
    memory_region_init_io(ahb1, NULL, NULL, NULL, "ahb1", 0x00070000);
    memory_region_add_subregion(get_system_memory(), 0x13090000, ahb1);

    // Register BCH on AHB1 bus
    IngenicBch *bch = INGENIC_BCH(qdev_new(TYPE_INGENIC_BCH));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(bch), &error_fatal);
    MemoryRegion *bch_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(bch), 0);
    memory_region_add_subregion(ahb1, 0x00040000, bch_mr);

    /* Register APB IO space at 0x10000000. */
    memory_region_init_io(apb, NULL, NULL, NULL, "apb", 0x01000000);
    memory_region_add_subregion(get_system_memory(), 0x10000000, apb);

    /* Register CGU on APB bus */
    MemoryRegion *cgu_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(cgu), 0);
    memory_region_add_subregion(apb, 0, cgu_mr);

    // Register GPIOs on APB bus
    IngenicGpio *gpio[6];
    for (int i = 0; i < 6; i++) {
        char name[] = "PA";
        name[1] = 'A' + i;
        gpio[i] = INGENIC_GPIO(qdev_new(TYPE_INGENIC_GPIO));
        object_property_set_str(OBJECT(gpio[i]), "name", name, &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(gpio[i]), &error_fatal);
        MemoryRegion *gpio_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(gpio[i]), 0);
        memory_region_add_subregion(apb, 0x00010000 + i * 0x0100, gpio_mr);
    }

    /* Initialise 16550 UART0 at APB 0x00030000 interrupt ? */
    ingenic_uart_init(apb, 0x00030000, env->irq[4],
        115200, serial_hd(0), DEVICE_NATIVE_ENDIAN);

    /* Initialise 16550 UART1 at APB 0x00031000 interrupt ? */
    ingenic_uart_init(apb, 0x00031000, env->irq[5],
        115200, serial_hd(1), DEVICE_NATIVE_ENDIAN);

    /* Initialise 16550 UART2 at APB 0x00032000 interrupt ? */
    ingenic_uart_init(apb, 0x00032000, env->irq[6],
        115200, serial_hd(2), DEVICE_NATIVE_ENDIAN);

    /* Init CPU internal devices. */
    cpu_mips_irq_init_cpu(cpu);
    cpu_mips_clock_init(cpu);

    // Connect GPIOs
    // PC27: NAND RB
    qemu_irq nand_rb = qdev_get_gpio_in_named(DEVICE(gpio[2]), "in", 27);
    qdev_connect_gpio_out_named(DEVICE(emc), "nand-rb", 0, nand_rb);

    return cpu;
}

static void mips_iriver_d88_init(MachineState *machine)
{
    char *filename;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *bootrom = g_new(MemoryRegion, 1);
    MIPSCPU *cpu;
    CPUMIPSState *env;
    ResetData *reset_info;
    int bootrom_size;

    // Board-specific parameters
    //machine->ram_size = 64 * 1024 * 1024;

    /* Init CPUs. */
    cpu = jz4755_init(machine);
    env = &cpu->env;

    reset_info = g_new0(ResetData, 1);
    reset_info->cpu = cpu;
    reset_info->vector = env->active_tc.PC;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* Allocate RAM. */
    memory_region_init_rom(bootrom, NULL, "mips_iriver_d88.bootrom", 8 * 1024,
                           &error_fatal);

    memory_region_add_subregion(address_space_mem, 0LL, machine->ram);

    /* Map the BIOS / boot exception handler. */
    memory_region_add_subregion(address_space_mem, 0x1fc00000LL, bootrom);
    /* Load a BIOS / boot exception handler image. */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);
    if (filename) {
        bootrom_size = load_image_targphys(filename, 0x1fc00000LL, 8 * 1024);
        g_free(filename);
    } else {
        bootrom_size = -1;
    }
    if ((bootrom_size < 0 || bootrom_size > BIOS_SIZE) &&
        machine->firmware && !qtest_enabled()) {
        /* Bail out if we have neither a kernel image nor boot vector code. */
        error_report("Could not load MIPS bios '%s'", machine->firmware);
        exit(1);
    } else {
        /* We have a boot vector start address. */
        env->active_tc.PC = (target_long)(int32_t)0xbfc00000;
    }
}

static void mips_iriver_d88_machine_init(MachineClass *mc)
{
    mc->desc = "MIPS IRIVER Dicple D88 platform";
    mc->init = mips_iriver_d88_init;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("XBurstR1");
    mc->default_ram_id = "mips_iriver_d88.ram";
    mc->default_ram_size = 16 * 1024;
}

DEFINE_MACHINE("iriver_d88", mips_iriver_d88_machine_init)

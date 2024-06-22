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
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/mips/bios.h"
#include "hw/loader.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "qemu/log.h"
#include "hw/mips/ingenic_jz4755.h"

#include "hw/audio/ar1010.h"
#include "hw/audio/wm8731.h"
#include "hw/input/stmpe2403.h"

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

static void mips_iriver_d88_init(MachineState *machine)
{
    char *filename;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *bootrom = g_new(MemoryRegion, 1);
    IngenicJZ4755 *soc;
    CPUMIPSState *env;
    ResetData *reset_info;
    int bootrom_size;

    // Board-specific parameters
    //machine->ram_size = 64 * 1024 * 1024;

    /* Init CPUs. */
    soc = ingenic_jz4755_init(machine);
    env = &soc->cpu->env;

    reset_info = g_new0(ResetData, 1);
    reset_info->cpu = soc->cpu;
    reset_info->vector = env->active_tc.PC;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* Allocate RAM. */
    memory_region_init_rom(bootrom, NULL, "mips_iriver_d88.bootrom", 8 * 1024,
                           &error_fatal);

    /* Map the BIOS / boot exception handler. */
    memory_region_add_subregion(address_space_mem, 0x1fc00000LL, bootrom);
    /* Load a BIOS / boot exception handler image. */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);
    if (filename) {
        bootrom_size = load_image_mr(filename, bootrom);
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

    // Other chips on I2C bus
    i2c_slave_create_simple(soc->i2c, TYPE_AR1010, AR1010_I2C_ADDR);
    i2c_slave_create_simple(soc->i2c, TYPE_WM8731, 0x1b);
    i2c_slave_create_simple(soc->i2c, TYPE_STMPE2403, STMPE2403_DEFAULT_I2C_ADDR);

    // Connect GPIOs
    // PE0: Lid detect
    qemu_irq lid_det = qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "in", 0);
    qemu_irq_lower(lid_det);
    // PE4: MSC1 CD
    qemu_irq msc1_cd = qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "in", 4);
    qemu_irq_raise(msc1_cd);
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

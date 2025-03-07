/*
 * Noah NP1380 board support
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
#include "hw/loader.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "qemu/log.h"

#include "hw/mips/ingenic_jz4740.h"
#include "hw/block/ingenic_emc.h"

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

static void mips_noah_np1380_init(MachineState *machine)
{
    char *filename;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *bootrom = g_new(MemoryRegion, 1);
    IngenicJZ4740 *soc;
    CPUMIPSState *env;
    ResetData *reset_info;
    int bootrom_size;

    // Board-specific parameters
    //machine->ram_size = 64 * 1024 * 1024;

    /* Init CPUs. */
    soc = ingenic_jz4740_init(machine);
    env = &soc->cpu->env;

    reset_info = g_new0(ResetData, 1);
    reset_info->cpu = soc->cpu;
    reset_info->vector = env->active_tc.PC;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* Allocate RAM. */
    memory_region_init_rom(bootrom, NULL, "mips_noah_np1380.bootrom", 8 * 1024,
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
    if (bootrom_size < 0 && machine->firmware && !qtest_enabled()) {
        /* Bail out if we have neither a kernel image nor boot vector code. */
        error_report("Could not load MIPS bios '%s'", machine->firmware);
        exit(1);
    } else {
        /* We have a boot vector start address. */
        env->active_tc.PC = (target_long)(int32_t)0xbfc00000;
    }

    // Register SDRAM at DCS 0
    IngenicEmcSdram *sdram = INGENIC_EMC_SDRAM(qdev_new(TYPE_INGENIC_EMC_SDRAM));
    object_property_set_uint(OBJECT(sdram), "cs", 0, &error_fatal);
    object_property_set_uint(OBJECT(sdram), "size", 0x04000000, &error_fatal);
    qdev_realize_and_unref(DEVICE(sdram), NULL, &error_fatal);

    // Register NAND at CS 1
    IngenicEmcNand *nand = INGENIC_EMC_NAND(qdev_new(TYPE_INGENIC_EMC_NAND));
    object_property_set_uint(OBJECT(nand), "cs",          1,            &error_fatal);
    object_property_set_str( OBJECT(nand), "nand-id",     "ecd514b674", &error_fatal);
    object_property_set_uint(OBJECT(nand), "block-pages", 128,          &error_fatal);
    object_property_set_uint(OBJECT(nand), "page-size",   4096,         &error_fatal);
    object_property_set_uint(OBJECT(nand), "oob-size",    128,          &error_fatal);
    qdev_realize_and_unref(DEVICE(nand), NULL, &error_fatal);

    // Connect GPIOs
    // PB27: MSC CD, 1: inserted
    qdev_connect_gpio_out_named(DEVICE(soc->msc), "io-cd", 0,
        qdev_get_gpio_in_named(DEVICE(soc->gpio['B' - 'A']), "gpio-in", 27));

    // PC23: LCD select, 0: KD035G6, 1: PT035TN01_V5
    qemu_irq lcd_sel = qdev_get_gpio_in_named(DEVICE(soc->gpio['C' - 'A']), "gpio-in", 23);
    qemu_irq_raise(lcd_sel);
    // PD29: POWER key, 0: pressed
    qemu_irq power_key = qdev_get_gpio_in_named(DEVICE(soc->gpio['D' - 'A']), "gpio-in", 29);
    qemu_irq_raise(power_key);
}

static void mips_noah_np1380_machine_init(MachineClass *mc)
{
    mc->desc = "MIPS Noah NP1380 platform";
    mc->init = mips_noah_np1380_init;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("XBurstR1");
    mc->default_ram_id = "mips_noah_np1380.ram";
    mc->default_ram_size = 16 * 1024;
}

DEFINE_MACHINE("noah_np1380", mips_noah_np1380_machine_init)

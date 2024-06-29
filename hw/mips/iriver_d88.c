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
#include "hw/input/d88_matrix_keypad.h"

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

    // STMPE2403 Keypad/GPIO controller
    I2CSlave *stmpe2403 = i2c_slave_new(TYPE_STMPE2403, STMPE2403_DEFAULT_I2C_ADDR);
    // P15 & P23 used as I2C ADDR, connected to GND
    object_property_set_uint(OBJECT(stmpe2403), "force-gpio-mask", BIT(15) | BIT(23), &error_fatal);
    object_property_set_uint(OBJECT(stmpe2403), "force-gpio-value", 0, &error_fatal);
    i2c_slave_realize_and_unref(stmpe2403, soc->i2c, &error_abort);

    // Keypad matrix
    D88MatrixKeypad *kp = D88_MATRIX_KEYPAD(qdev_new(TYPE_D88_MATRIX_KEYPAD));
    object_property_set_uint(OBJECT(kp), "num-rows", 5, &error_fatal);
    object_property_set_uint(OBJECT(kp), "num-cols", 13, &error_fatal);
    qdev_realize_and_unref(DEVICE(kp), NULL, &error_fatal);

    // Keypad IO connections
    const struct {
        bool row;
        uint32_t kp_io;
        uint32_t stmpe_io;
    } kp_ios[] = {
        { true,  0,  4},
        { true,  1, 12},
        { true,  2, 13},
        { true,  3, 14},
        { true,  4, 16},
        {false,  0,  0},
        {false,  1,  1},
        {false,  2,  2},
        {false,  3,  3},
        {false,  4,  7},
        {false,  5,  8},
        {false,  6,  9},
        {false,  7, 10},
        {false,  8, 11},
        {false,  9, 17},
        {false, 10, 18},
        {false, 11, 19},
        {false, 12, 20},
    };
    for (int i = 0; i < ARRAY_SIZE(kp_ios); i++) {
        const char *name = kp_ios[i].row ? "row-in" : "col-in";
        qemu_irq irq = qdev_get_gpio_in_named(DEVICE(kp), name, kp_ios[i].kp_io);
        qdev_connect_gpio_out_named(DEVICE(stmpe2403), "gpio-out", kp_ios[i].stmpe_io, irq);
        name = kp_ios[i].row ? "row-out" : "col-out";
        irq = qdev_get_gpio_in_named(DEVICE(stmpe2403), "gpio-in", kp_ios[i].stmpe_io);
        qdev_connect_gpio_out_named(DEVICE(kp), name, kp_ios[i].kp_io, irq);
    }

    // Floating signals
    static const uint8_t floating[] = {5, 6, 21, 22};
    for (int i = 0; i < ARRAY_SIZE(floating); i++) {
        qemu_irq irq = qdev_get_gpio_in_named(DEVICE(stmpe2403), "gpio-in", floating[i]);
        qdev_connect_gpio_out_named(DEVICE(stmpe2403), "gpio-out", floating[i], irq);
    }

    // Connect GPIOs
    // PE0: Lid detect, 0: closed
    qemu_irq lid_det = qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 0);
    qemu_irq_raise(lid_det);
    // PE4: MSC1 CD, 0: inserted
    qemu_irq msc1_cd = qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 4);
    qemu_irq_raise(msc1_cd);
    // PE6: UDC CD, active high
    qemu_irq udc_cd = qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 6);
    qemu_irq_lower(udc_cd);
    // PE9: Keyboard IRQ, falling edge active
    qemu_irq stmpe2403_irq = qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 9);
    qdev_connect_gpio_out_named(DEVICE(stmpe2403), "irq-out", 0, stmpe2403_irq);
    // PE10: Headphone, 0: inserted
    qemu_irq hp_cd = qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 10);
    qemu_irq_raise(hp_cd);
    // PE30: POWER key, active low
    qemu_irq power_key = qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 30);
    qemu_irq_raise(power_key);
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

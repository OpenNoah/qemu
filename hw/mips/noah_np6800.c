/*
 * Noah NP6800 board support
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
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/clock.h"
#include "hw/core/qdev-clock.h"
#include "hw/mips/mips.h"
#include "hw/char/serial.h"
#include "system/system.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/split-irq.h"

#include "hw/mips/ingenic_jz4750.h"
#include "hw/block/ingenic_emc.h"
#include "hw/input/fixed_irq.h"
#include "hw/input/gpio_matrix_keypad.h"

static void mips_noah_np6800_init(MachineState *machine)
{
    IngenicJZ4750 *soc = ingenic_jz4750_init(machine);

    // Register SDRAM at DCS 0
    IngenicEmcSdram *sdram = INGENIC_EMC_SDRAM(qdev_new(TYPE_INGENIC_EMC_SDRAM));
    object_property_set_uint(OBJECT(sdram), "cs", 0, &error_fatal);
    object_property_set_uint(OBJECT(sdram), "size", 0x04000000, &error_fatal);
    qdev_realize_and_unref(DEVICE(sdram), NULL, &error_fatal);

    // Connect GPIOs
    // PE30: POWER key, active low
    qemu_irq power_key = qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 30);
    qemu_irq_raise(power_key);

    // Fixed GPIO for triggering firmware upgrade
    FixedIrq *fixed = FIXED_IRQ(qdev_new(TYPE_FIXED_IRQ));
    object_property_set_int(OBJECT(fixed), "irq-value", 0, &error_fatal);
    qdev_realize_and_unref(DEVICE(fixed), NULL, &error_fatal);

    // PC4 is Keyboard LEFT
    // PC17 is Keyboard RIGHT
    qdev_connect_gpio_out(DEVICE(fixed), 0,
        qdev_get_gpio_in_named(DEVICE(soc->gpio['C' - 'A']), "gpio-in", 17));
}

static void mips_noah_np6800_machine_init(MachineClass *mc)
{
    mc->desc = "MIPS Noah NP6800 platform";
    mc->init = mips_noah_np6800_init;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("XBurstR1");
    mc->default_ram_id = "unused";
    mc->default_ram_size = 0;
}

DEFINE_MACHINE("noah_np6800", mips_noah_np6800_machine_init)

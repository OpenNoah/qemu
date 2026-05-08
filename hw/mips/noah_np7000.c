/*
 * Noah NP7000 board support
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

#include "hw/mips/ingenic_jz4755.h"
#include "hw/block/ingenic_emc.h"
#include "hw/input/gpio_matrix_keypad.h"

static void mips_noah_np7000_init(MachineState *machine)
{
    IngenicJZ4755 *soc = ingenic_jz4755_init(machine);

    // Register SDRAM at DCS 0
    IngenicEmcSdram *sdram = INGENIC_EMC_SDRAM(qdev_new(TYPE_INGENIC_EMC_SDRAM));
    object_property_set_uint(OBJECT(sdram), "cs", 0, &error_fatal);
    object_property_set_uint(OBJECT(sdram), "size", 0x04000000, &error_fatal);
    qdev_realize_and_unref(DEVICE(sdram), NULL, &error_fatal);

    // Keypad matrix
    GpioMatrixKeypad *kp = GPIO_MATRIX_KEYPAD(qdev_new(TYPE_GPIO_MATRIX_KEYPAD));
    object_property_set_uint(OBJECT(kp), "num-rows", 0, &error_fatal);
    object_property_set_uint(OBJECT(kp), "num-cols", 0, &error_fatal);
    object_property_set_uint(OBJECT(kp), "pin-invert", 0xc0000000, &error_fatal);
    object_property_set_uint(OBJECT(kp), "pin-pull", 0xffffffff, &error_fatal);
    object_property_set_uint(OBJECT(kp), "pin-pull-value", 0xffffffff, &error_fatal);
    qdev_realize_and_unref(DEVICE(kp), NULL, &error_fatal);

    // PE30 WKUP: Home button
    // load noahos.img from mmc1
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 0, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 30));
    // PE4: VOL+ button
    // NOAHOS UPDATE UTILITIES
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 1, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 4));
    // PE5: VOL- button
    // NOAHOS TEST UTILITIES
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 2, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 5));

    // PE2: 1: charing done
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 26, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 2));
    // PE6: 1: charger connected
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 27, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 6));
    // PE8: 0: USB connected
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 28, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 8));
    // PE10: 0: Headphone connected
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 29, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 10));
    // PE12: 0: MSC1 inserted
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 30, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 12));
    // PC31 BOOT_SEL1: 0: MSC0, 1: NAND
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 31, qdev_get_gpio_in_named(DEVICE(soc->gpio['C' - 'A']), "gpio-in", 31));
}

static void mips_noah_np7000_machine_init(MachineClass *mc)
{
    mc->desc = "MIPS Noah NP7000 platform";
    mc->init = mips_noah_np7000_init;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("XBurstR1");
    mc->default_ram_id = "unused";
    mc->default_ram_size = 0;
}

DEFINE_MACHINE("noah_np7000", mips_noah_np7000_machine_init)

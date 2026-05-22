/*
 * Noah NP2150 board support
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
#include "qemu/error-report.h"
#include "system/qtest.h"
#include "system/reset.h"
#include "qemu/log.h"

#include "hw/mips/ingenic_jz4740.h"
#include "hw/block/ingenic_emc.h"
#include "hw/input/gpio_matrix_keypad.h"

static void mips_noah_np2150_init(MachineState *machine)
{
    IngenicJZ4740 *soc = ingenic_jz4740_init(machine);

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

    // Keypad matrix
    GpioMatrixKeypad *kp = GPIO_MATRIX_KEYPAD(qdev_new(TYPE_GPIO_MATRIX_KEYPAD));
    object_property_set_uint(OBJECT(kp), "num-rows", 0, &error_fatal);
    object_property_set_uint(OBJECT(kp), "num-cols", 0, &error_fatal);
    object_property_set_uint(OBJECT(kp), "num-pins", 32, &error_fatal);
    // Attach pull-ups to all rows and cols
    object_property_set_uint(OBJECT(kp), "pin-invert", 0, &error_fatal);
    object_property_set_uint(OBJECT(kp), "pin-pull", 0xffffffff, &error_fatal);
    object_property_set_uint(OBJECT(kp), "pin-pull-value", 0xffffffff, &error_fatal);
    qdev_realize_and_unref(DEVICE(kp), NULL, &error_fatal);

    // PD29: POWER key, 0: pressed
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 0, qdev_get_gpio_in_named(DEVICE(soc->gpio['D' - 'A']), "gpio-in", 29));
    // PB30: Charging status, 1: charging done
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 1, qdev_get_gpio_in_named(DEVICE(soc->gpio['B' - 'A']), "gpio-in", 30));
    // PB29: USB device port, 1: connected
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 2, qdev_get_gpio_in_named(DEVICE(soc->gpio['B' - 'A']), "gpio-in", 29));
    // PB27: SD card, 1: inserted
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 3, qdev_get_gpio_in_named(DEVICE(soc->gpio['B' - 'A']), "gpio-in", 27));
    // PC23: LCD select, 0: KD035G6, 1: PT035TN01_V5
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 31, qdev_get_gpio_in_named(DEVICE(soc->gpio['C' - 'A']), "gpio-in", 23));
}

static void mips_noah_np2150_machine_init(MachineClass *mc)
{
    mc->desc = "MIPS Noah NP2150 platform";
    mc->init = mips_noah_np2150_init;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("JZ4740");
    mc->default_ram_id = "unused";
    mc->default_ram_size = 0;
}

DEFINE_MACHINE("noah_np2150", mips_noah_np2150_machine_init)

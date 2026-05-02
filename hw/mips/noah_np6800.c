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

#define FIRMWARE_UPGRADE    0

static void mips_noah_np6800_init(MachineState *machine)
{
    IngenicJZ4750 *soc = ingenic_jz4750_init(machine);

    // Register SDRAM at DCS 0
    IngenicEmcSdram *sdram = INGENIC_EMC_SDRAM(qdev_new(TYPE_INGENIC_EMC_SDRAM));
    object_property_set_uint(OBJECT(sdram), "cs", 0, &error_fatal);
    object_property_set_uint(OBJECT(sdram), "size", 0x04000000, &error_fatal);
    qdev_realize_and_unref(DEVICE(sdram), NULL, &error_fatal);

    // Keypad matrix
    GpioMatrixKeypad *kp = GPIO_MATRIX_KEYPAD(qdev_new(TYPE_GPIO_MATRIX_KEYPAD));
    object_property_set_uint(OBJECT(kp), "num-rows", 16, &error_fatal);
    object_property_set_uint(OBJECT(kp), "num-cols", 16, &error_fatal);
    object_property_set_uint(OBJECT(kp), "row-pull", 0, &error_fatal);
    object_property_set_uint(OBJECT(kp), "row-pull-value", 0xffffffff, &error_fatal);
    object_property_set_uint(OBJECT(kp), "col-pull", 0, &error_fatal);
    object_property_set_uint(OBJECT(kp), "col-pull-value", 0xffffffff, &error_fatal);
    object_property_set_uint(OBJECT(kp), "pin-invert", 0, &error_fatal);
    object_property_set_uint(OBJECT(kp), "pin-pull", 0xffffffff, &error_fatal);
    object_property_set_uint(OBJECT(kp), "pin-pull-value", 0xffffffff, &error_fatal);
    qdev_realize_and_unref(DEVICE(kp), NULL, &error_fatal);

    // Keypad IO connections
    const struct {
        bool row;
        char group;
        uint8_t pin;
    } kp_ios[] = {
        {false, 'C',  2},   // col-0
        {false, 'C',  4},   // col-1
        {false, 'C',  7},   // col-2
        {false, 'C', 12},   // col-3
        {false, 'C', 13},   // col-4
        {false, 'C', 14},   // col-5
        {false, 'C', 15},   // col-6
#if !FIRMWARE_UPGRADE
        {false, 'C', 17},   // col-7
#endif

        { true, 'C',  0},   // row-0
        { true, 'C',  1},   // row-1
        { true, 'C',  3},   // row-2
        { true, 'C',  5},   // row-3
        { true, 'C',  6},   // row-4
        { true, 'C',  8},   // row-5
        { true, 'C',  9},   // row-6
        { true, 'C', 10},   // row-7
        { true, 'C', 11},   // row-8
        { true, 'C', 18},   // row-9
        { true, 'E',  5},   // row-10
        { true, 'E',  8},   // row-11
        { true, 'F',  6},   // row-12
        { true, 'F',  7},   // row-13
        { true, 'F', 21},   // row-14
        { true, 'F', 22},   // row-15
    };
    int i_row = 0, i_col = 0;
    for (int i = 0; i < ARRAY_SIZE(kp_ios); i++) {
        const char *name = kp_ios[i].row ? "row-in" : "col-in";
        int *pi_kp = kp_ios[i].row ? &i_row : &i_col;
        qemu_irq irq = qdev_get_gpio_in_named(DEVICE(kp), name, *pi_kp);
        qdev_connect_gpio_out_named(DEVICE(soc->gpio[kp_ios[i].group - 'A']), "gpio-out", kp_ios[i].pin, irq);
        name = kp_ios[i].row ? "row-out" : "col-out";
        irq = qdev_get_gpio_in_named(DEVICE(soc->gpio[kp_ios[i].group - 'A']), "gpio-in", kp_ios[i].pin);
        qdev_connect_gpio_out_named(DEVICE(kp), name, *pi_kp, irq);
        *pi_kp += 1;
    }

    // PE30: POWER key, active low
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 0, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 30));
    // PD29: Charging status, 1: charging done
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 1, qdev_get_gpio_in_named(DEVICE(soc->gpio['D' - 'A']), "gpio-in", 29));
    // PF19: External power, 1: connected
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 2, qdev_get_gpio_in_named(DEVICE(soc->gpio['F' - 'A']), "gpio-in", 19));
    // PE2: SD card, 1: inserted
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 3, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 2));
    // PE16: USB device port, 1: connected
    qdev_connect_gpio_out_named(DEVICE(kp), "pin-out", 4, qdev_get_gpio_in_named(DEVICE(soc->gpio['E' - 'A']), "gpio-in", 16));

#if FIRMWARE_UPGRADE
    // Fixed GPIO for triggering firmware upgrade
    FixedIrq *fixed = FIXED_IRQ(qdev_new(TYPE_FIXED_IRQ));
    object_property_set_int(OBJECT(fixed), "irq-value", 0, &error_fatal);
    qdev_realize_and_unref(DEVICE(fixed), NULL, &error_fatal);

    // PC17 is Keyboard RIGHT
    qdev_connect_gpio_out(DEVICE(fixed), 0,
        qdev_get_gpio_in_named(DEVICE(soc->gpio['C' - 'A']), "gpio-in", 17));
#endif
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

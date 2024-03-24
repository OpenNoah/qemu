/*
 * Ingenic JZ4755 SoC support
 *
 * Emulates a very simple machine model of Ingenic JZ4755 SoC.
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
#include "hw/qdev-clock.h"
#include "hw/mips/mips.h"
#include "hw/char/serial.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/mips/bios.h"
#include "hw/loader.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qemu/log.h"

#include "hw/mips/ingenic_jz4755.h"
#include "hw/misc/ingenic_cgu.h"
#include "hw/block/ingenic_emc.h"
#include "hw/block/ingenic_bch.h"
#include "hw/gpio/ingenic_gpio.h"
#include "hw/char/ingenic_uart.h"
#include "hw/adc/ingenic_adc.h"
#include "hw/rtc/ingenic_rtc.h"
#include "hw/timer/ingenic_tcu.h"
#include "hw/display/ingenic_lcd.h"

MIPSCPU *ingenic_jz4755_init(MachineState *machine)
{
    MIPSCPU *cpu;
    CPUMIPSState *env;

    /* Needs to have clocks first */
    IngenicCgu *cgu = INGENIC_CGU(qdev_new(TYPE_INGENIC_CGU));
    object_property_set_uint(OBJECT(cgu), "ext-freq", 24000000, &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(cgu), &error_fatal);

    /* Init CPUs. */
    // machine->cpu_type = "XBurstR1-mips-cpu";
    cpu = mips_cpu_create_with_clock(machine->cpu_type, qdev_get_clock_out(DEVICE(cgu), "clk_cclk"));
    env = &cpu->env;

    // 0x80000000 Internal SRAM, 16kB
    MemoryRegion *sys_mem = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    memory_region_init_ram(sram, NULL, "sram", 16 * 1024, &error_fatal);
    memory_region_add_subregion(sys_mem, 0x80000000, sram);

    // 0xf4000000 TCSM SRAM, 16kB
    MemoryRegion *tcsm = g_new(MemoryRegion, 1);
    memory_region_init_ram(tcsm, NULL, "tcsm", 16 * 1024, &error_fatal);
    memory_region_add_subregion(sys_mem, 0xf4000000, tcsm);

    MemoryRegion *ahb0 = g_new(MemoryRegion, 1);
    MemoryRegion *ahb1 = g_new(MemoryRegion, 1);
    MemoryRegion *apb  = g_new(MemoryRegion, 1);

    // Register AHB0 IO space at 0x13000000
    memory_region_init(ahb0, NULL, "ahb0", 0x00090000);
    memory_region_add_subregion(sys_mem, 0x13000000, ahb0);

    // 0x13010000 Register EMC on AHB0
    IngenicEmc *emc = INGENIC_EMC(qdev_new(TYPE_INGENIC_EMC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(emc), &error_fatal);
    MemoryRegion *emc_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(emc), 0);
    memory_region_add_subregion(ahb0, 0x00010000, emc_mr);

    // 0x13050000 Register LCD controller on AHB0
    IngenicLcd *lcd = INGENIC_LCD(qdev_new(TYPE_INGENIC_LCD));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(lcd), &error_fatal);
    MemoryRegion *lcd_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(lcd), 0);
    memory_region_add_subregion(ahb0, 0x00050000, lcd_mr);

    // Register AHB1 IO space at 0x13090000
    memory_region_init(ahb1, NULL, "ahb1", 0x00070000);
    memory_region_add_subregion(sys_mem, 0x13090000, ahb1);

    // 0x130d0000 Register BCH on AHB1
    IngenicBch *bch = INGENIC_BCH(qdev_new(TYPE_INGENIC_BCH));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(bch), &error_fatal);
    MemoryRegion *bch_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(bch), 0);
    memory_region_add_subregion(ahb1, 0x00040000, bch_mr);

    // Register APB IO space at 0x10000000
    memory_region_init(apb, NULL, "apb", 0x01000000);
    memory_region_add_subregion(sys_mem, 0x10000000, apb);

    // 0x10000000 Register CGU on APB
    MemoryRegion *cgu_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(cgu), 0);
    memory_region_add_subregion(apb, 0, cgu_mr);

    // 0x1000204C Register TCU/OST/WDT on APB
    IngenicTcu *tcu = INGENIC_TCU(qdev_new(TYPE_INGENIC_TCU));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(tcu), &error_fatal);
    MemoryRegion *tcu_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(tcu), 0);
    memory_region_add_subregion(apb, 0x00002000, tcu_mr);

    // 0x10003000 Register RTC on APB
    IngenicRtc *rtc = INGENIC_RTC(qdev_new(TYPE_INGENIC_RTC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(rtc), &error_fatal);
    MemoryRegion *rtc_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(rtc), 0);
    memory_region_add_subregion(apb, 0x00003000, rtc_mr);

    // 0x10010000 Register GPIOs on APB
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

    // 0x10030000 Register 16550 UART0 on APB
    ingenic_uart_init(apb, 0x00030000, env->irq[4],
        115200, serial_hd(0), DEVICE_NATIVE_ENDIAN);

    // 0x10031000 Register 16550 UART1 on APB
    ingenic_uart_init(apb, 0x00031000, env->irq[5],
        115200, serial_hd(1), DEVICE_NATIVE_ENDIAN);

    // 0x10032000 Register 16550 UART2 on APB
    ingenic_uart_init(apb, 0x00032000, env->irq[6],
        115200, serial_hd(2), DEVICE_NATIVE_ENDIAN);

    // 0x10070000 Register ADC on APB bus
    IngenicAdc *adc = INGENIC_ADC(qdev_new(TYPE_INGENIC_ADC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(adc), &error_fatal);
    MemoryRegion *adc_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(adc), 0);
    memory_region_add_subregion(apb, 0x00070000, adc_mr);

    /* Init CPU internal devices. */
    cpu_mips_irq_init_cpu(cpu);
    cpu_mips_clock_init(cpu);

    // Connect GPIOs
    // PC27: NAND RB
    qemu_irq nand_rb = qdev_get_gpio_in_named(DEVICE(gpio[2]), "in", 27);
    qdev_connect_gpio_out_named(DEVICE(emc), "nand-rb", 0, nand_rb);

    return cpu;
}

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
#include "qemu/log.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"

#include "hw/qdev-clock.h"
#include "hw/mips/mips.h"
#include "hw/char/serial.h"
#include "hw/boards.h"
#include "hw/core/split-irq.h"
#include "hw/loader.h"
#include "hw/irq.h"
#include "hw/sysbus.h"

#include "hw/mips/ingenic_jz4755.h"
#include "hw/misc/ingenic_cgu.h"
#include "hw/intc/ingenic_intc.h"
#include "hw/dma/ingenic_dmac.h"
#include "hw/timer/ingenic_tcu.h"
#include "hw/gpio/ingenic_gpio.h"
#include "hw/block/ingenic_emc.h"
#include "hw/block/ingenic_bch.h"
#include "hw/display/ingenic_lcd.h"
#include "hw/char/ingenic_uart.h"
#include "hw/adc/ingenic_adc.h"
#include "hw/rtc/ingenic_rtc.h"
#include "hw/i2c/ingenic_i2c.h"
#include "hw/ssi/ingenic_msc.h"
#include "hw/audio/ingenic_aic.h"
#include "hw/usb/ingenic_udc.h"

IngenicJZ4755 *ingenic_jz4755_init(MachineState *machine)
{
    IngenicJZ4755 *soc = g_new(IngenicJZ4755, 1);

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
    soc->cpu = cpu;

    // 0x00000000 Cache may be used as SRAM, 16kB
    MemoryRegion *sys_mem = get_system_memory();
    MemoryRegion *cached_sram = g_new(MemoryRegion, 1);
    memory_region_init_ram(cached_sram, NULL, "sram.cached", 16 * 1024, &error_fatal);
    // Higher priority than SDRAM, to keep cached data when SDRAM gets enabled
    memory_region_add_subregion_overlap(sys_mem, 0, cached_sram, 1);

    // 0xa0000000 Cache write-through SRAM, 16kB
    // This is a terrible hack:
    // Bootloader may write to this uncached address to bypass I/D-cache,
    // whilst running code from I/D-cache.
    // Simply ignoring writes to this section should be fine,
    // as invaliding D-cache would write-back cached data to this address anyway.
    MemoryRegion *uncached_sram = g_new(MemoryRegion, 1);
    memory_region_init_ram(uncached_sram, NULL, "sram.uncached", 16 * 1024, &error_fatal);
    memory_region_add_subregion(sys_mem, 0xa0000000, uncached_sram);

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

    // 0x13020000 Register DMAC on AHB0
    IngenicDmac *dmac = INGENIC_DMAC(qdev_new(TYPE_INGENIC_DMAC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dmac), &error_fatal);
    MemoryRegion *dmac_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dmac), 0);
    memory_region_add_subregion(ahb0, 0x00020000, dmac_mr);

    // 0x13040000 Register UDC on AHB0
    IngenicUdc *udc = INGENIC_UDC(qdev_new(TYPE_INGENIC_UDC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(udc), &error_fatal);
    MemoryRegion *udc_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(udc), 0);
    memory_region_add_subregion(ahb0, 0x00040000, udc_mr);

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

    // 0x10001000 Register INTC on APB
    IngenicIntc *intc = INGENIC_INTC(qdev_new(TYPE_INGENIC_INTC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(intc), &error_fatal);
    MemoryRegion *intc_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(intc), 0);
    memory_region_add_subregion(apb, 0x00001000, intc_mr);

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
        soc->gpio[i] = gpio[i];
        object_property_set_str(OBJECT(gpio[i]), "name", name, &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(gpio[i]), &error_fatal);
        MemoryRegion *gpio_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(gpio[i]), 0);
        memory_region_add_subregion(apb, 0x00010000 + i * 0x0100, gpio_mr);
    }

    // 0x10020000 Register AIC on APB
    IngenicAic *aic = INGENIC_AIC(qdev_new(TYPE_INGENIC_AIC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(aic), &error_fatal);
    MemoryRegion *aic_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(aic), 0);
    memory_region_add_subregion(apb, 0x00020000, aic_mr);

    // 0x10021000 Register MSC0 on APB
    IngenicMsc *msc0 = INGENIC_MSC(qdev_new(TYPE_INGENIC_MSC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(msc0), &error_fatal);
    MemoryRegion *msc0_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(msc0), 0);
    memory_region_add_subregion(apb, 0x00021000, msc0_mr);

    // 0x10022000 Register MSC1 on APB
    IngenicMsc *msc1 = INGENIC_MSC(qdev_new(TYPE_INGENIC_MSC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(msc1), &error_fatal);
    MemoryRegion *msc1_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(msc1), 0);
    memory_region_add_subregion(apb, 0x00022000, msc1_mr);

    // 0x10030000 Register 16550 UART0 on APB
    ingenic_uart_init(apb, 0x00030000,
        qdev_get_gpio_in_named(DEVICE(intc), "irq-in", 9),
        115200, serial_hd(0), DEVICE_NATIVE_ENDIAN);

    // 0x10031000 Register 16550 UART1 on APB
    ingenic_uart_init(apb, 0x00031000,
        qdev_get_gpio_in_named(DEVICE(intc), "irq-in", 8),
        115200, serial_hd(1), DEVICE_NATIVE_ENDIAN);

    // 0x10032000 Register 16550 UART2 on APB
    ingenic_uart_init(apb, 0x00032000,
        qdev_get_gpio_in_named(DEVICE(intc), "irq-in", 7),
        115200, serial_hd(2), DEVICE_NATIVE_ENDIAN);

    // 0x10042000 Register I2C on APB
    IngenicI2c *i2c = INGENIC_I2C(qdev_new(TYPE_INGENIC_I2C));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(i2c), &error_fatal);
    MemoryRegion *i2c_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(i2c), 0);
    memory_region_add_subregion(apb, 0x00042000, i2c_mr);
    soc->i2c = I2C_BUS(qdev_get_child_bus(DEVICE(i2c), "i2c"));

    // 0x10070000 Register ADC on APB bus
    IngenicAdc *adc = INGENIC_ADC(qdev_new(TYPE_INGENIC_ADC));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(adc), &error_fatal);
    MemoryRegion *adc_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(adc), 0);
    memory_region_add_subregion(apb, 0x00070000, adc_mr);

    /* Init CPU internal devices. */
    cpu_mips_irq_init_cpu(cpu);
    cpu_mips_clock_init(cpu);

    // IO splitters
    // NAND RB from EMC
    DeviceState *nand_rb_splitter = qdev_new(TYPE_SPLIT_IRQ);
    qdev_prop_set_uint32(nand_rb_splitter, "num-lines", 2);
    qdev_realize_and_unref(nand_rb_splitter, NULL, &error_fatal);
    qdev_connect_gpio_out_named(DEVICE(emc), "nand-rb", 0, qdev_get_gpio_in(nand_rb_splitter, 0));

    // Connect GPIOs
    // PC27: NAND RB
    qdev_connect_gpio_out(nand_rb_splitter, 0,
        qdev_get_gpio_in_named(DEVICE(gpio['C' - 'A']), "gpio-in", 27));

    // Connect interrupts
    const struct {
        DeviceState *dev;
        const char *dev_irq_name;
        uint32_t dev_irq;
        uint32_t intc_irq;
    } irqs[] = {
        {DEVICE(lcd),  "irq-out", 0, 31},
        // 30 IPU
        {DEVICE(dmac), "irq-out", 0, 29},
        {DEVICE(dmac), "irq-out", 1, 28},
        // 24 MSC1
        {DEVICE(tcu),  "irq-out", 0, 23},
        {DEVICE(tcu),  "irq-out", 1, 22},
        {DEVICE(tcu),  "irq-out", 2, 21},
        {DEVICE(adc),  "irq-out", 0, 18},
        {DEVICE(gpio['A' - 'A']), "irq-out",  0, 16},
        {DEVICE(gpio['B' - 'A']), "irq-out",  0, 15},
        {DEVICE(gpio['C' - 'A']), "irq-out",  0, 14},
        {DEVICE(gpio['D' - 'A']), "irq-out",  0, 13},
        {DEVICE(gpio['E' - 'A']), "irq-out",  0, 12},
        {DEVICE(gpio['F' - 'A']), "irq-out",  0, 11},
        // 8 UART1
        // 6 RTC
        {0}
    };
    for (int i = 0; irqs[i].dev != NULL; i++) {
        qemu_irq irq;
        irq = qdev_get_gpio_in_named(DEVICE(intc), "irq-in", irqs[i].intc_irq);
        qdev_connect_gpio_out_named(irqs[i].dev,
                                    irqs[i].dev_irq_name, irqs[i].dev_irq, irq);
    }
    qdev_connect_gpio_out_named(DEVICE(intc), "irq-out", 0, env->irq[2]);

    // Connect DMA requests
    qdev_connect_gpio_out(nand_rb_splitter, 1,
        qdev_get_gpio_in_named(DEVICE(dmac), "req-in", 1));

    return soc;
}

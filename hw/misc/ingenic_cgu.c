/*
 * Ingenic JZ4755 Clock Reset and Power Controller emulation
 *
 * Copyright (c) 2024 Norman Zhi
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"

#include "hw/qdev-properties.h"
#include "hw/misc/ingenic_cgu.h"

void qmp_stop(Error **errp);

static void ingenic_cgu_reset(Object *obj, ResetType type)
{
    IngenicCgu *s = INGENIC_CGU(obj);

    // Initial register values
    s->CPCCR  = 0x42040000;
    s->CPCCR  = 0x28080011;
    s->CPPSR  = 0x80000000;
    s->I2SCDR = 0x00000004;
    s->LPCDR  = 0x00000004;
    s->MSCCDR = 0x00000000;
    s->SSICDR = 0x00000000;
    s->CIMCDR = 0x00000004;
    s->LCR    = 0x000000f8;
    s->CLKGR  = 0x00000000;
    s->OPCR   = 0x00001500;
    s->RSR    = 0x00000001;
}

static void ingenic_cgu_update_clocks(IngenicCgu *s)
{
    // Update clock frequencies
    if ((s->CPPCR & (BIT(8) | BIT(9))) == BIT(8)) {
        // Switch to PLL
        uint32_t m = (s->CPPCR >> 23) + 2;
        uint32_t n = ((s->CPPCR >> 18) & 0x1f) + 2;
        uint32_t od = (s->CPPCR >> 16) & 3;
        static const uint32_t od_map[] = {1, 2, 2, 4};
        od = od_map[od];
        clock_update(s->clk_pll, clock_get(s->clk_ext) * (n * od) / m);
    } else {
        // Switch to EXT
        clock_update(s->clk_pll, clock_get(s->clk_ext));
    }

    static const uint32_t div_map[16] = {1, 2, 3, 4, 6, 8, 0};

    // CCLK
    uint32_t cdiv = div_map[s->CPCCR & 0x0f];
    if (unlikely(cdiv == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: cclk div by 0\n", __func__);
        cdiv = 1;
    }
    clock_update(s->clk_cclk, clock_get(s->clk_pll) * cdiv);

    // MCLK
    uint32_t mdiv = div_map[(s->CPCCR >> 12) & 0x0f];
    if (unlikely(mdiv == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: mclk div by 0\n", __func__);
        mdiv = 1;
    }
    clock_update(s->clk_mclk, clock_get(s->clk_pll) * mdiv);

    // PCLK
    uint32_t pdiv = div_map[(s->CPCCR >> 8) & 0x0f];
    if (unlikely(pdiv == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: pdiv div by 0\n", __func__);
        pdiv = 1;
    }
    clock_update(s->clk_pclk, clock_get(s->clk_pll) * pdiv);

    // PCS peripherals
    uint64_t pcs_period = clock_get(s->clk_pll);
    if (!(s->CPCCR & BIT(21)))
        pcs_period *= 2;
    clock_update(s->clk_lcdpix, pcs_period * (s->LPCDR & 0x07ff));

    qemu_log("%s: cclk freq %"PRIu32"\n", __func__, clock_get_hz(s->clk_cclk));
}

static uint64_t ingenic_cgu_read(void *opaque, hwaddr addr, unsigned size)
{
    if (unlikely(size != 4 || (addr & 3) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "CGU read unaligned @ " HWADDR_FMT_plx "/%"PRIx32"\n",
                      addr, (uint32_t)size);
        qmp_stop(NULL);
        return 0;
    }

    IngenicCgu *cgu = opaque;
    hwaddr aligned_addr = addr; // & ~3;
    uint64_t data = 0;
    switch (aligned_addr) {
    case 0x00:
        data = cgu->CPCCR;
        break;
    case 0x10:
        data = cgu->CPPCR;
        break;
    case 0x20:
        data = cgu->CLKGR;
        break;
    case 0x64:
        data = cgu->LPCDR;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "CGU read unknown address " HWADDR_FMT_plx "\n", aligned_addr);
        qmp_stop(NULL);
    }
    //data = (data >> (8 * (addr & 3))) & ((1LL << (8 * size)) - 1);
    qemu_log("CGU read @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", addr, (uint32_t)size, data);
    return data;
}

static void ingenic_cgu_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    if (unlikely(size != 4 || (addr & 3) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "CGU write unaligned @ " HWADDR_FMT_plx "/%"PRIx32" 0x%"PRIx64"\n",
                      addr, (uint32_t)size, data);
        qmp_stop(NULL);
        return;
    }

    IngenicCgu *cgu = opaque;
    hwaddr aligned_addr = addr; // & ~3;
    qemu_log("CGU write @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", addr, (uint32_t)size, data);
    switch (aligned_addr) {
    case 0x00:
        cgu->CPCCR = data & 0xffefffff;
        ingenic_cgu_update_clocks(cgu);
        break;
    case 0x10:
        cgu->CPPCR = data & 0xffff03ff;
        if (cgu->CPPCR & BIT(8)) {
            // PLL ON
            cgu->CPPCR |= BIT(10);
        }
        ingenic_cgu_update_clocks(cgu);
        break;
    case 0x20:
        cgu->CLKGR = data & 0x01ffffff;
        break;
    case 0x64:
        cgu->LPCDR = data & 0xc00007ff;
        ingenic_cgu_update_clocks(cgu);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "CGU write unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
        qmp_stop(NULL);
        return;
    }
}

static MemoryRegionOps cgu_ops = {
    .read = ingenic_cgu_read,
    .write = ingenic_cgu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

IngenicCgu *ingenic_cgu_get_cgu()
{
    Object *obj = object_resolve_path_type("", TYPE_INGENIC_CGU, NULL);
    if (!obj) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: "TYPE_INGENIC_CGU" device not found", __func__);
        return NULL;
    }
    return INGENIC_CGU(obj);
}

static const ClockPortInitArray cgu_clks = {
    QDEV_CLOCK_OUT(IngenicCgu, clk_ext),
    QDEV_CLOCK_OUT(IngenicCgu, clk_rtc),
    QDEV_CLOCK_OUT(IngenicCgu, clk_pll),
    QDEV_CLOCK_OUT(IngenicCgu, clk_cclk),
    QDEV_CLOCK_OUT(IngenicCgu, clk_mclk),
    QDEV_CLOCK_OUT(IngenicCgu, clk_pclk),
    QDEV_CLOCK_OUT(IngenicCgu, clk_lcdpix),
    QDEV_CLOCK_END
};

static void ingenic_cgu_init(Object *obj)
{
    printf("%s enter\n", __func__);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IngenicCgu *s = INGENIC_CGU(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &cgu_ops, s, "cgu", 0x1000);
    sysbus_init_mmio(sbd, &s->mr);

    qdev_init_clocks(DEVICE(s), cgu_clks);
}

static void ingenic_cgu_realize(DeviceState *dev, Error **errp)
{
    IngenicCgu *s = INGENIC_CGU(dev);
    clock_set_hz(s->clk_ext, s->ext_freq);
    clock_set_hz(s->clk_rtc, s->rtc_freq);
    ingenic_cgu_update_clocks(s);
}

static void ingenic_cgu_finalize(Object *obj)
{
    printf("%s enter\n", __func__);
}

static Property ingenic_cgu_properties[] = {
    DEFINE_PROP_UINT32("ext-freq", IngenicCgu, ext_freq, 12000000),
    DEFINE_PROP_UINT32("rtc-freq", IngenicCgu, rtc_freq, 32768),
    DEFINE_PROP_END_OF_LIST(),
};

static void ingenic_cgu_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, ingenic_cgu_properties);
    dc->realize = ingenic_cgu_realize;

    IngenicCguClass *cgu_class = INGENIC_CGU_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_cgu_reset,
                                       NULL,
                                       NULL,
                                       &cgu_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicCgu, ingenic_cgu, INGENIC_CGU, SYS_BUS_DEVICE)

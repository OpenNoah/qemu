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
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/misc/ingenic_cgu.h"

#define REG_CPCCR   0x00
#define REG_LCR     0x04
#define REG_RSR     0x08
#define REG_CPPCR   0x10
#define REG_CPPSR   0x14    // JZ4755
#define REG_CLKGR   0x20
#define REG_OPCR    0x24    // JZ4755
#define REG_SCR     0x24    // JZ4740
#define REG_I2SCDR  0x60
#define REG_LPCDR   0x64
#define REG_MSCCDR  0x68
#define REG_UHCCDR  0x6c    // JZ4740
#define REG_SSICDR  0x74
#define REG_CIMCDR  0x7c    // JZ4755

void qmp_stop(Error **errp);

static void ingenic_cgu_reset(Object *obj, ResetType type)
{
    IngenicCgu *s = INGENIC_CGU(obj);
    s->reg.cpccr  = 0x42040000;
    s->reg.cppcr  = 0x28080011;
    s->reg.cppsr  = 0x80000000;
    s->reg.i2scdr = 0x00000004;
    s->reg.lpcdr  = 0x00000004;
    s->reg.msccdr = 0x00000000;
    s->reg.ssicdr = 0x00000000;
    s->reg.cimcdr = 0x00000004;
    s->reg.lcr    = 0x000000f8;
    s->reg.clkgr  = 0x00000000;
    s->reg.opcr   = 0x00001500;
    s->reg.scr    = 0x00001500;
    s->reg.rsr    = 0x00000001;
}

static void ingenic_cgu_update_clocks(IngenicCgu *s)
{
    // Update clock frequencies
    if ((s->reg.cppcr & (BIT(8) | BIT(9))) == BIT(8)) {
        // Switch to PLL
        uint32_t m = (s->reg.cppcr >> 23) + 2;
        uint32_t n = ((s->reg.cppcr >> 18) & 0x1f) + 2;
        uint32_t od = (s->reg.cppcr >> 16) & 3;
        static const uint32_t od_map[] = {1, 2, 2, 4};
        od = od_map[od];
        clock_update(s->clk_pll, clock_get(s->clk_ext) * (n * od) / m);
    } else {
        // Switch to EXT
        clock_update(s->clk_pll, clock_get(s->clk_ext));
    }

    static const uint32_t div_map[16] = {1, 2, 3, 4, 6, 8, 0};

    // CCLK
    uint32_t cdiv = div_map[s->reg.cpccr & 0x0f];
    if (unlikely(cdiv == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: cclk div by 0\n", __func__);
        cdiv = 1;
    }
    clock_update(s->clk_cclk, clock_get(s->clk_pll) * cdiv);

    // MCLK
    uint32_t mdiv = div_map[(s->reg.cpccr >> 12) & 0x0f];
    if (unlikely(mdiv == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: mclk div by 0\n", __func__);
        mdiv = 1;
    }
    clock_update(s->clk_mclk, clock_get(s->clk_pll) * mdiv);

    // PCLK
    uint32_t pdiv = div_map[(s->reg.cpccr >> 8) & 0x0f];
    if (unlikely(pdiv == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: pdiv div by 0\n", __func__);
        pdiv = 1;
    }
    clock_update(s->clk_pclk, clock_get(s->clk_pll) * pdiv);

    // PCS peripherals
    uint64_t pcs_period = clock_get(s->clk_pll);
    if (!(s->reg.cpccr & BIT(21)))
        pcs_period *= 2;
    clock_update(s->clk_lcdpix, pcs_period * (s->reg.lpcdr & 0x07ff));

    trace_ingenic_cgu_cclk_freq(clock_get_hz(s->clk_cclk));
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
    uint64_t data = 0;
    switch (addr) {
    case REG_CPCCR:
        data = cgu->reg.cpccr;
        break;
    case REG_LCR:
        data = cgu->reg.lcr;
        break;
    case REG_CPPCR:
        data = cgu->reg.cppcr;
        break;
    case REG_CLKGR:
        data = cgu->reg.clkgr;
        break;
    case REG_OPCR:  // REG_SCR
        if (cgu->model == 0x4755)
            data = cgu->reg.opcr;
        else
            data = cgu->reg.scr;
        break;
    case REG_I2SCDR:
        data = cgu->reg.i2scdr;
        break;
    case REG_LPCDR:
        data = cgu->reg.lpcdr;
        break;
    case REG_MSCCDR:
        data = cgu->reg.msccdr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "CGU read unknown address " HWADDR_FMT_plx "\n", addr);
        qmp_stop(NULL);
    }
    trace_ingenic_cgu_read(addr, data, size);
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
    trace_ingenic_cgu_write(addr, data, size);
    switch (addr) {
    case REG_CPCCR:
        if (cgu->model == 0x4755)
            cgu->reg.cpccr = data & 0xffefffff;
        else
            cgu->reg.cpccr = data;
        ingenic_cgu_update_clocks(cgu);
        break;
    case REG_LCR:
        cgu->reg.lcr = data & 0xff;
        break;
    case REG_CPPCR:
        cgu->reg.cppcr = data & 0xffff03ff;
        if (cgu->reg.cppcr & BIT(8)) {
            // PLL ON
            cgu->reg.cppcr |= BIT(10);
        }
        ingenic_cgu_update_clocks(cgu);
        break;
    case REG_CLKGR:
        if (cgu->model == 0x4755)
            cgu->reg.clkgr = data & 0x01ffffff;
        else
            cgu->reg.clkgr = data & 0xffff;
        break;
    case REG_OPCR:  // REG_SCR
        if (cgu->model == 0x4755)
            cgu->reg.opcr = data & 0xff74;
        else
            cgu->reg.scr = data & 0xffd0;
        break;
    case REG_I2SCDR:
        cgu->reg.i2scdr = data & 0x01ff;
        break;
    case REG_LPCDR:
        if (cgu->model == 0x4755)
            cgu->reg.lpcdr = data & 0xc00007ff;
        else
            cgu->reg.lpcdr = data & 0x800007ff;
        ingenic_cgu_update_clocks(cgu);
        break;
    case REG_MSCCDR:
        cgu->reg.msccdr = data & 0x1f;
        break;
    case REG_UHCCDR:
        cgu->reg.uhccdr = data & 0x0f;
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

IngenicCgu *ingenic_cgu_get_cgu(void)
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

static void ingenic_cgu_realize(DeviceState *dev, Error **errp)
{
    IngenicCgu *s = INGENIC_CGU(dev);
    clock_set_hz(s->clk_ext, s->ext_freq);
    clock_set_hz(s->clk_rtc, s->rtc_freq);
    ingenic_cgu_update_clocks(s);
}

OBJECT_DEFINE_TYPE(IngenicCgu, ingenic_cgu, INGENIC_CGU, SYS_BUS_DEVICE)

static void ingenic_cgu_init(Object *obj)
{
    IngenicCgu *s = INGENIC_CGU(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &cgu_ops, s, "cgu", 0x1000);
    sysbus_init_mmio(sbd, &s->mr);
    qdev_init_clocks(DEVICE(s), cgu_clks);
}

static void ingenic_cgu_finalize(Object *obj)
{
}

static Property ingenic_cgu_properties[] = {
    DEFINE_PROP_UINT32("model", IngenicCgu, model, 0x4755),
    DEFINE_PROP_UINT32("ext-freq", IngenicCgu, ext_freq, 24000000),
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

/*
 * Ingenic JZ47xx Clock Reset and Power Controller emulation
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
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/ingenic_cpm.h"

#define REG_CPCCR   0x00    // 4720, 4740, 4750, 4755
#define REG_LCR     0x04    // 4720, 4740, 4750, 4755
#define REG_RSR     0x08    // 4720, 4740, 4750, 4755
#define REG_CPPCR   0x10    // 4720, 4740, 4750, 4755
#define REG_CPPSR   0x14    //             4750, 4755
#define REG_CLKGR   0x20    // 4720, 4740, 4750, 4755
#define REG_OPCR    0x24    //             4750, 4755
#define REG_SCR     0x24    // 4720, 4740
#define REG_I2SCDR  0x60    // 4720, 4740, 4750, 4755
#define REG_LPCDR   0x64    // 4720, 4740, 4750, 4755
#define REG_MSCCDR  0x68    // 4720, 4740,       4755
#define REG_MSC0CDR 0x68    //             4750
#define REG_UHCCDR  0x6c    // 4720, 4740, 4750
#define REG_SSICDR  0x74    // 4720, 4740, 4750, 4755
#define REG_MSC1CDR 0x78    //             4750
#define REG_PCMCDR  0x7c    //             4750
#define REG_CIMCDR  0x7c    //                   4755

void qmp_stop(Error **errp);

static void ingenic_cpm_reset(Object *obj, ResetType type)
{
    IngenicCpm *s = INGENIC_CPM(obj);
    trace_ingenic_cpm_reset(s->model);
    if (s->model <= 0x4740) {
        s->reg.cpccr   = 0x42040000;
        s->reg.cppcr   = 0x28080011;
        s->reg.i2scdr  = 0x00000004;
        s->reg.lpcdr   = 0x00000004;
        s->reg.msccdr  = 0x00000004;
        s->reg.uhccdr  = 0x00000004;
        s->reg.ssicdr  = 0x00000004;
        s->reg.lcr     = 0x000000f8;
        s->reg.clkgr   = 0x00000000;
        s->reg.scr     = 0x00001500;
        s->reg.rsr     = 0x00000001; // Power-on reset
    } else if (s->model <= 0x4750) {
        s->reg.cpccr   = 0x42040000;
        s->reg.cppcr   = 0x28080011;
        s->reg.cppsr   = 0x80000000;
        s->reg.i2scdr  = 0x00000004;
        s->reg.lpcdr   = 0x00000004;
        s->reg.msc0cdr = 0x00000000;
        s->reg.uhccdr  = 0x00000004;
        s->reg.ssicdr  = 0x00000000;
        s->reg.msc1cdr = 0x00000000;
        s->reg.pcmcdr  = 0x00000004;
        s->reg.lcr     = 0x000000f8;
        s->reg.clkgr   = 0x00000000;
        s->reg.opcr    = 0x00001500;
        s->reg.rsr     = 0x00000001; // Power-on reset
    } else {    // 4755
        s->reg.cpccr   = 0x42040000;
        s->reg.cppcr   = 0x28080011;
        s->reg.cppsr   = 0x80000000;
        s->reg.i2scdr  = 0x00000004;
        s->reg.lpcdr   = 0x00000004;
        s->reg.msccdr  = 0x00000000;
        s->reg.ssicdr  = 0x00000000;
        s->reg.cimcdr  = 0x00000004;
        s->reg.lcr     = 0x000000f8;
        s->reg.clkgr   = 0x00000000;
        s->reg.opcr    = 0x00001500;
        s->reg.rsr     = 0x00000001; // Power-on reset
    }
}

static void ingenic_cpm_update_clocks(IngenicCpm *s)
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

    trace_ingenic_cpm_cclk_freq(clock_get_hz(s->clk_cclk));
}

static uint64_t ingenic_cpm_read(void *opaque, hwaddr addr, unsigned size)
{
    if (unlikely(size != 4 || (addr & 3) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "CPM read unaligned @ " HWADDR_FMT_plx "/%"PRIx32"\n",
                      addr, (uint32_t)size);
        qmp_stop(NULL);
        return 0;
    }

    IngenicCpm *cpm = opaque;
    uint64_t data = 0;
    switch (addr) {
    case REG_CPCCR:
        data = cpm->reg.cpccr;
        break;
    case REG_LCR:
        data = cpm->reg.lcr;
        break;
    case REG_RSR:
        data = cpm->reg.rsr;
        break;
    case REG_CPPCR:
        data = cpm->reg.cppcr;
        break;
    case REG_CPPSR:
        data = cpm->reg.cppsr;
        break;
    case REG_CLKGR:
        data = cpm->reg.clkgr;
        break;
    case REG_OPCR:
    // case REG_SCR:
        if (cpm->model >= 0x4750)
            data = cpm->reg.opcr;
        else
            data = cpm->reg.scr;
        break;
    case REG_I2SCDR:
        data = cpm->reg.i2scdr;
        break;
    case REG_LPCDR:
        data = cpm->reg.lpcdr;
        break;
    case REG_MSCCDR:
    // case REG_MSC0CDR:
        if (cpm->model == 0x4750)
            data = cpm->reg.msc0cdr;
        else
            data = cpm->reg.msccdr;
        break;
    case REG_UHCCDR:
        data = cpm->reg.uhccdr;
        break;
    case REG_SSICDR:
        data = cpm->reg.ssicdr;
        break;
    case REG_MSC1CDR:
        data = cpm->reg.msc1cdr;
        break;
    case REG_PCMCDR:
    // case REG_CIMCDR:
        if (cpm->model >= 0x4755)
            data = cpm->reg.cimcdr;
        else
            data = cpm->reg.pcmcdr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "CPM read unknown address " HWADDR_FMT_plx "\n", addr);
        qmp_stop(NULL);
    }
    trace_ingenic_cpm_read(addr, data, size);
    return data;
}

static void ingenic_cpm_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    if (unlikely(size != 4 || (addr & 3) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "CPM write unaligned @ " HWADDR_FMT_plx "/%"PRIx32" 0x%"PRIx64"\n",
                      addr, (uint32_t)size, data);
        qmp_stop(NULL);
        return;
    }

    IngenicCpm *cpm = opaque;
    trace_ingenic_cpm_write(addr, data, size);
    switch (addr) {
    case REG_CPCCR:
        if (cpm->model >= 0x4755)
            cpm->reg.cpccr = data & 0xffefffff;
        else
            cpm->reg.cpccr = data;
        ingenic_cpm_update_clocks(cpm);
        break;
    case REG_LCR:
        cpm->reg.lcr = data & 0xff;
        break;
    case REG_RSR:
        cpm->reg.rsr &= data & 0x03;
        break;
    case REG_CPPCR:
        cpm->reg.cppcr = data & 0xffff03ff;
        if (cpm->reg.cppcr & BIT(8)) {
            // PLL ON
            cpm->reg.cppcr |= BIT(10);
        }
        ingenic_cpm_update_clocks(cpm);
        break;
    case REG_CLKGR:
        if (cpm->model >= 0x4755)
            cpm->reg.clkgr = data & 0x01ffffff;
        else if (cpm->model >= 0x4750)
            cpm->reg.clkgr = data & 0x1fffffff;
        else
            cpm->reg.clkgr = data & 0xffff;
        break;
    case REG_OPCR:
    // case REG_SCR:
        if (cpm->model >= 0x4755)
            cpm->reg.opcr = data & 0xff74;
        else if (cpm->model >= 0x4750)
            cpm->reg.opcr = data & 0xffd7;
        else
            cpm->reg.scr = data & 0xffd0;
        break;
    case REG_I2SCDR:
        cpm->reg.i2scdr = data & 0x01ff;
        break;
    case REG_LPCDR:
        if (cpm->model >= 0x4755)
            cpm->reg.lpcdr = data & 0xc00007ff;
        else if (cpm->model >= 0x4750)
            cpm->reg.lpcdr = data & 0xe00007ff;
        else
            cpm->reg.lpcdr = data & 0x800007ff;
        ingenic_cpm_update_clocks(cpm);
        break;
    case REG_MSCCDR:
    // case REG_MSC0CDR:
        if (cpm->model == 0x4750)
            cpm->reg.msc0cdr = data & 0x1f;
        else
            cpm->reg.msccdr = data & 0x1f;
        break;
    case REG_MSC1CDR:
        cpm->reg.msc1cdr = data & 0x1f;
        break;
    case REG_UHCCDR:
        cpm->reg.uhccdr = data & 0x0f;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "CPM write unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
        qmp_stop(NULL);
        return;
    }
}

static MemoryRegionOps cpm_ops = {
    .read = ingenic_cpm_read,
    .write = ingenic_cpm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

IngenicCpm *ingenic_cpm_get_cpm(void)
{
    Object *obj = object_resolve_path_type("", TYPE_INGENIC_CPM, NULL);
    if (!obj) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: "TYPE_INGENIC_CPM" device not found", __func__);
        return NULL;
    }
    return INGENIC_CPM(obj);
}

static const ClockPortInitArray cpm_clks = {
    QDEV_CLOCK_OUT(IngenicCpm, clk_ext),
    QDEV_CLOCK_OUT(IngenicCpm, clk_rtc),
    QDEV_CLOCK_OUT(IngenicCpm, clk_pll),
    QDEV_CLOCK_OUT(IngenicCpm, clk_cclk),
    QDEV_CLOCK_OUT(IngenicCpm, clk_mclk),
    QDEV_CLOCK_OUT(IngenicCpm, clk_pclk),
    QDEV_CLOCK_OUT(IngenicCpm, clk_lcdpix),
    QDEV_CLOCK_END
};

static void ingenic_cpm_realize(DeviceState *dev, Error **errp)
{
    IngenicCpm *s = INGENIC_CPM(dev);
    clock_set_hz(s->clk_ext, s->ext_freq);
    clock_set_hz(s->clk_rtc, s->rtc_freq);
    ingenic_cpm_update_clocks(s);
}

OBJECT_DEFINE_TYPE(IngenicCpm, ingenic_cpm, INGENIC_CPM, SYS_BUS_DEVICE)

static void ingenic_cpm_init(Object *obj)
{
    IngenicCpm *s = INGENIC_CPM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &cpm_ops, s, "cpm", 0x1000);
    sysbus_init_mmio(sbd, &s->mr);
    qdev_init_clocks(DEVICE(s), cpm_clks);
}

static void ingenic_cpm_finalize(Object *obj)
{
}

static const Property ingenic_cpm_properties[] = {
    DEFINE_PROP_UINT32("model", IngenicCpm, model, 0x4755),
    DEFINE_PROP_UINT32("ext-freq", IngenicCpm, ext_freq, 24000000),
    DEFINE_PROP_UINT32("rtc-freq", IngenicCpm, rtc_freq, 32768),
};

static void ingenic_cpm_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, ingenic_cpm_properties);
    dc->realize = ingenic_cpm_realize;

    ResettableClass *rc = RESETTABLE_CLASS(class);
    rc->phases.enter = ingenic_cpm_reset;
}

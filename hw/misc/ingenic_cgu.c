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

static void ingenic_cgu_update_clocks(IngenicCgu *cgu)
{
    // Update clock frequencies
    uint32_t ext_clk = 24000000;
    uint32_t sys_clk = ext_clk;

    if ((cgu->CPPCR & (BIT(8) | BIT(9))) == BIT(8)) {
        // Switch to PLL
        uint32_t m = (cgu->CPPCR >> 23) + 2;
        uint32_t n = ((cgu->CPPCR >> 18) & 0x1f) + 2;
        uint32_t od = (cgu->CPPCR >> 16) & 3;
        static const uint32_t od_map[] = {1, 2, 2, 4};
        od = od_map[od];
        uint32_t pll_clk = ext_clk * m / n / od;
        sys_clk = pll_clk;
    }

    uint32_t cclk = sys_clk;
    uint32_t cdiv = cgu->CPCCR & 0x0f;
    static const uint32_t cdiv_map[16] = {1, 2, 3, 4, 6, 8, 0};
    cdiv = cdiv_map[cdiv];
    if (unlikely(cdiv == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "CGU cclk div by 0\n");
        cdiv = 1;
    }
    cclk /= cdiv;

    qemu_log("CGU freq cclk %"PRIu32"\n", cclk);
    clock_set_hz(qdev_get_clock_out(DEVICE(cgu), "clk-cclk"), cclk);
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

static void ingenic_cgu_init(Object *obj)
{
    printf("%s enter\n", __func__);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IngenicCgu *s = INGENIC_CGU(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &cgu_ops, s, "cgu", 0x1000);
    sysbus_init_mmio(sbd, &s->mr);

    qdev_init_clock_out(DEVICE(s), "clk-cclk");
    ingenic_cgu_update_clocks(s);
    printf("%s end\n", __func__);
}

static void ingenic_cgu_finalize(Object *obj)
{
    printf("%s enter\n", __func__);
}

static void ingenic_cgu_class_init(ObjectClass *class, void *data)
{
    IngenicCguClass *cgu_class = INGENIC_CGU_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_cgu_reset,
                                       NULL,
                                       NULL,
                                       &cgu_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicCgu, ingenic_cgu, INGENIC_CGU, SYS_BUS_DEVICE)

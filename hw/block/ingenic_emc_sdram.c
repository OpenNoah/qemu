/*
 * Ingenic External Memory Controller SDRAM interface emulation
 *
 * Copyright (c) 2024
 * Written by Norman Zhi <normanzyb@gmail.com>
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
#include "qemu/bswap.h"
#include "qapi/error.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"

#include "hw/qdev-properties.h"
#include "hw/block/ingenic_emc.h"

#define REG_DMCR    0x80
#define REG_RTCSR   0x84
#define REG_RTCNT   0x88
#define REG_RTCOR   0x8c
#define REG_DMAR1   0x90
#define REG_DMAR2   0x94

void qmp_stop(Error **errp);

static void ingenic_emc_sdram_init(Object *obj)
{
}

static void ingenic_emc_sdram_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    IngenicEmcSdram *s = INGENIC_EMC_SDRAM(obj);
    MemoryRegion *sys_mem = get_system_memory();

    // SDRAM bank data section container
    memory_region_init_ram(&s->mr, obj, "emc.sdram", s->size, &error_fatal);
    // Disabled for now
    memory_region_set_enabled(&s->mr, false);
    memory_region_add_subregion(sys_mem, 0, &s->mr);
    // No alias sections yet
    s->alias_mr = 0;
    // Register on EMC main controller
    ingenic_emc_register_sdram(s, s->cs);
}

static void ingenic_emc_sdram_finalize(Object *obj)
{
}

static Property ingenic_emc_sdram_properties[] = {
    DEFINE_PROP_UINT32("cs", IngenicEmcSdram, cs, 0),
    DEFINE_PROP_UINT32("size", IngenicEmcSdram, size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ingenic_emc_sdram_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, ingenic_emc_sdram_properties);
    dc->realize = ingenic_emc_sdram_realize;
}

OBJECT_DEFINE_TYPE(IngenicEmcSdram, ingenic_emc_sdram, INGENIC_EMC_SDRAM, DEVICE)


// EMC SDRAM configuration space

uint64_t ingenic_emc_sdram_read(IngenicEmc *emc, hwaddr addr, unsigned size)
{
    IngenicEmcSdramCfg *s = &emc->sdram_cfg;
    uint64_t data = 0;
    switch (addr) {
    case REG_DMCR:
        return s->reg.dmcr;
        break;
    case REG_RTCSR:
        return s->reg.rtcsr | BIT(7);
        break;
    case REG_RTCNT:
        return s->reg.rtcnt;
        break;
    case REG_RTCOR:
        return s->reg.rtcor;
        break;
    case REG_DMAR1:
        return s->reg.dmar[0];
        break;
    case REG_DMAR2:
        return s->reg.dmar[1];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    return data;
}

static void ingenic_emc_sdram_write_dmar(IngenicEmc *emc, int bank, uint16_t value)
{
    IngenicEmcSdramCfg *s = &emc->sdram_cfg;
    uint16_t dmar = value & 0xffff;
    s->reg.dmar[bank] = dmar;

    IngenicEmcSdram *sdram = emc->sdram[bank];
    if (!sdram)
        return;

    // Update main data region base address
    uint32_t ofs = (dmar & 0xff00) << 16;
    memory_region_set_address(&sdram->mr, ofs);
    memory_region_set_enabled(&sdram->mr, true);

    // Delete existing alias regions
    MemoryRegion *sys_mem = get_system_memory();
    if (sdram->alias_mr) {
        // It is an exception that alias regions may be unparented at any time
        for (int i = 0; i < sdram->num_aliases; i++) {
            memory_region_del_subregion(sys_mem, &sdram->alias_mr[i]);
            object_unparent(OBJECT(&sdram->alias_mr[i]));
        }
        g_free(sdram->alias_mr);
    }
    // Update alias regions to fill the entire SDRAM bank size
    // Holes not handled
    sdram->num_aliases = (((~dmar & 0xff) + 1) << 24) / sdram->size - 1;
    sdram->alias_mr = g_new(MemoryRegion, sdram->num_aliases);
    for (int i = 0; i < sdram->num_aliases; i++) {
        ofs += sdram->size;
        memory_region_init_alias(&sdram->alias_mr[i], OBJECT(sdram), "emc.sdram.alias",
                                 &sdram->mr, 0, sdram->size);
        memory_region_add_subregion(sys_mem, ofs, &sdram->alias_mr[i]);
    }
}

void ingenic_emc_sdram_write(IngenicEmc *emc, hwaddr addr, uint64_t data, unsigned size)
{
    if (addr >= 0x1000) {
        // Mode register write
        trace_ingenic_sdram_dmr_write(addr, data);
        return;
    }

    IngenicEmcSdramCfg *s = &emc->sdram_cfg;
    switch (addr) {
    case REG_DMCR:
        s->reg.dmcr = data & 0x9fbfff7f;
        break;
    case REG_RTCSR:
        s->reg.rtcsr = data & 0x0007;
        break;
    case REG_RTCNT:
        s->reg.rtcnt = data & 0xffff;
        break;
    case REG_RTCOR:
        s->reg.rtcor = data & 0xffff;
        break;
    case REG_DMAR1:
    case REG_DMAR2:
        ingenic_emc_sdram_write_dmar(emc, (addr - REG_DMAR1) / 4, data);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

void ingenic_emc_sdram_reset(IngenicEmc *emc, ResetType type)
{
    IngenicEmcSdramCfg *s = &emc->sdram_cfg;
    s->reg.dmcr = 0;
    ingenic_emc_sdram_write_dmar(emc, 0, 0x20f8);
    ingenic_emc_sdram_write_dmar(emc, 1, 0x28f8);
}

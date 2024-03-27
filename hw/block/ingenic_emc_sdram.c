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

void qmp_stop(Error **errp);

static uint64_t ingenic_emc_sdram_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicEmcSdram *s = INGENIC_EMC_SDRAM(opaque);
    uint64_t data = 0;
    switch (addr) {
    case 0x00:
        return s->dmcr;
        break;
    case 0x04:
        return s->rtcsr;
        break;
    case 0x08:
        return s->rtcnt;
        break;
    case 0x0c:
        return s->rtcor;
        break;
    case 0x10:
        return s->dmar[0];
        break;
    case 0x14:
        return s->dmar[1];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);
    return data;
}

static void ingenic_emc_sdram_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);

    IngenicEmcSdram *s = INGENIC_EMC_SDRAM(opaque);
    switch (addr) {
    case 0x00:
        s->dmcr = data & 0x9fbfff7f;
        break;
    case 0x04:
        s->rtcsr = data & 0x0007;
        break;
    case 0x08:
        s->rtcnt = data & 0xffff;
        break;
    case 0x0c:
        s->rtcor = data & 0xffff;
        break;
    case 0x10:
        s->dmar[0] = data & 0xffff;
        memory_region_set_address(&s->mr[0], (s->dmar[0] & 0xff00) << 16);
        break;
    case 0x14:
        s->dmar[1] = data & 0xffff;
        memory_region_set_address(&s->mr[1], (s->dmar[1] & 0xff00) << 16);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps sdram_ops = {
    .read = ingenic_emc_sdram_read,
    .write = ingenic_emc_sdram_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_emc_sdram_dmr_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);

    IngenicEmcSdram *s = INGENIC_EMC_SDRAM(opaque);
    if (s->dmcr & BIT(23)) {
        // SDMR write, enable SDRAM banks
        uint32_t bank = (s->dmcr >> 16) & 1;
        qemu_log("%s: Enabling bank %"PRIu32"\n", __func__, bank);
        if (!s->size[bank]) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bank %"PRIu32" no media\n", __func__, bank);
            return;
        }
        memory_region_set_enabled(&s->mr[bank], true);
    }
}

static MemoryRegionOps sdram_dmr_ops = {
    .write = ingenic_emc_sdram_dmr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_emc_sdram_init(Object *obj)
{
    qemu_log("%s enter\n", __func__);

    IngenicEmcSdram *s = INGENIC_EMC_SDRAM(obj);
    MemoryRegion *sys_mem = get_system_memory();

    // Aliased SDRAM region at 0
    memory_region_init_alias(&s->origin_alias_mr, obj, "emc.sdram.alias",
                             sys_mem, 0x20000000, 0x08000000);
    memory_region_add_subregion(sys_mem, 0x00000000, &s->origin_alias_mr);

    // EMC registers
    memory_region_init_io(&s->emc_mr, obj, &sdram_ops, s, "emc.sdram", 0x80);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->emc_mr);
    memory_region_init_io(&s->dmr_mr, obj, &sdram_dmr_ops, s, "emc.sdram.dmr", 0x8000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->dmr_mr);
}

static void ingenic_emc_sdram_realize(DeviceState *dev, Error **errp)
{
    qemu_log("%s enter\n", __func__);

    Object *obj = OBJECT(dev);
    IngenicEmcSdram *s = INGENIC_EMC_SDRAM(obj);
    MemoryRegion *sys_mem = get_system_memory();

    // Add SDRAM banks, now that size is known
    for (int bank = 0; bank < 2; bank++) {
        // TODO set as reset value
        s->dmar[bank] = 0x20f8 + (0x0800 * bank);
        if (!s->size[bank])
            continue;

        // SDRAM bank data section (128MiB) container
        char name[] = "emc.sdram.bank0";
        name[14] = '0' + bank;
        uint32_t bank_size = 128 * 1024 * 1024;
        memory_region_init(&s->mr[bank], obj, name, bank_size);
        // Disabled for now
        memory_region_set_enabled(&s->mr[bank], false);
        memory_region_add_subregion(sys_mem, (s->dmar[bank] & 0xff00) << 16,
                                    &s->mr[bank]);

        // Allocate main and alias sections
        uint32_t num_aliases = bank_size / s->size[bank];
        s->alias_mr[bank] = g_new(MemoryRegion, num_aliases);

        // Main SDRAM data section at alias[0]
        memory_region_init_ram(&s->alias_mr[bank][0], obj, "data", s->size[bank], &error_fatal);
        memory_region_add_subregion(&s->mr[bank], 0, &s->alias_mr[bank][0]);

        // Add aliases to fill the entire SDRAM bank region
        for (int i = 1; i < num_aliases; i++) {
            memory_region_init_alias(&s->alias_mr[bank][i], obj, "alias",
                                     &s->alias_mr[bank][0], 0, s->size[bank]);
            memory_region_add_subregion(&s->mr[bank], s->size[bank], &s->alias_mr[bank][i]);
        }
    }
}

static void ingenic_emc_sdram_finalize(Object *obj)
{
    ;
}

static Property ingenic_emc_sdram_properties[] = {
    DEFINE_PROP_UINT32("bank0-size", IngenicEmcSdram, size[0], 0),
    DEFINE_PROP_UINT32("bank1-size", IngenicEmcSdram, size[1], 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ingenic_emc_sdram_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, ingenic_emc_sdram_properties);
    dc->realize = ingenic_emc_sdram_realize;
}

OBJECT_DEFINE_TYPE(IngenicEmcSdram, ingenic_emc_sdram, INGENIC_EMC_SDRAM, SYS_BUS_DEVICE)

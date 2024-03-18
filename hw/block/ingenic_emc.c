/*
 * Ingenic External Memory Controller (SDRAM, NAND) emulation
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
#include "qapi/error.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"

#include "hw/qdev-properties.h"
#include "hw/block/ingenic_emc.h"

void qmp_stop(Error **errp);

static void ingenic_emc_reset(Object *obj, ResetType type)
{
    IngenicEmc *s = INGENIC_EMC(obj);

    // Initial register values
    s->BCR = 0x00000001;
    for (int i = 0; i < 4; i++)
        s->SMCR[i] = 0x0fff7700;
    s->SACR[0] = 0x000018fc;
    s->SACR[1] = 0x000014fc;
    s->SACR[2] = 0x00000cfc;
    s->SACR[3] = 0x000008fc;
}

static uint64_t ingenic_emc_read(void *opaque, hwaddr addr, unsigned size)
{
    if (unlikely(size != 4 || (addr & 3) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "EMC read unaligned @ " HWADDR_FMT_plx "/%"PRIx32"\n",
                      addr, (uint32_t)size);
        qmp_stop(NULL);
        return 0;
    }

    IngenicEmc *emc = opaque;
    hwaddr aligned_addr = addr; // & ~3;
    uint64_t data = 0;
    switch (aligned_addr) {
    case 0x00:
        data = emc->BCR;
        break;
    case 0x14:
    case 0x18:
    case 0x1c:
    case 0x20:
        data = emc->SMCR[(aligned_addr - 0x14) / 4];
        break;
    case 0x34:
    case 0x38:
    case 0x3c:
    case 0x40:
        data = emc->SACR[(aligned_addr - 0x34) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "EMC read unknown address " HWADDR_FMT_plx "\n", aligned_addr);
        qmp_stop(NULL);
    }
    //data = (data >> (8 * (addr & 3))) & ((1LL << (8 * size)) - 1);
    qemu_log("EMC read @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", addr, (uint32_t)size, data);
    return data;
}

static void ingenic_emc_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    if (unlikely(size != 4 || (addr & 3) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "EMC write unaligned @ " HWADDR_FMT_plx "/%"PRIx32" 0x%"PRIx64"\n",
                      addr, (uint32_t)size, data);
        qmp_stop(NULL);
        return;
    }

    IngenicEmc *emc = opaque;
    hwaddr aligned_addr = addr; // & ~3;
    qemu_log("EMC write @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", addr, (uint32_t)size, data);
    switch (aligned_addr) {
    case 0x00:
        emc->BCR = (data & 0x00000004) | 0x00000001;
        break;
    case 0x14:
    case 0x18:
    case 0x1c:
    case 0x20:
        emc->SMCR[(aligned_addr - 0x14) / 4] = data & 0x0fff77cf;
        break;
    case 0x34:
    case 0x38:
    case 0x3c:
    case 0x40:
        emc->SACR[(aligned_addr - 0x34) / 4] = data & 0x0000ffff;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "EMC write unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
        qmp_stop(NULL);
        return;
    }
}

static MemoryRegionOps emc_ops = {
    .read = ingenic_emc_read,
    .write = ingenic_emc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ingenic_emc_init(Object *obj)
{
    qemu_log("%s enter\n", __func__);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IngenicEmc *s = INGENIC_EMC(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &emc_ops, s, "emc", 0x10000);
    sysbus_init_mmio(sbd, &s->mr);

    qemu_log("%s end\n", __func__);
}

static void ingenic_emc_finalize(Object *obj)
{
    qemu_log("%s enter\n", __func__);
}

static void ingenic_emc_class_init(ObjectClass *class, void *data)
{
    IngenicEmcClass *emc_class = INGENIC_EMC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_emc_reset,
                                       NULL,
                                       NULL,
                                       &emc_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicEmc, ingenic_emc, INGENIC_EMC, SYS_BUS_DEVICE)

/*
 * Ingenic External Memory Controller emulation
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
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"

#include "hw/qdev-properties.h"
#include "hw/block/ingenic_emc.h"

void qmp_stop(Error **errp);

static const uint32_t static_bank_addr[] = {
    0x18000000, 0x14000000, 0x0c000000, 0x08000000,
};

static uint64_t ingenic_emc_static_null_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Read with no device attached @ "
                  HWADDR_FMT_plx "/%"PRIu32"\n", __func__, addr, size);
    return __UINT64_MAX__;
}

static void ingenic_emc_static_null_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Write with no device attached @ "
                  HWADDR_FMT_plx "/%"PRIu32": 0x%"PRIx64"\n", __func__, addr, size, data);
}

static MemoryRegionOps emc_static_null_ops = {
    .read = ingenic_emc_static_null_read,
    .write = ingenic_emc_static_null_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

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
    case 0x50:
        data = emc->NFCSR;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "EMC read unknown address " HWADDR_FMT_plx "\n", aligned_addr);
        qmp_stop(NULL);
    }
    //data = (data >> (8 * (addr & 3))) & ((1LL << (8 * size)) - 1);
    trace_ingenic_emc_read(addr, data);
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
    uint32_t diff = 0;
    uint32_t bank = 0;
    trace_ingenic_emc_write(addr, data);
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
        bank = (addr - 0x34) / 4;
        emc->SACR[bank] = data & 0x0000ffff;
        // TODO Update memory region
        break;
    case 0x50:
        diff = (emc->NFCSR ^ data) & 0x55;
        emc->NFCSR = data & 0xff;
        if (diff) {
            for (int bank = 0; bank < 4; bank++) {
                bool nand_mode = !!(emc->NFCSR & BIT(bank * 2));
                qemu_log("%s: EMC bank %"PRIu32": %s\n", __func__, bank + 1, nand_mode ? "NAND" : "SRAM");
                if (emc->nand[bank]) {
                    memory_region_set_enabled(&emc->nand[bank]->mr, nand_mode);
                    memory_region_set_enabled(&emc->static_null_mr[bank], !nand_mode);
                } else if (nand_mode) {
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Attempting to enable NAND %"PRIu32
                                  ", but no ingenic-emc-nand attached\n",
                                  __func__, bank + 1);
                }
            }
        }
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
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static IngenicEmc *get_emc()
{
    Object *obj = object_resolve_path_type("", TYPE_INGENIC_EMC, NULL);
    if (!obj) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: ingenic-emc device not found", __func__);
        return NULL;
    }
    return INGENIC_EMC(obj);
}

IngenicEmc *ingenic_emc_register_nand(IngenicEmcNand *nand, uint32_t cs)
{
    if (cs < 1 || cs > 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid CS %"PRIu32", 1~4 supported by ingenic-emc", cs);
        return NULL;
    }

    IngenicEmc *s = get_emc();
    uint32_t bank = cs - 1;
    s->nand[bank] = nand;

    // Disable and attach to static memory region
    MemoryRegion *sys_mem = get_system_memory();
    MemoryRegion *nand_mr = &nand->mr;
    memory_region_set_enabled(nand_mr, false);
    memory_region_add_subregion(sys_mem, static_bank_addr[bank], nand_mr);

    return s;
}

static void ingenic_emc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IngenicEmc *emc = INGENIC_EMC(obj);
    MemoryRegion *sys_mem = get_system_memory();

    memory_region_init(&emc->emc_mr, NULL, "emc", 0x10000);
    sysbus_init_mmio(sbd, &emc->emc_mr);

    memory_region_init_io(&emc->emc_sramcfg_mr, obj, &emc_ops, emc, "emc.sramcfg", 0x80);
    memory_region_add_subregion(&emc->emc_mr, 0, &emc->emc_sramcfg_mr);

    qdev_init_gpio_out_named(DEVICE(obj), &emc->io_nand_rb, "nand-rb", 1);
    qemu_irq_raise(emc->io_nand_rb);

    emc->sdram = INGENIC_EMC_SDRAM(qdev_new(TYPE_INGENIC_EMC_SDRAM));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(emc->sdram), &error_fatal);
    memory_region_add_subregion(&emc->emc_mr, 0x80,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(emc->sdram), 0));
    memory_region_add_subregion(&emc->emc_mr, 0x8000,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(emc->sdram), 1));

    // Add NOP background static memory regions
    // Write gets ignored, read returns 0xff
    for (int bank = 0; bank < 4; bank++) {
        memory_region_init_io(&emc->static_null_mr[bank], OBJECT(emc),
                              &emc_static_null_ops, NULL, "emc.static.null", 0x04000000);
        memory_region_add_subregion_overlap(sys_mem, static_bank_addr[bank], &emc->static_null_mr[bank], -1);
    }
}

static void ingenic_emc_finalize(Object *obj)
{
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

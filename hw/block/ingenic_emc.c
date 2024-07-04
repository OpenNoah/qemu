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

#define REG_BCR     0x0000

#define REG_SMCR1   0x0014
#define REG_SMCR2   0x0018
#define REG_SMCR3   0x001c
#define REG_SMCR4   0x0020
#define REG_SACR1   0x0034
#define REG_SACR2   0x0038
#define REG_SACR3   0x003c
#define REG_SACR4   0x0040

#define REG_NFCSR   0x0050

void qmp_stop(Error **errp);

static const uint32_t static_bank_addr[] = {
    0x18000000, 0x14000000, 0x0c000000, 0x08000000,
};

static uint64_t ingenic_emc_static_null_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t bank = (uint64_t)opaque;
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bank %u no device attached @ "
                  HWADDR_FMT_plx "/%"PRIu32"\n",
                  __func__, bank + 1, addr, size);
    return __UINT64_MAX__;
}

static void ingenic_emc_static_null_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    uint32_t bank = (uint64_t)opaque;
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bank %u no device attached @ "
                  HWADDR_FMT_plx "/%"PRIu32": 0x%"PRIx64"\n",
                  __func__, bank + 1, addr, size, data);
}

static MemoryRegionOps emc_static_null_ops = {
    .read = ingenic_emc_static_null_read,
    .write = ingenic_emc_static_null_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_emc_reset(Object *obj, ResetType type)
{
    IngenicEmc *s = INGENIC_EMC(obj);
    s->BCR = 0x00000001;
    for (int i = 0; i < 4; i++)
        s->SMCR[i] = 0x0fff7700;
    s->SACR[0] = 0x000018fc;
    s->SACR[1] = 0x000014fc;
    s->SACR[2] = 0x00000cfc;
    s->SACR[3] = 0x000008fc;

    ingenic_emc_nand_ecc_reset(s, type);
    ingenic_emc_sdram_reset(s, type);
}

static uint64_t ingenic_emc_read(void *opaque, hwaddr addr, unsigned size)
{
    if (unlikely(size != 4 || (addr & 3) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "EMC read unaligned @ " HWADDR_FMT_plx "/%"PRIx32"\n",
                      addr, (uint32_t)size);
        qmp_stop(NULL);
        return 0;
    }

    IngenicEmc *s = INGENIC_EMC(opaque);
    uint64_t data = 0;
    if (addr < 0x80) {
        // Static RAM interface
        switch (addr) {
        case REG_BCR:
            data = s->BCR;
            break;
        case REG_SMCR1:
        case REG_SMCR2:
        case REG_SMCR3:
        case REG_SMCR4:
            data = s->SMCR[(addr - REG_SMCR1) / 4];
            break;
        case REG_SACR1:
        case REG_SACR2:
        case REG_SACR3:
        case REG_SACR4:
            data = s->SACR[(addr - REG_SACR1) / 4];
            break;
        case REG_NFCSR:
            data = s->NFCSR;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown address " HWADDR_FMT_plx "\n", __func__, addr);
            qmp_stop(NULL);
        }

    } else if (addr < 0x0100) {
        // SDRAM interface
        data = ingenic_emc_sdram_read(s, addr, size);

    } else if (addr < 0x0200) {
        // NAND ECC module
        data = ingenic_emc_nand_ecc_read(s, addr, size);

    }
    trace_ingenic_emc_read(addr, data);
    return data;
}

static void ingenic_emc_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    IngenicEmc *s = INGENIC_EMC(opaque);
    trace_ingenic_emc_write(addr, data);
    uint32_t diff = 0;
    uint32_t bank = 0;
    if (addr < 0x80) {
        // Static RAM interface
        switch (addr) {
        case REG_BCR:
            if (s->model == 0x4755)
                s->BCR = (data & 0x00000004) | 0x00000001;
            else
                s->BCR = (data & 0x00000002) | 0x00000001;
            break;
        case REG_SMCR1:
        case REG_SMCR2:
        case REG_SMCR3:
        case REG_SMCR4:
            s->SMCR[(addr - REG_SMCR1) / 4] = data & 0x0fff77cf;
            break;
        case REG_SACR1:
        case REG_SACR2:
        case REG_SACR3:
        case REG_SACR4: {
            bank = (addr - REG_SACR1) / 4;
            s->SACR[bank] = data & 0xffff;
            // Update memory region
            MemoryRegion *mr = s->nand[bank] ? &s->nand[bank]->mr : &s->static_mr[bank];
            memory_region_set_address(mr, (data & 0xff00) << 16);
            memory_region_set_size(mr, ((~data & 0xff) + 1) << 24);
            if ((data & 0xff) != 0xfc) {
                qemu_log_mask(LOG_UNIMP, "%s: Unsupported mask 0x%"PRIx64"\n", __func__, data);
                qmp_stop(NULL);
            }
            break;
        }
        case REG_NFCSR:
            diff = (s->NFCSR ^ data) & 0x55;
            s->NFCSR = data & 0xff;
            if (diff) {
                for (int bank = 0; bank < 4; bank++) {
                    bool nand_mode = !!(s->NFCSR & BIT(bank * 2));
                    trace_ingenic_emc_mode(bank + 1, nand_mode ? "NAND" : "SRAM");
                    if (s->nand[bank]) {
                        memory_region_set_enabled(&s->nand[bank]->mr, nand_mode);
                        memory_region_set_enabled(&s->static_mr[bank], !nand_mode);
                    } else if (nand_mode) {
                        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bank %u no NAND attached\n",
                                      __func__, bank + 1);
                    }
                }
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown address " HWADDR_FMT_plx "\n", __func__, addr);
            qmp_stop(NULL);
        }

    } else if (addr < 0x0100) {
        // SDRAM interface
        ingenic_emc_sdram_write(s, addr, data, size);

    } else if (addr < 0x0200) {
        // NAND ECC module
        ingenic_emc_nand_ecc_write(s, addr, data, size);

    } else {
        // SDRAM mode register
        ingenic_emc_sdram_write(s, addr, data, size);

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
    IngenicEmc *s = get_emc();
    uint32_t bank = cs - 1;
    if (bank >= ARRAY_SIZE(s->nand)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid CS %u", __func__, cs);
        return NULL;
    }
    s->nand[bank] = nand;

    // Disable and attach to static memory region
    MemoryRegion *sys_mem = get_system_memory();
    MemoryRegion *nand_mr = &nand->mr;
    memory_region_set_enabled(nand_mr, false);
    memory_region_add_subregion(sys_mem, static_bank_addr[bank], nand_mr);
    return s;
}

IngenicEmc *ingenic_emc_register_sdram(IngenicEmcSdram *sdram, uint32_t cs)
{
    IngenicEmc *s = get_emc();
    uint32_t bank = cs;
    if (bank >= ARRAY_SIZE(s->sdram)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid CS %u", __func__, cs);
        return NULL;
    }
    s->sdram[bank] = sdram;
    // SDRAM manages its own system memory region
    return s;
}

static void ingenic_emc_init(Object *obj)
{
    IngenicEmc *s = INGENIC_EMC(obj);
    // EMC SRAM/NAND/SDRAM configuration space
    memory_region_init_io(&s->mr, obj, &emc_ops, s, "emc", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    // Static RAM
    MemoryRegion *sys_mem = get_system_memory();
    for (int bank = 0; bank < 4; bank++) {
        // Add NOP background static memory regions
        // Write gets ignored, read returns 0xff
        memory_region_init_io(&s->static_mr[bank], obj,
                              &emc_static_null_ops, (void *)(uintptr_t)bank, "emc.static.null", 0x04000000);
        memory_region_add_subregion_overlap(sys_mem, static_bank_addr[bank], &s->static_mr[bank], -1);
    }

    // NAND
    qdev_init_gpio_out_named(DEVICE(obj), &s->io_nand_rb, "nand-rb", 1);
    qemu_irq_raise(s->io_nand_rb);

    // Aliased SDRAM region at 0
    memory_region_init_alias(&s->sdram_alias_mr, obj, "emc.sdram.alias0",
                             sys_mem, 0x20000000, 0x08000000);
    memory_region_add_subregion(sys_mem, 0x00000000, &s->sdram_alias_mr);
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

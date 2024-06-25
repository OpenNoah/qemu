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

#ifndef INGENIC_EMC_H
#define INGENIC_EMC_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/block/block.h"

// EMC

#define TYPE_INGENIC_EMC "ingenic-emc"
OBJECT_DECLARE_TYPE(IngenicEmc, IngenicEmcClass, INGENIC_EMC)

typedef struct IngenicEmcNand  IngenicEmcNand;
typedef struct IngenicEmcSdram IngenicEmcSdram;

typedef struct IngenicEmc {
    SysBusDevice parent_obj;

    MemoryRegion emc_mr;
    MemoryRegion emc_sramcfg_mr;
    MemoryRegion static_null_mr[4];

    IngenicEmcNand *nand[4];
    IngenicEmcSdram *sdram;

    // GPIO
    qemu_irq io_nand_rb;

    // Properties
    uint32_t sdram_size;

    // Registers
    uint32_t BCR;
    uint32_t SMCR[4];
    uint32_t SACR[4];
    uint32_t NFCSR;
} IngenicEmc;

typedef struct IngenicEmcClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicEmcClass;

IngenicEmc *ingenic_emc_register_nand(IngenicEmcNand *nand, uint32_t cs);

// NAND

#define TYPE_INGENIC_EMC_NAND "ingenic-emc-nand"
OBJECT_DECLARE_TYPE(IngenicEmcNand, IngenicEmcNandClass, INGENIC_EMC_NAND)

typedef struct IngenicEmcNand {
    // Class inheritance
    DeviceState parent_obj;
    MemoryRegion mr;
    // Properties
    BlockBackend *blk;
    IngenicEmc *emc;
    char *nand_id_str;
    uint64_t nand_id;
    uint32_t total_pages;
    uint32_t page_size;
    uint32_t oob_size;
    uint32_t cs;
    bool writable;
    // States
    uint8_t prev_cmd;
    uint8_t status;
    uint32_t addr_ofs;
    uint64_t addr;
    // Read/write buffers
    uint8_t *page_buf;
    uint32_t page_ofs;
} IngenicEmcNand;

typedef struct IngenicEmcNandClass
{
    SysBusDeviceClass parent_class;
} IngenicEmcNandClass;

// SDRAM

#define TYPE_INGENIC_EMC_SDRAM "ingenic-emc-sdram"
OBJECT_DECLARE_TYPE(IngenicEmcSdram, IngenicEmcSdramClass, INGENIC_EMC_SDRAM)

typedef struct IngenicEmcSdram {
    // Class inheritance
    SysBusDevice parent_obj;
    // Properties
    uint32_t size[2];
    MemoryRegion mr[2];
    MemoryRegion *alias_mr[2];
    MemoryRegion origin_alias_mr;
    MemoryRegion emc_mr;
    MemoryRegion dmr_mr;
    // Registers
    uint32_t dmcr;
    uint16_t rtcsr;
    uint16_t rtcnt;
    uint16_t rtcor;
    uint16_t dmar[2];
} IngenicEmcSdram;

typedef struct IngenicEmcSdramClass
{
    SysBusDeviceClass parent_class;
} IngenicEmcSdramClass;

#endif /* INGENIC_EMC_H */

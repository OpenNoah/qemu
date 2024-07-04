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

typedef struct IngenicEmc IngenicEmc;

// EMC NAND instances

#define TYPE_INGENIC_EMC_NAND "ingenic-emc-nand"
OBJECT_DECLARE_TYPE(IngenicEmcNand, IngenicEmcNandClass, INGENIC_EMC_NAND)

typedef struct IngenicEmcNand {
    DeviceState parent_obj;
    MemoryRegion mr;
    IngenicEmc *emc;

    // Properties
    BlockBackend *blk;
    char *nand_id_str;
    uint64_t nand_id;
    uint32_t total_pages;
    uint32_t block_pages;
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
    DeviceClass parent_class;
} IngenicEmcNandClass;

// EMC NAND ECC module

typedef struct IngenicEmcNandEcc {
    uint32_t data_count;
    struct {
        uint8_t  nfeccr;
        uint32_t nfecc;
        uint32_t nfpar[3];
        uint32_t nfints;
        uint8_t  nfinte;
        uint32_t nferr[4];
    } reg;
} IngenicEmcNandEcc;

void ingenic_emc_nand_ecc_reset(IngenicEmc *emc, ResetType type);
uint64_t ingenic_emc_nand_ecc_read(IngenicEmc *emc, hwaddr addr, unsigned size);
void ingenic_emc_nand_ecc_write(IngenicEmc *emc, hwaddr addr, uint64_t value, unsigned size);
void ingenic_emc_nand_ecc_data(IngenicEmc *emc, uint64_t value, unsigned size);

// EMC SDRAM instances

#define TYPE_INGENIC_EMC_SDRAM "ingenic-emc-sdram"
OBJECT_DECLARE_TYPE(IngenicEmcSdram, IngenicEmcSdramClass, INGENIC_EMC_SDRAM)

typedef struct IngenicEmcSdram {
    // Class inheritance
    DeviceState parent_obj;
    MemoryRegion mr;
    int num_aliases;
    MemoryRegion *alias_mr;
    // Properties
    uint32_t cs;
    uint32_t size;
} IngenicEmcSdram;

typedef struct IngenicEmcSdramClass
{
    DeviceClass parent_class;
} IngenicEmcSdramClass;

typedef struct IngenicEmcSdramCfg {
    struct {
        // Registers
        uint32_t dmcr;
        uint16_t rtcsr;
        uint16_t rtcnt;
        uint16_t rtcor;
        uint16_t dmar[2];
    } reg;
} IngenicEmcSdramCfg;

void ingenic_emc_sdram_reset(IngenicEmc *emc, ResetType type);
uint64_t ingenic_emc_sdram_read(IngenicEmc *emc, hwaddr addr, unsigned size);
void ingenic_emc_sdram_write(IngenicEmc *emc, hwaddr addr, uint64_t value, unsigned size);

// EMC main controller

#define TYPE_INGENIC_EMC "ingenic-emc"
OBJECT_DECLARE_TYPE(IngenicEmc, IngenicEmcClass, INGENIC_EMC)

typedef struct IngenicEmc {
    SysBusDevice parent_obj;
    MemoryRegion mr;
    MemoryRegion sdram_alias_mr;
    MemoryRegion static_mr[4];

    IngenicEmcSdram *sdram[2];
    IngenicEmcSdramCfg sdram_cfg;
    IngenicEmcNand *nand[4];
    IngenicEmcNandEcc nand_ecc;

    // GPIO
    qemu_irq io_nand_rb;

    // Properties
    uint32_t model;

    // Registers
    uint32_t BCR;
    uint32_t SMCR[4];
    uint16_t SACR[4];
    uint32_t NFCSR;
} IngenicEmc;

typedef struct IngenicEmcClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicEmcClass;

IngenicEmc *ingenic_emc_register_nand(IngenicEmcNand *nand, uint32_t cs);
IngenicEmc *ingenic_emc_register_sdram(IngenicEmcSdram *sdram, uint32_t cs);

#endif /* INGENIC_EMC_H */

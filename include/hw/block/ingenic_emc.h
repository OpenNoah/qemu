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

#define TYPE_INGENIC_EMC "ingenic-emc"
OBJECT_DECLARE_TYPE(IngenicEmc, IngenicEmcClass, INGENIC_EMC)

typedef struct IngenicEmc {
    SysBusDevice parent_obj;

    MemoryRegion emc_mr;
    MemoryRegion origin_mr;
    MemoryRegion sdram_mr;
    MemoryRegion static_mr[4];

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

#endif /* INGENIC_EMC_H */

/*
 * Ingenic BCH hardware ECC emulation
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

#ifndef INGENIC_BCH_H
#define INGENIC_BCH_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/block/block.h"

#define TYPE_INGENIC_BCH "ingenic-bch"
OBJECT_DECLARE_TYPE(IngenicBch, IngenicBchClass, INGENIC_BCH)

typedef struct IngenicBch {
    SysBusDevice parent_obj;

    MemoryRegion mr;

    // States
    uint32_t nbytes;
    uint8_t mask_and;
    uint8_t mask_or;

    // Registers
    uint8_t bhcr;
    uint8_t bhinte;
    uint32_t bhint;
    uint32_t bhcnt;
    uint32_t bhpar[4];
    uint32_t bherr[4];
} IngenicBch;

typedef struct IngenicBchClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicBchClass;

#endif /* INGENIC_BCH_H */

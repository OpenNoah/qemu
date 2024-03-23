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

#ifndef INGENIC_CGU_H
#define INGENIC_CGU_H

#include "hw/sysbus.h"
#include "hw/clock.h"
#include "qom/object.h"

#define TYPE_INGENIC_CGU "ingenic-cgu"
OBJECT_DECLARE_TYPE(IngenicCgu, IngenicCguClass, INGENIC_CGU)

typedef struct IngenicCgu {
    SysBusDevice parent_obj;

    MemoryRegion mr;

    // Registers
    uint32_t CPCCR;
    uint32_t LCR;
    uint32_t RSR;
    uint32_t CPPCR;
    uint32_t CPPSR;
    uint32_t CLKGR;
    uint32_t OPCR;
    uint32_t I2SCDR;
    uint32_t LPCDR;
    uint32_t MSCCDR;
    uint32_t SSICDR;
    uint32_t CIMCDR;

    // Clocks
    uint32_t ext_freq;
    uint32_t rtc_freq;
    Clock *clk_ext;
    Clock *clk_rtc;
    Clock *clk_pll;
    Clock *clk_cclk;
    Clock *clk_mclk;
    Clock *clk_pclk;
} IngenicCgu;

typedef struct IngenicCguClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicCguClass;

IngenicCgu *ingenic_cgu_get_cgu(void);

#endif /* INGENIC_CGU_H */

/*
 * Ingenic JZ4740 Clock Reset and Power Controller emulation
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

#ifndef INGENIC_JZ4740_CGU_H
#define INGENIC_JZ4740_CGU_H

#include "hw/sysbus.h"
#include "hw/clock.h"
#include "qom/object.h"

#define TYPE_INGENIC_JZ4740_CGU "ingenic-jz4740-cgu"
OBJECT_DECLARE_TYPE(IngenicJz4740Cgu, IngenicJz4740CguClass, INGENIC_JZ4740_CGU)

typedef struct IngenicJz4740Cgu {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    uint32_t ext_freq;
    uint32_t rtc_freq;
    Clock *clk_ext;
    Clock *clk_rtc;
    Clock *clk_pll;
    Clock *clk_cclk;
    Clock *clk_mclk;
    Clock *clk_pclk;
    Clock *clk_lcdpix;

    struct {
        uint32_t cpccr;
        uint8_t  lcr;
        //uint32_t rsr;
        uint32_t cppcr;
        uint16_t clkgr;
        uint16_t i2scdr;
        uint32_t lpcdr;
        uint8_t  msccdr;
        uint8_t  uhccdr;
        uint32_t ssicdr;
    } reg;
} IngenicJz4740Cgu;

typedef struct IngenicJz4740CguClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicJz4740CguClass;

IngenicJz4740Cgu *ingenic_jz4740_cgu_get_cgu(void);

#endif /* INGENIC_JZ4740_CGU_H */

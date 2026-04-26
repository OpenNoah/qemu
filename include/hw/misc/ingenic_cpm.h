/*
 * Ingenic JZ47xx Clock Reset and Power Controller emulation
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

#ifndef INGENIC_CPM_H
#define INGENIC_CPM_H

#include "hw/core/sysbus.h"
#include "hw/core/clock.h"
#include "qom/object.h"

#define TYPE_INGENIC_CPM "ingenic-cpm"
OBJECT_DECLARE_TYPE(IngenicCpm, IngenicCpmClass, INGENIC_CPM)

typedef struct IngenicCpm {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    uint32_t model;

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
        uint32_t rsr;
        uint32_t cppcr;
        uint32_t cppsr;
        uint32_t clkgr;
        union {
            uint16_t opcr;
            uint16_t scr;
        };
        uint16_t i2scdr;
        uint32_t lpcdr;
        union {
            uint8_t  msccdr;
            uint8_t  msc0cdr;
        };
        uint8_t  uhccdr;
        uint32_t ssicdr;
        uint8_t  msc1cdr;
        union {
            uint32_t pcmcdr;
            uint32_t cimcdr;
        };
    } reg;
} IngenicCpm;

typedef struct IngenicCpmClass
{
    SysBusDeviceClass parent_class;
} IngenicCpmClass;

IngenicCpm *ingenic_cpm_get_cpm(void);

#endif /* INGENIC_CPM_H */

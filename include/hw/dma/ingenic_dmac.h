/*
 * Ingenic DMA Controller emulation model
 *
 * Copyright (c) 2024 Norman Zhi (normanzyb@gmail.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef INGENIC_DMAC_H
#define INGENIC_DMAC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_INGENIC_DMAC "ingenic-dmac"
OBJECT_DECLARE_TYPE(IngenicDmac, IngenicDmacClass, INGENIC_DMAC)

typedef struct IngenicDmac
{
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mr;

    // Registers
    struct {
        uint32_t dsa;   // Source address
        uint32_t dta;   // Target address
        uint32_t dtc;   // Transfer count
        uint32_t drt;   // Request source
        uint32_t dcs;   // Control & status
        uint32_t dcm;   // Command
        uint32_t dda;   // Descriptor address
        uint32_t dsd;   // Stride address
    } ch[8];
    struct {
        uint32_t dmac;  // Control
        uint32_t dirqp; // Interrupt pending
        uint32_t ddr;   // Doorbell
    } ctrl[2];
} IngenicDmac;

typedef struct IngenicDmacClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicDmacClass;

#endif /* INGENIC_DMAC_H */

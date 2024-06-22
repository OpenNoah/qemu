/*
 * Ingenic (MUSB-like) UDC controller emulation model
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

#ifndef INGENIC_UDC_H
#define INGENIC_UDC_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/usb/hcd-musb.h"

#define INGENIC_UDC_MAX_DMA_CHANNELS    8

#define TYPE_INGENIC_UDC "ingenic-udc"
OBJECT_DECLARE_TYPE(IngenicUdc, IngenicUdcClass, INGENIC_UDC)

typedef struct IngenicUdc
{
    SysBusDevice parent_obj;
    MemoryRegion mr;
    MUSBState *musb;

    // DMA channels
    uint32_t dma_intr;
    struct {
        uint32_t cntl;
        uint32_t addr;
        uint32_t count;
    } dma[INGENIC_UDC_MAX_DMA_CHANNELS];
} IngenicUdc;

typedef struct IngenicUdcClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicUdcClass;

#endif /* INGENIC_UDC_H */

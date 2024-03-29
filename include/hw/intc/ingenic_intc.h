/*
 * Ingenic Interrupt Controller emulation model
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

#ifndef INGENIC_INTC_H
#define INGENIC_INTC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_INGENIC_INTC "ingenic-intc"
OBJECT_DECLARE_TYPE(IngenicIntc, IngenicIntcClass, INGENIC_INTC)

typedef struct IngenicIntc
{
    SysBusDevice parent_obj;
    MemoryRegion mr;

    // Registers
    uint32_t icsr;  // Source
    uint32_t icmr;  // Mask
    uint32_t icpr;  // Pending
} IngenicIntc;

typedef struct IngenicIntcClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicIntcClass;

#endif /* INGENIC_INTC_H */

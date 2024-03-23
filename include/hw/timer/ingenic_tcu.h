/*
 * Ingenic TCU emulation model
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

#ifndef INGENIC_TCU_H
#define INGENIC_TCU_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_INGENIC_TCU "ingenic-tcu"
OBJECT_DECLARE_TYPE(IngenicTcu, IngenicTcuClass, INGENIC_TCU)

typedef struct IngenicTcu
{
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mr;

    struct {
        uint32_t tstr;
        uint32_t tsr;
        uint16_t ter;
        uint32_t tfr;
        uint32_t tmr;
        struct {
            uint16_t tdfr;
            uint16_t tdhr;
            uint16_t tcnt;
            uint16_t tcsr;
        } timer[6];
    } tcu;

    struct {
        uint32_t ostdr;
        uint32_t ostcnt;
        uint16_t ostcsr;
    } ost;

    struct {
        uint16_t tdr;
        uint8_t tcer;
        uint16_t tcnt;
        uint16_t tcsr;
    } wdt;
} IngenicTcu;

typedef struct IngenicTcuClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicTcuClass;

#endif /* INGENIC_TCU_H */

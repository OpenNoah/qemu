/*
 * Ingenic RTC emulation model
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

#ifndef INGENIC_RTC_H
#define INGENIC_RTC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_INGENIC_RTC "ingenic-rtc"
OBJECT_DECLARE_TYPE(IngenicRtc, IngenicRtcClass, INGENIC_RTC)

typedef struct IngenicRtc
{
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mr;

    // Registers
    uint32_t rtcsr;
    uint32_t rtcsar;
    uint32_t rtcgr;
    uint32_t hspr;
    uint16_t hwfcr;
    uint16_t hrcr;
    uint8_t hwcr;
    uint8_t rtccr;
} IngenicRtc;

typedef struct IngenicRtcClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicRtcClass;

#endif /* INGENIC_RTC_H */

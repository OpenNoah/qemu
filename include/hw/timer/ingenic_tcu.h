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
#include "hw/irq.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_INGENIC_TCU "ingenic-tcu"
OBJECT_DECLARE_TYPE(IngenicTcu, IngenicTcuClass, INGENIC_TCU)

typedef struct IngenicTcu IngenicTcu;

typedef struct IngenicTcuTimerCommon
{
    IngenicTcu *tcu;
    QEMUTimer qts;
    int64_t qts_start_ns;
    uint64_t clk_period;
    uint64_t clk_ticks;
    uint32_t top;
    uint32_t comp;
    uint32_t cnt;
    uint32_t irq_top_mask;
    uint32_t irq_comp_mask;
    bool enabled;
} IngenicTcuTimerCommon;

typedef struct IngenicTcuTimer
{
    IngenicTcu *tcu;
    IngenicTcuTimerCommon tmr;

    // Registers
    uint16_t tcsr;
} IngenicTcuTimer;

typedef struct IngenicTcu
{
    SysBusDevice parent_obj;
    MemoryRegion mr;
    qemu_irq irq[3];
    uint32_t irq_state;

    struct {
        uint32_t tstr;
        uint32_t tsr;
        uint16_t ter;
        uint32_t tfr;
        uint32_t tmr;
        IngenicTcuTimer timer[6];
    } tcu;

    struct {
        uint16_t tcsr;
        IngenicTcuTimerCommon tmr;
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

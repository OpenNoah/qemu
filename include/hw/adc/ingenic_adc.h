/*
 * Ingenic ADC emulation model
 *
 * Copyright (c) 2024 Norman Zhi <normanzyb@gmail.com>
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

#ifndef INGENIC_ADC_H
#define INGENIC_ADC_H

#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_INGENIC_ADC "ingenic-adc"
OBJECT_DECLARE_TYPE(IngenicAdc, IngenicAdcClass, INGENIC_ADC)

enum ingenic_adc_sampler {
    IngenicAdcSamplerIdle,
    IngenicAdcSamplerIn,
    IngenicAdcSamplerBat,
    //IngenicAdcSamplerTsX,
    //IngenicAdcSamplerTsY,
    //IngenicAdcSamplerTsZ1,
    //IngenicAdcSamplerTsZ2,
};

typedef struct IngenicAdc
{
    SysBusDevice parent_obj;
    MemoryRegion mr;
    qemu_irq irq;

    QEMUTimer sampler_timer;
    QEMUTimer ts_timer;

    enum ingenic_adc_sampler sampler;
    uint8_t adtch_state;
    uint16_t x, y, z[4];
    uint32_t adtch_fifo;
    uint8_t prev_state;
    bool pressed;

    // Registers
    uint8_t adena;
    uint32_t adcfg;
    uint8_t adctrl;
    uint8_t adstate;
    uint16_t adsame;
    uint16_t adwait;
    uint16_t adbdat;
    uint16_t adsdat;
    uint16_t adflt;
    uint32_t adclk;
} IngenicAdc;

typedef struct IngenicAdcClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicAdcClass;

#endif /* INGENIC_ADC_H */

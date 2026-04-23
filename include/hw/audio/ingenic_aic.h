/*
 * Ingenic AIC emulation model
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

#ifndef INGENIC_AIC_H
#define INGENIC_AIC_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qemu/audio.h"

#define TYPE_INGENIC_AIC "ingenic-aic"
OBJECT_DECLARE_TYPE(IngenicAic, IngenicAicClass, INGENIC_AIC)

typedef struct IngenicAic
{
    SysBusDevice parent_obj;
    MemoryRegion mr;

    AudioBackend *audio_be;
    union {
        SWVoiceIn *in;
        SWVoiceOut *out;
    } voice;

    struct {
        uint16_t aicfr;
        uint32_t aiccr;
        uint16_t i2scr;
        uint32_t aicsr;
        uint8_t  i2sdiv;
        uint32_t cdccr1;
        uint32_t cdccr2;
    } reg;

    int srate;

    struct {
        qemu_irq dma_req;
        int bitw;

        uint32_t fifo_wptr;
        uint32_t fifo_rptr;
        int32_t fifo[2048];
    } in, out;
} IngenicAic;

typedef struct IngenicAicClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicAicClass;

uint32_t ingenic_aic_dma_tx_available(IngenicAic *s);

#endif /* INGENIC_AIC_H */

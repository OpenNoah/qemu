/*
 * Ingenic MMC/SD controller emulation model
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

#ifndef INGENIC_MSC_H
#define INGENIC_MSC_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/sd/sd.h"

#define TYPE_INGENIC_MSC "ingenic-msc"
OBJECT_DECLARE_TYPE(IngenicMsc, IngenicMscClass, INGENIC_MSC)

//#define TYPE_INGENIC_MSC_BUS "ingenic-msc-bus"

typedef struct IngenicMsc
{
    SysBusDevice parent_obj;
    MemoryRegion mr;
    SDBus sdbus;
    qemu_irq irq;
    qemu_irq gpio_cd;

    // Properties
    BlockBackend *blk;
    uint16_t resp[8];
    uint8_t  resp_offset;
    uint8_t  data_fifo[4096];
    uint32_t data_offset;
    uint32_t data_size;

    // Registers
    struct {
        uint32_t stat;      // 0x04
        uint8_t  clkrt;     // 0x08
        uint32_t cmdat;     // 0x0c
        uint16_t blklen;    // 0x18
        uint16_t nob;       // 0x1c
        uint16_t snob;      // 0x20
        uint16_t imask;     // 0x24
        uint16_t ireg;      // 0x28
        uint8_t  cmd;       // 0x2c
        uint32_t arg;       // 0x30
        uint8_t  lpm;       // 0x40
    } reg;
} IngenicMsc;

typedef struct IngenicMscClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicMscClass;

#endif // INGENIC_MSC_H

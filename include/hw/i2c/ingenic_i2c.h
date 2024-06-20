/*
 * Ingenic I2C controller emulation model
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

#ifndef INGENIC_I2C_H
#define INGENIC_I2C_H

#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_INGENIC_I2C "ingenic-i2c"
OBJECT_DECLARE_TYPE(IngenicI2c, IngenicI2cClass, INGENIC_I2C)

typedef enum {
    IngenicI2cIdle = 0,
    IngenicI2cStart,
    IngenicI2cWrite,
    IngenicI2cRead,
    IngenicI2cNak,
} IngenicI2cState;

typedef struct IngenicI2c
{
    SysBusDevice parent_obj;
    MemoryRegion mr;

    I2CBus *bus;

    IngenicI2cState state;
    int delay;

    // Registers
    uint8_t dr;
    uint8_t cr;
    uint8_t sr;
    uint16_t gr;
} IngenicI2c;

typedef struct IngenicI2cClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicI2cClass;

#endif /* INGENIC_I2C_H */

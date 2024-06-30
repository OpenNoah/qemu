/*
 * Ingenic JZ4740 SoC support
 *
 * Emulates a very simple machine model of Ingenic JZ4740 SoC.
 *
 * Copyright (c) 2024 Norman Zhi
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

#ifndef JZ4740_H
#define JZ4740_H

#include "qemu/typedefs.h"
#include "target/mips/cpu.h"
#include "hw/gpio/ingenic_gpio.h"
#include "hw/i2c/i2c.h"

typedef struct IngenicJZ4740 {
    MIPSCPU *cpu;
    IngenicGpio *gpio[6];
    I2CBus *i2c;
} IngenicJZ4740;

IngenicJZ4740 *ingenic_jz4740_init(MachineState *machine);

#endif

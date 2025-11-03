/*
 * Ingenic JZ47xx UART
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

#ifndef INGENIC_UART_H
#define INGENIC_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"
#include "hw/char/serial.h"

#define TYPE_INGENIC_UART "ingenic-uart"
OBJECT_DECLARE_TYPE(IngenicUartState, IngenicUartClass, INGENIC_UART)

struct IngenicUartState {
    SerialMM parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    // Registers
    uint8_t isr;
    uint8_t umr;
    uint16_t uacr;
};

typedef struct IngenicUartClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
    DeviceRealize smm_realize;
} IngenicUartClass;

IngenicUartState *ingenic_uart_init(MemoryRegion *address_space,
                                    hwaddr base,
                                    qemu_irq irq, int baudbase,
                                    Chardev *chr, enum device_endian end);

#endif /* INGENIC_UART_H */

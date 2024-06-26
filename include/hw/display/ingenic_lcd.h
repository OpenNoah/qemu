/*
 * Ingenic LCD Controller emulation model
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

#ifndef INGENIC_LCD_H
#define INGENIC_LCD_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "ui/console.h"

#define TYPE_INGENIC_LCD "ingenic-lcd"
OBJECT_DECLARE_TYPE(IngenicLcd, IngenicLcdClass, INGENIC_LCD)

typedef struct IngenicLcd
{
    SysBusDevice parent_obj;
    MemoryRegion mr;
    MemoryRegionSection fbsection;
    QemuConsole *con;
    qemu_irq irq;

    // Variables
    uint32_t xres;
    uint32_t yres;
    uint32_t mode;
    uint32_t osd_mode[2];
    bool invalidate;

    // Registers
    uint32_t lcdcfg;
    uint32_t lcdvsync;
    uint32_t lcdhsync;
    uint32_t lcdvat;
    uint32_t lcddah;
    uint32_t lcddav;
    uint32_t lcdctrl;
    uint8_t  lcdstate;
    uint16_t lcdrgbc;
    uint16_t lcdosdctrl;
    uint32_t lcdbgc;
    uint8_t  lcdalpha;
    uint32_t lcdipur;
    struct {
        uint32_t lcdkey;
        uint32_t lcdsize;
    } fg[2];
    struct {
        uint32_t lcdda;
        uint32_t lcdsa;
        uint32_t lcdfid;
        uint32_t lcdcmd;
        uint32_t lcdoffs;
        uint32_t lcdpw;
        uint32_t lcdcnum;
        uint32_t lcddessize;
    } desc[2];
} IngenicLcd;

typedef struct IngenicLcdClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicLcdClass;

#endif /* INGENIC_LCD_H */

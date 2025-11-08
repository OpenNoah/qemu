/*
 * Ingenic DMA Controller emulation model
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

#ifndef INGENIC_DMAC_H
#define INGENIC_DMAC_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define INGENIC_DMAC_NUM_DMAC   2
#define INGENIC_DMAC_NUM_CH     6

#define TYPE_INGENIC_DMAC "ingenic-dmac"
OBJECT_DECLARE_TYPE(IngenicDmac, IngenicDmacClass, INGENIC_DMAC)

enum ingenic_dmac_ch_state {
    IngenicDmacChIdle, IngenicDmacChDesc, IngenicDmacChTxfr,
};

typedef struct IngenicMsc IngenicMsc;
typedef struct IngenicAic IngenicAic;

typedef struct IngenicDmac
{
    SysBusDevice parent_obj;
    MemoryRegion mr;
    QEMUBH *trigger_bh;
    qemu_irq irq[INGENIC_DMAC_NUM_DMAC];

    struct {
        struct {
            enum ingenic_dmac_ch_state state;
            uint32_t saddr; // Source address counter
            uint32_t taddr; // Target address counter
        } ch[INGENIC_DMAC_NUM_CH];
    } dma[INGENIC_DMAC_NUM_DMAC];

    IngenicMsc *msc;
    IngenicAic *aic;

    // Properties
    uint32_t model;

    // Registers
    struct {
        struct {
            uint32_t dsa;   // Source address
            uint32_t dta;   // Target address
            uint32_t dtc;   // Transfer count
            uint8_t  drt;   // Request source
            uint32_t dcs;   // Control & status
            uint32_t dcm;   // Command
            uint32_t dda;   // Descriptor address
            uint32_t dsd;   // Stride address
        } ch[INGENIC_DMAC_NUM_CH];
        uint32_t dmac;  // Control
        uint32_t dirqp; // Interrupt pending
        uint8_t  ddr;   // Doorbell
        uint8_t  dcke;  // Clock enable
    } reg[INGENIC_DMAC_NUM_DMAC];
} IngenicDmac;

typedef struct IngenicDmacClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicDmacClass;

typedef enum IngenicDmacReqType {
    INGENIC_DMAC_REQ_NAND    = 1,
    INGENIC_DMAC_REQ_BCH_ENC = 2,
    INGENIC_DMAC_REQ_BCH_DEC = 3,
    INGENIC_DMAC_REQ_AUTO    = 8,
    INGENIC_DMAC_REQ_UART_TX = 20,
    INGENIC_DMAC_REQ_UART_RX = 21,
    INGENIC_DMAC_REQ_SSI_TX  = 22,
    INGENIC_DMAC_REQ_SSI_RX  = 23,
    INGENIC_DMAC_REQ_AIC_TX  = 24,
    INGENIC_DMAC_REQ_AIC_RX  = 25,
    INGENIC_DMAC_REQ_MSC0_TX = 26,
    INGENIC_DMAC_REQ_MSC0_RX = 27,
    INGENIC_DMAC_REQ_TCU     = 28,
    INGENIC_DMAC_REQ_SADC    = 29,
    INGENIC_DMAC_REQ_SLCD    = 30,
} IngenicDmacReqType;

#endif /* INGENIC_DMAC_H */

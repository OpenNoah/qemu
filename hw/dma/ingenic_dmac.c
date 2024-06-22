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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/dma/ingenic_dmac.h"
#include "trace.h"

#define REG_CH_DSA  0x00
#define REG_CH_DTA  0x04
#define REG_CH_DTC  0x08
#define REG_CH_DRT  0x0c
#define REG_CH_DCS  0x10
#define REG_CH_DCM  0x14
#define REG_CH_DDA  0x18
#define REG_CH_DSD  0xc0

#define REG_DMAC    0x00
#define REG_DIRQP   0x04
#define REG_DDR     0x08
#define REG_DDRS    0x0c
#define REG_DCKE    0x10

void qmp_stop(Error **errp);

static void ingenic_dmac_reset(Object *obj, ResetType type)
{
    IngenicDmac *s = INGENIC_DMAC(obj);
    for (int dmac = 0; dmac < INGENIC_DMAC_NUM_DMAC; dmac++) {
        for (int ch = 0; ch < INGENIC_DMAC_NUM_CH; ch++) {
            s->ctrl[dmac].ch[ch].dsa = 0;
            s->ctrl[dmac].ch[ch].dta = 0;
            s->ctrl[dmac].ch[ch].dtc = 0;
            s->ctrl[dmac].ch[ch].drt = 0;
            s->ctrl[dmac].ch[ch].dcs = 0;
            s->ctrl[dmac].ch[ch].dcm = 0;
            s->ctrl[dmac].ch[ch].dda = 0;
            s->ctrl[dmac].ch[ch].dsd = 0;
        }
        s->ctrl[dmac].dmac  = 0;
        s->ctrl[dmac].dirqp = 0;
        s->ctrl[dmac].ddr   = 0;
        s->ctrl[dmac].dcke  = 0;
    }
}

static uint64_t ingenic_dmac_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicDmac *s = INGENIC_DMAC(opaque);
    uint64_t data = 0;
    uint32_t dmac = (addr / 0x0100) % INGENIC_DMAC_NUM_DMAC;
    uint32_t ch = addr % 0x0100;
    ch = ch >= 0xc0 ? (ch - 0xc0) / INGENIC_DMAC_NUM_CH : ch / 0x20;
    switch (addr) {
    case 0x0000 ... 0x02ff:
        if (ch >= INGENIC_DMAC_NUM_CH) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid channel %u.%u\n", __func__, dmac, ch);
            qmp_stop(NULL);
        } else {
            switch (addr & 0x9f) {
            case REG_CH_DSA:
                data = s->ctrl[dmac].ch[ch].dsa;
                break;
            case REG_CH_DTA:
                data = s->ctrl[dmac].ch[ch].dta;
                break;
            case REG_CH_DTC:
                data = s->ctrl[dmac].ch[ch].dtc;
                break;
            case REG_CH_DRT:
                data = s->ctrl[dmac].ch[ch].drt;
                break;
            case REG_CH_DCS:
                data = s->ctrl[dmac].ch[ch].dcs;
                break;
            case REG_CH_DCM:
                data = s->ctrl[dmac].ch[ch].dcm;
                break;
            case REG_CH_DDA:
                data = s->ctrl[dmac].ch[ch].dda;
                break;
            case REG_CH_DSD & 0x9f:
                data = s->ctrl[dmac].ch[ch].dsd;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown CH address " HWADDR_FMT_plx "\n", __func__, addr);
                qmp_stop(NULL);
            }
        }
        break;
    case 0x0300 ... 0x04ff:
        switch (addr & 0xff) {
        case REG_DMAC:
            data = s->ctrl[dmac].dmac;
            break;
        // TODO case REG_DIRQP:
        case REG_DDR:
            data = s->ctrl[dmac].ddr;
            break;
        case REG_DCKE:
            data = s->ctrl[dmac].dcke;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown DMAC address " HWADDR_FMT_plx "\n", __func__, addr);
            qmp_stop(NULL);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    trace_ingenic_dmac_read(addr, data);
    return data;
}

static void ingenic_dmac_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    IngenicDmac *s = INGENIC_DMAC(opaque);
    trace_ingenic_dmac_write(addr, data);
    uint32_t dmac = (addr / 0x0100) % INGENIC_DMAC_NUM_DMAC;
    uint32_t ch = addr % 0x0100;
    ch = ch >= 0xc0 ? (ch - 0xc0) / INGENIC_DMAC_NUM_CH : ch / 0x20;
    switch (addr) {
    case 0x0000 ... 0x02ff:
        if (ch >= INGENIC_DMAC_NUM_CH) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid channel %u.%u\n", __func__, dmac, ch);
            qmp_stop(NULL);
        } else {
            switch (addr & 0x9f) {
            case REG_CH_DSA:
                s->ctrl[dmac].ch[ch].dsa = data;
                break;
            case REG_CH_DTA:
                s->ctrl[dmac].ch[ch].dta = data;
                break;
            case REG_CH_DTC:
                s->ctrl[dmac].ch[ch].dtc = data & 0x00ffffff;
                break;
            case REG_CH_DRT:
                s->ctrl[dmac].ch[ch].drt = data & 0x3f;
                break;
            case REG_CH_DCS:
                s->ctrl[dmac].ch[ch].dcs = data & 0xc0ff00df;
                if ((data & 1) && (s->ctrl[dmac].dmac & 1)) {
                    // Start DMA transfer
                    qemu_log_mask(LOG_UNIMP, "%s: TODO %u.%u START\n", __func__, dmac, ch);
                    qmp_stop(NULL);
                }
                break;
            case REG_CH_DCM:
                s->ctrl[dmac].ch[ch].dcm = data & 0xf2cff73f;
                break;
            case REG_CH_DDA:
                s->ctrl[dmac].ch[ch].dda = data & 0xfffffff0;
                break;
            case REG_CH_DSD & 0x9f:
                s->ctrl[dmac].ch[ch].dsd = data;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown CH address " HWADDR_FMT_plx "\n", __func__, addr);
                qmp_stop(NULL);
            }
        }
        break;
    case 0x0300 ... 0x04ff:
        switch (addr & 0xff) {
        case REG_DMAC:
            s->ctrl[dmac].dmac = data & 0xf800030d;
            if (data & 1) {
                for (int ch = 0; ch < INGENIC_DMAC_NUM_CH; ch++) {
                    if (s->ctrl[dmac].ch[ch].dcs & 1) {
                        // Start DMA transfer
                        qemu_log_mask(LOG_UNIMP, "%s: TODO %u.%u START\n", __func__, dmac, ch);
                        qmp_stop(NULL);
                    }
                }
            }
            break;
#if 0   // TODO
        case REG_DDRS:
            s->ctrl[dmac].ddr |= data & 0x0f;
            break;
#endif
        case REG_DCKE:
            s->ctrl[dmac].dcke = data & 0x0f;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown DMAC address " HWADDR_FMT_plx "\n", __func__, addr);
            qmp_stop(NULL);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps dmac_ops = {
    .read = ingenic_dmac_read,
    .write = ingenic_dmac_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_dmac_init(Object *obj)
{
    IngenicDmac *s = INGENIC_DMAC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &dmac_ops, s, "dmac", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
}

static void ingenic_dmac_finalize(Object *obj)
{
}

static void ingenic_dmac_class_init(ObjectClass *class, void *data)
{
    IngenicDmacClass *bch_class = INGENIC_DMAC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_dmac_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicDmac, ingenic_dmac, INGENIC_DMAC, SYS_BUS_DEVICE)

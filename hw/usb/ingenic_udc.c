/*
 * Ingenic (MUSB-like) UDC controller emulation model
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
#include "hw/usb/ingenic_udc.h"
#include "trace.h"

#define DMA_INTR    0x0200
#define DMA_CH_BASE 0x0200
#define DMA_CH_SIZE 0x0010
#define DMA_CNTL    0x0004
#define DMA_ADDR    0x0008
#define DMA_COUNT   0x000c

void qmp_stop(Error **errp);

static void ingenic_udc_reset(Object *obj, ResetType type)
{
    IngenicUdc *s = INGENIC_UDC(obj);
    musb_reset(s->musb);
    s->dma_intr = 0;
    for (int i = 0; i < INGENIC_UDC_MAX_DMA_CHANNELS; i++) {
        s->dma[i].cntl = 0;
        s->dma[i].addr = 0;
        s->dma[i].count = 0;
    }
}

static void ingenic_udc_irq(void *opaque, int source, int level)
{
    IngenicUdc *s = INGENIC_UDC(opaque);
    (void)s;
    trace_ingenic_udc_irq(source, level);
}

static void ingenic_udc_dma_ch_write(IngenicUdc *s, uint32_t ch, uint32_t reg, uint32_t value)
{
    switch (reg) {
    case DMA_CNTL:
        s->dma[ch].cntl = value & 0x07ff;
        if (s->dma[ch].cntl & 1) {
            qemu_log_mask(LOG_UNIMP, "%s: TODO CH%u START\n", __func__, ch);
            qmp_stop(NULL);
        }
        break;
    case DMA_ADDR:
        s->dma[ch].addr = value;
        break;
    case DMA_COUNT:
        s->dma[ch].count = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: CH%u Unknown reg 0x%x 0x%0x\n",
                      __func__, ch, reg, value);
        qmp_stop(NULL);
    }
}

static uint64_t ingenic_udc_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicUdc *s = INGENIC_UDC(opaque);
    uint64_t value = 0;
    switch (addr) {
    case 0 ... 0x1ff:
        switch (size) {
        case 1:
            value = musb_read[0](s->musb, addr);
            break;
        case 2:
            value = musb_read[1](s->musb, addr);
            break;
        case 4:
            value = musb_read[2](s->musb, addr);
            break;
        default:
            g_assert_not_reached();
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    trace_ingenic_udc_read(addr, value);
    return value;
}

static void ingenic_udc_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IngenicUdc *s = INGENIC_UDC(opaque);
    trace_ingenic_udc_write(addr, value);
    switch (addr) {
    case 0 ... 0x1ff:
        switch (size) {
        case 1:
            musb_write[0](s->musb, addr, value);
            break;
        case 2:
            musb_write[1](s->musb, addr, value);
            break;
        case 4:
            musb_write[2](s->musb, addr, value);
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case (DMA_CH_BASE + 4) ... (DMA_CH_BASE + INGENIC_UDC_MAX_DMA_CHANNELS * (DMA_CH_SIZE) - 1):
        ingenic_udc_dma_ch_write(s, (addr - DMA_CH_BASE) / INGENIC_UDC_MAX_DMA_CHANNELS,
                                 (addr - DMA_CH_BASE) % INGENIC_UDC_MAX_DMA_CHANNELS, value);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, value);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps udc_ops = {
    .read = ingenic_udc_read,
    .write = ingenic_udc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_udc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    IngenicUdc *s = INGENIC_UDC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &udc_ops, s, "udc", 0x00010000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    qdev_init_gpio_in(dev, ingenic_udc_irq, musb_irq_max);
    s->musb = musb_init(dev, 0);
}

static void ingenic_udc_finalize(Object *obj)
{
}

static void ingenic_udc_class_init(ObjectClass *class, void *data)
{
    IngenicUdcClass *bch_class = INGENIC_UDC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_udc_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicUdc, ingenic_udc, INGENIC_UDC, SYS_BUS_DEVICE)

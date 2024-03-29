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

void qmp_stop(Error **errp);

static void ingenic_dmac_reset(Object *obj, ResetType type)
{
    qemu_log("%s enter\n", __func__);
    IngenicDmac *s = INGENIC_DMAC(obj);
    // Initial values
    (void)s;
}

static uint64_t ingenic_dmac_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicDmac *s = INGENIC_DMAC(opaque);
    uint64_t data = 0;
    uint32_t idx = 0;
    switch (addr) {
    case 0x0000 ... 0x02ff:
        idx = (addr / 0x0100) * 4;
        if ((addr & 0xff) >= 0xc0)
            idx += ((addr & 0xff) - 0xc0) / 4;
        else
            idx += (addr & 0xff) / 0x20;

        if (idx >= 8) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid channel %"PRIu32"\n", __func__, idx);
            qmp_stop(NULL);
        } else {
            switch (addr & 0x9f) {
            case 0x14:
                data = s->ch[idx].dcm;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
                qmp_stop(NULL);
            }
        }
        break;
    case 0x0300 ... 0x04ff:
        idx = (addr - 0x0300) / 0x0100;
        switch (addr & 0xff) {
        case 0x00:
            data = s->ctrl[idx].dmac;
            break;
        case 0x08:
            data = s->ctrl[idx].ddr;
            break;
        case 0x10:
            data = s->ctrl[idx].dcke;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
            qmp_stop(NULL);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);
    return data;
}

static void ingenic_dmac_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);

    IngenicDmac *s = INGENIC_DMAC(opaque);
    uint32_t idx = 0;
    switch (addr) {
    case 0x0000 ... 0x02ff:
        idx = (addr / 0x0100) * 4;
        if ((addr & 0xff) >= 0xc0)
            idx += ((addr & 0xff) - 0xc0) / 4;
        else
            idx += (addr & 0xff) / 0x20;

        if (idx >= 8) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid channel %"PRIu32"\n", __func__, idx);
            qmp_stop(NULL);
        } else {
            switch (addr & 0x9f) {
            case 0x14:
                s->ch[idx].dcm = data & 0xf2cff73f;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
                qmp_stop(NULL);
            }
        }
        break;
    case 0x0300 ... 0x04ff:
        idx = (addr - 0x0300) / 0x0100;
        switch (addr & 0xff) {
        case 0x00:
            s->ctrl[idx].dmac = data & 0xf800030d;
            break;
        case 0x10:
            s->ctrl[idx].dcke = data & 0x0f;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
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

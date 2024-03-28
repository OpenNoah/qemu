/*
 * Ingenic BCH hardware ECC emulation
 *
 * Copyright (c) 2024
 * Written by Norman Zhi <normanzyb@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"

#include "hw/qdev-properties.h"
#include "hw/block/ingenic_bch.h"

void qmp_stop(Error **errp);

static void ingenic_bch_reset(Object *obj, ResetType type)
{
    IngenicBch *s = INGENIC_BCH(obj);

    // Initial values
    s->bhcr = 0;
    s->bhcnt = 0;
    s->bhint = 0;
    s->bhinte = 0;
    s->nbytes = 0;
}

static uint64_t ingenic_bch_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicBch *s = opaque;
    uint64_t data = 0;
    switch (addr) {
    case 0x00:
        data = s->bhcr;
        break;
    case 0x0c:
        data = s->bhcnt;
        break;
    case 0x24:
        //qemu_log("BCH read int 0x%"PRIx32" 0x%"PRIx32"\n", s->bhcnt, s->nbytes);
        data = s->bhint;
        break;
    case 0x38:
        data = s->bhinte;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "BCH read unknown address " HWADDR_FMT_plx "\n", addr);
        qmp_stop(NULL);
        break;
    }

    //qemu_log("BCH read @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", addr, (uint32_t)size, data);
    return data;
}

static void ingenic_bch_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    //qemu_log("BCH write @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", addr, (uint32_t)size, data);

    IngenicBch *s = opaque;
    switch (addr) {
    case 0x04:
        s->bhcr |= data & 0x1d;
        if (data & BIT(1)) {
            // Reset
            s->nbytes   = 0;
            s->mask_and = 0xff;
            s->mask_or  = 0;
            s->bhint    = 0;
        }
        break;
    case 0x08:
        s->bhcr &= ~data & 0x1f;
        break;
    case 0x0c:
        s->bhcnt = data & 0x03ff03ff;
        break;
    case 0x10:
        // Data register
        // Only update interrupts, ECC algorithm isn't implemented yet
        s->mask_and &= data;
        s->mask_or  |= data;
        s->nbytes   += 1;
        if (s->nbytes == ((s->bhcr & BIT(3)) ?
            /* Encoding */ s->bhcnt & 0xffff :
            /* Decoding */ s->bhcnt >> 16)) {
            // ECC done, update interrupts
            if (s->mask_and == s->mask_or && s->mask_or == 0)
                s->bhint |= BIT(5);     // All 0x00
            if (s->mask_and == s->mask_or && s->mask_or == 0xff)
                s->bhint |= BIT(4);     // All 0xff
            if (s->bhcr & BIT(3))
                s->bhint |= BIT(2);     // Encoding finished
            else
                s->bhint |= BIT(3);     // Decoding finished
        }
        break;
    case 0x24:
        s->bhint &= ~data & 0x3f;
        break;
    case 0x3c:
        s->bhinte |= data & 0x3f;
        break;
    case 0x40:
        s->bhinte &= ~data & 0x3f;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "BHC write unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
        qmp_stop(NULL);
        break;
    }
}

static MemoryRegionOps bch_ops = {
    .read = ingenic_bch_read,
    .write = ingenic_bch_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_bch_init(Object *obj)
{
    qemu_log("%s enter\n", __func__);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IngenicBch *bch = INGENIC_BCH(obj);

    memory_region_init_io(&bch->mr, OBJECT(bch), &bch_ops, bch, "bch", 0x100);
    sysbus_init_mmio(sbd, &bch->mr);
}

static void ingenic_bch_finalize(Object *obj)
{
    qemu_log("%s enter\n", __func__);
}

static void ingenic_bch_class_init(ObjectClass *class, void *data)
{
    IngenicBchClass *bch_class = INGENIC_BCH_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_bch_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicBch, ingenic_bch, INGENIC_BCH, SYS_BUS_DEVICE)

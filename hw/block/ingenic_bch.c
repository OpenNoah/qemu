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
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/block/ingenic_bch.h"
#include "trace.h"

#define REG_BHCR    0x00
#define REG_BHCSR   0x04
#define REG_BHCCR   0x08
#define REG_BHCNT   0x0c
#define REG_BHDR    0x10
#define REG_BHPAR0  0x14
#define REG_BHPAR1  0x18
#define REG_BHPAR2  0x1c
#define REG_BHPAR3  0x20
#define REG_BHINT   0x24
#define REG_BHERR0  0x28
#define REG_BHERR1  0x2c
#define REG_BHERR2  0x30
#define REG_BHERR3  0x34
#define REG_BHINTE  0x38
#define REG_BHINTES 0x3c
#define REG_BHINTEC 0x40

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
    case REG_BHCR:
        data = s->bhcr;
        break;
    case REG_BHCNT:
        data = s->bhcnt;
        break;
    case REG_BHINT:
        //qemu_log("BCH read int 0x%"PRIx32" 0x%"PRIx32"\n", s->bhcnt, s->nbytes);
        data = s->bhint;
        break;
    case REG_BHERR0 ... REG_BHERR3:
        // TODO
        data = 0;
        break;
    case REG_BHINTE:
        data = s->bhinte;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "BCH read unknown address " HWADDR_FMT_plx "\n", addr);
        qmp_stop(NULL);
        break;
    }
    trace_ingenic_bch_read(addr, data);
    return data;
}

static void ingenic_bch_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    IngenicBch *s = opaque;
    if (addr == REG_BHDR)
        trace_ingenic_bch_write_data(addr, data);
    else
        trace_ingenic_bch_write(addr, data);
    switch (addr) {
    case REG_BHCSR:
        s->bhcr |= data & 0x1d;
        if (data & BIT(1)) {
            // Reset
            s->nbytes   = 0;
            s->mask_and = 0xff;
            s->mask_or  = 0;
            s->bhint    = 0;
        }
        break;
    case REG_BHCCR:
        s->bhcr &= ~data & 0x1f;
        break;
    case REG_BHCNT:
        s->bhcnt = data & 0x03ff03ff;
        break;
    case REG_BHDR:
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
    case REG_BHINT:
        s->bhint &= ~data & 0x3f;
        break;
    case REG_BHINTES:
        s->bhinte |= data & 0x3f;
        if (data) {
            qemu_log_mask(LOG_UNIMP, "%s: TODO: Interrupts 0x%"PRIx64"\n", __func__, data);
            qmp_stop(NULL);
        }
        break;
    case REG_BHINTEC:
        s->bhinte &= ~data & 0x3f;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
            __func__, addr, data);
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
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IngenicBch *bch = INGENIC_BCH(obj);
    memory_region_init_io(&bch->mr, OBJECT(bch), &bch_ops, bch, "bch", 0x100);
    sysbus_init_mmio(sbd, &bch->mr);
}

static void ingenic_bch_finalize(Object *obj)
{
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

/*
 * Ingenic Interrupt Controller emulation model
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
#include "qemu/timer.h"
#include "hw/intc/ingenic_intc.h"
#include "trace.h"

#define REG_ICSR    0x00
#define REG_ICMR    0x04
#define REG_ICMSR   0x08
#define REG_ICMCR   0x0c
#define REG_ICPR    0x10

void qmp_stop(Error **errp);

static void intc_update(IngenicIntc *s)
{
    uint32_t diff = s->icpr;
    s->icpr = s->icsr & ~s->icmr;
    diff ^= s->icpr;
    if (diff) {
        qemu_set_irq(s->irq, !!s->icpr);
        trace_ingenic_intc_update(s->icsr, s->icpr);
#if 0
        int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        qemu_log("%s: time %.6f pending 0x%"PRIx32" unmasked 0x%"PRIx32"\n",
                 __func__, (double)now_ns / 1000000000., s->icsr, s->icpr);
#endif
    }
}

static void ingenic_intc_reset(Object *obj, ResetType type)
{
    IngenicIntc *s = INGENIC_INTC(obj);
    // Initial values
    s->icsr = 0;
    s->icmr = 0xffffffff;
    s->icpr = 0;
}

static uint64_t ingenic_intc_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicIntc *s = INGENIC_INTC(opaque);
    uint64_t data = 0;
    switch (addr) {
    case REG_ICSR:
        data = s->icsr;
        break;
    case REG_ICMR:
        data = s->icmr;
        break;
    case REG_ICPR:
        data = s->icpr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    trace_ingenic_intc_read(addr, data);
    return data;
}

static void ingenic_intc_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    trace_ingenic_intc_write(addr, data);
    IngenicIntc *s = INGENIC_INTC(opaque);
    uint32_t icmr = s->icmr;
    switch (addr) {
    case REG_ICMSR:
        s->icmr |= data;
        if (~icmr & data)
            trace_ingenic_intc_enable(s->icmr);
        intc_update(s);
        break;
    case REG_ICMCR:
        s->icmr &= ~data;
        if (icmr & data)
            trace_ingenic_intc_enable(s->icmr);
        intc_update(s);
        break;
    case REG_ICPR:
        // Datasheet says ICPR is read-only
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static void intc_irq(void *opaque, int n, int level)
{
    IngenicIntc *s = INGENIC_INTC(opaque);
    if (level)
        s->icsr |= 1 << n;
    else
        s->icsr &= ~(1 << n);
    intc_update(s);
}

static MemoryRegionOps intc_ops = {
    .read = ingenic_intc_read,
    .write = ingenic_intc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

OBJECT_DEFINE_TYPE(IngenicIntc, ingenic_intc, INGENIC_INTC, SYS_BUS_DEVICE)

static void ingenic_intc_init(Object *obj)
{
    IngenicIntc *s = INGENIC_INTC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &intc_ops, s, "intc", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    qdev_init_gpio_in_named_with_opaque(DEVICE(obj), &intc_irq, s, "irq-in", 32);
    qdev_init_gpio_out_named(DEVICE(obj), &s->irq, "irq-out", 1);
}

static void ingenic_intc_finalize(Object *obj)
{
}

static void ingenic_intc_class_init(ObjectClass *class, void *data)
{
    IngenicIntcClass *bch_class = INGENIC_INTC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_intc_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

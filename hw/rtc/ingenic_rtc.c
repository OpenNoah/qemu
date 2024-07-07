/*
 * Ingenic RTC emulation model
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
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/rtc/ingenic_rtc.h"
#include "trace.h"

void qmp_stop(Error **errp);

static void ingenic_rtc_reset(Object *obj, ResetType type)
{
    IngenicRtc *s = INGENIC_RTC(obj);
    s->rtccr = 0x81;
}

static uint64_t ingenic_rtc_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicRtc *s = INGENIC_RTC(opaque);
    uint64_t data = 0;
    switch (addr) {
    case 0x00:
        data = s->rtccr;
        break;
    case 0x04:
        data = s->rtcsr;
        data = get_clock_realtime() / 1000000000;
        break;
    case 0x08:
        data = s->rtcsar;
        break;
    case 0x0c:
        data = s->rtcgr;
        break;
    case 0x20:
        data = 0;
        break;
    case 0x24:
        data = s->hwfcr;
        break;
    case 0x28:
        data = s->hrcr;
        break;
    case 0x2c:
        data = s->hwcr;
        break;
    case 0x30:
        data = 0;
        break;
    case 0x34:
        data = s->hspr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    trace_ingenic_rtc_read(addr, data);
    return data;
}

static void ingenic_rtc_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    IngenicRtc *s = INGENIC_RTC(opaque);
    trace_ingenic_rtc_write(addr, data);
    switch (addr) {
    case 0x00:
        s->rtccr = (data & 0x2f) | BIT(7);
        break;
    case 0x04:
        s->rtcsr = data;
        break;
    case 0x08:
        s->rtcsar = data;
        break;
    case 0x0c:
        if ((s->rtcgr & BIT(31)) == 0)
            s->rtcgr = data & 0x83ffffff;
        break;
    case 0x24:
        s->hwfcr = data & 0xffe0;
        break;
    case 0x28:
        s->hrcr = data & 0x0fe0;
        break;
    case 0x2c:
        s->hwcr = data & 0x01;
        break;
    case 0x34:
        s->hspr = data;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps rtc_ops = {
    .read = ingenic_rtc_read,
    .write = ingenic_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

OBJECT_DEFINE_TYPE(IngenicRtc, ingenic_rtc, INGENIC_RTC, SYS_BUS_DEVICE)

static void ingenic_rtc_init(Object *obj)
{
    IngenicRtc *s = INGENIC_RTC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &rtc_ops, s, "rtc", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
}

static void ingenic_rtc_finalize(Object *obj)
{
}

static void ingenic_rtc_class_init(ObjectClass *class, void *data)
{
    IngenicRtcClass *bch_class = INGENIC_RTC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_rtc_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

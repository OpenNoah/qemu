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

#define REG_RTCCR   0x00
#define REG_RTCSR   0x04
#define REG_RTCSAR  0x08
#define REG_RTCGR   0x0c

#define REG_HCR     0x20
#define REG_HWFCR   0x24
#define REG_HRCR    0x28
#define REG_HWCR    0x2c
#define REG_HWRSR   0x30
#define REG_HSPR    0x34

void qmp_stop(Error **errp);

static void ingenic_rtc_reset(Object *obj, ResetType type)
{
    IngenicRtc *s = INGENIC_RTC(obj);
    s->rtcsr = 0;
    s->rtccr = 0x81;
}

static uint64_t ingenic_rtc_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicRtc *s = INGENIC_RTC(opaque);
    uint64_t data = 0;
    switch (addr) {
    case REG_RTCCR:
        data = s->rtccr;
        break;
    case REG_RTCSR:
        data = (uint32_t)(get_clock_realtime() / 1000000000 - s->rtcsr);
        break;
    case REG_RTCSAR:
        data = s->rtcsar;
        break;
    case REG_RTCGR:
        data = s->rtcgr;
        break;
    case REG_HCR:
        data = 0;
        break;
    case REG_HWFCR:
        data = s->hwfcr;
        break;
    case REG_HRCR:
        data = s->hrcr;
        break;
    case REG_HWCR:
        data = s->hwcr;
        break;
    case REG_HWRSR:
        data = 0;
        break;
    case REG_HSPR:
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
    case REG_RTCCR:
        s->rtccr = (data & 0x2f) | BIT(7);
        break;
    case REG_RTCSR:
        s->rtcsr = get_clock_realtime() / 1000000000 - data;
        break;
    case REG_RTCSAR:
        s->rtcsar = data;
        break;
    case REG_RTCGR:
        if ((s->rtcgr & BIT(31)) == 0)
            s->rtcgr = data & 0x83ffffff;
        break;
    case REG_HCR:
        if (data & 1)
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Guest requested power-down\n", __func__);
        break;
    case REG_HWFCR:
        s->hwfcr = data & 0xffe0;
        break;
    case REG_HRCR:
        s->hrcr = data & 0x0fe0;
        break;
    case REG_HWCR:
        s->hwcr = data & 0x01;
        break;
    case REG_HWRSR:
        break;
    case REG_HSPR:
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

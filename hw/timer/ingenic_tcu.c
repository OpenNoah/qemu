/*
 * Ingenic TCU emulation model
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
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/timer/ingenic_tcu.h"
#include "hw/misc/ingenic_cgu.h"

void qmp_stop(Error **errp);

static uint64_t ingenic_tcu_timer_read(IngenicTcuTimer *timer, hwaddr addr, unsigned size)
{
    uint64_t data = 0;
    switch (addr % 0x10) {
    case 0x00:
        data = timer->tdfr;
        break;
    case 0x04:
        data = timer->tdhr;
        break;
    case 0x08:
        data = timer->tcnt;
        break;
    case 0x0c:
        data = timer->tcsr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    qmp_stop(NULL);
    return data;
}

static void ingenic_tcu_timer_write(IngenicTcuTimer *timer, hwaddr addr, uint64_t data, unsigned size)
{
    switch (addr % 0x10) {
    case 0x00:
        timer->tdfr = data;
        break;
    case 0x04:
        timer->tdhr = data;
        break;
    case 0x08:
        timer->tcnt = data;
        break;
    case 0x0c: {
            bool freq_change = ((timer->tcsr ^ data) & 0x3f) != 0;
            timer->tcsr = data & 0x03bf;
            if (freq_change) {
                // Configure timer frequency
                static const uint32_t clkdiv_map[] = {1, 4, 16, 64, 256, 1024, 0, 0};
                uint32_t clkdiv = clkdiv_map[(timer->tcsr >> 3) & 7];
                IngenicCgu *cgu = ingenic_cgu_get_cgu();
                Clock *clock = NULL;
                if (timer->tcsr & BIT(2))
                    clock = qdev_get_clock_out(DEVICE(cgu), "clk_ext");
                else if (timer->tcsr & BIT(1))
                    clock = qdev_get_clock_out(DEVICE(cgu), "clk_rtc");
                else if (timer->tcsr & BIT(0))
                    clock = qdev_get_clock_out(DEVICE(cgu), "clk_pclk");
                if (clkdiv != 0 && clock != NULL) {
                    ptimer_transaction_begin(timer->ptimer);
                    ptimer_set_period_from_clock(timer->ptimer, clock, clkdiv);
                    ptimer_transaction_commit(timer->ptimer);
                    qemu_log("%s: timer freq %"PRIu32"\n", __func__, (uint32_t)(clock_get_hz(clock) / clkdiv));
                }
            }
            if (timer->tcu2 && (data & BIT(10))) {
                qemu_log("%s: TODO Clear counter to 0\n", __func__);
                timer->tcnt = 0;
            }
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                    __func__, addr, data);
        qmp_stop(NULL);
    }
}

static void ingenic_tcu_timer_cb(void *opaque)
{
    IngenicTcuTimer *timer = opaque;
    (void)timer;
}

static void ingenic_tcu_reset(Object *obj, ResetType type)
{
    qemu_log("%s enter\n", __func__);
    IngenicTcu *s = INGENIC_TCU(obj);
    // Initial values
    (void)s;
}

static uint64_t ingenic_tcu_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicTcu *s = INGENIC_TCU(opaque);
    uint64_t data = 0;
    if (addr >= 0x40 && addr < 0x100) {
        uint32_t timer = (addr - 0x40) / 0x10;
        data = ingenic_tcu_timer_read(&s->tcu.timer[timer], addr, size);
    } else {
        switch (addr) {
        case 0x10:
            data = s->tcu.ter;
            break;
        case 0x1c:
            data = s->tcu.tsr;
            break;
        case 0x20:
            data = s->tcu.tfr;
            break;
        case 0x30:
            data = s->tcu.tmr;
            break;
        case 0xf0:
            data = s->tcu.tstr;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
            qmp_stop(NULL);
        }
    }

    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);
    return data;
}

static void ingenic_tcu_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);

    IngenicTcu *s = INGENIC_TCU(opaque);
    if (addr >= 0x40 && addr < 0x100) {
        uint32_t timer = (addr - 0x40) / 0x10;
        ingenic_tcu_timer_write(&s->tcu.timer[timer], addr, data, size);
    } else {
        switch (addr) {
        case 0x14:
            s->tcu.ter |=  data & 0x803f;
            qemu_log("%s: timer enables 0x%"PRIx16"\n", __func__, s->tcu.ter);
            break;
        case 0x18:
            s->tcu.ter &= ~data & 0x803f;
            qemu_log("%s: timer enables 0x%"PRIx16"\n", __func__, s->tcu.ter);
            break;
        case 0x24:
            s->tcu.tfr |=  data & 0x003f803f;
            break;
        case 0x28:
            s->tcu.tfr &= ~data & 0x003f803f;
            break;
        case 0x2c:
            s->tcu.tsr |=  data & 0x0001803f;
            break;
        case 0x3c:
            s->tcu.tsr &= ~data & 0x0001803f;
            break;
        case 0x34:
            s->tcu.tmr |=  data & 0x003f803f;
            break;
        case 0x38:
            s->tcu.tmr &= ~data & 0x003f803f;
            break;
        case 0xf4:
            s->tcu.tstr |=  data & 0x00060006;
            break;
        case 0xf8:
            s->tcu.tstr &= ~data & 0x00060006;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                        __func__, addr, data);
            qmp_stop(NULL);
        }
    }
}

static MemoryRegionOps tcu_ops = {
    .read = ingenic_tcu_read,
    .write = ingenic_tcu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_tcu_init(Object *obj)
{
    qemu_log("%s: enter\n", __func__);
    IngenicTcu *s = INGENIC_TCU(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &tcu_ops, s, "tcu", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    for (int i = 0; i < 6; i++) {
        s->tcu.timer[i].tcu = s;
        s->tcu.timer[i].ptimer = ptimer_init(&ingenic_tcu_timer_cb, &s->tcu.timer[i],
                                             PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD);
    }
    s->tcu.timer[1].tcu2 = true;
    s->tcu.timer[2].tcu2 = true;
}

static void ingenic_tcu_finalize(Object *obj)
{
    IngenicTcu *s = INGENIC_TCU(obj);
    for (int i = 0; i < 6; i++)
        ptimer_free(s->tcu.timer[i].ptimer);

}

static void ingenic_tcu_class_init(ObjectClass *class, void *data)
{
    IngenicTcuClass *bch_class = INGENIC_TCU_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_tcu_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicTcu, ingenic_tcu, INGENIC_TCU, SYS_BUS_DEVICE)

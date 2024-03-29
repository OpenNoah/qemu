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

static void tmr_enable(IngenicTcuTimerCommon *tmr, bool en);

static void update_irq(IngenicTcu *s)
{
    uint32_t irq = s->tcu.tfr & ~s->tcu.tmr;
    if (irq != s->irq_state) {
        s->irq_state = irq;
        // OST
        qemu_set_irq(s->ost.irq, !!(irq & 0x00008000));
        // TCU1: 0, 3, 4, 5
        qemu_set_irq(s->tcu.irq[0], !!(irq & 0x00390039));
        // TCU2: 1, 2
        qemu_set_irq(s->tcu.irq[1], !!(irq & 0x00060006));
    }
}

static void tmr_update_clk_period(IngenicTcuTimerCommon *tmr, uint32_t tcsr)
{
    // Configure timer frequency
    static const uint32_t clkdiv_map[] = {1, 4, 16, 64, 256, 1024, 0, 0};
    uint32_t clkdiv = clkdiv_map[(tcsr >> 3) & 7];
    IngenicCgu *cgu = ingenic_cgu_get_cgu();
    Clock *clock = NULL;
    if (tcsr & BIT(2))
        clock = qdev_get_clock_out(DEVICE(cgu), "clk_ext");
    else if (tcsr & BIT(1))
        clock = qdev_get_clock_out(DEVICE(cgu), "clk_rtc");
    else if (tcsr & BIT(0))
        clock = qdev_get_clock_out(DEVICE(cgu), "clk_pclk");
    uint64_t clk_period = 0;
    if (clkdiv != 0 && clock != NULL)
        clk_period = clock_get(clock) * clkdiv;

    // Configure timer frequency
    tmr->clk_period = clk_period;
    qemu_log("%s: timer freq %"PRIu32"\n", __func__,
             (uint32_t)CLOCK_PERIOD_TO_HZ(clk_period));
}

static void tmr_schedule(IngenicTcuTimerCommon *tmr)
{
    // timer currently expired, set up for a new period
    uint32_t full = tmr->top;
    // Timer does not tick when FULL == 0
    if (full == 0) {
        tmr_enable(tmr, false);
        return;
    }
    uint32_t half = tmr->comp;
    uint32_t count = tmr->cnt;
    // Find the next event
    uint64_t wrap = count > full ? 0x10000 : full + 1;
    uint32_t next_full = (full + (wrap - count - 1)) % wrap;
    uint32_t next_half = (half + (wrap - count - 1)) % wrap;

    uint32_t delta_ticks = MIN(next_half, next_full) + 1;
    // Convert to timer interval
    // To avoid wrapping, limit maximum delta_ticks to 1 sec
    uint64_t max_ticks = CLOCK_PERIOD_1SEC / tmr->clk_period;
    uint64_t target_ticks = tmr->clk_ticks + MIN(delta_ticks, max_ticks);
    uint64_t target_ns = tmr->qts_start_ns +
                         target_ticks * tmr->clk_period / CLOCK_PERIOD_FROM_NS(1);
    timer_mod_anticipate_ns(&tmr->qts, target_ns);
    //qemu_log("%s: count %"PRIu32" half %"PRIu32" full %"PRIu32"\n", __func__, count, half, full);
    //qemu_log("%s: delta %"PRIu32" target_ns %"PRIu64"\n", __func__, delta_ticks, target_ns);
}

static void tmr_update_cnt(IngenicTcuTimerCommon *tmr)
{
    uint64_t delta_ticks = 0;
    if (tmr->enabled) {
        // Timer is running, update from current time
        int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int64_t delta_ns = now_ns - tmr->qts_start_ns;
        // To avoid wrapping around in calculations, advance starting ns time
        if (delta_ns >= 1000000000) {
            uint64_t inc_ticks = tmr->clk_ticks;
            tmr->clk_ticks = 0;
            qemu_log("%s: wrap_before_ns %"PRIi64, __func__, tmr->qts_start_ns);
            tmr->qts_start_ns += inc_ticks * tmr->clk_period / CLOCK_PERIOD_FROM_NS(1);
            delta_ns = now_ns - tmr->qts_start_ns;
            qemu_log(" wrap_after_ns %"PRIi64" delta_ns %"PRIi64" ticks %"PRIu64"\n", tmr->qts_start_ns, delta_ns, inc_ticks);
        }
        delta_ticks = delta_ns * CLOCK_PERIOD_FROM_NS(1) / tmr->clk_period - tmr->clk_ticks;
        //qemu_log("%s: now_ns %"PRIu64" delta_ns %"PRIu64"\n", __func__, now_ns, delta_ns);
        tmr->clk_ticks += delta_ticks;
    }
    //qemu_log("%s: delta_ticks %"PRIu64"\n", __func__, delta_ticks);

    uint32_t irq_mask = 0;
    for (;;) {
        // HALF match
        if (tmr->comp == tmr->cnt)
            irq_mask |= tmr->irq_comp_mask;
        // FULL match
        if (tmr->top == tmr->cnt)
            irq_mask |= tmr->irq_top_mask;
        // Update clock & timer counter
        if (delta_ticks--)
            tmr->cnt = tmr->top == tmr->cnt ? 0 : tmr->cnt + 1;
        else
            break;
    }

    if (irq_mask) {
        IngenicTcu *s = tmr->tcu;
        s->tcu.tfr |= irq_mask;
        update_irq(s);
    }
}

static void tmr_cb(void *opaque)
{
    //qemu_log("%s: cb %"PRIi64"\n", __func__, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    IngenicTcuTimerCommon *tmr = opaque;
    tmr_update_cnt(tmr);
    tmr_schedule(tmr);
}

static void tmr_enable(IngenicTcuTimerCommon *tmr, bool en)
{
    if (!en) {
        tmr_update_cnt(tmr);
        timer_del(&tmr->qts);
    } else {
        tmr->qts_start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        tmr->clk_ticks = 0;
        //qemu_log("%s: start_ns %"PRIi64"\n", __func__, tmr->qts_start_ns);
        tmr_schedule(tmr);
        tmr_update_cnt(tmr);
    }
    tmr->enabled = en;
}

static uint64_t ingenic_tcu_timer_read(IngenicTcuTimer *timer, hwaddr addr, unsigned size)
{
    uint64_t data = 0;
    switch (addr % 0x10) {
    case 0x00:
        data = timer->tmr.top;
        break;
    case 0x04:
        data = timer->tmr.comp;
        break;
    case 0x08:
        tmr_update_cnt(&timer->tmr);
        data = timer->tmr.cnt;
        break;
    case 0x0c:
        data = timer->tcsr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    return data;
}

static void ingenic_tcu_timer_write(IngenicTcuTimer *timer, hwaddr addr, uint64_t data, unsigned size)
{
    uint32_t diff = 0;
    switch (addr % 0x10) {
    case 0x00:
        timer->tmr.top = data & 0xff;
        break;
    case 0x04:
        timer->tmr.comp = data;
        break;
    case 0x08:
        timer->tmr.cnt = data;
        break;
    case 0x0c:
        diff = (timer->tcsr ^ data) & 0x3f;
        timer->tcsr = data & 0x03bf;
        // Configure timer frequency
        if (diff)
            tmr_update_clk_period(&timer->tmr, timer->tcsr);
        if (/* TODO timer->tcu2 && */ (data & BIT(10))) {
            qemu_log("%s: TODO Clear counter to 0\n", __func__);
            qmp_stop(NULL);
            timer->tmr.cnt = 0;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
    tmr_update_cnt(&timer->tmr);
}

static void ingenic_tcu_reset(Object *obj, ResetType type)
{
    qemu_log("%s: enter\n", __func__);
    IngenicTcu *s = INGENIC_TCU(obj);
    // Initial values
    (void)s;
}

static uint64_t ingenic_tcu_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicTcu *s = INGENIC_TCU(opaque);
    uint64_t data = 0;
    if (addr >= 0x40 && addr < 0xa0) {
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
        case 0xe0:
            data = s->ost.tmr.comp;
            break;
        case 0xe8:
            tmr_update_cnt(&s->ost.tmr);
            data = s->ost.tmr.cnt;
            break;
        case 0xec:
            data = s->ost.tcsr;
            break;
        case 0xf0:
            data = s->tcu.tstr;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
            qmp_stop(NULL);
        }
        //qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);
    }
    return data;
}

static void ingenic_tcu_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    //qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);

    IngenicTcu *s = INGENIC_TCU(opaque);
    if (addr >= 0x40 && addr < 0xa0) {
        uint32_t timer = (addr - 0x40) / 0x10;
        ingenic_tcu_timer_write(&s->tcu.timer[timer], addr, data, size);
    } else {
        uint32_t diff = 0;
        switch (addr) {
        case 0x14:
        case 0x18:
            diff = s->tcu.ter ^ data;
            if (addr == 0x14)
                s->tcu.ter |=  data & 0x803f;
            else
                s->tcu.ter &= ~data & 0x803f;

            // Update timers
            qemu_log("%s: timer enables 0x%"PRIx16"\n", __func__, s->tcu.ter);
            for (int i = 0; i < 6; i++) {
                if (diff & BIT(i)) {
                    bool en = s->tcu.ter & BIT(i);
                    tmr_enable(&s->tcu.timer[i].tmr, en);
                }
            }
            if (diff & BIT(15)) {
                bool en = s->tcu.ter & BIT(15);
                tmr_enable(&s->ost.tmr, en);
            }
            break;
        case 0x24:
            s->tcu.tfr |=  data & 0x003f803f;
            update_irq(s);
            break;
        case 0x28:
            s->tcu.tfr &= ~data & 0x003f803f;
            update_irq(s);
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
        case 0xe0:
            s->ost.tmr.comp = data;
            if (!(s->ost.tcsr & BIT(15)))
                s->ost.tmr.top = data;
            break;
        case 0xe8:
            s->ost.tmr.cnt = data;
            break;
        case 0xec:
            diff = (s->ost.tcsr ^ data) & 0x3f;
            s->ost.tcsr = data & 0x823f;
            // Configure timer frequency
            if (diff)
                tmr_update_clk_period(&s->ost.tmr, s->ost.tcsr);
            s->ost.tmr.top = s->ost.tcsr & BIT(15) ? 0xffffffff : s->ost.tmr.comp;
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
    IngenicTcu *s = INGENIC_TCU(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &tcu_ops, s, "tcu", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    // General purpose timers
    for (int i = 0; i < 6; i++) {
        s->tcu.timer[i].tmr.tcu = s;
        s->tcu.timer[i].tmr.irq_top_mask  = 0x00000001 << i;
        s->tcu.timer[i].tmr.irq_comp_mask = 0x00010000 << i;
        timer_init_ns(&s->tcu.timer[i].tmr.qts, QEMU_CLOCK_VIRTUAL,
                      &tmr_cb, &s->tcu.timer[i].tmr);
    }

    // Operating system timer
    s->ost.tmr.tcu = s;
    s->ost.tmr.irq_top_mask  = 0x00008000;
    s->ost.tmr.irq_comp_mask = 0x00008000;
    timer_init_ns(&s->ost.tmr.qts, QEMU_CLOCK_VIRTUAL, &tmr_cb, &s->ost.tmr);

    // Interrupts
    qdev_init_gpio_out_named(DEVICE(obj), &s->ost.irq,    "irq-tcu0", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->tcu.irq[0], "irq-tcu1", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->tcu.irq[1], "irq-tcu2", 1);
}

static void ingenic_tcu_finalize(Object *obj)
{
    IngenicTcu *s = INGENIC_TCU(obj);
    for (int i = 0; i < 6; i++)
        timer_del(&s->tcu.timer[i].tmr.qts);
    timer_del(&s->ost.tmr.qts);
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

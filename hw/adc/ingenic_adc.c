/*
 * Ingenic ADC emulation model
 *
 * Copyright (c) 2024 Norman Zhi <normanzyb@gmail.com>
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
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "ui/console.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/adc/ingenic_adc.h"
#include "trace.h"

#define ADC_SAMPLE_RATE_HZ  (180 * 1000)
#define ADC_UPDATE_NS       ((1000 * 1000 * 1000) / ADC_SAMPLE_RATE_HZ)
#define TS_SAMPLE_RATE_HZ   (500)
#define TS_UPDATE_NS        ((1000 * 1000 * 1000) / TS_SAMPLE_RATE_HZ)

#define REG_ADENA   0x00
#define REG_ADCFG   0x04
#define REG_ADCTRL  0x08
#define REG_ADSTATE 0x0c
#define REG_ADSAME  0x10
#define REG_ADWAIT  0x14
#define REG_ADTCH   0x18
#define REG_ADBDAT  0x1c
#define REG_ADSDAT  0x20
#define REG_ADFLT   0x24
#define REG_ADCLK   0x28

void qmp_stop(Error **errp);

static void ingenic_adc_reset(Object *obj, ResetType type)
{
    IngenicAdc *s = INGENIC_ADC(obj);
    timer_del(&s->sampler_timer);
    timer_del(&s->ts_timer);
    s->sampler = IngenicAdcSamplerIdle;
    s->adtch_state = 0;
    s->prev_state = 0;
    s->pressed = 0;
    s->adena = 0;
    s->adcfg = 0;
    s->adctrl = 0;
    s->adstate = 0;
}

static void ingenic_adc_update_irq(IngenicAdc *s)
{
    uint8_t prev_state = s->prev_state;
    uint8_t state = s->adstate & ~s->adctrl;
    if (prev_state == state)
        return;
    s->prev_state = state;
    bool irq = !!state;
    trace_ingenic_adc_irq(irq, state);
    if (!!(prev_state) != irq)
        qemu_set_irq(s->irq, irq);
}

static void ingenic_adc_ts_timer(void *opaque)
{
    IngenicAdc *s = INGENIC_ADC(opaque);
    if (!(s->adena & BIT(2)) || !s->pressed) {
        // Pen up, timer no longer needed
        timer_del(&s->ts_timer);
        return;
    }

    // Set up timer to re-trigger touchscreen data ready interrupt
    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer_mod_anticipate_ns(&s->ts_timer, now_ns + TS_UPDATE_NS);

    s->adstate |= BIT(2);
    ingenic_adc_update_irq(s);
}

static void ingenic_adc_ts_event(void *opaque, int x, int y, int z, int buttons_state)
{
    IngenicAdc *s = INGENIC_ADC(opaque);
    qemu_set_irq(s->debug_irq, !!(buttons_state & 2));
    if (!(s->adena & BIT(2)))
        return;     // Touchscreen disabled

    bool pressed = !!(buttons_state & 1);
    bool update = pressed != s->pressed;
    s->pressed = pressed;
    if (update) {
        s->adstate |= BIT(pressed ? 4 : 3);
        ingenic_adc_update_irq(s);
    }
    if (pressed) {
        int vcc = 32768;
        // Handle screen flip
        y = vcc - y;
        // Calculate X, Y and pressure
        int rplate = 4096;
        int rtouch = 128;
        int rxp = x * rplate / vcc;
        int rxm = rplate - rxp;
        int ryp = y * rplate / vcc;
        int rym = rplate - ryp;
        int max = 4095;
        s->x = rxp * max / rplate;
        s->y = ryp * max / rplate;
        int rz = rxp + rtouch + rym;
        s->z[0] = rxp * max / rz;
        s->z[1] = (rxp + rtouch) * max / rz;
        rz = ryp + rtouch + rxm;
        s->z[2] = ryp * max / rz;
        s->z[3] = (ryp + rtouch) * max / rz;
    }
    if (update)
        ingenic_adc_ts_timer(s);
    if (pressed || update)
        trace_ingenic_adc_ts(pressed, s->x, s->y, s->z[0], s->z[1], s->z[2], s->z[3]);
}

static void ingenic_adc_sampler_enable(IngenicAdc *s)
{
    // Priority is SADCIN > PBAT > ENTR_SLP > TOUCH
    enum ingenic_adc_sampler next = IngenicAdcSamplerIdle;
    if (s->adena & BIT(0))
        next = IngenicAdcSamplerIn;
    else if (s->adena & BIT(1))
        next = IngenicAdcSamplerBat;
    s->sampler = next;
    if (next != IngenicAdcSamplerIdle) {
        int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        timer_mod_anticipate_ns(&s->sampler_timer, now_ns + ADC_UPDATE_NS);
    }
}

static void ingenic_adc_sampler_timer(void *opaque)
{
    IngenicAdc *s = INGENIC_ADC(opaque);
    trace_ingenic_adc_sampler(s->sampler);
    switch (s->sampler) {
    case IngenicAdcSamplerIdle:
        break;
    case IngenicAdcSamplerIn:
        s->adena   &= ~BIT(0);
        s->adstate |=  BIT(0);
        break;
    case IngenicAdcSamplerBat:
        s->adena   &= ~BIT(1);
        s->adstate |=  BIT(1);
        break;
    }
    ingenic_adc_sampler_enable(s);
    ingenic_adc_update_irq(s);
}

static uint64_t ingenic_adc_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicAdc *s = INGENIC_ADC(opaque);
    uint64_t data = 0;
    switch (addr) {
    case REG_ADENA:
        data = s->adena;
        break;
    case REG_ADCFG:
        data = s->adcfg;
        break;
    case REG_ADCTRL:
        data = s->adctrl;
        break;
    case REG_ADSTATE:
        data = s->adstate;
        break;
    case REG_ADSAME:
        data = s->adsame;
        qemu_log_mask(LOG_UNIMP, "%s: TODO ADSAME\n", __func__);
        qmp_stop(NULL);
        break;
    case REG_ADWAIT:
        data = s->adwait;
        qemu_log_mask(LOG_UNIMP, "%s: TODO ADWAIT\n", __func__);
        qmp_stop(NULL);
        break;
    case REG_ADTCH:
        // Touch screen data
        switch ((s->adcfg >> 13) & 3) {
        // X -> Y
        case 0b00:
            data = (s->y << 16) | s->x;
            break;
        // X -> Y -> Z
        case 0b01:
            if (!(s->adtch_state & 1)) {
                data = (s->y << 16) | s->x;
                s->adtch_fifo = s->z[0];
            } else {
                data = s->adtch_fifo;
            }
            s->adtch_state++;
            break;
        // X -> Y -> Z1/3 -> Z2/4
        case 0b10: {
            uint8_t type = s->adtch_state & 2;
            uint32_t mask = type ? 0x80008000 : 0;
            if (!(s->adtch_state & 1)) {
                data = mask | (s->y << 16) | s->x;
                s->adtch_fifo = mask | (s->z[type + 1] << 16) | s->z[type + 0];
            } else {
                data = s->adtch_fifo;
            }
            s->adtch_state++;
            break;
        }
        default:    // Reserved
            data = 0;
        }
        break;
    case REG_ADBDAT:
        // TODO PBAT data
        if (s->adcfg & BIT(4))
            data = 1.8 / 2.5 * 4095.;
        else
            data = 4.0 / 7.5 * 4095.;
        break;
    case REG_ADSDAT:
        // TODO SADCIN data
        data = 3.2 / 3.3 * 4095.;
        break;
    case REG_ADFLT:
        data = s->adflt;
        break;
    case REG_ADCLK:
        data = s->adclk;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    trace_ingenic_adc_read(addr, data);
    return data;
}

static void ingenic_adc_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    trace_ingenic_adc_write(addr, data);
    IngenicAdc *s = INGENIC_ADC(opaque);
    switch (addr) {
    case REG_ADENA:
        s->adena = data & 0x07;
        if (data & (BIT(6) | BIT(5))) {
            qemu_log_mask(LOG_UNIMP, "%s: TODO SLEEP mode 0x%x\n", __func__, (int)data);
            qmp_stop(NULL);
        }
        if (s->sampler == IngenicAdcSamplerIdle)
            ingenic_adc_sampler_enable(s);
        break;
    case REG_ADCFG:
        s->adcfg = data & 0xc007fc10;
        if (data & BIT(15)) {
            qemu_log_mask(LOG_UNIMP, "%s: TODO DMA EN 0x%x\n", __func__, (int)data);
            qmp_stop(NULL);
        }
        break;
    case REG_ADCTRL:
        s->adctrl = data & 0x3f;
        ingenic_adc_update_irq(s);
        break;
    case REG_ADSTATE:
        s->adstate &= ~(data & 0x3f);
        ingenic_adc_update_irq(s);
        break;
    case REG_ADSAME:
        s->adsame = data & 0xffff;
        break;
    case REG_ADWAIT:
        s->adwait = data & 0xffff;
        break;
    case REG_ADTCH:
        // Write clears touch screen data to 0
        s->adtch_state = 0;
        break;
    case REG_ADBDAT:
        // Write clears PBAT data to 0
        s->adbdat = 0;
        break;
    case REG_ADSDAT:
        // Write clears SADCIN data to 0
        s->adsdat = 0;
        break;
    case REG_ADFLT:
        s->adflt = data & 0x8fff;
        break;
    case REG_ADCLK:
        s->adclk = data & 0x007f003f;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps adc_ops = {
    .read = ingenic_adc_read,
    .write = ingenic_adc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_adc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    IngenicAdc *s = INGENIC_ADC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &adc_ops, s, "adc", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    qdev_init_gpio_out_named(dev, &s->irq, "irq-out", 1);
    qdev_init_gpio_out_named(dev, &s->debug_irq, "debug-out", 1);
    timer_init_ns(&s->sampler_timer, QEMU_CLOCK_VIRTUAL, &ingenic_adc_sampler_timer, s);
    timer_init_ns(&s->ts_timer, QEMU_CLOCK_VIRTUAL, &ingenic_adc_ts_timer, s);

    qemu_add_mouse_event_handler(&ingenic_adc_ts_event, s, 1, "Ingenic ADC touchscreen");
}

static void ingenic_adc_finalize(Object *obj)
{
    IngenicAdc *s = INGENIC_ADC(obj);
    timer_del(&s->sampler_timer);
    timer_del(&s->ts_timer);
}

static void ingenic_adc_class_init(ObjectClass *class, void *data)
{
    IngenicAdcClass *bch_class = INGENIC_ADC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_adc_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicAdc, ingenic_adc, INGENIC_ADC, SYS_BUS_DEVICE)

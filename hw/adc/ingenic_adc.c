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
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/adc/ingenic_adc.h"

void qmp_stop(Error **errp);

static void ingenic_adc_reset(Object *obj, ResetType type)
{
    qemu_log("%s enter\n", __func__);
    IngenicAdc *s = INGENIC_ADC(obj);
    // Initial values
    s->adena = 0;
    s->adcfg = 0;
    s->adctrl = 0;
    s->adstate = 0;
}

static uint64_t ingenic_adc_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicAdc *s = INGENIC_ADC(opaque);
    uint64_t data = 0;
    switch (addr) {
    case 0x00:
        data = s->adena;
        break;
    case 0x04:
        data = s->adcfg;
        break;
    case 0x08:
        data = s->adctrl;
        break;
    case 0x0c:
        data = s->adstate;
        break;
    case 0x10:
        data = s->adsame;
        break;
    case 0x14:
        data = s->adwait;
        break;
    case 0x18:
        // TODO touch screen data
        qmp_stop(NULL);
        break;
    case 0x1c:
        // TODO PBAT data
        if (s->adcfg & BIT(4))
            data = 1.8 / 2.5 * 4095.;
        else
            data = 3.6 / 7.5 * 4095.;
        break;
    case 0x20:
        // TODO SADCIN data
        data = 3.3 / 3.3 * 4095.;
        break;
    case 0x24:
        data = s->adflt;
        break;
    case 0x28:
        data = s->adclk;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);
    return data;
}

static void ingenic_adc_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);

    IngenicAdc *s = INGENIC_ADC(opaque);
    switch (addr) {
    case 0x00:
        s->adena = data & 0x04;
        if (s->adena & BIT(2))      // Touch screen enable
            s->adstate |= BIT(2);
        if (data & BIT(1))          // PBAT enable
            s->adstate |= BIT(1);
        if (data & BIT(0))          // SADCIN enable
            s->adstate |= BIT(0);
        break;
    case 0x04:
        s->adcfg = data & 0xc007fc10;
        break;
    case 0x08:
        s->adctrl = data & 0x3f;
        break;
    case 0x0c:
        s->adstate &= ~data & 0x3f;
        if (s->adena & BIT(2))      // Touch screen enable
            s->adstate |= BIT(2);
        break;
    case 0x10:
        s->adsame = data & 0xffff;
        break;
    case 0x14:
        s->adwait = data & 0xffff;
        break;
    case 0x18:
        // Write clears touch screen data to 0
        s->adtch[0] = 0;
        s->adtch[1] = 0;
        break;
    case 0x1c:
        // Write clears PBAT data to 0
        s->adbdat = 0;
        break;
    case 0x20:
        // Write clears SADCIN data to 0
        s->adsdat = 0;
        break;
    case 0x24:
        s->adflt = data & 0x8fff;
        break;
    case 0x28:
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
    qemu_log("%s enter\n", __func__);
    IngenicAdc *s = INGENIC_ADC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &adc_ops, s, "adc", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
}

static void ingenic_adc_finalize(Object *obj)
{
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

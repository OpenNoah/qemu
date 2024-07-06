/*
 * Ingenic AIC emulation model
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
#include "hw/audio/ingenic_aic.h"
#include "trace.h"

#define REG_AICFR   0x00
#define REG_AICCR   0x04
#define REG_ACCR1   0x08
#define REG_ACCR2   0x0c
#define REG_I2SCR   0x10
#define REG_AICSR   0x14
#define REG_ACSR    0x18
#define REG_I2SSR   0x1c
#define REG_ACCAR   0x20
#define REG_ACCDR   0x24
#define REG_ACSAR   0x28
#define REG_ACSDR   0x2c
#define REG_I2SDIV  0x30
#define REG_AICDR   0x34
// JZ4740
#define REG_CDCCR1  0x80
#define REG_CDCCR2  0x84
// JZ4755
#define REG_CKCFG   0xa0
#define REG_RGADW   0xa4
#define REG_RGDATA  0xa8

void qmp_stop(Error **errp);

static void ingenic_aic_reset(Object *obj, ResetType type)
{
    IngenicAic *s = INGENIC_AIC(obj);
    if (type != -1) {
        s->reg.aicfr  = 0x7800;
        s->reg.i2sdiv = 0x03;
    }
    s->reg.aiccr = 0x00240000;
    s->reg.i2scr = 0;
    s->reg.aicsr = BIT(3);
    s->reg.cdccr1 = 0x001b2302;
    s->reg.cdccr2 = 0x00170803;
}

static uint64_t ingenic_aic_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicAic *s = INGENIC_AIC(opaque);
    uint64_t data = 0;
    switch (addr) {
    case REG_AICFR:
        data = s->reg.aicfr;
        break;
    case REG_AICCR:
        data = s->reg.aiccr;
        break;
    case REG_I2SCR:
        data = s->reg.i2scr;
        break;
    case REG_AICSR:
        data = s->reg.aicsr;
        break;
    case REG_I2SDIV:
        data = s->reg.i2sdiv;
        break;
    case REG_CDCCR1:
        data = s->reg.cdccr1;
        break;
    case REG_CDCCR2:
        data = s->reg.cdccr2;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    trace_ingenic_aic_read(addr, data);
    return data;
}

static void ingenic_aic_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    trace_ingenic_aic_write(addr, data);
    IngenicAic *s = INGENIC_AIC(opaque);
    switch (addr) {
    case REG_AICFR:
        if (data & BIT(3))
            ingenic_aic_reset(opaque, -1);
        s->reg.aicfr = data & 0xff77;
        break;
    case REG_AICCR:
        s->reg.aiccr = data & 0x003fce7f;
        if (data & (BIT(8) | BIT(7))) {
            qemu_log_mask(LOG_UNIMP, "%s: TODO\n", __func__);
            qmp_stop(NULL);
        }
        break;
    case REG_I2SCR:
        s->reg.i2scr = data & 0x1011;
        break;
    case REG_I2SDIV:
        s->reg.i2sdiv = data & 0x0f;
        break;
    case REG_CDCCR1:
        s->reg.cdccr1 = data & 0x3f1f7f03;
        break;
    case REG_CDCCR2:
        s->reg.cdccr2 = data & 0x001f0f33;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps adc_ops = {
    .read = ingenic_aic_read,
    .write = ingenic_aic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_aic_init(Object *obj)
{
    IngenicAic *s = INGENIC_AIC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &adc_ops, s, "adc", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
}

static void ingenic_aic_finalize(Object *obj)
{
}

static void ingenic_aic_class_init(ObjectClass *class, void *data)
{
    IngenicAicClass *bch_class = INGENIC_AIC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_aic_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicAic, ingenic_aic, INGENIC_AIC, SYS_BUS_DEVICE)

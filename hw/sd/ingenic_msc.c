/*
 * Ingenic MMC/SD controller emulation model
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
#include "hw/ssi/ingenic_msc.h"
#include "trace.h"

#define REG_CTRL    0x00
#define REG_STAT    0x04
#define REG_CLKRT   0x08
#define REG_CMDAT   0x0c
#define REG_RESTO   0x10
#define REG_RDTO    0x14
#define REG_BLKLEN  0x18
#define REG_NOB     0x1c
#define REG_SNOB    0x20
#define REG_IMASK   0x24
#define REG_IREG    0x28
#define REG_CMD     0x2c
#define REG_ARG     0x30
#define REG_RES     0x34
#define REG_RXFIFO  0x38
#define REG_TXFIFO  0x3c
#define REG_LPM     0x40

void qmp_stop(Error **errp);

static void ingenic_msc_reset(Object *obj, ResetType type)
{
    IngenicMsc *s = INGENIC_MSC(obj);
    // DATA_FIFO_EMPTY
    s->reg.stat  = BIT(6);
    s->reg.clkrt = 0;
    s->reg.cmdat = 0;
    s->reg.imask = 0xffff;
    s->reg.ireg  = 0x2000;
    s->reg.cmd   = 0;
    s->reg.arg   = 0;
    s->reg.lpm   = 0;
}

static uint64_t ingenic_msc_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicMsc *s = INGENIC_MSC(opaque);
    uint64_t data = 0;
    switch (addr) {
    case REG_STAT:
        data = s->reg.stat;
        break;
    case REG_CLKRT:
        data = s->reg.clkrt;
        break;
    case REG_CMDAT:
        data = s->reg.cmdat;
        break;
    case REG_IMASK:
        data = s->reg.imask;
        break;
    case REG_IREG:
        data = s->reg.ireg;
        break;
    case REG_CMD:
        data = s->reg.cmd;
        break;
    case REG_ARG:
        data = s->reg.arg;
        break;
    case REG_LPM:
        data = s->reg.lpm;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    trace_ingenic_msc_read(addr, data);
    return data;
}

static void ingenic_msc_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    trace_ingenic_msc_write(addr, data);
    IngenicMsc *s = INGENIC_MSC(opaque);
    (void)s;
    switch (addr) {
    case REG_CTRL:
        if (data & BIT(3)) {
            data &= ~BIT(3);
            ingenic_msc_reset(opaque, 0);
        }
        if (data & BIT(2)) {
            trace_ingenic_msc_start_op(s->reg.cmd, s->reg.cmdat & 7, s->reg.arg);
            s->reg.stat |= BIT(11) | BIT(1);
            qemu_log_mask(LOG_UNIMP, "%s: TODO START OPERATION\n", __func__);
        }
        if (data & 0xc0f3) {
            qemu_log_mask(LOG_UNIMP, "%s: TODO\n", __func__);
            qmp_stop(NULL);
        }
        break;
    case REG_CLKRT:
        s->reg.clkrt = data & 7;
        break;
    case REG_CMDAT:
        s->reg.cmdat = data & 0x0002f7ff;
        if (data & 0xc0010808) {
            qemu_log_mask(LOG_UNIMP, "%s: TODO\n", __func__);
            qmp_stop(NULL);
        }
        break;
    case REG_IMASK:
        s->reg.imask = data | 0x0018;
        break;
    case REG_IREG:
        s->reg.ireg = s->reg.ireg & ~(data & 7);
        break;
    case REG_CMD:
        s->reg.cmd = data & 0x3f;
        break;
    case REG_ARG:
        s->reg.arg = data;
        break;
    case REG_LPM:
        s->reg.lpm = data & BIT(0);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps msc_ops = {
    .read = ingenic_msc_read,
    .write = ingenic_msc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_msc_init(Object *obj)
{
    IngenicMsc *s = INGENIC_MSC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &msc_ops, s, "msc", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_SD_BUS, DEVICE(s), "sd-bus");
}

static void ingenic_msc_finalize(Object *obj)
{
}

static void ingenic_msc_class_init(ObjectClass *class, void *data)
{
    IngenicMscClass *bch_class = INGENIC_MSC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_msc_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicMsc, ingenic_msc, INGENIC_MSC, SYS_BUS_DEVICE);

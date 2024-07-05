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
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
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

#define TYPE_INGENIC_SDHOST_BUS "ingenic-sdhost-bus"
/* This is reusing the SDBus typedef from SD_BUS */
DECLARE_INSTANCE_CHECKER(SDBus, INGENIC_SDHOST_BUS,
                         TYPE_INGENIC_SDHOST_BUS)

void qmp_stop(Error **errp);

static void ingenic_msc_reset(Object *obj, ResetType type)
{
    IngenicMsc *s = INGENIC_MSC(obj);
    // DATA_FIFO_EMPTY
    s->reg.stat  = BIT(6);
    s->reg.clkrt = 0;
    s->reg.cmdat = 0;
    s->reg.imask = 0xffff;
    s->reg.ireg  = 0; // 0x2000;
    s->reg.cmd   = 0;
    s->reg.arg   = 0;
    s->reg.lpm   = 0;
    qemu_set_irq(s->gpio_cd, sdbus_get_inserted(&s->sdbus));
}

static void ingenic_msc_start(IngenicMsc *s)
{
    // Process command and response
    SDRequest request;
    request.cmd = s->reg.cmd;
    request.arg = s->reg.arg;
    int rtype = s->reg.cmdat & 7;
    trace_ingenic_msc_cmd(request.cmd, rtype, request.arg);

    // Stat register bits that are persistent
    static const uint32_t stat_mask = BIT(8) | BIT(6);
    s->resp_offset = 0;

    uint8_t resp[16];
    int rlen = sdbus_do_command(&s->sdbus, &request, resp);
    switch (rtype) {
    case 0:
        if (rlen != 0)
            goto err;
        s->reg.stat = (s->reg.stat & stat_mask) | BIT(11);
        s->reg.ireg |= BIT(2);
        break;
    case 1:
        if (rlen != 4)
            goto err;
        // RES_FIFO is bit [47:32], bit[31:16], bit[15:8]
        s->resp[0] = (request.cmd << 8) | resp[0];
        s->resp[1] = (    resp[1] << 8) | resp[2];
        s->resp[2] = resp[3];
        s->reg.stat = (s->reg.stat & stat_mask) | BIT(11);
        s->reg.ireg |= BIT(2);
        break;
    case 2:
        if (rlen != 16)
            goto err;
        // response read from MSC_RES is bit [135:8]
        s->resp[0] = (    0x3f << 8) | resp[ 0];
        s->resp[1] = (resp[ 1] << 8) | resp[ 2];
        s->resp[2] = (resp[ 3] << 8) | resp[ 4];
        s->resp[3] = (resp[ 5] << 8) | resp[ 6];
        s->resp[4] = (resp[ 7] << 8) | resp[ 8];
        s->resp[5] = (resp[ 9] << 8) | resp[10];
        s->resp[6] = (resp[11] << 8) | resp[12];
        s->resp[7] = (resp[13] << 8) | resp[14];
        s->reg.stat = (s->reg.stat & stat_mask) | BIT(11);
        s->reg.ireg |= BIT(2);
        break;
    case 3:
        if (rlen != 4)
            goto err;
        // RES_FIFO is bit [47:32], bit[31:16], bit[15:8]
        s->resp[0] = (   0x3f << 8) | resp[0];
        s->resp[1] = (resp[1] << 8) | resp[2];
        s->resp[2] = resp[3];
        s->reg.stat = (s->reg.stat & stat_mask) | BIT(11);
        s->reg.ireg |= BIT(2);
        break;
    case 6:
        if (rlen != 4)
            goto err;
        // RES_FIFO is bit [47:32], bit[31:16], bit[15:8]
        s->resp[0] = (request.cmd << 8) | resp[0];
        s->resp[1] = (    resp[1] << 8) | resp[2];
        s->resp[2] = resp[3];
        s->reg.stat = (s->reg.stat & stat_mask) | BIT(11);
        s->reg.ireg |= BIT(2);
        break;
    case 4:
    case 5:
        qemu_log_mask(LOG_UNIMP, "%s: TODO Unknown response type\n", __func__);
        qmp_stop(NULL);
        goto err;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid response type %u\n", __func__, rtype);
        qmp_stop(NULL);
        goto err;
    }

    // Process data transfer
    if (s->reg.cmdat & BIT(11)) {
        qemu_log_mask(LOG_UNIMP, "%s: TODO\n", __func__);
        qmp_stop(NULL);
    }
    if (s->reg.cmdat & BIT(3)) {
        s->data_offset = 0;
        s->data_size = s->reg.blklen * s->reg.nob;
        if (s->reg.cmdat & BIT(4)) {
            // Write operation
            qemu_log_mask(LOG_UNIMP, "%s: TODO\n", __func__);
            qmp_stop(NULL);
        } else {
            // Read operation
            if (s->data_size != 0) {
                // FIFO not empty
                s->reg.stat &= ~BIT(6);
            }
        }
        s->reg.snob = 0;
    }

    return;

err:
    // No response, timed out
    s->reg.stat = (s->reg.stat & stat_mask) | (BIT(11) | BIT(1));
    s->reg.ireg |= BIT(2);
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
    case REG_RES:
        data = s->resp[s->resp_offset];
        s->resp_offset = (s->resp_offset + 1) % ARRAY_SIZE(s->resp);
        break;
    case REG_RXFIFO:
        if (s->data_offset != s->data_size) {
            if (s->data_offset % ARRAY_SIZE(s->data_fifo) == 0) {
                // Read more data
                uint32_t rlen = MIN(ARRAY_SIZE(s->data_fifo), s->data_size - s->data_offset);
                trace_ingenic_msc_fifo_offset(s->data_offset, rlen);
                sdbus_read_data(&s->sdbus, &s->data_fifo[0], rlen);
            }
        }
        data = ldl_le_p(&s->data_fifo[s->data_offset % ARRAY_SIZE(s->data_fifo)]);
        if (s->data_offset != s->data_size)
            s->data_offset += 4;
        if (s->data_offset == s->data_size) {
            // Read complete
            // FIFO empty, data transfer done
            s->reg.stat |= BIT(12) | BIT(6);
            s->reg.ireg |= BIT(0);
        } else {
            // FIFO not empty
            s->reg.stat &= ~BIT(6);
        }
        s->reg.snob = s->data_offset / s->reg.blklen;
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
    switch (addr) {
    case REG_CTRL:
        if (data & BIT(3)) {
            data &= ~BIT(3);
            ingenic_msc_reset(opaque, 0);
        }
        if ((data & 3) == 0b01) {
            // Stop clock
            s->reg.stat &= ~BIT(8);
        } else if ((data & 3) == 0b10) {
            // Start clock
            s->reg.stat |= BIT(8);
        }
        if (data & 0xc0f0) {
            qemu_log_mask(LOG_UNIMP, "%s: TODO\n", __func__);
            qmp_stop(NULL);
        }
        if (data & BIT(2))
            ingenic_msc_start(s);
        break;
    case REG_CLKRT:
        s->reg.clkrt = data & 7;
        break;
    case REG_CMDAT:
        s->reg.cmdat = data & 0x0003ffff;
        break;
    case REG_BLKLEN:
        s->reg.blklen = data & 0xffff;
        break;
    case REG_NOB:
        s->reg.nob = data & 0xffff;
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
    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_INGENIC_SDHOST_BUS, DEVICE(s), "sd-bus");
    qdev_init_gpio_out_named(DEVICE(obj), &s->irq, "irq-out", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->gpio_cd, "io-cd", 1);
}

static void ingenic_msc_finalize(Object *obj)
{
}

static void ingenic_msc_class_init(ObjectClass *class, void *data)
{
    IngenicMscClass *msc_class = INGENIC_MSC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_msc_reset,
                                       NULL,
                                       NULL,
                                       &msc_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicMsc, ingenic_msc, INGENIC_MSC, SYS_BUS_DEVICE)

static const TypeInfo ingenic_sdhost_types[] = {
    {
        .name           = TYPE_INGENIC_SDHOST_BUS,
        .parent         = TYPE_SD_BUS,
        .instance_size  = sizeof(SDBus),
    },
};

DEFINE_TYPES(ingenic_sdhost_types)

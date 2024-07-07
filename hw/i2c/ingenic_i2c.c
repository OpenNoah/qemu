/*
 * Ingenic I2C controller emulation model
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
#include "hw/i2c/ingenic_i2c.h"
#include "trace.h"

void qmp_stop(Error **errp);

static void ingenic_i2c_read_transfer(IngenicI2c *s)
{
    s->dr = i2c_recv(s->bus);
    s->sr |= BIT(1);
    trace_ingenic_i2c_event("READ", s->dr);
    if (s->cr & BIT(1)) {
        // Send NACK
        trace_ingenic_i2c_event("NAK", 1);
        i2c_nack(s->bus);
        s->state = IngenicI2cNak;
    } else {
        // Send ACK
        trace_ingenic_i2c_event("ACK", 0);
        i2c_ack(s->bus);
    }
}

static void ingenic_i2c_reset(Object *obj, ResetType type)
{
    qemu_log("%s: enter\n", __func__);
    IngenicI2c *s = INGENIC_I2C(obj);
    // Initial values
    s->cr = 0;
    s->sr = BIT(2);
    s->gr = 0;
}

static uint64_t ingenic_i2c_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicI2c *s = INGENIC_I2C(opaque);
    uint64_t data = 0;
    switch (addr) {
    case 0x00:
        data = s->dr;
        break;
    case 0x04:
        data = s->cr;
        break;
    case 0x08:
        if (s->state == IngenicI2cRead) {
            // Ingenic driver code racing workaround
            if (s->delay)
                s->delay--;
            else if (!(s->sr & BIT(1)))
                ingenic_i2c_read_transfer(s);
        }
        data = s->sr;
        break;
    case 0x0c:
        data = s->gr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    trace_ingenic_i2c_reg_read(addr, data);
    return data;
}

static void ingenic_i2c_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    trace_ingenic_i2c_reg_write(addr, data);

    IngenicI2c *s = INGENIC_I2C(opaque);
    switch (addr) {
    case 0x00:
        s->dr = data;
        break;
    case 0x04:
        s->cr = data & 0x13;
        if (data & BIT(3)) {
            switch (s->state) {
            case IngenicI2cIdle:
                // Start transfer
                trace_ingenic_i2c_event("START", 0);
                s->state = IngenicI2cStart;
                break;
            case IngenicI2cWrite:
                // Repeated start transfer
                trace_ingenic_i2c_event("RESTART", 0);
                s->state = IngenicI2cStart;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: START mismatched state %u\n",
                              __func__, s->state);
                qmp_stop(NULL);
            }
        }
        if (data & BIT(2)) {
            // Stop bit
            switch (s->state) {
            case IngenicI2cIdle:    // Repeated STOP ignored
            case IngenicI2cWrite:
            case IngenicI2cNak:
                trace_ingenic_i2c_event("STOP", 0);
                i2c_end_transfer(s->bus);
                s->state = IngenicI2cIdle;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: STOP mismatched state %u\n",
                              __func__, s->state);
                qmp_stop(NULL);
            }
        }
        break;
    case 0x08:
        if (data & BIT(1)) {
            // Send data
            trace_ingenic_i2c_event("WRITE", s->dr);
            switch (s->state) {
            case IngenicI2cStart:
                // First byte: send start + address
                s->state = s->dr & BIT(0) ? IngenicI2cRead : IngenicI2cWrite;
                if (i2c_start_transfer(s->bus, s->dr >> 1, s->dr & BIT(0))) {
                    // Start returned NACK
                    trace_ingenic_i2c_event("NAK", 1);
                    s->sr |= BIT(0);
                    s->state = IngenicI2cNak;
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: I2C NAK from 0x%02x\n",
                                  __func__, s->dr >> 1);
                    qmp_stop(NULL);
                } else {
                    // Start returned ACK
                    trace_ingenic_i2c_event("ACK", 0);
                    s->sr &= ~BIT(0);
                    if (s->state == IngenicI2cRead) {
                        // Ready to read 1 byte
                        // Wait for a couple of reads of SR with DRF == 0
                        // to workaround racing issue in Ingenic driver code
                        // It waits for DRF == 0 after sending the address byte
                        s->delay = 5;
                    }
                }
                break;
            case IngenicI2cWrite:
                // Next data byte
                i2c_send(s->bus, s->dr);
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: DATA mismatched state %u\n",
                              __func__, s->state);
                qmp_stop(NULL);
            }
        } else {
            // Clear DRF
            // When reading I2C, technically it should start reading a new byte here
            // But Ingenic driver code configures NACK status after checking SR
            // So need to workaround racing issue too
            s->sr &= ~BIT(1);
        }
        break;
    case 0x0c:
        s->gr = data & 0xffff;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps i2c_ops = {
    .read = ingenic_i2c_read,
    .write = ingenic_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

OBJECT_DEFINE_TYPE(IngenicI2c, ingenic_i2c, INGENIC_I2C, SYS_BUS_DEVICE)

static void ingenic_i2c_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    IngenicI2c *s = INGENIC_I2C(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &i2c_ops, s, "i2c", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    s->bus = i2c_init_bus(dev, "i2c");
}

static void ingenic_i2c_finalize(Object *obj)
{
}

static void ingenic_i2c_class_init(ObjectClass *class, void *data)
{
    IngenicI2cClass *bch_class = INGENIC_I2C_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_i2c_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

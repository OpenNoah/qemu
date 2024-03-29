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

void qmp_stop(Error **errp);

static void ingenic_i2c_reset(Object *obj, ResetType type)
{
    qemu_log("%s: enter\n", __func__);
    IngenicI2c *s = INGENIC_I2C(obj);
    // Initial values
    s->cr = 0;
    s->sr = 4;
    s->gr = 0;
}

static uint64_t ingenic_i2c_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicI2c *s = INGENIC_I2C(opaque);
    uint64_t data = 0;
    switch (addr) {
#if 0
    case 0x00:
        data = s->dr;
        break;
#endif
    case 0x04:
        data = s->cr;
        break;
    case 0x08:
        data = s->sr;
        break;
    case 0x0c:
        data = s->gr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);
    return data;
}

static void ingenic_i2c_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);

    IngenicI2c *s = INGENIC_I2C(opaque);
    switch (addr) {
    case 0x00:
        s->dr = data;
        break;
    case 0x04:
        s->cr = data & 0x1f;
        break;
    case 0x08:
        if (data & BIT(1)) {
            // TODO Send data
            s->sr = BIT(2) | BIT(0);
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

static void ingenic_i2c_init(Object *obj)
{
    IngenicI2c *s = INGENIC_I2C(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &i2c_ops, s, "i2c", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
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

OBJECT_DEFINE_TYPE(IngenicI2c, ingenic_i2c, INGENIC_I2C, SYS_BUS_DEVICE)

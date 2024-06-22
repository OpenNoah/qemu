/*
 * Ingenic JZ47xx UART
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
#include "qapi/error.h"
#include "hw/char/ingenic_uart.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

void qmp_stop(Error **errp);

static void ingenic_uart_reset(Object *obj, ResetType type)
{
    IngenicUartState *s = INGENIC_UART(obj);

    // Initial values
    s->isr = 0;
    s->umr = 0;
    s->uacr = 0;
}

static uint64_t ingenic_uart_read(void *opaque, hwaddr addr,
                                  unsigned int size)
{
    IngenicUartState *s = INGENIC_UART(opaque);
    uint64_t data = 0;

    switch (addr) {
    case 0x00:
        data = s->isr;
        break;
    case 0x04:
        data = s->umr;
        break;
    case 0x08:
        data = s->uacr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    trace_ingenic_uart_read(addr, data);
    return data;
}

static void ingenic_uart_write(void *opaque, hwaddr addr,
                               uint64_t data, unsigned int size)
{
    IngenicUartState *s = INGENIC_UART(opaque);
    trace_ingenic_uart_write(addr, data);
    switch (addr) {
    case 0x00:
        s->isr = data & 0x1f;
        if (s->isr & 3)
            qemu_log_mask(LOG_GUEST_ERROR, "%s: IrDA decoder not implemented\n", __func__);
        break;
    case 0x04:
        s->umr = data & 0x1f;
        break;
    case 0x08:
        s->uacr = data & 0x0fff;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

// MMIO for extra registers not handled by SerialMM
static const MemoryRegionOps ingenic_uart_ops = {
    .read = ingenic_uart_read,
    .write = ingenic_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

IngenicUartState *ingenic_uart_init(MemoryRegion *address_space,
                                    hwaddr base,
                                    qemu_irq irq, int baudbase,
                                    Chardev *chr, enum device_endian end)
{
    IngenicUartState *s = INGENIC_UART(qdev_new(TYPE_INGENIC_UART));
    SerialMM *smm = SERIAL_MM(s);

    qdev_prop_set_uint8(DEVICE(smm), "regshift", 2);
    qdev_prop_set_uint32(DEVICE(smm), "baudbase", baudbase);
    qdev_prop_set_chr(DEVICE(smm), "chardev", chr);
    qdev_set_legacy_instance_id(DEVICE(smm), base, 2);
    qdev_prop_set_uint8(DEVICE(smm), "endianness", end);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);

    sysbus_connect_irq(SYS_BUS_DEVICE(smm), 0, irq);
    memory_region_add_subregion(address_space, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(s), 0));
    memory_region_add_subregion(address_space, base + 0x20, &s->mmio);

    return s;
}

static void ingenic_uart_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    IngenicUartState *s = INGENIC_UART(dev);
    IngenicUartClass *sc = INGENIC_UART_GET_CLASS(dev);

    sc->smm_realize(dev, errp);

    memory_region_init_io(&s->mmio, obj, &ingenic_uart_ops, s, TYPE_INGENIC_UART, 0x1000 - 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void ingenic_uart_class_init(ObjectClass *class, void *data)
{
    IngenicUartClass *idc = INGENIC_UART_CLASS(class);
    DeviceClass *dc = DEVICE_CLASS(class);

    idc->smm_realize = dc->realize;
    dc->realize = ingenic_uart_realize;

    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_uart_reset,
                                       NULL,
                                       NULL,
                                       &idc->parent_phases);
}

static const TypeInfo ingenic_uart_info = {
    .name          = TYPE_INGENIC_UART,
    .parent        = TYPE_SERIAL_MM,
    .instance_size = sizeof(IngenicUartState),
    .class_size    = sizeof(IngenicUartClass),
    .class_init    = ingenic_uart_class_init,
};

static void ingenic_uart_register_types(void)
{
    type_register_static(&ingenic_uart_info);
}

type_init(ingenic_uart_register_types)

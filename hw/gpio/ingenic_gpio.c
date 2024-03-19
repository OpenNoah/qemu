/*
 * Ingenic GPIO emulation.
 *
 * Copyright (C) 2024 Norman Zhi <normanzyb@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/gpio/ingenic_gpio.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

void qmp_stop(Error **errp);

static void ingenic_gpio_reset(Object *obj, ResetType type)
{
    IngenicGpio *gpio = INGENIC_GPIO(obj);

    // Initial register values
    gpio->pin = 0x00000000;
    gpio->dat = 0x00000000;
    gpio->im  = 0xffffffff;
    gpio->pe  = 0x00000000;
    gpio->fun = 0x00000000;
    gpio->sel = 0x00000000;
    gpio->dir = 0x00000000;
    gpio->trg = 0x00000000;
    gpio->flg = 0x00000000;
}

static uint64_t ingenic_gpio_read(void *opaque, hwaddr addr, unsigned size)
{
    if (unlikely(size != 4 || (addr & 3) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "GPIO read unaligned @ " HWADDR_FMT_plx "/%"PRIx32"\n",
                      addr, (uint32_t)size);
        qmp_stop(NULL);
        return 0;
    }

    IngenicGpio *gpio = opaque;
    hwaddr aligned_addr = addr; // & ~3;
    uint32_t data = 0;
    switch (aligned_addr) {
    case 0x00:
        data = gpio->pin;
        break;
    case 0x10:
        data = gpio->dat;
        break;
    case 0x20:
        data = gpio->im;
        break;
    case 0x30:
        data = gpio->pe;
        break;
    case 0x40:
        data = gpio->fun;
        break;
    case 0x50:
        data = gpio->sel;
        break;
    case 0x60:
        data = gpio->dir;
        break;
    case 0x70:
        data = gpio->trg;
        break;
    case 0x80:
        data = gpio->flg;
        qmp_stop(NULL);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "GPIO read unknown address " HWADDR_FMT_plx "\n", aligned_addr);
        qmp_stop(NULL);
    }
    //data = (data >> (8 * (addr & 3))) & ((1LL << (8 * size)) - 1);
    qemu_log("GPIO read @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx32"\n", addr, (uint32_t)size, data);
    return data;
}

static void ingenic_gpio_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    if (unlikely(size != 4 || (addr & 3) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "GPIO write unaligned @ " HWADDR_FMT_plx "/%"PRIx32" 0x%"PRIx64"\n",
                      addr, (uint32_t)size, data);
        qmp_stop(NULL);
        return;
    }

    IngenicGpio *gpio = opaque;
    hwaddr aligned_addr = addr; // & ~3;
    qemu_log("GPIO write @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", addr, (uint32_t)size, data);
    switch (aligned_addr) {
    case 0x14:
        gpio->dat |= data;
        // TODO Only clear when in interrupt mode
        gpio->flg &= ~data;
        break;
    case 0x18:
        gpio->dat &= ~data;
        break;
    case 0x24:
        gpio->im  |= data;
        break;
    case 0x28:
        gpio->im  &= ~data;
        break;
    case 0x34:
        gpio->pe  |= data;
        break;
    case 0x38:
        gpio->pe  &= ~data;
        break;
    case 0x44:
        gpio->fun |= data;
        break;
    case 0x48:
        gpio->fun &= ~data;
        break;
    case 0x54:
        gpio->sel |= data;
        break;
    case 0x58:
        gpio->sel &= ~data;
        break;
    case 0x64:
        gpio->dir |= data;
        break;
    case 0x68:
        gpio->dir &= ~data;
        break;
    case 0x74:
        gpio->trg |= data;
        break;
    case 0x78:
        gpio->trg &= ~data;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "GPIO write unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
        qmp_stop(NULL);
        return;
    }
}

static MemoryRegionOps gpio_ops = {
    .read = ingenic_gpio_read,
    .write = ingenic_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void gpio_input_irq(void *opaque, int n, int level)
{
    IngenicGpio *gpio = opaque;
    qemu_log("%s: GPIO %s%"PRIu32" -> %"PRIu32"\n", __func__, gpio->name, n, level);
}

static void ingenic_gpio_init(Object *obj)
{
    qemu_log("%s enter\n", __func__);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IngenicGpio *s = INGENIC_GPIO(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &gpio_ops, s, "gpio", 0x100);
    sysbus_init_mmio(sbd, &s->mr);

    // Initialise GPIO inputs & outputs
    qdev_init_gpio_in_named_with_opaque(DEVICE(obj), &gpio_input_irq, s, "in", 32);
    qdev_init_gpio_out(DEVICE(obj), &s->output[0], 32);

    qemu_log("%s end\n", __func__);
}

static void ingenic_gpio_finalize(Object *obj)
{
    qemu_log("%s enter\n", __func__);
}

static Property ingenic_gpio_properties[] = {
    DEFINE_PROP_STRING("name", IngenicGpio, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void ingenic_gpio_class_init(ObjectClass *class, void *data)
{
    device_class_set_props(DEVICE_CLASS(class), ingenic_gpio_properties);
    IngenicGpioClass *gpio_class = INGENIC_GPIO_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_gpio_reset,
                                       NULL,
                                       NULL,
                                       &gpio_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicGpio, ingenic_gpio, INGENIC_GPIO, SYS_BUS_DEVICE)

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
#include "trace.h"

#define REG_PAPIN   0x00
#define REG_PADAT   0x10
#define REG_PADATS  0x14
#define REG_PADATC  0x18
#define REG_PAIM    0x20
#define REG_PAIMS   0x24
#define REG_PAIMC   0x28
#define REG_PAPE    0x30
#define REG_PAPES   0x34
#define REG_PAPEC   0x38
#define REG_PAFUN   0x40
#define REG_PAFUNS  0x44
#define REG_PAFUNC  0x48
#define REG_PASEL   0x50
#define REG_PASELS  0x54
#define REG_PASELC  0x58
#define REG_PADIR   0x60
#define REG_PADIRS  0x64
#define REG_PADIRC  0x68
#define REG_PATRG   0x70
#define REG_PATRGS  0x74
#define REG_PATRGC  0x78
#define REG_PAFLG   0x80
#define REG_PAFLGC  0x14

void qmp_stop(Error **errp);

static void ingenic_gpio_reset(Object *obj, ResetType type)
{
    IngenicGpio *gpio = INGENIC_GPIO(obj);
    gpio->pin = gpio->reset;
    gpio->dat = 0x00000000;
    gpio->im  = 0xffffffff;
    gpio->pe  = 0x00000000;
    gpio->fun = 0x00000000;
    gpio->sel = 0x00000000;
    gpio->dir = 0x00000000;
    gpio->trg = 0x00000000;
    gpio->flg = 0x00000000;
}

static void ingenic_gpio_update_irq(IngenicGpio *s, uint32_t prev_pin)
{
    // imask=1: Interrupt mode
    uint32_t imask = ~s->fun & s->sel;
    // edge=1: Edge trigger
    uint32_t edge = s->trg;
    // dir=1: High-level or rising edge
    uint32_t dir = s->dir;

    // Edge trigger
    s->flg |= imask &  edge &  dir & ( s->pin & ~prev_pin);
    s->flg |= imask &  edge & ~dir & (~s->pin &  prev_pin);

    // Level trigger
    s->flg |= imask & ~edge &  dir &  s->pin;
    s->flg |= imask & ~edge & ~dir & ~s->pin;

    // Update IRQ output
    int irq = !!(~s->im & s->flg);
    if (irq != s->prev_irq_level) {
        s->prev_irq_level = irq;
        trace_ingenic_gpio_irq(s->name, irq);
        qemu_set_irq(s->irq_out, irq);
    }
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
    case REG_PAPIN:
        data = gpio->pin;
        {
            uint32_t to_raise = gpio->pending_raise & ~gpio->pin;
            uint32_t to_fall = gpio->pending_fall & gpio->pin;
            gpio->pin = (gpio->pin | to_raise) & ~to_fall;
            gpio->pending_raise &= ~to_raise;
            gpio->pending_fall &= ~to_fall;
        }
        break;
    case REG_PADAT:
        data = gpio->dat;
        break;
    case REG_PAIM:
        data = gpio->im;
        break;
    case REG_PAPE:
        data = gpio->pe;
        break;
    case REG_PAFUN:
        data = gpio->fun;
        break;
    case REG_PASEL:
        data = gpio->sel;
        break;
    case REG_PADIR:
        data = gpio->dir;
        break;
    case REG_PATRG:
        data = gpio->trg;
        break;
    case REG_PAFLG:
        data = gpio->flg;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "GPIO read unknown address " HWADDR_FMT_plx "\n", aligned_addr);
        qmp_stop(NULL);
    }
    trace_ingenic_gpio_read(gpio->name, addr, data);
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
    hwaddr aligned_addr = addr;
    trace_ingenic_gpio_write(gpio->name, addr, data);
    switch (aligned_addr) {
    case REG_PADATS:    // also REG_PAFLGC
        gpio->dat |=  data;
        gpio->flg &= ~data;
        break;
    case REG_PADATC:
        gpio->dat &= ~data;
        break;
    case REG_PAIMS:
        gpio->im  |= data;
        break;
    case REG_PAIMC:
        gpio->im  &= ~data;
        break;
    case REG_PAPES:
        gpio->pe  |= data;
        break;
    case REG_PAPEC:
        gpio->pe  &= ~data;
        break;
    case REG_PAFUNS:
        gpio->fun |= data;
        break;
    case REG_PAFUNC:
        gpio->fun &= ~data;
        break;
    case REG_PASELS:
        gpio->sel |= data;
        break;
    case REG_PASELC:
        gpio->sel &= ~data;
        break;
    case REG_PADIRS:
        gpio->dir |= data;
        break;
    case REG_PADIRC:
        gpio->dir &= ~data;
        break;
    case REG_PATRGS:
        gpio->trg |= data;
        break;
    case REG_PATRGC:
        gpio->trg &= ~data;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "GPIO write unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
        qmp_stop(NULL);
        return;
    }
    trace_ingenic_gpio_config(gpio->name, gpio->im, gpio->fun, gpio->sel, gpio->dir);
    trace_ingenic_gpio_status(gpio->name, gpio->pin, gpio->dat, gpio->flg);
    ingenic_gpio_update_irq(gpio, gpio->pin);
}

static MemoryRegionOps ingenic_gpio_ops = {
    .read = ingenic_gpio_read,
    .write = ingenic_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_gpio_input_irq(void *opaque, int n, int level)
{
    IngenicGpio *gpio = opaque;
    trace_ingenic_gpio_in(gpio->name, n, level);
    uint32_t mask = 1 << n;
    uint32_t val = level << n;
    // Update pin state
    uint32_t pin = gpio->pin;
    gpio->pin = (gpio->pin & ~mask) | val;
    if (level)
        gpio->pending_raise |= mask;
    else
        gpio->pending_fall |= mask;
    ingenic_gpio_update_irq(gpio, pin);
    trace_ingenic_gpio_status(gpio->name, gpio->pin, gpio->dat, gpio->flg);
}

static void ingenic_gpio_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IngenicGpio *s = INGENIC_GPIO(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &ingenic_gpio_ops, s, "gpio", 0x100);
    sysbus_init_mmio(sbd, &s->mr);

    // Initialise GPIO inputs & outputs
    qdev_init_gpio_in_named_with_opaque(dev, &ingenic_gpio_input_irq, s, "gpio-in", 32);
    qdev_init_gpio_out_named(dev, &s->output[0], "gpio-out", 32);
    qdev_init_gpio_out_named(dev, &s->irq_out, "irq-out", 1);
}

static void ingenic_gpio_finalize(Object *obj)
{
}

static Property ingenic_gpio_properties[] = {
    DEFINE_PROP_STRING("name",  IngenicGpio, name),
    DEFINE_PROP_UINT32("pull",  IngenicGpio, pull, 0xffffffff),
    DEFINE_PROP_UINT32("reset", IngenicGpio, reset, 0xffffffff),
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

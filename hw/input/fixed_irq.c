/*
 * Fixed IRQ value, applies the same value after resets
 *
 * Copyright (c) 2026 Norman Zhi (normanzyb@gmail.com)
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
#include "system/reset.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/input/fixed_irq.h"

static void fixed_irq_reset(Object *obj, ResetType type)
{
    FixedIrq *s = FIXED_IRQ(obj);
    qemu_set_irq(s->irq, s->irq_value);
}

static void fixed_irq_realize(DeviceState *dev, Error **errp)
{
    FixedIrq *s = FIXED_IRQ(dev);
    qdev_init_gpio_out(dev, &s->irq, 1);

    // Device is not on a bus, reset needs to be registered explicitly
    qemu_register_resettable(OBJECT(dev));
}

OBJECT_DEFINE_TYPE(FixedIrq, fixed_irq, FIXED_IRQ, DEVICE)

static void fixed_irq_init(Object *obj)
{
}

static void fixed_irq_finalize(Object *obj)
{
}

static const Property fixed_irq_properties[] = {
    DEFINE_PROP_INT32("irq-value", FixedIrq, irq_value, 0),
};

static void fixed_irq_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    dc->realize = fixed_irq_realize;
    device_class_set_props(dc, fixed_irq_properties);

    ResettableClass *rc = RESETTABLE_CLASS(class);
    rc->phases.exit = &fixed_irq_reset;
}

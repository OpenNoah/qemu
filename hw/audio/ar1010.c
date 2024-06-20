/*
 * Airoha Technology Corp. AR1000/AR1010 FM radio
 *
 * Copyright (c) 2024 Norman Zhi (normanzyb@gmail.com)
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
#include "hw/audio/ar1010.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "ui/console.h"
#include "qom/object.h"
#include "trace.h"

typedef struct AR1010State {
    I2CSlave parent_obj;
} AR1010State;

void qmp_stop(Error **errp);

static int ar1010_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    trace_ar1010_i2c_event("EVENT", event);
    return 0;
}

static uint8_t ar1010_i2c_rx(I2CSlave *i2c)
{
    uint8_t data = 0;
    trace_ar1010_i2c_event("RX", data);
    return data;
}

static int ar1010_i2c_tx(I2CSlave *i2c, uint8_t data)
{
    trace_ar1010_i2c_event("TX", data);
    return 0;
}

static void ar1010_reset(DeviceState *dev)
{
}

static void ar1010_realize(DeviceState *dev, Error **errp)
{
}

static void ar1010_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = ar1010_reset;
    dc->realize = ar1010_realize;
    k->event = ar1010_i2c_event;
    k->recv = ar1010_i2c_rx;
    k->send = ar1010_i2c_tx;
    //dc->vmsd = &vmstate_lm_kbd;
}

static const TypeInfo ar1010_info = {
    .name          = TYPE_AR1010,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(AR1010State),
    .class_init    = ar1010_class_init,
};

static void ar1010_register_types(void)
{
    type_register_static(&ar1010_info);
}

type_init(ar1010_register_types)

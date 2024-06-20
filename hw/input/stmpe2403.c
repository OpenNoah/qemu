/*
 * STMicroelectronics STMPE2403 GPIO / keypad controller
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
#include "hw/input/stmpe2403.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "ui/console.h"
#include "qom/object.h"
#include "trace.h"

typedef struct STMPE2403State {
    I2CSlave parent_obj;
} STMPE2403State;

void qmp_stop(Error **errp);

static int stmpe2403_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    trace_stmpe2403_i2c_event("EVENT", event);
    return 0;
}

static uint8_t stmpe2403_i2c_rx(I2CSlave *i2c)
{
    uint8_t data = 0;
    trace_stmpe2403_i2c_event("RX", data);
    return data;
}

static int stmpe2403_i2c_tx(I2CSlave *i2c, uint8_t data)
{
    trace_stmpe2403_i2c_event("TX", data);
    return 0;
}

static void stmpe2403_reset(DeviceState *dev)
{
}

static void stmpe2403_realize(DeviceState *dev, Error **errp)
{
}

static void stmpe2403_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = stmpe2403_reset;
    dc->realize = stmpe2403_realize;
    k->event = stmpe2403_i2c_event;
    k->recv = stmpe2403_i2c_rx;
    k->send = stmpe2403_i2c_tx;
    //dc->vmsd = &vmstate_lm_kbd;
}

static const TypeInfo stmpe2403_info = {
    .name          = TYPE_STMPE2403,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(STMPE2403State),
    .class_init    = stmpe2403_class_init,
};

static void stmpe2403_register_types(void)
{
    type_register_static(&stmpe2403_info);
}

type_init(stmpe2403_register_types)

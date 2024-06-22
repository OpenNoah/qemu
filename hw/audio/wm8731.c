/*
 * WM8731 audio CODEC.
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
#include "hw/audio/wm8731.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "ui/console.h"
#include "qom/object.h"
#include "trace.h"

void qmp_stop(Error **errp);

static void wm8731_write(Wm8731 *s, uint8_t reg, uint8_t value)
{
    trace_wm8731_reg_write(reg, value);
}

static int wm8731_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    Wm8731 *s = WM8731(i2c);
    trace_wm8731_i2c_event("EVENT", event);
    s->i2c_start = event == I2C_START_SEND;
    return 0;
}

static uint8_t wm8731_i2c_rx(I2CSlave *i2c)
{
    uint8_t data = 0;
    trace_wm8731_i2c_event("RX", data);
    qmp_stop(NULL);
    return data;
}

static int wm8731_i2c_tx(I2CSlave *i2c, uint8_t data)
{
    Wm8731 *s = WM8731(i2c);
    trace_wm8731_i2c_event("TX", data);
    if (s->i2c_start) {
        s->reg_addr = data;
        s->i2c_start = 0;
    } else {
        wm8731_write(s, s->reg_addr, data);
    }
    return 0;
}

static void wm8731_reset(DeviceState *dev)
{
}

static void wm8731_realize(DeviceState *dev, Error **errp)
{
}

static void wm8731_init(Object *obj)
{
}

static void wm8731_finalize(Object *obj)
{
}

static void wm8731_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = wm8731_reset;
    dc->realize = wm8731_realize;
    k->event = wm8731_i2c_event;
    k->recv = wm8731_i2c_rx;
    k->send = wm8731_i2c_tx;
    //dc->vmsd = &vmstate_lm_kbd;
}

OBJECT_DEFINE_TYPE(Wm8731, wm8731, WM8731, I2C_SLAVE)

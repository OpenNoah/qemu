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
#include "qemu/log.h"
#include "ui/console.h"
#include "qom/object.h"
#include "trace.h"

#define GPMR_MSB    0xa2
#define GPMR_CSB    0xa3
#define GPMR_LSB    0xa4

void qmp_stop(Error **errp);

static uint8_t stmpe2403_read(Stmpe2403 *s, uint8_t reg)
{
    uint8_t value = 0;
    switch (reg) {
    case GPMR_MSB:
    case GPMR_CSB:
    case GPMR_LSB:
        value = 0xff;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: TODO reg=0x%02x\n", __func__, reg);
        qmp_stop(NULL);
    }
    trace_stmpe2403_reg_read(reg, value);
    return value;
}

static void stmpe2403_write(Stmpe2403 *s, uint8_t reg, uint8_t value)
{
    trace_stmpe2403_reg_write(reg, value);
}

static int stmpe2403_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    Stmpe2403 *s = STMPE2403(i2c);
    trace_stmpe2403_i2c_event("EVENT", event);
    s->i2c_start = event == I2C_START_SEND;
    return 0;
}

static uint8_t stmpe2403_i2c_rx(I2CSlave *i2c)
{
    Stmpe2403 *s = STMPE2403(i2c);
    uint8_t value = stmpe2403_read(s, s->reg_addr);
    s->reg_addr++;
    trace_stmpe2403_i2c_event("RX", value);
    return value;
}

static int stmpe2403_i2c_tx(I2CSlave *i2c, uint8_t data)
{
    Stmpe2403 *s = STMPE2403(i2c);
    trace_stmpe2403_i2c_event("TX", data);
    if (s->i2c_start) {
        s->reg_addr = data;
        s->i2c_start = 0;
    } else {
        stmpe2403_write(s, s->reg_addr, data);
        s->reg_addr++;
    }
    return 0;
}

static void stmpe2403_reset(DeviceState *dev)
{
}

static void stmpe2403_realize(DeviceState *dev, Error **errp)
{
}

static void stmpe2403_init(Object *obj)
{
}

static void stmpe2403_finalize(Object *obj)
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

OBJECT_DEFINE_TYPE(Stmpe2403, stmpe2403, STMPE2403, I2C_SLAVE)

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

// System controller
#define REG_SYSCON          0x02
#define REG_SYSCON2         0x03
// Interrupt system
#define REG_ICR_MSB         0x10
#define REG_ICR_LSB         0x11
#define REG_IER_MSB         0x12
#define REG_IER_LSB         0x13
#define REG_ISR_MSB         0x14
#define REG_ISR_LSB         0x15
#define REG_IEGPIOR_MSB     0x16
#define REG_IEGPIOR_CSB     0x17
#define REG_IEGPIOR_LSB     0x18
#define REG_ISGPIOR_MSB     0x19
#define REG_ISGPIOR_CSB     0x1a
#define REG_ISGPIOR_LSB     0x1b
// Keypad controller
#define REG_KPC_COL         0x60
#define REG_KPC_ROW_MSB     0x61
#define REG_KPC_ROW_LSB     0x62
#define REG_KPC_CTRL_MSB    0x63
#define REG_KPC_CTRL_LSB    0x64
#define REG_KPC_DATA_BYTE0  0x68
#define REG_KPC_DATA_BYTE1  0x69
#define REG_KPC_DATA_BYTE2  0x6a
#define REG_KPC_DATA_BYTE3  0x6b
#define REG_KPC_DATA_BYTE4  0x6c
// System controller
#define REG_CHIP_ID         0x80
#define REG_VERSION_ID      0x81
// GPIO controller
#define REG_GPSR_MSB        0x83
#define REG_GPSR_CSB        0x84
#define REG_GPSR_LSB        0x85
#define REG_GPCR_MSB        0x86
#define REG_GPCR_CSB        0x87
#define REG_GPCR_LSB        0x88
#define REG_GPDR_MSB        0x89
#define REG_GPDR_CSB        0x8a
#define REG_GPDR_LSB        0x8b
#define REG_GPEDR_MSB       0x8c
#define REG_GPEDR_CSB       0x8d
#define REG_GPEDR_LSB       0x8e
#define REG_GPRER_MSB       0x8f
#define REG_GPRER_CSB       0x90
#define REG_GPRER_LSB       0x91
#define REG_GPFER_MSB       0x92
#define REG_GPFER_CSB       0x93
#define REG_GPFER_LSB       0x94
#define REG_GPPUR_MSB       0x95
#define REG_GPPUR_CSB       0x96
#define REG_GPPUR_LSB       0x97
#define REG_GPPDR_MSB       0x98
#define REG_GPPDR_CSB       0x99
#define REG_GPPDR_LSB       0x9a
#define REG_GPAFR_U_MSB     0x9b
#define REG_GPAFR_U_CSB     0x9c
#define REG_GPAFR_U_LSB     0x9d
#define REG_GPAFR_L_MSB     0x9e
#define REG_GPAFR_L_CSB     0x9f
#define REG_GPAFR_L_LSB     0xa0
#define REG_MUX_CTRL        0xa1
#define REG_GPMR_MSB        0xa2
#define REG_GPMR_CSB        0xa3
#define REG_GPMR_LSB        0xa4
#define REG_COMPAT2401      0xa5

void qmp_stop(Error **errp);

static void stmpe2403_reset(DeviceState *dev)
{
    Stmpe2403 *s = STMPE2403(dev);
    s->gpio_out_level = 0;
    s->prev_irq_out   = 0;
    s->reg.syscon   = 0x0f;
    s->reg.syscon2  = 0;
    s->reg.icr      = 0;
    s->reg.ier      = 0;
    s->reg.isr      = 0;
    s->reg.iegpior  = 0;
    s->reg.isgpior  = 0;
    s->reg.gpin     = s->force_gpio_value;
    s->reg.gpout    = 0;
    s->reg.gpdr     = 0;
    s->reg.gpedr    = 0;
    s->reg.gprer    = 0;
    s->reg.gpfer    = 0;
    s->reg.gppur    = 0;
    s->reg.gppdr    = 0;
    s->reg.gpafr_u  = 0;
    s->reg.gpafr_l  = 0;
    s->reg.mcr = 0;
    s->reg.compat2401 = 0;
}

static void stmpe2403_update_irq(Stmpe2403 *s, uint32_t prev_dir, uint32_t prev_pin)
{
    // Update input level of output GPIOs
    s->reg.gpin = (s->reg.gpin & ~s->reg.gpdr) | (s->reg.gpout & s->reg.gpdr);
    s->reg.gpin = (s->reg.gpin & ~s->force_gpio_mask) | s->force_gpio_value;
    // Edge detection
    uint32_t change = prev_pin ^ s->reg.gpin;
    uint32_t edge_det = (change & s->reg.gprer &  s->reg.gpin) |
                        (change & s->reg.gpfer & ~s->reg.gpin);
    s->reg.gpedr |= edge_det;

    trace_stmpe2403_gpio(s->reg.gpdr, s->reg.gpin,
        s->reg.gpedr, s->reg.gprer, s->reg.gpfer,
        s->reg.gppur, s->reg.gppdr, ((uint64_t)s->reg.gpafr_u << 24) | s->reg.gpafr_l);

    // Update GPIO output levels with pull-up/down registers
    uint32_t prev_gpio_out_level = s->gpio_out_level;
    s->gpio_out_level = (s->reg.gpdr & s->reg.gpout) | (~s->reg.gpdr & s->reg.gppur);
    int i = 0;
    change = (prev_dir ^ s->reg.gpdr) | (prev_gpio_out_level ^ s->gpio_out_level);
    while (change) {
        if (change & 1) {
            int level = (s->gpio_out_level >> i) & 1;
            level |= ((~s->reg.gpdr >> i) & 1) << 1;
            qemu_set_irq(s->gpio_out[i], level);
        }
        change >>= 1;
        i++;
    }

    // Update GPIO controller interrupt
    s->reg.isgpior |= edge_det;
    if (s->reg.isgpior & s->reg.iegpior)
        s->reg.isr |= BIT(8);

    // Update IRQ output
    bool irq = (s->reg.isr & s->reg.ier) && (s->reg.icr & BIT(0));
    if (irq != s->prev_irq_out) {
        trace_stmpe2403_irq(irq, s->reg.gpin);
        s->prev_irq_out = irq;
        // Polarity bit, 1: active high / raising edge
        bool low = !(s->reg.icr & BIT(2));
        if (s->reg.icr & BIT(1)) {
            // Edge interrupt
            if (irq) {
                // An edge interrupt will only assert a pulse width of 250ns
                qemu_set_irq(s->irq_out, low ^ 1);
                qemu_set_irq(s->irq_out, low ^ 0);
            }
        } else {
            // Level interrupt
            qemu_set_irq(s->irq_out, low ^ irq);
        }
    }
}

static void stmpe2403_gpio_irq(void *opaque, int n, int level)
{
    Stmpe2403 *s = STMPE2403(opaque);
    trace_stmpe2403_gpio_in(n, level);
    // Decode GPIO level
    uint32_t mask = 1 << n;
    uint32_t weak = ((level & 2) >> 1) << n;
    uint32_t val = (level & 1) << n;
    // Check for signal on an output pin
    if (mask & s->reg.gpdr) {
        // Check for conflicting strong signal on an output pin
        if (mask & ~weak & (val ^ s->reg.gpout)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Conflicting change PIN %d to %d\n",
                __func__, n, level);
            //qmp_stop(NULL);
            val = mask & s->reg.gpout;
        }
    }
    // Update pin level
    uint32_t pin = s->reg.gpin;
    s->reg.gpin = (s->reg.gpin & ~mask) | val;
    s->reg.gpin = (s->reg.gpin & ~s->force_gpio_mask) | s->force_gpio_value;
    if (pin != s->reg.gpin)
        stmpe2403_update_irq(s, s->reg.gpdr, pin);
}

static uint8_t stmpe2403_read_mb(uint8_t lsb, uint8_t reg, uint32_t rv)
{
    int ofs = 8 * (lsb - reg);
    return rv >> ofs;
}

static uint32_t stmpe2403_write_mb(uint8_t lsb, uint8_t reg, uint32_t rv, uint8_t value)
{
    int ofs = 8 * (lsb - reg);
    return (rv & ~(0xff << ofs)) | (value << ofs);
}

static uint8_t stmpe2403_read(Stmpe2403 *s, uint8_t reg)
{
    uint8_t value = 0;
    switch (reg) {
    case REG_SYSCON:
        value = s->reg.syscon;
        break;
    case REG_SYSCON2:
        value = s->reg.syscon2;
        break;
    case REG_ICR_MSB:
        break;
    case REG_ICR_LSB:
        value = s->reg.icr;
        break;
    case REG_KPC_COL:
        value = s->reg.kpc_col;
        break;
    case REG_KPC_ROW_MSB:
    case REG_KPC_ROW_LSB:
        value = stmpe2403_read_mb(REG_KPC_ROW_LSB, reg, s->reg.kpc_row);
        break;
    case REG_KPC_CTRL_MSB:
    case REG_KPC_CTRL_LSB:
        value = stmpe2403_read_mb(REG_KPC_CTRL_LSB, reg, s->reg.kpc_ctrl);
        break;
    case REG_CHIP_ID:
        value = 0x01;
        break;
    case REG_VERSION_ID:
        value = 0x02;
        break;
    case REG_GPAFR_U_MSB:
    case REG_GPAFR_U_CSB:
    case REG_GPAFR_U_LSB:
        value = stmpe2403_read_mb(REG_GPAFR_U_LSB, reg, s->reg.gpafr_u);
        break;
    case REG_GPAFR_L_MSB:
    case REG_GPAFR_L_CSB:
    case REG_GPAFR_L_LSB:
        value = stmpe2403_read_mb(REG_GPAFR_L_LSB, reg, s->reg.gpafr_l);
        break;
    case REG_GPMR_MSB:
    case REG_GPMR_CSB:
    case REG_GPMR_LSB:
        value = stmpe2403_read_mb(REG_GPMR_LSB, reg, s->reg.gpin);
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
    uint32_t dir = s->reg.gpdr;
    uint32_t pin = s->reg.gpin;
    switch (reg) {
    // System controller
    case REG_SYSCON:
        s->reg.syscon = value & 0x7f;
        if (value & 0x80)
            stmpe2403_reset(DEVICE(s));
        break;
    case REG_SYSCON2:
        s->reg.syscon2 = value & 0x0f;
        break;
    // Interrupt system
    case REG_ICR_MSB:
    case REG_ICR_LSB:
        s->reg.icr = stmpe2403_write_mb(REG_ICR_LSB, reg, s->reg.icr, value) & 0x0007;
        break;
    case REG_IER_MSB:
    case REG_IER_LSB:
        s->reg.ier = stmpe2403_write_mb(REG_IER_LSB, reg, s->reg.ier, value) & 0x01ff;
        break;
    case REG_ISR_MSB:
    case REG_ISR_LSB:
        s->reg.isr &= ~stmpe2403_write_mb(REG_ISR_LSB, reg, 0, value) & 0x01ff;
        break;
    case REG_IEGPIOR_MSB:
    case REG_IEGPIOR_CSB:
    case REG_IEGPIOR_LSB:
        s->reg.iegpior = stmpe2403_write_mb(REG_IEGPIOR_LSB, reg, s->reg.iegpior, value);
        break;
    case REG_ISGPIOR_MSB:
    case REG_ISGPIOR_CSB:
    case REG_ISGPIOR_LSB:
        s->reg.isgpior &= ~stmpe2403_write_mb(REG_ISGPIOR_LSB, reg, 0, value);
        break;
    // Keypad controller
    case REG_KPC_COL:
        s->reg.kpc_col = value;
        break;
    case REG_KPC_ROW_MSB:
    case REG_KPC_ROW_LSB:
        s->reg.kpc_row = stmpe2403_write_mb(REG_KPC_ROW_LSB, reg, s->reg.kpc_row, value) & 0xefff;
        break;
    case REG_KPC_CTRL_MSB:
    case REG_KPC_CTRL_LSB:
        s->reg.kpc_ctrl = stmpe2403_write_mb(REG_KPC_CTRL_LSB, reg, s->reg.kpc_ctrl, value);
        break;
    // GPIO controller
    case REG_GPSR_MSB:
    case REG_GPSR_CSB:
    case REG_GPSR_LSB:
        s->reg.gpout |= stmpe2403_write_mb(REG_GPSR_LSB, reg, 0, value);
        break;
    case REG_GPCR_MSB:
    case REG_GPCR_CSB:
    case REG_GPCR_LSB:
        s->reg.gpout &= ~stmpe2403_write_mb(REG_GPCR_LSB, reg, 0, value);
        break;
    case REG_GPDR_MSB:
    case REG_GPDR_CSB:
    case REG_GPDR_LSB:
        s->reg.gpdr = stmpe2403_write_mb(REG_GPDR_LSB, reg, s->reg.gpdr, value);
        break;
    case REG_GPEDR_MSB:
    case REG_GPEDR_CSB:
    case REG_GPEDR_LSB:
        s->reg.gpedr = stmpe2403_write_mb(REG_GPEDR_LSB, reg, s->reg.gpedr, value);
        break;
    case REG_GPRER_MSB:
    case REG_GPRER_CSB:
    case REG_GPRER_LSB:
        s->reg.gprer = stmpe2403_write_mb(REG_GPRER_LSB, reg, s->reg.gprer, value);
        break;
    case REG_GPFER_MSB:
    case REG_GPFER_CSB:
    case REG_GPFER_LSB:
        s->reg.gpfer = stmpe2403_write_mb(REG_GPFER_LSB, reg, s->reg.gpfer, value);
        break;
    case REG_GPPUR_MSB:
    case REG_GPPUR_CSB:
    case REG_GPPUR_LSB:
        s->reg.gppur = stmpe2403_write_mb(REG_GPPUR_LSB, reg, s->reg.gppur, value);
        break;
    case REG_GPPDR_MSB:
    case REG_GPPDR_CSB:
    case REG_GPPDR_LSB:
        s->reg.gppdr = stmpe2403_write_mb(REG_GPPDR_LSB, reg, s->reg.gppdr, value);
        break;
    case REG_GPAFR_U_MSB:
    case REG_GPAFR_U_CSB:
    case REG_GPAFR_U_LSB:
        s->reg.gpafr_u = stmpe2403_write_mb(REG_GPAFR_U_LSB, reg, s->reg.gpafr_u, value);
        break;
    case REG_GPAFR_L_MSB:
    case REG_GPAFR_L_CSB:
    case REG_GPAFR_L_LSB:
        s->reg.gpafr_l = stmpe2403_write_mb(REG_GPAFR_L_LSB, reg, s->reg.gpafr_l, value);
        break;
    case REG_MUX_CTRL:
        s->reg.mcr = value & 0x0f;
        break;
    case REG_GPMR_MSB:
    case REG_GPMR_CSB:
    case REG_GPMR_LSB:
        // Writes ignored
        break;
    case REG_COMPAT2401:
        s->reg.compat2401 = value & 1;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: TODO reg=0x%02x\n", __func__, reg);
        qmp_stop(NULL);
    }
    stmpe2403_update_irq(s, dir, pin);
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

static void stmpe2403_realize(DeviceState *dev, Error **errp)
{
}

OBJECT_DEFINE_TYPE(Stmpe2403, stmpe2403, STMPE2403, I2C_SLAVE)

static void stmpe2403_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    Stmpe2403 *s = STMPE2403(obj);
    qdev_init_gpio_in_named_with_opaque(dev, &stmpe2403_gpio_irq, s, "gpio-in", 24);
    qdev_init_gpio_out_named(dev, &s->gpio_out[0], "gpio-out", 24);
    qdev_init_gpio_out_named(dev, &s->irq_out, "irq-out", 1);
}

static void stmpe2403_finalize(Object *obj)
{
}

static Property stmpe2403_properties[] = {
    DEFINE_PROP_UINT32("force-gpio-mask",  Stmpe2403, force_gpio_mask,  0),
    DEFINE_PROP_UINT32("force-gpio-value", Stmpe2403, force_gpio_value, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void stmpe2403_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    device_class_set_props(dc, stmpe2403_properties);
    dc->reset = stmpe2403_reset;
    dc->realize = stmpe2403_realize;
    k->event = stmpe2403_i2c_event;
    k->recv = stmpe2403_i2c_rx;
    k->send = stmpe2403_i2c_tx;
    //dc->vmsd = &vmstate_lm_kbd;
}

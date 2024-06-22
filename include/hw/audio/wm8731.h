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

#ifndef HW_INPUT_WM8731_H
#define HW_INPUT_WM8731_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"

// WM8731 I2C device address can be 0x1a, 0x1b
#define WM8731_DEFAULT_I2C_ADDR 0x1a

#define TYPE_WM8731 "wm8731"
OBJECT_DECLARE_TYPE(Wm8731, Wm8731Class, WM8731)

enum wm8731_i2c_state {
    Wm8731I2cIdle,
    Wm8731I2cStart,
    Wm8731I2cWrite,
};

typedef struct Wm8731
{
    I2CSlave parent_obj;

    uint8_t i2c_start;
    uint8_t reg_addr;

    // Registers
    struct {
        uint16_t aicfr;
        uint32_t aiccr;
        uint16_t i2scr;
        uint8_t  i2sdiv;
    } reg;
} Wm8731;

typedef struct Wm8731Class
{
    I2CSlaveClass parent_class;
} Wm8731Class;

#endif // HW_INPUT_WM8731_H

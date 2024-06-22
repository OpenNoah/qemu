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

#ifndef HW_INPUT_STMPE2403_H
#define HW_INPUT_STMPE2403_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"

// STMPE2403 I2C device address can be 0x42, 0x43, 0x44, 0x45
#define STMPE2403_DEFAULT_I2C_ADDR 0x42

#define TYPE_STMPE2403 "stmpe2403"
OBJECT_DECLARE_TYPE(Stmpe2403, Stmpe2403Class, STMPE2403)

typedef struct Stmpe2403
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
} Stmpe2403;

typedef struct Stmpe2403Class
{
    I2CSlaveClass parent_class;
} Stmpe2403Class;

#endif // HW_INPUT_STMPE2403_H

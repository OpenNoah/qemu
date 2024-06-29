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
    qemu_irq irq_out;
    qemu_irq gpio_out[24];

    uint32_t force_gpio_mask;
    uint32_t force_gpio_value;
    uint32_t gpio_out_level;
    bool prev_irq_out;

    uint8_t i2c_start;
    uint8_t reg_addr;

    // Registers
    struct {
        // System controller
        uint8_t syscon;
        uint8_t syscon2;
        // Interrupt system
        uint8_t icr;
        uint16_t ier;
        uint16_t isr;
        uint32_t iegpior;
        uint32_t isgpior;
        // Keypad controller
        uint8_t kpc_col;
        uint16_t kpc_row;
        uint16_t kpc_ctrl;
        // GPIO controller
        uint32_t gpin;              // Input state
        uint32_t gpout;             // Output state
        uint32_t gpdr;              // Direction, 0: in, 1: out
        uint32_t gpedr;             // Edge detected
        uint32_t gprer;             // Rising edge detect enable
        uint32_t gpfer;             // Falling edge detect enable
        uint32_t gppur;             // Pull-up enable
        uint32_t gppdr;             // Pull-down enable
        uint32_t gpafr_u, gpafr_l;  // Alternative function
        uint8_t mcr;
        uint8_t compat2401;
    } reg;
} Stmpe2403;

typedef struct Stmpe2403Class
{
    I2CSlaveClass parent_class;
} Stmpe2403Class;

#endif // HW_INPUT_STMPE2403_H

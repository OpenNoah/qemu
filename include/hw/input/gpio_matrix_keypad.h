/*
 * Generic GPIO matrix keypad
 *
 * Copyright (c) 2025 Norman Zhi (normanzyb@gmail.com)
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

#ifndef HW_INPUT_GPIO_MATRIX_KEYPAD_H
#define HW_INPUT_GPIO_MATRIX_KEYPAD_H

#include "qom/object.h"
#include "hw/qdev-core.h"
#include "ui/input.h"

#define TYPE_GPIO_MATRIX_KEYPAD "gpio-matrix-keypad"
OBJECT_DECLARE_TYPE(GpioMatrixKeypad, GpioMatrixKeypadClass, GPIO_MATRIX_KEYPAD)

typedef struct GpioMatrixKeypadIO {
    qemu_irq *irq;
    const char *name;
    uint8_t num_pins;
    uint32_t ext_pull;
    uint32_t ext_pull_value;
    uint32_t floating;
    uint32_t pull;
    uint32_t value;
} GpioMatrixKeypadIO;

typedef struct GpioMatrixKeypad
{
    DeviceState parent_obj;
    GpioMatrixKeypadIO row, col;
    char *map_file;
    QKeyCode *key_map;
    uint32_t *btn_hold_map;
    uint32_t *btn_press_map;
} GpioMatrixKeypad;

typedef struct GpioMatrixKeypadClass
{
    DeviceClass parent_class;
    ResettablePhases parent_phases;
} GpioMatrixKeypadClass;

#endif // HW_INPUT_GPIO_MATRIX_KEYPAD_H

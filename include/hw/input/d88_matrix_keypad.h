/*
 * Iriver D88 GPIO matrix keypad
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

#ifndef HW_INPUT_D88_MATRIX_KEYPAD_H
#define HW_INPUT_D88_MATRIX_KEYPAD_H

#include "qom/object.h"
#include "hw/qdev-core.h"

#define TYPE_D88_MATRIX_KEYPAD "d88_matrix_keypad"
OBJECT_DECLARE_TYPE(D88MatrixKeypad, D88MatrixKeypadClass, D88_MATRIX_KEYPAD)

typedef struct D88MatrixKeypad
{
    DeviceState parent_obj;
    qemu_irq *row_out;
    qemu_irq *col_out;
    uint8_t num_rows;
    uint8_t num_cols;

    uint32_t *row_col_map;
    uint32_t row_weak;
    uint32_t row_weak_value;
    uint32_t row_strong_value;
    uint32_t col_weak;
    uint32_t col_weak_value;
    uint32_t col_strong_value;
} D88MatrixKeypad;

typedef struct D88MatrixKeypadClass
{
    DeviceClass parent_class;
} D88MatrixKeypadClass;

#endif // HW_INPUT_D88_MATRIX_KEYPAD_H

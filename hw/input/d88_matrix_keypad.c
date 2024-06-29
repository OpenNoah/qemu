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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "ui/input.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/input/d88_matrix_keypad.h"
#include "trace.h"

static void d88_matrix_keypad_reset(DeviceState *dev)
{
    D88MatrixKeypad *s = D88_MATRIX_KEYPAD(dev);
    for (int row = 0; row < s->num_rows; row++)
        s->row_col_map[row] = 0;
    // D88 has external pull-down connected to ROW IOs
    s->row_weak = (1 << s->num_rows) - 1;
    s->row_weak_value = 0;
    s->row_strong_value = 0;
    s->col_weak = (1 << s->num_cols) - 1;
    s->col_weak_value = 0;
    s->col_strong_value = 0;
}

static void d88_matrix_keypad_row_in(void *opaque, int n, int level)
{
    D88MatrixKeypad *s = D88_MATRIX_KEYPAD(opaque);
    trace_d88_matrix_keypad_row_in(n, level);
    uint32_t mask = 1 << n;
    s->row_weak = (s->row_weak & ~mask) | (((level & 2) >> 1) << n);
    if (level & 2) {
        // Weak signal
        // D88 has external pull-down connected to row IOs
        level = 2;
        s->row_weak_value = (s->row_weak_value & ~mask) | ((level & 1) << n);
        // Look up connected strong column signals
        uint32_t col_strong = s->row_col_map[n] & ~s->col_weak;
        if (col_strong)
            level = !!(s->row_col_map[n] & s->col_strong_value);
        trace_d88_matrix_keypad_row_out(n, level);
        qemu_set_irq(s->row_out[n], level);
    } else {
        // Strong signal
        s->row_strong_value = (s->row_strong_value & ~mask) | ((level & 1) << n);
        // Look up connected weak column signals
        uint32_t col_weak = s->row_col_map[n] & s->col_weak;
        for (int col = 0; col < s->num_cols; col++) {
            if (col_weak & (1 << col)) {
                trace_d88_matrix_keypad_col_out(col, level);
                qemu_set_irq(s->col_out[col], level);
            }
        }
        trace_d88_matrix_keypad_row_out(n, level);
        qemu_set_irq(s->row_out[n], level);
    }
    trace_d88_matrix_keypad_state(s->row_weak, s->row_weak_value, s->row_strong_value,
                                  s->col_weak, s->col_weak_value, s->col_strong_value);
}

static void d88_matrix_keypad_col_in(void *opaque, int n, int level)
{
    D88MatrixKeypad *s = D88_MATRIX_KEYPAD(opaque);
    trace_d88_matrix_keypad_col_in(n, level);
    uint32_t mask = 1 << n;
    s->col_weak = (s->col_weak & ~mask) | (((level & 2) >> 1) << n);
    // Column to row map
    int col_row_map = 0;
    for (int row = 0; row < s->num_rows; row++)
        if (s->row_col_map[row] & mask)
            col_row_map |= 1 << row;
    if (level & 2) {
        // Weak signal
        s->col_weak_value = (s->col_weak_value & ~mask) | ((level & 1) << n);
        // Look up connected strong row signals
        uint32_t row_strong = col_row_map & ~s->row_weak;
        if (row_strong)
            level = !!(col_row_map & s->row_strong_value);
        trace_d88_matrix_keypad_col_out(n, level);
        qemu_set_irq(s->col_out[n], level);
    } else {
        // Strong signal
        s->col_strong_value = (s->col_strong_value & ~mask) | ((level & 1) << n);
        // Look up connected weak row signals
        uint32_t row_weak = col_row_map & s->row_weak;
        for (int row = 0; row < s->num_rows; row++) {
            if (row_weak & (1 << row)) {
                trace_d88_matrix_keypad_row_out(row, level);
                qemu_set_irq(s->row_out[row], level);
            }
        }
        trace_d88_matrix_keypad_col_out(n, level);
        qemu_set_irq(s->col_out[n], level);
    }
    trace_d88_matrix_keypad_state(s->row_weak, s->row_weak_value, s->row_strong_value,
                                  s->col_weak, s->col_weak_value, s->col_strong_value);
}

static void d88_matrix_keypad_event(DeviceState *dev, QemuConsole *src, InputEvent *evt)
{
    D88MatrixKeypad *s = D88_MATRIX_KEYPAD(dev);
    int qcode = qemu_input_key_value_to_qcode(evt->u.key.data->key);
    int down = !!evt->u.key.data->down;

    // Map to matrix row & column IO
    static const int stmpe_map[] = {
        // 0, 1, 2, 3, 4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
        // C, C, C, C, R,  -,  -, C, C, C,  C,  C,  R,  R,  R,  -,  R,  C,  C,  C,  C
           0, 1, 2, 3, 0, -1, -1, 4, 5, 6,  7,  8,  1,  2,  3, -1,  4,  9, 10, 11, 12,
    };
    static const struct {
        int stmpe_row;
        int stmpe_col;
    } map[] = {
        // 1st row
        [Q_KEY_CODE_ESC]            = {13,  9},
        [Q_KEY_CODE_F1]             = {13, 10},     // Menu
        [Q_KEY_CODE_F2]             = {13,  0},     // Multi-dict lookup
        [Q_KEY_CODE_F3]             = {13,  1},     // Chinese
        [Q_KEY_CODE_F4]             = {13,  8},     // English -> Chinese
        [Q_KEY_CODE_F5]             = {13, 11},     // English
        [Q_KEY_CODE_F6]             = {13,  2},     // Korean -> Chinese
        [Q_KEY_CODE_F7]             = {13,  3},     // Sentence translation
        [Q_KEY_CODE_F8]             = {13, 17},     // MP3.FM
        [Q_KEY_CODE_PGUP]           = {13, 19},
        [Q_KEY_CODE_PGDN]           = {13,  7},
        [Q_KEY_CODE_BACKSPACE]      = {16,  7},
        // 2nd row
        [Q_KEY_CODE_Q]              = {16,  9},
        [Q_KEY_CODE_W]              = {16, 10},
        [Q_KEY_CODE_E]              = {16,  0},
        [Q_KEY_CODE_R]              = {16,  1},
        [Q_KEY_CODE_T]              = {16,  8},
        [Q_KEY_CODE_Y]              = {16, 11},
        [Q_KEY_CODE_U]              = {16,  2},
        [Q_KEY_CODE_I]              = {16, 18},
        [Q_KEY_CODE_O]              = {16,  3},
        [Q_KEY_CODE_P]              = {16, 17},
        [Q_KEY_CODE_GRAVE_ACCENT]   = {16, 19},     // Symbol table / Numpad
        [Q_KEY_CODE_1]              = {16,  9},
        [Q_KEY_CODE_2]              = {16, 10},
        [Q_KEY_CODE_3]              = {16,  0},
        [Q_KEY_CODE_4]              = {16,  1},
        [Q_KEY_CODE_5]              = {16,  8},
        [Q_KEY_CODE_6]              = {16, 11},
        [Q_KEY_CODE_7]              = {16,  2},
        [Q_KEY_CODE_8]              = {16, 18},
        [Q_KEY_CODE_9]              = {16,  3},
        [Q_KEY_CODE_0]              = {16, 17},
        // 3rd row
        [Q_KEY_CODE_TAB]            = {12,  9},     // ...
        [Q_KEY_CODE_A]              = {14,  9},
        [Q_KEY_CODE_S]              = {14, 10},
        [Q_KEY_CODE_D]              = {14,  0},
        [Q_KEY_CODE_F]              = {14,  1},
        [Q_KEY_CODE_G]              = {14,  8},
        [Q_KEY_CODE_H]              = {14, 11},
        [Q_KEY_CODE_J]              = {14,  2},
        [Q_KEY_CODE_K]              = {14, 18},
        [Q_KEY_CODE_L]              = {14,  3},
        [Q_KEY_CODE_RET]            = {14,  7},     // Enter
        // 4th row
        [Q_KEY_CODE_SHIFT]          = {14, 20},
        [Q_KEY_CODE_Z]              = {12, 10},
        [Q_KEY_CODE_X]              = {12,  0},
        [Q_KEY_CODE_C]              = {12,  1},
        [Q_KEY_CODE_V]              = {12,  8},
        [Q_KEY_CODE_B]              = {12, 11},
        [Q_KEY_CODE_N]              = {12,  2},
        [Q_KEY_CODE_M]              = {12, 18},
        [Q_KEY_CODE_SLASH]          = { 4, 11},     // Jump lookup
        [Q_KEY_CODE_UP]             = {12, 19},
        [Q_KEY_CODE_SHIFT_R]        = {12, 20},
        [Q_KEY_CODE_CTRL_R]         = {12, 20},
        // 5th row
        [Q_KEY_CODE_CTRL]           = { 4,  9},     // Voice
        [Q_KEY_CODE_F9]             = { 4, 10},     // Example / Solution
        [Q_KEY_CODE_F10]            = { 4,  1},     // Insert
        [Q_KEY_CODE_F11]            = { 4,  8},     // Word revise
        [Q_KEY_CODE_F12]            = {12,  7},     // Transform
        [Q_KEY_CODE_SPC]            = { 4,  2},
        [Q_KEY_CODE_COMMA]          = { 4, 18},     // Input method
        [Q_KEY_CODE_DOT]            = { 4,  3},     // Quick lookup
        [Q_KEY_CODE_LEFT]           = { 4, 17},
        [Q_KEY_CODE_DOWN]           = { 4, 19},
        [Q_KEY_CODE_RIGHT]          = { 4,  7},
    };
    int row = -1;
    int col = -1;
    if (qcode < ARRAY_SIZE(map)) {
        int stmpe_row = map[qcode].stmpe_row;
        int stmpe_col = map[qcode].stmpe_col;
        if (stmpe_row || stmpe_col) {
            row = stmpe_map[stmpe_row];
            col = stmpe_map[stmpe_col];
        }
    }
    trace_d88_matrix_keypad_event(down, qcode, row, col);
    if (row < 0 || col < 0)
        return;

    uint32_t col_mask = 1 << col;
    uint32_t value = s->row_col_map[row] & col_mask;
    if (!down == !value)    // Ignore auto-repeat
        return;

    // Update IO outputs
    s->row_col_map[row] = (s->row_col_map[row] & ~col_mask) | (down << col);
    if (s->col_weak & col_mask) {
        // Update column from row signal
        uint32_t row_mask = 1 << row;
        // D88 has external pull-down connected to row IOs
        value = (s->row_weak & row_mask) ? 2 : !!(s->row_strong_value & row_mask);
        trace_d88_matrix_keypad_col_out(col, value);
        qemu_set_irq(s->col_out[col], value);
    } else {
        // Update row from column signal
        value = !!(s->col_strong_value & col_mask);
        trace_d88_matrix_keypad_row_out(row, value);
        qemu_set_irq(s->row_out[row], value);
    }
    trace_d88_matrix_keypad_state(s->row_weak, s->row_weak_value, s->row_strong_value,
                                  s->col_weak, s->col_weak_value, s->col_strong_value);
}

static const QemuInputHandler d88_matrix_keypad_handler = {
    .name  = "D88 Matrix Keypad",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = &d88_matrix_keypad_event,
};

static void d88_matrix_keypad_init(Object *obj)
{
}

static void d88_matrix_keypad_realize(DeviceState *dev, Error **errp)
{
    D88MatrixKeypad *s = D88_MATRIX_KEYPAD(dev);
    s->row_out = g_new(qemu_irq, s->num_rows);
    s->col_out = g_new(qemu_irq, s->num_cols);
    qdev_init_gpio_in_named_with_opaque(dev, &d88_matrix_keypad_row_in, s, "row-in", s->num_rows);
    qdev_init_gpio_in_named_with_opaque(dev, &d88_matrix_keypad_col_in, s, "col-in", s->num_cols);
    qdev_init_gpio_out_named(dev, s->row_out, "row-out", s->num_rows);
    qdev_init_gpio_out_named(dev, s->col_out, "col-out", s->num_cols);

    s->row_col_map = g_new(uint32_t, s->num_rows);
    d88_matrix_keypad_reset(dev);

    qemu_input_handler_register(dev, &d88_matrix_keypad_handler);
}

static void d88_matrix_keypad_finalize(Object *obj)
{
    D88MatrixKeypad *s = D88_MATRIX_KEYPAD(obj);
    g_free(s->row_out);
    g_free(s->col_out);
}

static Property d88_matrix_keypad_properties[] = {
    DEFINE_PROP_UINT8("num-rows", D88MatrixKeypad, num_rows,  5),
    DEFINE_PROP_UINT8("num-cols", D88MatrixKeypad, num_cols, 13),
    DEFINE_PROP_END_OF_LIST(),
};

static void d88_matrix_keypad_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, d88_matrix_keypad_properties);
    dc->realize = d88_matrix_keypad_realize;
    dc->reset = d88_matrix_keypad_reset;
    //dc->vmsd = &vmstate_lm_kbd;
}

OBJECT_DEFINE_TYPE(D88MatrixKeypad, d88_matrix_keypad, D88_MATRIX_KEYPAD, DEVICE)

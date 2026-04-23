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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "ui/input.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/input/gpio_matrix_keypad.h"
#include "hw/gpio/ingenic_gpio.h"
#include "trace.h"

static void send_pin_state(GpioMatrixKeypad *s, GpioMatrixKeypadIO *io, int pin)
{
    uint32_t mask = 1ul << pin;
    int level = INGENIC_GPIO_LEVEL_FLOATING;
    if (~io->floating & io->pull & mask)
        level = (io->value & mask) ? INGENIC_GPIO_LEVEL_PULL_HIGH : INGENIC_GPIO_LEVEL_PULL_LOW;
    else if (~io->floating & ~io->pull & mask)
        level = (io->value & mask) ? INGENIC_GPIO_LEVEL_HIGH : INGENIC_GPIO_LEVEL_LOW;
    trace_gpio_matrix_keypad_out(io->name, pin, IngenicGpioLevel_str(level));
    qemu_set_irq(io->irq[pin], level);
}

static void gpio_matrix_keypad_reset(Object *obj, ResetType type)
{
    GpioMatrixKeypad *s = GPIO_MATRIX_KEYPAD(obj);
    for (int row = 0; row < s->row.num_pins; row++) {
        s->btn_hold_map[row] = 0;
        s->btn_press_map[row] = 0;
    }
    s->row.floating = ~s->row.ext_pull;
    s->row.pull = s->row.ext_pull;
    s->row.value = s->row.ext_pull_value;
    s->col.floating = ~s->col.ext_pull;
    s->col.pull = s->col.ext_pull;
    s->col.value = s->col.ext_pull_value;
    for (int pin = 0; pin < s->row.num_pins; pin++)
        send_pin_state(s, &s->row, pin);
    for (int pin = 0; pin < s->col.num_pins; pin++)
        send_pin_state(s, &s->col, pin);
}

static bool update_pin_state(GpioMatrixKeypad *s, GpioMatrixKeypadIO *io, int pin, int level)
{
    if (unlikely(pin >= io->num_pins)) {
        error_report(TYPE_GPIO_MATRIX_KEYPAD ": invalid %s pin %d, level=%s",
            io->name, pin, IngenicGpioLevel_str(level));
        return false;
    }

    // Find current pin level
    int mask = 1ul << pin;
    int cur_level = INGENIC_GPIO_LEVEL_FLOATING;
    if (~io->floating & io->pull & mask)
        cur_level = (io->value & mask) ? INGENIC_GPIO_LEVEL_PULL_HIGH : INGENIC_GPIO_LEVEL_PULL_LOW;
    else if (~io->floating & ~io->pull & mask)
        cur_level = (io->value & mask) ? INGENIC_GPIO_LEVEL_HIGH : INGENIC_GPIO_LEVEL_LOW;

    // Check against external pull resistors
    if (level == INGENIC_GPIO_LEVEL_FLOATING) {
        if (io->ext_pull & mask)
            level = (io->ext_pull_value & mask) ? INGENIC_GPIO_LEVEL_PULL_HIGH : INGENIC_GPIO_LEVEL_PULL_LOW;
    } else if (level == INGENIC_GPIO_LEVEL_PULL_HIGH || level == INGENIC_GPIO_LEVEL_PULL_LOW) {
        if (io->ext_pull & mask) {
            int ext_level = (io->ext_pull_value & mask) ? INGENIC_GPIO_LEVEL_PULL_HIGH : INGENIC_GPIO_LEVEL_PULL_LOW;
            if (unlikely(ext_level != level)) {
                error_report(TYPE_GPIO_MATRIX_KEYPAD ": %s pin %d pull=%d mismatch, ext_pull=%d",
                    io->name, pin, level, ext_level);
            }
        }
    }

    // Update pin state
    io->floating = (io->floating & ~mask) | (level == INGENIC_GPIO_LEVEL_FLOATING ? mask : 0);
    io->pull = (io->pull & ~mask) | (level == INGENIC_GPIO_LEVEL_PULL_HIGH ||
        level == INGENIC_GPIO_LEVEL_PULL_LOW ? mask : 0);
    io->value = (io->value & ~mask) | (level == INGENIC_GPIO_LEVEL_PULL_HIGH ||
        level == INGENIC_GPIO_LEVEL_HIGH ? mask : 0);

    // Send back IRQ level change for the same pin
    if (level != cur_level) {
        trace_gpio_matrix_keypad_out(io->name, pin, IngenicGpioLevel_str(level));
        qemu_set_irq(io->irq[pin], level);
        return true;
    }
    return false;
}

static void update_matrix_state(GpioMatrixKeypad *s, GpioMatrixKeypadIO *src, GpioMatrixKeypadIO *dst,
    bool src_is_row, bool active_poll)
{
    for (int idst = 0; idst < dst->num_pins; idst++) {
        // Skip strong-driven pins
        uint32_t dst_mask = 1ul << idst;
        if (dst_mask & ~dst->floating & ~dst->pull)
            continue;
        // Check for pressed buttons
        uint32_t src_btn_mask = 0;
        if (src_is_row) {
            for (int row = 0; row < src->num_pins; row++) {
                src_btn_mask |= (s->btn_hold_map[row] & dst_mask) ? (1ul << row) : 0;
                if (active_poll)
                    src_btn_mask |= (s->btn_press_map[row] & dst_mask) ? (1ul << row) : 0;
            }
        } else {
            src_btn_mask = s->btn_hold_map[idst];
            if (active_poll)
                src_btn_mask = s->btn_press_map[idst];
        }
        // Check for new output level
        int dst_level = INGENIC_GPIO_LEVEL_FLOATING;
        uint32_t src_strong = src_btn_mask & ~src->floating & ~src->pull;
        uint32_t src_pull = src_btn_mask & ~src->floating & src->pull;
        if (src_strong) {
            // Driven by button press and push-pull output
            if ((src_strong & src->value) == src_strong) {
                dst_level = INGENIC_GPIO_LEVEL_HIGH;
            } else if ((src_strong & src->value) == 0) {
                dst_level = INGENIC_GPIO_LEVEL_LOW;
            } else {
                dst_level = INGENIC_GPIO_LEVEL_LOW;
                error_report(TYPE_GPIO_MATRIX_KEYPAD ": %s short circuit floating=0x%x pull=0x%x value=0x%x btn=0x%x",
                    src->name, src->floating, src->pull, src->value, src_btn_mask);
            }
        } else if (src_pull) {
            // Driven by button press and pull resistor
            if ((src_pull & src->value) == src_pull) {
                dst_level = INGENIC_GPIO_LEVEL_PULL_HIGH;
            } else if ((src_pull & src->value) == 0) {
                dst_level = INGENIC_GPIO_LEVEL_PULL_LOW;
            } else {
                dst_level = INGENIC_GPIO_LEVEL_PULL_LOW;
                error_report(TYPE_GPIO_MATRIX_KEYPAD ": %s pull conflict floating=0x%x pull=0x%x value=0x%x btn=0x%x",
                    src->name, src->floating, src->pull, src->value, src_btn_mask);
            }
        } else if (dst_mask & ~dst->floating & dst->pull) {
            // Driven by internal pull resistor
            dst_level = (dst_mask & dst->value) ? INGENIC_GPIO_LEVEL_PULL_HIGH : INGENIC_GPIO_LEVEL_PULL_LOW;
        } else if (dst_mask & dst->ext_pull) {
            // Driven by external pull resistor
            dst_level = (dst_mask & dst->ext_pull_value) ? INGENIC_GPIO_LEVEL_PULL_HIGH : INGENIC_GPIO_LEVEL_PULL_LOW;
        }
        trace_gpio_matrix_keypad_out(dst->name, idst, IngenicGpioLevel_str(dst_level));
        qemu_set_irq(dst->irq[idst], dst_level);
    }
    // Clear button presses on strongly driven src lines
    if (active_poll) {
        uint32_t src_mask = (1ul << src->num_pins) - 1;
        uint32_t src_strong = src_mask & ~src->floating & ~src->pull;
        if (src_strong) {
            for (int isrc = 0; isrc < src->num_pins; isrc++) {
                if (src_strong & (1ul << isrc)) {
                    if (src_is_row) {
                        s->btn_press_map[isrc] = 0;
                    } else {
                        for (int row = 0; row < dst->num_pins; row++)
                            s->btn_press_map[row] &= ~(1ul << isrc);
                    }
                }
            }
        }
    }
}

static void gpio_matrix_keypad_row_in(void *opaque, int n, int level)
{
    GpioMatrixKeypad *s = GPIO_MATRIX_KEYPAD(opaque);
    trace_gpio_matrix_keypad_row_in(n, IngenicGpioLevel_str(level));
    if (update_pin_state(s, &s->row, n, level))
        update_matrix_state(s, &s->row, &s->col, true, true);
}

static void gpio_matrix_keypad_col_in(void *opaque, int n, int level)
{
    GpioMatrixKeypad *s = GPIO_MATRIX_KEYPAD(opaque);
    trace_gpio_matrix_keypad_col_in(n, IngenicGpioLevel_str(level));
    if (update_pin_state(s, &s->col, n, level))
        update_matrix_state(s, &s->col, &s->row, false, true);
}

static void gpio_matrix_keypad_event(DeviceState *dev, QemuConsole *src, InputEvent *evt)
{
    GpioMatrixKeypad *s = GPIO_MATRIX_KEYPAD(dev);
    int qcode = qemu_input_key_value_to_qcode(evt->u.key.data->key);
    int down = !!evt->u.key.data->down;

    // Map to matrix row & column IO
    int row, col;
    for (row = 0; row < s->row.num_pins; row++) {
        for (col = 0; col < s->col.num_pins; col++) {
            if (s->key_map[row * s->col.num_pins + col] == qcode)
                goto key_found;
        }
    }
    trace_gpio_matrix_keypad_event(QKeyCode_str(qcode), down, -1, -1);
    return;

key_found:
    {
        // Ignore auto-repeat
        uint32_t col_mask = 1ul << col;
        uint32_t value = s->btn_hold_map[row] & col_mask;
        if (!down == !value)
            return;
    }

    // Update IO outputs
    trace_gpio_matrix_keypad_event(QKeyCode_str(qcode), down, row, col);
    s->btn_hold_map[row] ^= 1ul << col;
    if (down)
        s->btn_press_map[row] |= 1ul << col;
    update_matrix_state(s, &s->row, &s->col, true, false);
    update_matrix_state(s, &s->col, &s->row, false, false);
}

static const QemuInputHandler gpio_matrix_keypad_handler = {
    .name  = "Gpio Matrix Keypad",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = &gpio_matrix_keypad_event,
};

static gchar *strip_string_quotes(gchar *str)
{
    g_strstrip(str);
    if (str[0] == '"') {
        size_t len = strlen(str);
        if (str[len - 1] == '"') {
            str[len - 1] = '\0';
            return str + 1;
        }
    }
    return str;
}

static void gpio_matrix_keypad_load_keymap_file(GpioMatrixKeypad *s, char *path)
{
    gchar *contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, NULL)) {
        warn_report(TYPE_GPIO_MATRIX_KEYPAD ": failed to load \"%s\"", path);
    } else {
        int row = 0;
        gchar **lines = g_strsplit_set(contents, "\r\n", 0);
        gchar **line = lines;
        int line_cnt = 1;
        while (*line != NULL) {
            gchar **line_no_comment = g_strsplit(*line, "#", 2);
            if (line_no_comment[0] != NULL && line_no_comment[0][0] != '\0') {
                gchar **kv = g_strsplit(line_no_comment[0], ":", 2);
                if (kv[0] != NULL && kv[1] != NULL) {
                    gchar *key = kv[0];
                    gchar *value = kv[1];
                    key = strip_string_quotes(key);
                    value = strip_string_quotes(value);

                    if (g_str_has_prefix(key, "row-")) {
                        int new_row = g_ascii_strtoull(key + 4, NULL, 0);
                        if (new_row >= s->row.num_pins) {
                            warn_report(TYPE_GPIO_MATRIX_KEYPAD ": %s:%d invalid row %d ignored",
                                path, line_cnt, new_row);
                        } else {
                            row = new_row;
                        }
                    } else if (g_str_has_prefix(key, "col-")) {
                        int col = g_ascii_strtoull(key + 4, NULL, 0);
                        Error *errp = NULL;
                        int key_code = qapi_enum_parse(&QKeyCode_lookup, value, Q_KEY_CODE_UNMAPPED, &errp);
                        if (errp) {
                            warn_report(TYPE_GPIO_MATRIX_KEYPAD ": %s:%d invalid key \"%s\" ignored",
                                path, line_cnt, value);
                        }
                        s->key_map[row * s->col.num_pins + col] = key_code;
                    }
                }
                g_strfreev(kv);
            }
            line += 1;
            line_cnt += 1;
            g_strfreev(line_no_comment);
        }
        g_strfreev(lines);
        g_free(contents);
    }

    for (int row = 0; row < s->row.num_pins; row++)
        for (int col = 0; col < s->col.num_pins; col++)
            trace_gpio_matrix_keypad_map(row, col, QKeyCode_str(s->key_map[row * s->col.num_pins + col]));
}

static void gpio_matrix_keypad_realize(DeviceState *dev, Error **errp)
{
    GpioMatrixKeypad *s = GPIO_MATRIX_KEYPAD(dev);
    s->row.irq = g_new(qemu_irq, s->row.num_pins);
    s->col.irq = g_new(qemu_irq, s->col.num_pins);
    qdev_init_gpio_in_named_with_opaque(dev, &gpio_matrix_keypad_row_in, s, "row-in", s->row.num_pins);
    qdev_init_gpio_in_named_with_opaque(dev, &gpio_matrix_keypad_col_in, s, "col-in", s->col.num_pins);
    qdev_init_gpio_out_named(dev, s->row.irq, "row-out", s->row.num_pins);
    qdev_init_gpio_out_named(dev, s->col.irq, "col-out", s->col.num_pins);

    // Read key map file
    s->key_map = g_new0(QKeyCode, s->row.num_pins * s->col.num_pins);
    if (!s->map_file)
        warn_report(TYPE_GPIO_MATRIX_KEYPAD ": map-file not set");
    else
        gpio_matrix_keypad_load_keymap_file(s, s->map_file);

    s->btn_hold_map = g_new0(uint32_t, s->row.num_pins);
    s->btn_press_map = g_new0(uint32_t, s->row.num_pins);
    gpio_matrix_keypad_reset(OBJECT(dev), RESET_TYPE_COLD);

    qemu_input_handler_register(dev, &gpio_matrix_keypad_handler);
}

OBJECT_DEFINE_TYPE(GpioMatrixKeypad, gpio_matrix_keypad, GPIO_MATRIX_KEYPAD, DEVICE)

static void gpio_matrix_keypad_init(Object *obj)
{
    GpioMatrixKeypad *s = GPIO_MATRIX_KEYPAD(obj);
    s->row.name = "row";
    s->col.name = "col";
}

static void gpio_matrix_keypad_finalize(Object *obj)
{
    GpioMatrixKeypad *s = GPIO_MATRIX_KEYPAD(obj);
    g_free(s->btn_press_map);
    g_free(s->btn_hold_map);
    g_free(s->key_map);
    g_free(s->col.irq);
    g_free(s->row.irq);
}

static const Property gpio_matrix_keypad_properties[] = {
    DEFINE_PROP_UINT8("num-rows", GpioMatrixKeypad, row.num_pins,  5),
    DEFINE_PROP_UINT8("num-cols", GpioMatrixKeypad, col.num_pins, 13),
    DEFINE_PROP_UINT32("row-pull", GpioMatrixKeypad, row.ext_pull, 0),
    DEFINE_PROP_UINT32("row-pull-value", GpioMatrixKeypad, row.ext_pull_value, 0),
    DEFINE_PROP_UINT32("col-pull", GpioMatrixKeypad, col.ext_pull, 0),
    DEFINE_PROP_UINT32("col-pull-value", GpioMatrixKeypad, col.ext_pull_value, 0),
    DEFINE_PROP_STRING("map-file", GpioMatrixKeypad, map_file),
};

static void gpio_matrix_keypad_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, gpio_matrix_keypad_properties);
    dc->realize = gpio_matrix_keypad_realize;
    // dc->vmsd = &vmstate_lm_kbd;

    GpioMatrixKeypadClass *kp_class = GPIO_MATRIX_KEYPAD_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       gpio_matrix_keypad_reset,
                                       NULL,
                                       NULL,
                                       &kp_class->parent_phases);
}

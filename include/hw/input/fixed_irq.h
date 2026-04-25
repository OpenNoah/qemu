/*
 * Fixed IRQ value, applies the same value after resets
 *
 * Copyright (c) 2026 Norman Zhi (normanzyb@gmail.com)
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

#ifndef HW_INPUT_FIXED_IRQ_H
#define HW_INPUT_FIXED_IRQ_H

#include "qom/object.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"

#define TYPE_FIXED_IRQ "fixed-irq"
OBJECT_DECLARE_TYPE(FixedIrq, FixedIrqClass, FIXED_IRQ)

typedef struct FixedIrq
{
    DeviceState parent_obj;
    int32_t irq_value;
    qemu_irq irq;
} FixedIrq;

typedef struct FixedIrqClass
{
    DeviceClass parent_class;
    ResettablePhases parent_phases;
} FixedIrqClass;

#endif // HW_INPUT_FIXED_IRQ_H

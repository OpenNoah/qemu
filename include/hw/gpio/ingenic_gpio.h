/*
 * Ingenic GPIO emulation.
 *
 * Copyright (C) 2024 Norman Zhi <normanzyb@gmail.com>
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

#ifndef INGENIC_GPIO_H
#define INGENIC_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_INGENIC_GPIO "ingenic-gpio"
OBJECT_DECLARE_TYPE(IngenicGpio, IngenicGpioClass, INGENIC_GPIO)

typedef struct IngenicGpioIrqData {
    IngenicGpio *gpio;
    uint32_t port;
} IngenicGpioIrqData;

typedef struct IngenicGpio {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mr;
    char *name;

    // Registers
    uint32_t pin;
    uint32_t dat;
    uint32_t im;
    uint32_t pe;
    uint32_t fun;
    uint32_t sel;
    uint32_t dir;
    uint32_t trg;
    uint32_t flg;

    // IRQs
    qemu_irq output[32];
} IngenicGpio;

typedef struct IngenicGpioClass
{
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
} IngenicGpioClass;

#endif /* INGENIC_GPIO_H */

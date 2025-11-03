/*
 * Ingenic External Memory Controller (SDRAM, NAND) emulation
 *
 * Copyright (c) 2024
 * Written by Norman Zhi <normanzyb@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"

#include "hw/qdev-properties.h"
#include "hw/block/ingenic_emc.h"

#define CMD_RESET   0xff

void qmp_stop(Error **errp);

static uint64_t ingenic_nand_io_read(void *opaque, hwaddr addr, unsigned size)
{
    //NandOpData *opdata = opaque;
    uint32_t data = 0;
    qemu_log("EMC NAND read @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx32"\n", addr, (uint32_t)size, data);
    qmp_stop(NULL);
    return data;

}

static void ingenic_nand_io_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    NandOpData *opdata = opaque;
    IngenicEmc *emc = opdata->emc;
    uint32_t bank = opdata->bank;

    (void)emc;
    (void)bank;

    qemu_log("EMC NAND write @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", addr, (uint32_t)size, data);

    // TODO Non-bus shared address
    if (addr >= 0x0c0000) {
        // Reserved
        qemu_log_mask(LOG_GUEST_ERROR, "EMC NAND write reserved address " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
        qmp_stop(NULL);

    } else if (addr >= 0x010000) {
        // Address space
        qmp_stop(NULL);

    } else if (addr >= 0x008000) {
        // Command space
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "EMC NAND command unaligned " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
            qmp_stop(NULL);
            return;
        }

        uint8_t cmd = data;
        switch (cmd) {
        case CMD_RESET:
            break;
        default:
            qmp_stop(NULL);
        }

    } else {
        // Data space
        qmp_stop(NULL);
    }
}

MemoryRegionOps nand_io_ops = {
    .read = ingenic_nand_io_read,
    .write = ingenic_nand_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

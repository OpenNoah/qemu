/*
 * Ingenic External Memory Controller NAND interface emulation
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
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"

#include "hw/qdev-properties.h"
#include "hw/block/ingenic_emc.h"

void qmp_stop(Error **errp);

// IO operations for IngenicEmc

#define CMD_READ        0x00
#define CMD_READ_2      0x30
#define CMD_READ_ID     0x90
#define CMD_RESET       0xff

static void nand_read_page(IngenicEmcNand *nand);

static uint64_t ingenic_nand_io_read(void *opaque, hwaddr addr, unsigned size)
{
    NandOpData *opdata = opaque;
    IngenicEmc *emc = opdata->emc;
    uint32_t bank = opdata->bank;
    IngenicEmcNand *nand = emc->nand[bank];
    uint64_t data = 0;

    if (!nand) {
        qemu_log_mask(LOG_GUEST_ERROR, "EMC NAND %"PRIu32" no media\n", bank);
        qmp_stop(NULL);
        return 0;
    }

    for (int i = 0; i < size; i++) {
        data |= (uint64_t)nand->page_buf[nand->page_ofs] << (8 * i);
        if (nand->page_ofs == nand->page_size + nand->oob_size) {
            qemu_log_mask(LOG_GUEST_ERROR, "EMC NAND %"PRIu32" read beyond page+oob size\n", bank);
            qmp_stop(NULL);
        } else {
            nand->page_ofs++;
        }
    }

    //qemu_log("EMC NAND %"PRIu32" read @ " HWADDR_FMT_plx "/%"PRIx32": 0x%08"PRIx64"\n",
    //         bank + 1, addr, (uint32_t)size, data);
    return data;
}

static void ingenic_nand_io_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    NandOpData *opdata = opaque;
    IngenicEmc *emc = opdata->emc;
    uint32_t bank = opdata->bank;
    IngenicEmcNand *nand = emc->nand[bank];

    //qemu_log("EMC NAND %"PRIu32" write @ " HWADDR_FMT_plx "/%"PRIx32": 0x%08"PRIx64"\n",
    //         bank + 1, addr, (uint32_t)size, data);

    if (!nand) {
        qemu_log_mask(LOG_GUEST_ERROR, "EMC NAND %"PRIu32" no media\n", bank);
        qmp_stop(NULL);
        return;
    }

    // TODO Non-bus shared address
    if (addr >= 0x0c0000) {
        // Reserved
        qemu_log_mask(LOG_GUEST_ERROR, "EMC NAND write reserved address " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
        qmp_stop(NULL);

    } else if (addr >= 0x010000) {
        // Address space
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "EMC NAND address unaligned " HWADDR_FMT_plx " 0x%"PRIx64"\n", addr, data);
            qmp_stop(NULL);
            return;
        }

        switch (nand->prev_cmd) {
        case CMD_READ:
            nand->addr |= data << (8 * nand->addr_ofs);
            nand->addr_ofs++;
            break;
        case CMD_READ_ID:
            nand->page_ofs = data;  // Maybe? expects data should be 0
            break;
        default:
            qmp_stop(NULL);
        }

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
            qemu_irq_lower(emc->io_nand_rb);
            qemu_irq_raise(emc->io_nand_rb);
            break;
        case CMD_READ:
            nand->addr = 0;
            nand->addr_ofs = 0;
            break;
        case CMD_READ_2:
            qemu_log("EMC NAND %"PRIu32" CMD_READ_2 @ 0x%"PRIx64"\n", bank + 1, nand->addr);
            qemu_irq_lower(emc->io_nand_rb);
            nand_read_page(nand);
            qemu_irq_raise(emc->io_nand_rb);
            break;
        case CMD_READ_ID:
            nand->page_ofs = 0;
            for (int i = 0; i < 8; i++)
                nand->page_buf[i] = nand->nand_id >> (8 * i);
            break;
        default:
            qmp_stop(NULL);
        }
        nand->prev_cmd = cmd;

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

// IngenicEmcNand Block

static void nand_read_page(IngenicEmcNand *nand)
{
    uint32_t row = nand->addr >> 16;
    nand->page_ofs = nand->addr % nand->page_size;
    if (blk_pread(nand->blk, row * (nand->page_size + nand->oob_size),
                  nand->page_size + nand->oob_size, nand->page_buf, 0) < 0) {
        printf("%s: read error at address 0x%"PRIx64"\n", __func__, nand->addr);
        qmp_stop(NULL);
        return;
    }
}

static void ingenic_emc_nand_realize(DeviceState *dev, Error **errp)
{
    qemu_log("%s enter\n", __func__);

    IngenicEmcNand *s = INGENIC_EMC_NAND(dev);

    if (!s->blk) {
        error_setg(errp, "drive property not set");
        return;
    }
    if (!blk_is_inserted(s->blk)) {
        error_setg(errp, "Device needs media, but drive is empty");
        return;
    }

    uint64_t size = blk_getlength(s->blk);
    if (size % (s->page_size + s->oob_size)) {
        error_setg(errp, "Drive size not aligned");
        return;
    }
    s->total_pages = size / (s->page_size + s->oob_size);

    Object *obj = object_resolve_path_type("", TYPE_INGENIC_EMC, NULL);
    if (!obj) {
        error_setg(errp, "ingenic-emc device not found");
        return;
    }

    IngenicEmc *emc = INGENIC_EMC(obj);
    if (s->cs < 1 || s->cs > 4) {
        error_setg(errp, "Invalid CS, 1~4 supported by ingenic-emc");
        return;
    }
    emc->nand[s->cs - 1] = s;

    if (!s->nand_id_str) {
        error_setg(errp, "nand-id not set");
        return;
    }
    s->nand_id = 0;
    const char *c = s->nand_id_str;
    qemu_log("EMC NAND ID:");
    for (int i = 0; i < 8 && *c != 0; i++) {
        uint8_t b = 0;
        for (int ib = 0; ib < 2; ib++) {
            uint8_t v = *c++;
            if (v >= '0' && v <= '9') {
                v = v - '0';
            } else if (v >= 'a' && v <= 'f') {
                v = v + 0x0a - 'a';
            } else if (v >= 'A' && v <= 'F') {
                v = v + 0x0a - 'A';
            } else {
                error_setg(errp, "nand-id not (aligned) hexadecimal");
                return;
            }
            b = (b << 4) | v;
        }
        qemu_log(" %02x", b);
        s->nand_id |= (uint64_t)b << (8 * i);
    }
    qemu_log("\n");

    s->page_buf = g_new(uint8_t, s->page_size + s->oob_size);
}

static void ingenic_emc_nand_unrealize(DeviceState *dev)
{
    qemu_log("%s enter\n", __func__);

    IngenicEmcNand *s = INGENIC_EMC_NAND(dev);
    g_free(s->page_buf);
}

static Property ingenic_emc_nand_properties[] = {
    DEFINE_PROP_DRIVE("drive", IngenicEmcNand, blk),
    DEFINE_PROP_UINT32("page-size", IngenicEmcNand, page_size, 2048),
    DEFINE_PROP_UINT32("oob-size", IngenicEmcNand, oob_size, 64),
    DEFINE_PROP_UINT32("cs", IngenicEmcNand, cs, 1),
    DEFINE_PROP_STRING("nand-id", IngenicEmcNand, nand_id_str),
    DEFINE_PROP_END_OF_LIST(),
};

static void ingenic_emc_nand_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, ingenic_emc_nand_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->realize = ingenic_emc_nand_realize;
    dc->unrealize = ingenic_emc_nand_unrealize;
}

static void ingenic_emc_nand_init(Object *obj)
{
    qemu_log("%s enter\n", __func__);
}

static void ingenic_emc_nand_finalize(Object *obj)
{
    qemu_log("%s enter\n", __func__);
}

OBJECT_DEFINE_TYPE(IngenicEmcNand, ingenic_emc_nand, INGENIC_EMC_NAND, DEVICE)

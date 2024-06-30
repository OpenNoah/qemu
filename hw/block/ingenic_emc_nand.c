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
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/block/ingenic_emc.h"
#include "trace.h"

#define CMD_READ            0x00
#define CMD_READ_NORMAL     0x30
#define CMD_READ_STATUS     0x70
#define CMD_PROGRAM         0x80
#define CMD_PROGRAM_PAGE    0x10
#define CMD_READ_ID         0x90
#define CMD_RESET           0xff

void qmp_stop(Error **errp);

static void nand_read_page(IngenicEmcNand *nand)
{
    uint64_t row = nand->addr >> 16;
    nand->page_ofs = nand->addr % (nand->page_size * 2);
    if (unlikely(blk_pread(nand->blk, row * (nand->page_size + nand->oob_size),
                           nand->page_size + nand->oob_size, nand->page_buf, 0) < 0)) {
        printf("%s: read error at address 0x%"PRIx64"\n", __func__, nand->addr);
        qmp_stop(NULL);
    }
}

static void nand_write_page(IngenicEmcNand *nand)
{
    uint64_t row = nand->addr >> 16;
    uint64_t page_ofs = nand->addr % (nand->page_size * 2);
    if (unlikely(blk_pwrite(nand->blk,
                            row * (nand->page_size + nand->oob_size) + page_ofs,
                            nand->page_ofs, nand->page_buf, 0) < 0)) {
        printf("%s: write error at address 0x%"PRIx64"\n", __func__, nand->addr);
        qmp_stop(NULL);
    }
}

static uint64_t ingenic_nand_io_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicEmcNand *nand = INGENIC_EMC_NAND(opaque);
    uint32_t bank = nand->cs;
    uint64_t data = 0;

    if (unlikely(!nand)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bank %"PRIu32" no media\n", __func__, bank);
        qmp_stop(NULL);
        return 0;
    }

    for (int i = 0; i < size; i++) {
        if (unlikely(nand->page_ofs >= nand->page_size + nand->oob_size)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bank %u read beyond page+oob size\n", __func__, bank);
            qmp_stop(NULL);
        } else {
            data |= (uint64_t)nand->page_buf[nand->page_ofs] << (8 * i);
            nand->page_ofs++;
        }
    }
    trace_ingenic_nand_read(addr, data);
    return data;
}

static void ingenic_nand_io_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    IngenicEmcNand *nand = INGENIC_EMC_NAND(opaque);
    uint32_t bank = nand->cs;
    IngenicEmc *emc = nand->emc;
    trace_ingenic_nand_write(addr, data);
    if (unlikely(!nand)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bank %u no media\n", __func__, bank);
        qmp_stop(NULL);
        return;
    }

    // TODO Non-bus shared address
    if (unlikely(emc->BCR & BIT(2))) {
        qemu_log_mask(LOG_UNIMP, "%s: TODO Non-bus shared\n", __func__);
        qmp_stop(NULL);
    }
    addr = addr % 0x00100000;

    if (addr >= 0x0c0000) {
        // Reserved
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Reserved address " HWADDR_FMT_plx " 0x%"PRIx64"\n", __func__, addr, data);
        qmp_stop(NULL);

    } else if (addr >= 0x010000) {
        // Address space
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Address unaligned " HWADDR_FMT_plx " 0x%"PRIx64"\n", __func__, addr, data);
            qmp_stop(NULL);
            return;
        }

        switch (nand->prev_cmd) {
        case CMD_READ:
        case CMD_PROGRAM:
            nand->addr |= data << (8 * nand->addr_ofs);
            nand->addr_ofs++;
            break;
        case CMD_READ_ID:
            nand->page_ofs = data;  // Maybe? expects data should be 0
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: Unknown command 0x%02"PRIx8"\n", __func__, nand->prev_cmd);
            qmp_stop(NULL);
        }

    } else if (addr >= 0x008000) {
        // Command space
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Command unaligned " HWADDR_FMT_plx " 0x%"PRIx64"\n", __func__, addr, data);
            qmp_stop(NULL);
            return;
        }

        uint8_t cmd = data;
        switch (cmd) {
        case CMD_RESET:
            trace_ingenic_nand_cmd(bank + 1, "CMD_RESET", 0);
            qemu_irq_lower(emc->io_nand_rb);
            qemu_irq_raise(emc->io_nand_rb);
            break;
        case CMD_READ:
            trace_ingenic_nand_cmd(bank + 1, "CMD_READ", 0);
            nand->addr = 0;
            nand->addr_ofs = 0;
            nand->page_ofs = 0;
            break;
        case CMD_READ_NORMAL:
            trace_ingenic_nand_cmd(bank + 1, "CMD_READ_NORMAL", nand->addr);
            if (nand->prev_cmd != CMD_READ) {
                qemu_log_mask(LOG_UNIMP, "%s: Unknown command 0x%02"PRIx8"\n", __func__, cmd);
                qmp_stop(NULL);
            }
            nand->status = nand->writable ? BIT(7) : 0;
            qemu_irq_lower(emc->io_nand_rb);
            nand_read_page(nand);
            nand->status |= BIT(6);
            qemu_irq_raise(emc->io_nand_rb);
            break;
        case CMD_PROGRAM:
            trace_ingenic_nand_cmd(bank + 1, "CMD_PROGRAM", 0);
            nand->addr = 0;
            nand->addr_ofs = 0;
            nand->page_ofs = 0;
            break;
        case CMD_PROGRAM_PAGE:
            trace_ingenic_nand_cmd(bank + 1, "CMD_PROGRAM_PAGE", nand->addr);
            if (nand->prev_cmd != CMD_PROGRAM) {
                qemu_log_mask(LOG_UNIMP, "%s: Unknown command 0x%02"PRIx8"\n", __func__, cmd);
                qmp_stop(NULL);
            }
            nand->status = nand->writable ? BIT(7) : 0;
            qemu_irq_lower(emc->io_nand_rb);
            nand_write_page(nand);
            nand->status |= BIT(6);
            qemu_irq_raise(emc->io_nand_rb);
            break;
        case CMD_READ_STATUS:
            trace_ingenic_nand_cmd(bank + 1, "CMD_READ_STATUS", nand->status);
            nand->page_ofs = 0;
            nand->page_buf[0] = nand->status;
            break;
        case CMD_READ_ID:
            trace_ingenic_nand_cmd(bank + 1, "CMD_READ_ID", nand->nand_id);
            nand->page_ofs = 0;
            for (int i = 0; i < 8; i++)
                nand->page_buf[i] = nand->nand_id >> (8 * i);
            break;
        default:
            trace_ingenic_nand_cmd(bank + 1, "CMD_UNKNOWN", cmd);
            qemu_log_mask(LOG_UNIMP, "%s: Unknown command 0x%02"PRIx8"\n", __func__, cmd);
            qmp_stop(NULL);
        }
        nand->prev_cmd = cmd;

    } else {
        // Data space
        for (int i = 0; i < size; i++) {
            if (unlikely(nand->page_ofs >= nand->page_size + nand->oob_size)) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bank %u write beyond page+oob size\n", __func__, bank);
                qmp_stop(NULL);
            } else {
                nand->page_buf[nand->page_ofs] = data >> (8 * i);
                nand->page_ofs++;
            }
        }
    }
}

static MemoryRegionOps nand_io_ops = {
    .read = ingenic_nand_io_read,
    .write = ingenic_nand_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_emc_nand_realize(DeviceState *dev, Error **errp)
{
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

    s->writable = blk_supports_write_perm(s->blk);
    if (s->writable) {
        blk_set_perm(s->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                     BLK_PERM_ALL, NULL);
        s->writable = blk_is_writable(s->blk);
    }

    // Register with EMC
    s->emc = ingenic_emc_register_nand(s, s->cs);

    if (!s->nand_id_str) {
        error_setg(errp, "nand-id not set");
        return;
    }
    s->nand_id = 0;
    const char *c = s->nand_id_str;
    //qemu_log("EMC NAND ID:");
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
        //qemu_log(" %02x", b);
        s->nand_id |= (uint64_t)b << (8 * i);
    }
    //qemu_log("\n");

    s->page_buf = g_new(uint8_t, s->page_size + s->oob_size);
}

static void ingenic_emc_nand_unrealize(DeviceState *dev)
{
    // Deregister with EMC
    IngenicEmcNand *s = INGENIC_EMC_NAND(dev);
    s->emc->nand[s->cs - 1] = NULL;
    g_free(s->page_buf);
}

static void ingenic_emc_nand_init(Object *obj)
{
    IngenicEmcNand *s = INGENIC_EMC_NAND(obj);
    memory_region_init_io(&s->mr, obj, &nand_io_ops, s, "emc.nand.io", 0x04000000);
}

static void ingenic_emc_nand_finalize(Object *obj)
{
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

OBJECT_DEFINE_TYPE(IngenicEmcNand, ingenic_emc_nand, INGENIC_EMC_NAND, DEVICE)

// ECC module

#define REG_NFECCR  0x00
#define REG_NFECC   0x04
#define REG_NFPAR0  0x08
#define REG_NFPAR1  0x0c
#define REG_NFPAR2  0x10
#define REG_NFINTS  0x14
#define REG_NFINTE  0x18
#define REG_NFERR0  0x1c
#define REG_NFERR1  0x20
#define REG_NFERR2  0x24
#define REG_NFERR3  0x28

static void ingenic_emc_nand_ecc_reset(Object *obj, ResetType type)
{
    IngenicEmcNandEcc *s = INGENIC_EMC_NAND_ECC(obj);
    s->reg.nfeccr = 0;
    s->reg.nfpar[0] = 0xdeadbeef;
    s->reg.nfpar[1] = 0x01234567;
    s->reg.nfpar[2] = 0x5a;
}

static uint64_t ingenic_emc_nand_ecc_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicEmcNandEcc *s = INGENIC_EMC_NAND_ECC(opaque);
    uint64_t value = 0;
    switch (addr) {
    case REG_NFECCR:
        value = s->reg.nfeccr;
        break;
    case REG_NFECC:
        value = s->reg.nfecc;
        break;
    case REG_NFPAR0:
    case REG_NFPAR1:
    case REG_NFPAR2:
        value = s->reg.nfpar[(addr - REG_NFPAR0) / 4];
        break;
    case REG_NFINTS:
        value = s->reg.nfints;
        break;
    case REG_NFINTE:
        value = s->reg.nfinte;
        break;
    case REG_NFERR0:
    case REG_NFERR1:
    case REG_NFERR2:
    case REG_NFERR3:
        value = s->reg.nferr[(addr - REG_NFERR0) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    trace_ingenic_nand_ecc_read(addr, value);
    return value;
}

static void ingenic_emc_nand_ecc_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IngenicEmcNandEcc *s = INGENIC_EMC_NAND_ECC(opaque);
    trace_ingenic_nand_ecc_write(addr, value);
    switch (addr) {
    case REG_NFECCR:
        if (value & BIT(1))
            ingenic_emc_nand_ecc_reset(OBJECT(s), 0);
        s->reg.nfeccr = value & 0x0d;
        if (!(value & BIT(3)) && (value & BIT(4))) {
            // Parity ready, decoding done
            s->reg.nfints |= BIT(4) | BIT(3);
        }
        if (value & BIT(3)) {
            qemu_log_mask(LOG_UNIMP, "%s: TODO Encoding\n", __func__);
            qmp_stop(NULL);
        }
        break;
    case REG_NFPAR0 + 0:
    case REG_NFPAR0 + 1:
    case REG_NFPAR0 + 2:
    case REG_NFPAR0 + 3:
    case REG_NFPAR1 + 0:
    case REG_NFPAR1 + 1:
    case REG_NFPAR1 + 2:
    case REG_NFPAR1 + 3:
    case REG_NFPAR2:
        // TODO ECC
        break;
    case REG_NFINTS:
        s->reg.nfints = s->reg.nfints & (0xe0000000 | (value & 0x1f));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, value);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps nand_ecc_ops = {
    .read = ingenic_emc_nand_ecc_read,
    .write = ingenic_emc_nand_ecc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_emc_nand_ecc_init(Object *obj)
{
    IngenicEmcNandEcc *s = INGENIC_EMC_NAND_ECC(obj);
    memory_region_init_io(&s->mr, obj, &nand_ecc_ops, s, "emc.nand.ecc", 0x40);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
}

static void ingenic_emc_nand_ecc_finalize(Object *obj)
{
}

static void ingenic_emc_nand_ecc_class_init(ObjectClass *class, void *data)
{
}

OBJECT_DEFINE_TYPE(IngenicEmcNandEcc, ingenic_emc_nand_ecc, INGENIC_EMC_NAND_ECC, SYS_BUS_DEVICE)

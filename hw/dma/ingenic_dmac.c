/*
 * Ingenic DMA Controller emulation model
 *
 * Copyright (c) 2024 Norman Zhi (normanzyb@gmail.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "migration/vmstate.h"
#include "block/aio.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/ssi/ingenic_msc.h"
#include "hw/dma/ingenic_dmac.h"
#include "trace.h"

#define MSC_RX_PASS_THROUGH 1
#define MSC_TX_PASS_THROUGH 1

#define REG_CH_DSA  0x00
#define REG_CH_DTA  0x04
#define REG_CH_DTC  0x08
#define REG_CH_DRT  0x0c
#define REG_CH_DCS  0x10
#define REG_CH_DCM  0x14
#define REG_CH_DDA  0x18
#define REG_CH_DSD  0xc0

#define REG_DMAC    0x00
#define REG_DIRQP   0x04
#define REG_DDR     0x08
#define REG_DDRS    0x0c
#define REG_DCKE    0x10

#define REQ_NAND    1
#define REQ_BCH_ENC 2
#define REQ_BCH_DEC 3
#define REQ_AUTO    8
#define REQ_MSC0_TX 26
#define REQ_MSC0_RX 27

void qmp_stop(Error **errp);

static void ingenic_dmac_reset(Object *obj, ResetType type)
{
    IngenicDmac *s = INGENIC_DMAC(obj);
    for (int dmac = 0; dmac < INGENIC_DMAC_NUM_DMAC; dmac++) {
        for (int ch = 0; ch < INGENIC_DMAC_NUM_CH; ch++) {
            s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
            s->reg[dmac].ch[ch].dsa = 0;
            s->reg[dmac].ch[ch].dta = 0;
            s->reg[dmac].ch[ch].dtc = 0;
            s->reg[dmac].ch[ch].drt = 0;
            s->reg[dmac].ch[ch].dcs = 0;
            s->reg[dmac].ch[ch].dcm = 0;
            s->reg[dmac].ch[ch].dda = 0;
            s->reg[dmac].ch[ch].dsd = 0;
        }
        s->reg[dmac].dmac  = 0;
        s->reg[dmac].dirqp = 0;
        s->reg[dmac].ddr   = 0;
        s->reg[dmac].dcke  = 0;
    }

    // Find MSC
    s->msc = INGENIC_MSC(object_resolve_path_type("", TYPE_INGENIC_MSC, NULL));
}

static void ingenic_dmac_update_irq(IngenicDmac *s, int dmac, int ch)
{
    uint8_t dirqp = s->reg[dmac].dirqp & ~BIT(ch);
    uint32_t dcs = s->reg[dmac].ch[ch].dcs;
    uint32_t dcm = s->reg[dmac].ch[ch].dcm;
    if (
        // Descriptor invalid
        ((dcm & BIT(2)) && (dcs & BIT(6))) |
        // Transfer interrupt
        //((dcm & BIT(1)) && (dcs & (BIT(3) | BIT(1)))) |
        ((dcm & BIT(1)) && (dcs & BIT(3))) |
        // Unmaskable errors
        (dcs & BIT(4)))
        dirqp |= 1 << ch;
    bool update = !(dirqp) != !(s->reg[dmac].dirqp);
    s->reg[dmac].dirqp = dirqp;
    if (update) {
        qemu_set_irq(s->irq[dmac], !!dirqp);
        trace_ingenic_dmac_interrupt(dmac, ch, !!dirqp);
    }
}

static void ingenic_dmac_channel_trigger(IngenicDmac *s, int dmac, int ch)
{
    trace_ingenic_dmac_start1(dmac, ch, s->reg[dmac].dmac,
        s->reg[dmac].ch[ch].dcs, s->reg[dmac].ch[ch].dcm, s->reg[dmac].ch[ch].drt);
    trace_ingenic_dmac_start2(dmac, ch,
        s->reg[dmac].ch[ch].dsa, s->reg[dmac].ch[ch].dta, s->reg[dmac].ch[ch].dtc,
        s->reg[dmac].ch[ch].dda, s->reg[dmac].ch[ch].dsd);

    uint32_t dcs   = s->reg[dmac].ch[ch].dcs;
    uint8_t  ndes  = (dcs >> 31) & 1;
    uint32_t dcm   = s->reg[dmac].ch[ch].dcm;
    uint8_t  blast = (dcm >> 25) & 1;
    uint8_t  sai   = (dcm >> 23) & 1;
    uint8_t  dai   = (dcm >> 22) & 1;
    uint8_t  sp    = (dcm >> 14) & 3;
    uint8_t  dp    = (dcm >> 12) & 3;
    uint8_t  tsz   = (dcm >>  8) & 7;
    uint8_t  inv   = (dcm >>  6) & 1;
    uint8_t  stde  = (dcm >>  5) & 1;
    //uint8_t  v     = (dcm >>  4) & 1;
    uint8_t  vm    = (dcm >>  3) & 1;
    uint8_t  link  = (dcm >>  0) & 1;

    if (inv) {
        trace_ingenic_dmac_terminate(dmac, ch, "INV");
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
        return;
    }

    if (stde) {
        qemu_log_mask(LOG_UNIMP, "%s: %u.%u TODO Stride mode\n", __func__, dmac, ch);
        qmp_stop(NULL);
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
        return;
    }

    // Decode transfer unit size
    static const uint8_t sdp_map[4] = {4, 1, 2, 0};
    uint8_t sp_b = sdp_map[sp];
    uint8_t dp_b = sdp_map[dp];
    static const uint8_t tsz_map[8] = {4, 1, 2, 16, 32, 0, 0, 0};
    uint8_t tsz_b = tsz_map[tsz];
    if (!sp_b || !dp_b || !tsz_b) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %u.%u Invalid size %u, %u, %u\n",
            __func__, dmac, ch, sp, dp, tsz);
        qmp_stop(NULL);
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
        return;
    }
    uint32_t dsa = s->reg[dmac].ch[ch].dsa;
    uint32_t dta = s->reg[dmac].ch[ch].dta;
    uint32_t size = s->reg[dmac].ch[ch].dtc * tsz_b;
    uint32_t avail = size;

    // Transfers
    uint32_t src     = dsa;
    uint8_t  src_b   = sp_b;
    bool     src_inc = sai;
    uint32_t dst     = dta;
    uint8_t  dst_b   = dp_b;
    bool     dst_inc = dai;
    uint8_t req = s->reg[dmac].ch[ch].drt;
    switch (req) {
    case REQ_AUTO:
    case REQ_NAND:
        break;
    case REQ_MSC0_TX:
    case REQ_MSC0_RX:
        avail = MIN(size, ingenic_msc_available(s->msc));
        break;
    case REQ_BCH_DEC:
        // DMA read data from memory pointed by DSAR0 and write to BCH data register BHDR
        dst     = 0x130d0010;
        dst_b   = 1;
        dst_inc = false;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: %u.%u TODO Unknown req type 0x%x\n", __func__, dmac, ch, req);
        qmp_stop(NULL);
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
        return;
    }

    // Continuous transfer, no need to wait
    trace_ingenic_dmac_transfer(dmac, ch, dst, src, avail);
    while (avail) {
        uint8_t buf[4096];
        uint32_t len = MIN(sizeof(buf), avail);
        avail -= len;
        size -= len;
        // Clear last u32 buffer word
        //*((uint32_t *)&buf[0] + (len % sizeof(buf)) / 4) = 0;
        // Read from source
        if (src_inc) {
            cpu_physical_memory_read(src, &buf[0], len);
            src += len;
#if MSC_RX_PASS_THROUGH
        } else if (req == REQ_MSC0_RX && src == 0x10021038) {
            // Fast pass-through for MSC RX
            len = ingenic_msc_sd_read(s->msc, buf, len);
#endif
        } else {
            uint8_t *pbuf = &buf[0];
            for (int32_t i = len; i > 0; i -= src_b) {
                cpu_physical_memory_read(src, pbuf, src_b);
                pbuf += src_b;
            }
        }
        // Write to target
        if (dst_inc) {
            cpu_physical_memory_write(dst, &buf[0], len);
            dst += len;
#if MSC_TX_PASS_THROUGH
        } else if (req == REQ_MSC0_TX && dst == 0x1002103c) {
            // Fast pass-through for MSC TX
            len = ingenic_msc_sd_write(s->msc, buf, len);
#endif
        } else {
            uint8_t *pbuf = &buf[0];
            for (int32_t i = len; i > 0; i -= dst_b) {
                cpu_physical_memory_write(dst, pbuf, dst_b);
                pbuf += dst_b;
            }
        }
    }

    // Update registers
    switch (req) {
    case REQ_AUTO:
    case REQ_NAND:
    case REQ_MSC0_TX:
    case REQ_MSC0_RX:
        s->reg[dmac].ch[ch].dtc = size / tsz_b;
        //s->reg[dmac].ch[ch].dsa = src;
        //s->reg[dmac].ch[ch].dta = dst;
        break;
    case REQ_BCH_DEC:
        s->reg[dmac].ch[ch].dtc = size / tsz_b;
        //s->reg[dmac].ch[ch].dsa = src;
        if (blast) {
            // after BCH decoding finishes, if there is error in the data block
            // DMA will write BHINT, BHERR0~3 (8-bit BCH) or BHERR0~1 (4-bit BCH)
            // to memory pointed by DTAR0
            // or if there is no error in the data block,
            // DMA will only write BHINT to memory,
            uint32_t bhcr;
            cpu_physical_memory_read(0x130d0000, &bhcr, 4);
            uint32_t n_err = 1 + (bhcr & BIT(2) ? 4 : 2);
            uint32_t buf[5];
            cpu_physical_memory_read(0x130d0024, &buf[0], 4 * n_err);
            if (buf[0] & BIT(0)) {
                // BCH error
                s->reg[dmac].ch[ch].dcs |= BIT(7);
            } else {
                // BCH no error
                n_err = 1;
                s->reg[dmac].ch[ch].dcs &= ~BIT(7);
            }
            cpu_physical_memory_write(dta, &buf[0], 4 * n_err);
            dta += 4 * n_err;
            // and then DMA clear BHINT and set BCH reset to BCH.
            uint32_t bhcr_reset = BIT(1);
            cpu_physical_memory_write(0x130d0004, &bhcr_reset, 4);
            //s->reg[dmac].ch[ch].dta = dta;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: %u.%u TODO Unknown req type 0x%x\n", __func__, dmac, ch, req);
        qmp_stop(NULL);
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
        return;
    }

    if (size != 0) {
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
        return;
    }

    // Transfer complete
    // TODO generate interrupts
    if (vm) {
        // If VM=1, clear V to 0
        s->reg[dmac].ch[ch].dcm &= ~BIT(4);
    }
    if (link) {
        // If LINK=1, set CT to 1
        s->reg[dmac].ch[ch].dcs |= BIT(1);
    } else {
        // Otherwise, set TT to 1
        s->reg[dmac].ch[ch].dcs |= BIT(3);
    }
    ingenic_dmac_update_irq(s, dmac, ch);
    if (ndes || !link) {
        // No follow-up descriptors
        trace_ingenic_dmac_terminate(dmac, ch, "END");
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
    } else {
        // Parse next descriptor
        trace_ingenic_dmac_terminate(dmac, ch, "LINK");
        s->dma[dmac].ch[ch].state = IngenicDmacChDesc;
        qemu_bh_schedule(s->trigger_bh);
    }
}

static void ingenic_dmac_parse_descriptor(IngenicDmac *s, int dmac, int ch, uint32_t addr, uint32_t nwords)
{
    uint32_t desc[8] = {0};
    s->reg[dmac].ddr &= ~BIT(ch);
    cpu_physical_memory_read(addr, &desc[0], 4 * nwords);
    trace_ingenic_dmac_desc(dmac, ch, nwords, addr, desc[0], desc[1], desc[2], desc[3], desc[4], desc[5]);

    // 4-word descriptor
    //uint8_t  eacks = (desc[0] >> 31) & 1;   // External DMA DACKn output polarity select
    //uint8_t  eackm = (desc[0] >> 30) & 1;   // External DMA DACKn output Mode select
    //uint8_t  erdm  = (desc[0] >> 28) & 3;   // External DMA request detection Mode
    //uint8_t  eopm  = (desc[0] >> 27) & 1;   // External DMA End of process mode
    //uint8_t  blast = (desc[0] >> 25) & 1;   // BCH Last (Only for BCH and Nand transfer)
    uint8_t  doa   = desc[3] >> 24;         // Descriptor Offset address
    //uint8_t  sai   = (desc[0] >> 23) & 1;   // Source Address Increment
    //uint8_t  dai   = (desc[0] >> 22) & 1;   // Target Address Increment
    //uint8_t  rdil  = (desc[0] >> 16) & 15;  // Request Detection Interval Length
    //uint8_t  sp    = (desc[0] >> 14) & 3;   // Source port width
    //uint8_t  dp    = (desc[0] >> 12) & 3;   // Target port width
    //uint8_t  tsz   = (desc[0] >>  8) & 7;   // Transfer Data Size
    //uint8_t  tm    = (desc[0] >>  7) & 1;   // Transfer Mode
    //uint8_t  stde  = (desc[0] >>  5) & 1;   // Stride transfer enable
    //uint8_t  v     = (desc[0] >>  4) & 1;   // Descriptor Valid
    //uint8_t  vm    = (desc[0] >>  3) & 1;   // Descriptor Valid Mode
    //uint8_t  vie   = (desc[0] >>  2) & 1;   // Descriptor Invalid Interrupt Enable
    //uint8_t  tie   = (desc[0] >>  1) & 1;   // Transfer Interrupt Enable
    //uint8_t  link  = (desc[0] >>  0) & 1;   // Descriptor Link Enable
    //uint32_t dsa   = desc[1];               // Source Address
    //uint32_t dta   = desc[2];               // Target Address
    uint32_t dtc   = desc[3] & 0x00ffffff;  // Transfer Counter
    // 8-word descriptor
    //uint16_t tsd   = desc[4] >> 16;         // Target Stride Address
    //uint16_t ssd   = desc[4] & 0xffff;      // Source Stride Address
    //uint8_t  drt   = desc[5]; & 0x3f;       // DMA Request Type

    // Parse descriptor valid
    uint8_t  v     = (desc[0] >>  4) & 1;   // Descriptor Valid
    uint8_t  vm    = (desc[0] >>  3) & 1;   // Descriptor Valid Mode
    if (vm == 0)
        v = 1;      // Ignored
    // Unknown fields?
    uint8_t  eopm  = (desc[0] >> 27) & 1;   // External DMA End of process mode
    uint8_t  tm    = (desc[0] >>  7) & 1;   // Transfer Mode
    if (eopm || tm) {
        qemu_log_mask(LOG_UNIMP, "%s: %u.%u Unknown DCM 0x%08x\n", __func__, dmac, ch, desc[0]);
        qmp_stop(NULL);
    }

    // Update register values
    // 4-word descriptor
    s->reg[dmac].ch[ch].dcs = (s->reg[dmac].ch[ch].dcs & 0xc000009f) |
        (((s->reg[dmac].ch[ch].dda >> 4) & 0xff) << 16) | ((!v) << 6);
    s->reg[dmac].ch[ch].dcm = desc[0] & 0xf2cff73f;
    s->reg[dmac].ch[ch].dsa = desc[1];
    s->reg[dmac].ch[ch].dta = desc[2];
    s->reg[dmac].ch[ch].dtc = dtc;
    s->reg[dmac].ch[ch].dda = (s->reg[dmac].ch[ch].dda & 0xfffff000) | (doa << 4);
    if (nwords >= 8) {
        // 8-word descriptor
        s->reg[dmac].ch[ch].dsd = desc[4];
        s->reg[dmac].ch[ch].drt = desc[5] & 0x3f;
    }

    // Update interrupts
    ingenic_dmac_update_irq(s, dmac, ch);
}

static void ingenic_dmac_wait_req(IngenicDmac *s, int dmac, int ch)
{
    bool v = !(s->reg[dmac].ch[ch].dcs & BIT(6));
    uint8_t req = s->reg[dmac].ch[ch].drt;
    switch (req) {
    case REQ_NAND:
        // Wait for request trigger
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
        break;
    case REQ_MSC0_TX:
    case REQ_MSC0_RX:
        // Wait for request trigger
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
        if (unlikely(!s->msc)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: MSC controller not found\n", __func__);
            qmp_stop(NULL);
        } else if (ingenic_msc_available(s->msc)) {
            // Data/space available, start immediately
            s->dma[dmac].ch[ch].state = IngenicDmacChTxfr;
        }
        break;
    case REQ_BCH_ENC:
    case REQ_BCH_DEC:
    case REQ_AUTO:
        // No request trigger, start immediately
        s->dma[dmac].ch[ch].state = v ? IngenicDmacChTxfr : IngenicDmacChIdle;
        break;
    default:
        s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
        qemu_log_mask(LOG_UNIMP, "%s: %u.%u TODO Unknown req type 0x%x\n", __func__, dmac, ch, req);
        qmp_stop(NULL);
        break;
    }
}

static void ingenic_dmac_trigger_bh(void *opaque)
{
    IngenicDmac *s = INGENIC_DMAC(opaque);
    for (int dmac = 0; dmac < INGENIC_DMAC_NUM_DMAC; dmac++) {
        for (int ch = 0; ch < INGENIC_DMAC_NUM_CH; ch++) {
            if (s->dma[dmac].ch[ch].state == IngenicDmacChDesc) {
                // Fetch descriptor
                uint32_t dcs = s->reg[dmac].ch[ch].dcs;
                if (!(dcs & BIT(31))) {
                    // Fetch descriptor
                    int nwords = dcs & BIT(30) ? 8 : 4;
                    uint32_t addr = s->reg[dmac].ch[ch].dda;
                    ingenic_dmac_parse_descriptor(s, dmac, ch, addr, nwords);
                }
                ingenic_dmac_wait_req(s, dmac, ch);
            }
            if (s->dma[dmac].ch[ch].state == IngenicDmacChTxfr)
                ingenic_dmac_channel_trigger(s, dmac, ch);
        }
    }
}

static int ingenic_dmac_channel_is_enabled(IngenicDmac *s, int dmac, int ch)
{
    // HLT, AR, DMAE
    if ((s->reg[dmac].dmac & (BIT(3) | BIT(2) | BIT(0))) != BIT(0))
        return 0;
    // INV, AR, TT, HLT, CTE
    if ((s->reg[dmac].ch[ch].dcs & 0x5d) != 1)
        return 0;
    return 1;
}

static void ingenic_dmac_channel_req_detect(IngenicDmac *s, int dmac, int ch, int req, int level)
{
    switch (req) {
    case REQ_NAND:
    case REQ_MSC0_RX:
        if (level) {
            // Trigger on rising edge
            s->dma[dmac].ch[ch].state = IngenicDmacChTxfr;
            qemu_bh_schedule(s->trigger_bh);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %u.%u Unknown DMA request %u\n", __func__, dmac, ch, req);
        qmp_stop(NULL);
        break;
    }
}

static void ingenic_dmac_req(void *opaque, int req, int level)
{
    // Send to relevant channels
    IngenicDmac *s = INGENIC_DMAC(opaque);
    for (int dmac = 0; dmac < INGENIC_DMAC_NUM_DMAC; dmac++)
        for (int ch = 0; ch < INGENIC_DMAC_NUM_CH; ch++)
            if (ingenic_dmac_channel_is_enabled(s, dmac, ch))
                if (s->reg[dmac].ch[ch].drt == req)
                    ingenic_dmac_channel_req_detect(s, dmac, ch, req, level);
}

static uint64_t ingenic_dmac_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicDmac *s = INGENIC_DMAC(opaque);
    uint64_t data = 0;
    uint32_t dmac = (addr / 0x0100) % INGENIC_DMAC_NUM_DMAC;
    uint32_t ch = (addr % 0x0100) / 0x20;
    switch (addr) {
    case 0x0000 ... 0x02ff:
        if (ch >= INGENIC_DMAC_NUM_CH) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid channel %u.%u\n", __func__, dmac, ch);
            qmp_stop(NULL);
        } else if ((addr & 0xff) >= 0xc0) {
            ch = (ch - 0xc0) / INGENIC_DMAC_NUM_CH;
            switch (addr & 0xe0) {
            case REG_CH_DSD & 0xe0:
                data = s->reg[dmac].ch[ch].dsd;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown CH address " HWADDR_FMT_plx "\n", __func__, addr);
                qmp_stop(NULL);
            }
        } else {
            switch (addr & 0x1f) {
            case REG_CH_DSA:
                data = s->reg[dmac].ch[ch].dsa;
                break;
            case REG_CH_DTA:
                data = s->reg[dmac].ch[ch].dta;
                break;
            case REG_CH_DTC:
                data = s->reg[dmac].ch[ch].dtc;
                break;
            case REG_CH_DRT:
                data = s->reg[dmac].ch[ch].drt;
                break;
            case REG_CH_DCS:
                data = s->reg[dmac].ch[ch].dcs;
                break;
            case REG_CH_DCM:
                data = s->reg[dmac].ch[ch].dcm;
                break;
            case REG_CH_DDA:
                data = s->reg[dmac].ch[ch].dda;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown CH address " HWADDR_FMT_plx "\n", __func__, addr);
                qmp_stop(NULL);
            }
        }
        break;
    case 0x0300 ... 0x04ff:
        dmac = ((addr - 0x300) / 0x100) % INGENIC_DMAC_NUM_DMAC;
        switch (addr & 0xff) {
        case REG_DMAC:
            data = s->reg[dmac].dmac;
            break;
        case REG_DIRQP:
            data = s->reg[dmac].dirqp;
            break;
        case REG_DDR:
            data = s->reg[dmac].ddr;
            break;
        case REG_DCKE:
            data = s->reg[dmac].dcke;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown DMAC address " HWADDR_FMT_plx "\n", __func__, addr);
            qmp_stop(NULL);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    trace_ingenic_dmac_read(addr, data);
    return data;
}

static void ingenic_dmac_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    IngenicDmac *s = INGENIC_DMAC(opaque);
    trace_ingenic_dmac_write(addr, data);
    uint32_t dmac = (addr / 0x0100) % INGENIC_DMAC_NUM_DMAC;
    uint32_t ch = (addr % 0x0100) / 0x20;
    switch (addr) {
    case 0x0000 ... 0x02ff:
        if (ch >= INGENIC_DMAC_NUM_CH) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid channel %u.%u\n", __func__, dmac, ch);
            qmp_stop(NULL);
        } else if ((addr & 0xff) >= 0xc0) {
            ch = (ch - 0xc0) / INGENIC_DMAC_NUM_CH;
            switch (addr & 0xe0) {
            case REG_CH_DSD & 0xe0:
                s->reg[dmac].ch[ch].dsd = data;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown CH address " HWADDR_FMT_plx "\n", __func__, addr);
                qmp_stop(NULL);
            }
        } else {
            switch (addr & 0x1f) {
            case REG_CH_DSA:
                s->reg[dmac].ch[ch].dsa = data;
                break;
            case REG_CH_DTA:
                s->reg[dmac].ch[ch].dta = data;
                break;
            case REG_CH_DTC:
                s->reg[dmac].ch[ch].dtc = data & 0x00ffffff;
                break;
            case REG_CH_DRT:
                s->reg[dmac].ch[ch].drt = data & 0x3f;
                break;
            case REG_CH_DCS:
                s->reg[dmac].ch[ch].dcs = data & 0xc0ff00df;
                ingenic_dmac_update_irq(s, dmac, ch);
                // Start DMA transfer
                if (ingenic_dmac_channel_is_enabled(s, dmac, ch)) {
                    s->dma[dmac].ch[ch].state = IngenicDmacChDesc;
                    qemu_bh_schedule(s->trigger_bh);
                } else {
                    s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
                }
                break;
            case REG_CH_DCM:
                s->reg[dmac].ch[ch].dcm = data & 0xf2cff73f;
                ingenic_dmac_update_irq(s, dmac, ch);
                break;
            case REG_CH_DDA:
                s->reg[dmac].ch[ch].dda = data & 0xfffffff0;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown CH address " HWADDR_FMT_plx "\n", __func__, addr);
                qmp_stop(NULL);
            }
        }
        break;
    case 0x0300 ... 0x04ff:
        dmac = ((addr - 0x300) / 0x100) % INGENIC_DMAC_NUM_DMAC;
        switch (addr & 0xff) {
        case REG_DMAC:
            s->reg[dmac].dmac = data & 0xf800030d;
            if (data & (BIT(2) | BIT(3))) {
                qemu_log_mask(LOG_UNIMP, "%s: TODO DMA %u halted\n", __func__, dmac);
                qmp_stop(NULL);
            }
            // Start DMA transfer
            for (int ch = 0; ch < INGENIC_DMAC_NUM_CH; ch++) {
                if (ingenic_dmac_channel_is_enabled(s, dmac, ch)) {
                    s->dma[dmac].ch[ch].state = IngenicDmacChDesc;
                    qemu_bh_schedule(s->trigger_bh);
                } else {
                    s->dma[dmac].ch[ch].state = IngenicDmacChIdle;
                }
            }
            break;
        case REG_DIRQP:
            break;
        case REG_DDRS:
            s->reg[dmac].ddr |= data & 0x0f;
            break;
        case REG_DCKE:
            s->reg[dmac].dcke = data & 0x0f;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown DMAC address " HWADDR_FMT_plx "\n", __func__, addr);
            qmp_stop(NULL);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps dmac_ops = {
    .read = ingenic_dmac_read,
    .write = ingenic_dmac_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_dmac_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    IngenicDmac *s = INGENIC_DMAC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &dmac_ops, s, "dmac", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    qdev_init_gpio_in_named_with_opaque(dev, &ingenic_dmac_req, s, "req-in", 64);
    qdev_init_gpio_out_named(dev, &s->irq[0], "irq-out", INGENIC_DMAC_NUM_DMAC);
    // To avoid re-entrancy, defer DMA triggers to main loop
    s->trigger_bh = aio_bh_new(qemu_get_aio_context(), &ingenic_dmac_trigger_bh, s);
}

static void ingenic_dmac_finalize(Object *obj)
{
}

static Property ingenic_dmac_properties[] = {
    DEFINE_PROP_UINT32("model", IngenicDmac, model, 0x4755),
    DEFINE_PROP_END_OF_LIST(),
};

static void ingenic_dmac_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, ingenic_dmac_properties);

    IngenicDmacClass *bch_class = INGENIC_DMAC_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_dmac_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicDmac, ingenic_dmac, INGENIC_DMAC, SYS_BUS_DEVICE)

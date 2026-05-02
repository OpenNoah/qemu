/*
 * Ingenic AIC emulation model
 *
 * Copyright (c) 2024 Norman Zhi <normanzyb@gmail.com>
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
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/audio/ingenic_aic.h"
#include "trace.h"

typedef enum IngenicAicReg {
    REG_AICFR  = 0x00,
    REG_AICCR  = 0x04,
    REG_ACCR1  = 0x08,
    REG_ACCR2  = 0x0c,
    REG_I2SCR  = 0x10,
    REG_AICSR  = 0x14,
    REG_ACSR   = 0x18,
    REG_I2SSR  = 0x1c,
    REG_ACCAR  = 0x20,
    REG_ACCDR  = 0x24,
    REG_ACSAR  = 0x28,
    REG_ACSDR  = 0x2c,
    REG_I2SDIV = 0x30,
    REG_AICDR  = 0x34,
    // 4740
    REG_CDCCR1 = 0x80,
    REG_CDCCR2 = 0x84,
    // 4750, 4755
    REG_CKCFG  = 0xa0,
    REG_RGADW  = 0xa4,
    REG_RGDATA = 0xa8,
} IngenicAicReg;

void qmp_stop(Error **errp);

static void ingenic_aic_reset(Object *obj, ResetType type)
{
    IngenicAic *s = INGENIC_AIC(obj);
    if (type != -1) {
        s->reg.aicfr  = 0x7800;
        s->reg.i2sdiv = 0x03;
    }
    s->reg.aiccr = 0x00240000;
    s->reg.i2scr = 0;
    s->reg.aicsr = BIT(3);
    s->reg.cdccr1 = 0x001b2302;
    s->reg.cdccr2 = 0x00170803;

    audio_be_set_active_out(s->audio_be, s->voice.out, false);

    s->srate = 0;
    s->out.bitw = 0;
    s->out.fifo_rptr = 0;
    s->out.fifo_wptr = 0;
}

uint32_t ingenic_aic_dma_tx_available(IngenicAic *s)
{
    const uint32_t fifo_size = ARRAY_SIZE(s->out.fifo);
    return (fifo_size - 1 - s->out.fifo_wptr + s->out.fifo_rptr) % fifo_size;
}

static void ingenic_aic_play_sample(IngenicAic *s, uint32_t v)
{
    int32_t smp = v << (32 - s->out.bitw);
    // smp /= 1 << s->out.bitw;
    s->out.fifo[s->out.fifo_wptr] = smp;
    s->out.fifo_wptr = (s->out.fifo_wptr + 1) % ARRAY_SIZE(s->out.fifo);
}

static void ingenic_aic_pcm_out_cb(void *opaque, int available)
{
    IngenicAic *s = INGENIC_AIC(opaque);
    int32_t buffer[ARRAY_SIZE(s->out.fifo)];
    int size = 0;
    while (s->out.fifo_rptr != s->out.fifo_wptr && size < available) {
        buffer[size / 4] = s->out.fifo[s->out.fifo_rptr];
        size += 4;
        s->out.fifo_rptr = (s->out.fifo_rptr + 1) % ARRAY_SIZE(s->out.fifo);
    }
    audio_be_write(s->audio_be, s->voice.out, &buffer[0], size);
    if (size) {
        trace_ingenic_aic_pcm_sample(available, size);
        qemu_irq_raise(s->out.dma_req);
    }
}

static void ingenic_aic_aiccr_update(IngenicAic *s, uint32_t data)
{
    int out_bps = (data >> 19) & 7;
    int in_bps  = (data >> 16) & 7;
    static const int bps_lut[] = {8, 16, 18, 20, 24};
    in_bps  = in_bps >= 5 ? 0 : bps_lut[in_bps];
    out_bps = out_bps >= 5 ? 0 : bps_lut[out_bps];
    s->out.bitw  = out_bps;

    int mono = (data >> 11) & 1;
    int play = (data >>  1) & 1;
    int rec  = (data >>  0) & 1;

    if (mono || rec || data & 0x0e00) {
        qemu_log_mask(LOG_UNIMP, "%s: TODO 0x%x\n", __func__, data);
        qmp_stop(NULL);
    }

    trace_ingenic_aic_aiccr(data, out_bps, in_bps, mono, play, rec);
    s->reg.aiccr = data & 0x003fce7f;

    if (data & (1 << 8)) {
        // FIFO flush
        audio_be_set_active_out(s->audio_be, s->voice.out, false);
        s->out.fifo_rptr = 0;
        s->out.fifo_wptr = 0;
    }

    if (play) {
        if (!audio_be_is_active_out(s->audio_be, s->voice.out)) {
            audsettings as;
            as.nchannels = MIN(AUDIO_MAX_CHANNELS, 2);
            as.fmt = AUDIO_FORMAT_S32;
            as.freq = s->srate;
            as.big_endian = false;
            s->voice.out = audio_be_open_out(s->audio_be, s->voice.out,
                            TYPE_INGENIC_AIC ".out", s, &ingenic_aic_pcm_out_cb, &as);
            audio_be_set_volume_out_lr(s->audio_be, s->voice.out, 0, 255, 255);
            audio_be_set_active_out(s->audio_be, s->voice.out, true);
        }
    } else {
        if (audio_be_is_active_out(s->audio_be, s->voice.out)) {
            audio_be_set_active_out(s->audio_be, s->voice.out, false);
        }
    }
}

static void ingenic_aic_cdccr2_update(IngenicAic *s, uint32_t data)
{
    int srate = (data >> 8) & 0x0f;
    if (srate >= 9) {
        srate = 0;
    } else {
        static const int srate_lut[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
        srate = srate_lut[srate];
    }
    s->srate = srate;

    int ain_gain_x10 = (data >> 16) & 0x1f;
    ain_gain_x10 = ain_gain_x10 * 15 - 345;

    static const int hp_gain_lut[] = {0, 2, 4, 6};
    int hp_gain = hp_gain_lut[data & 3];

    static const int mic_gain_lut[] = {0, 6, 12, 20};
    int mic_gain = mic_gain_lut[(data >> 4) & 3];

    trace_ingenic_aic_cdccr2(data, srate, hp_gain, ain_gain_x10, mic_gain);
    s->reg.cdccr2 = data & 0x001f0f33;
}

static uint64_t ingenic_aic_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicAic *s = INGENIC_AIC(opaque);
    uint64_t data = 0;
    switch (addr) {
    case REG_AICFR:
        data = s->reg.aicfr;
        break;
    case REG_AICCR:
        data = s->reg.aiccr;
        break;
    case REG_I2SCR:
        data = s->reg.i2scr;
        break;
    case REG_AICSR:
        data = s->reg.aicsr;
        break;
    case REG_I2SDIV:
        data = s->reg.i2sdiv;
        break;
    case REG_CDCCR1:
        data = s->reg.cdccr1;
        break;
    case REG_CDCCR2:
        data = s->reg.cdccr2;
        break;

    // Internal CODEC
    case REG_RGADW:
        qemu_log_mask(LOG_UNIMP, "%s: CODEC not implemented\n", __func__);
        return data;
    case REG_RGDATA:
        qemu_log_mask(LOG_UNIMP, "%s: CODEC not implemented\n", __func__);
        return data;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    trace_ingenic_aic_read(addr, data);
    return data;
}

static void ingenic_aic_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    trace_ingenic_aic_write(addr, data);
    IngenicAic *s = INGENIC_AIC(opaque);
    switch (addr) {
    case REG_AICFR:
        if (data & BIT(3))
            ingenic_aic_reset(opaque, -1);
        s->reg.aicfr = data & 0xff77;
        break;
    case REG_AICCR:
        ingenic_aic_aiccr_update(s, data);
        break;
    case REG_I2SCR:
        s->reg.i2scr = data & 0x1011;
        break;
    case REG_I2SDIV:
        s->reg.i2sdiv = data & 0x0f;
        break;
    case REG_AICDR:
        ingenic_aic_play_sample(s, data);
        break;
    case REG_CDCCR1:
        s->reg.cdccr1 = data & 0x3f1f7f03;
        break;
    case REG_CDCCR2:
        ingenic_aic_cdccr2_update(s, data);
        break;

    // Internal CODEC
    case REG_RGADW:
        qemu_log_mask(LOG_UNIMP, "%s: CODEC not implemented\n", __func__);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps adc_ops = {
    .read = ingenic_aic_read,
    .write = ingenic_aic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

OBJECT_DEFINE_TYPE(IngenicAic, ingenic_aic, INGENIC_AIC, SYS_BUS_DEVICE)

static void ingenic_aic_init(Object *obj)
{
    IngenicAic *s = INGENIC_AIC(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &adc_ops, s, "adc", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    qdev_init_gpio_out_named(DEVICE(obj), &s->out.dma_req, "dma-tx-req", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->in.dma_req, "dma-rx-req", 1);
}

static void ingenic_aic_realize(DeviceState *dev, Error **errp)
{
    IngenicAic *s = INGENIC_AIC(dev);
    if (!audio_be_check(&s->audio_be, errp)) {
        return;
    }
}

static void ingenic_aic_finalize(Object *obj)
{
}

static const Property ingenic_aic_properties[] = {
    DEFINE_PROP_UINT32("model", IngenicAic, model, 0x4755),
    DEFINE_AUDIO_PROPERTIES(IngenicAic, audio_be),
};

static void ingenic_aic_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    device_class_set_props(dc, ingenic_aic_properties);
    dc->realize = &ingenic_aic_realize;

    ResettableClass *rc = RESETTABLE_CLASS(class);
    rc->phases.enter = &ingenic_aic_reset;
}

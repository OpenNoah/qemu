/*
 * Ingenic LCD Controller emulation model
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
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "framebuffer.h"
#include "ui/pixel_ops.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "hw/display/ingenic_lcd.h"
#include "trace.h"

#define REG_LCDCFG      0x0000
#define REG_LCDVSYNC    0x0004
#define REG_LCDHSYNC    0x0008
#define REG_LCDVAT      0x000C
#define REG_LCDDAH      0x0010
#define REG_LCDDAV      0x0014
#define REG_LCDPS       0x0018
#define REG_LCDCLS      0x001C
#define REG_LCDSPL      0x0020
#define REG_LCDREV      0x0024
#define REG_LCDCTRL     0x0030
#define REG_LCDSTATE    0x0034
#define REG_LCDIID      0x0038
#define REG_LCDDA0      0x0040
#define REG_LCDSA0      0x0044
#define REG_LCDFID0     0x0048
#define REG_LCDCMD0     0x004C
#define REG_LCDDA1      0x0050
#define REG_LCDSA1      0x0054
#define REG_LCDFID1     0x0058
#define REG_LCDCMD1     0x005C
#define REG_LCDOFFS0    0x0060
#define REG_LCDPW0      0x0064
#define REG_LCDCNUM0    0x0068
#define REG_LCDDESSIZE0 0x006C
#define REG_LCDOFFS1    0x0070
#define REG_LCDPW1      0x0074
#define REG_LCDCNUM1    0x0078
#define REG_LCDDESSIZE1 0x007C
#define REG_LCDRGBC     0x0090
#define REG_LCDOSDC     0x0100
#define REG_LCDOSDCTRL  0x0104
#define REG_LCDOSDS     0x0108
#define REG_LCDBGC      0x010C
#define REG_LCDKEY0     0x0110
#define REG_LCDKEY1     0x0114
#define REG_LCDALPHA    0x0118
#define REG_LCDIPUR     0x011C
#define REG_LCDXYP0     0x0120
#define REG_LCDXYP1     0x0124
#define REG_LCDSIZE0    0x0128
#define REG_LCDSIZE1    0x012C

void qmp_stop(Error **errp);

static void ingenic_lcd_update_irq(IngenicLcd *s)
{
    bool irq = !!(s->lcdstate & (s->lcdctrl >> 8) & 0x3f);
    irq |= !!(s->lcdstate & s->lcdctrl & BIT(7));
    qemu_set_irq(s->irq, irq);
}

static void draw_row(void *opaque, uint8_t *dst, const uint8_t *src,
                     int width, int deststep)
{
    IngenicLcd *s = INGENIC_LCD(opaque);
    DisplaySurface *surface = qemu_console_surface(s->con);
    int bpp = surface_bits_per_pixel(surface);

    while (width--) {
        uint32_t tmp;
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        switch (s->mode) {
        case 565:
            tmp = *(uint16_t *)src;
            src += 2;
            r = (tmp >> 8) & 0xf8;
            g = (tmp >> 3) & 0xfc;
            b = (tmp << 3) & 0xf8;
            break;
        case 666:
            b = *src++ & 0xfc;
            g = *src++ & 0xfc;
            r = *src++ & 0xfc;
            src++;
            break;
        case 888:
            b = *src++;
            g = *src++;
            r = *src++;
            src++;
            break;
        }

        switch (bpp) {
        case 8:
            *dst++ = rgb_to_pixel8(r, g, b);
            break;
        case 15:
            *(uint16_t *)dst = rgb_to_pixel15(r, g, b);
            dst += 2;
            break;
        case 16:
            *(uint16_t *)dst = rgb_to_pixel16(r, g, b);
            dst += 2;
            break;
        case 24:
            tmp = rgb_to_pixel24(r, g, b);
            *dst++ = (tmp >>  0) & 0xff;
            *dst++ = (tmp >>  8) & 0xff;
            *dst++ = (tmp >> 16) & 0xff;
            break;
        case 32:
            *(uint32_t *)dst = rgb_to_pixel32(r, g, b);
            dst += 4;
            break;
        }
    }
}

static void ingenic_lcd_update_display(void *opaque)
{
    IngenicLcd *s = INGENIC_LCD(opaque);
    DisplaySurface *surface = qemu_console_surface(s->con);

    uint32_t src_width = 0;
    switch (s->mode) {
    case 565:
        src_width = s->xres * 2;
        break;
    case 666:
    case 888:
        src_width = s->xres * 4;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad source color depth\n", __func__);
        return;
    }

    uint32_t dest_width = 0;
    switch (surface_bits_per_pixel(surface)) {
    case 8:
        dest_width = s->xres;
        break;
    case 15:
    case 16:
        dest_width = s->xres * 2;
        break;
    case 24:
        dest_width = s->xres * 3;
        break;
    case 32:
        dest_width = s->xres * 4;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad surface color depth\n", __func__);
        return;
    }

    // Find a framebuffer from descriptor chain
    for (;;) {
        uint32_t idesc = 0;
        uint32_t da = s->desc[idesc].lcdda;
        uint32_t desc[8];
        uint32_t nwords = s->lcdcfg & BIT(28) ? 8 : 4;
        cpu_physical_memory_read(da, &desc[0], 4 * nwords);
        s->desc[idesc].lcdda  = desc[0];
        s->desc[idesc].lcdsa  = desc[1];
        s->desc[idesc].lcdfid = desc[2];
        s->desc[idesc].lcdcmd = desc[3];
        if (nwords == 8) {
            s->desc[idesc].lcdoffs    = desc[4];
            s->desc[idesc].lcdpw      = desc[5];
            s->desc[idesc].lcdcnum    = desc[6];
            s->desc[idesc].lcddessize = desc[7];
        }

        if (s->desc[idesc].lcdcmd & 0xf0000000) {
            qemu_log_mask(LOG_UNIMP, "%s: Unsupported CMD 0x%"PRIx32"\n",
                          __func__, s->desc[idesc].lcdcmd);
            qmp_stop(NULL);
            continue;
        } else if (s->lcdcfg & BIT(28)) {
            uint32_t xres = s->desc[idesc].lcddessize & 0xffff;
            uint32_t yres = s->desc[idesc].lcddessize >> 16;
            if (xres != s->xres || yres != s->yres) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Descriptor size mismatch 0x%"PRIx32"\n",
                              __func__, s->desc[idesc].lcddessize);
                qmp_stop(NULL);
                continue;
            }
        }

        trace_ingenic_lcd_desc(s->desc[idesc].lcdda,   s->desc[idesc].lcdsa,
                               s->desc[idesc].lcdfid,  s->desc[idesc].lcdcmd,
                               s->desc[idesc].lcdoffs, s->desc[idesc].lcdpw,
                               s->desc[idesc].lcdcnum, s->desc[idesc].lcddessize);

        framebuffer_update_memory_section(&s->fbsection, get_system_memory(),
                                          s->desc[idesc].lcdsa,
                                          s->yres, src_width);
        break;
    }

    int first = 0, last = 0;
    framebuffer_update_display(surface, &s->fbsection,
                               s->xres, s->yres,
                               src_width, dest_width, 0, s->invalidate,
                               &draw_row, s, &first, &last);

    if (first >= 0)
        dpy_gfx_update(s->con, 0, first, s->xres, last - first + 1);

    s->invalidate = false;
    s->lcdstate |= BIT(4) | BIT(5);     // Frame start & end flags
    ingenic_lcd_update_irq(s);
}

static void ingenic_lcd_invalidate_display(void * opaque)
{
    IngenicLcd *s = INGENIC_LCD(opaque);
    s->invalidate = true;
}

static const GraphicHwOps fb_ops = {
    .invalidate = ingenic_lcd_invalidate_display,
    .gfx_update = ingenic_lcd_update_display,
};

static void ingenic_lcd_enable(IngenicLcd *s, bool en)
{
    trace_ingenic_lcd_enable(en);

    if (!en) {
        // LCD controller disabled
        s->mode = 0;
        return;
    }

    // LCD controller enabled
    // Get display parameters from registers
    s->xres = (s->lcddah & 0xffff) - (s->lcddah >> 16);
    s->yres = (s->lcddav & 0xffff) - (s->lcddav >> 16);
    s->mode = 0;
    if ((s->lcdcfg & 0x0f) == 0) {
        if (s->lcdcfg & BIT(6))
            s->mode = 888;
        else if (s->lcdcfg & BIT(7))
            s->mode = 666;
        else
            s->mode = 565;
    }

    // TODO: Check OSD mode
    switch (s->lcdctrl & 7) {
    case 0b100:
        s->mode = 565;
        break;
    case 0b101:
        s->mode = 888;
        break;
    default:
        s->mode = 0;
        break;
    }

    trace_ingenic_lcd_mode(s->xres, s->yres, s->mode);

    if (!s->xres || !s->yres || !s->mode) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported configuration\n", __func__);
        qmp_stop(NULL);
        return;
    }

    qemu_console_resize(s->con, s->xres, s->yres);
}

static void ingenic_lcd_reset(Object *obj, ResetType type)
{
    IngenicLcd *s = INGENIC_LCD(obj);
    (void)s;
    // TODO Initial values
}

static uint64_t ingenic_lcd_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicLcd *s = INGENIC_LCD(opaque);
    uint64_t data = 0;
    switch (addr) {
    case REG_LCDCFG:
        data = s->lcdcfg;
        break;
    case REG_LCDVSYNC:
        data = s->lcdvsync;
        break;
    case REG_LCDHSYNC:
        data = s->lcdhsync;
        break;
    case REG_LCDVAT:
        data = s->lcdvat;
        break;
    case REG_LCDDAH:
        data = s->lcddah;
        break;
    case REG_LCDDAV:
        data = s->lcddav;
        break;
    case REG_LCDCTRL:
        data = s->lcdctrl;
        break;
    case REG_LCDSTATE:
        data = s->lcdstate;
        break;
    case REG_LCDDA0:
        data = s->desc[0].lcdda;
        break;
    case REG_LCDSA0:
        data = s->desc[0].lcdsa;
        break;
    case REG_LCDFID0:
        data = s->desc[0].lcdfid;
        break;
    case REG_LCDCMD0:
        data = s->desc[0].lcdcmd;
        break;
    case REG_LCDDA1:
        data = s->desc[1].lcdda;
        break;
    case REG_LCDSA1:
        data = s->desc[1].lcdsa;
        break;
    case REG_LCDFID1:
        data = s->desc[1].lcdfid;
        break;
    case REG_LCDCMD1:
        data = s->desc[1].lcdcmd;
        break;
    case REG_LCDRGBC:
        data = s->lcdrgbc;
        break;
    case REG_LCDOSDC:
        data = s->lcdosdc;
        break;
    case REG_LCDOSDCTRL:
        data = s->lcdosdctrl;
        break;
    case REG_LCDBGC:
        data = s->lcdbgc;
        break;
    case REG_LCDKEY0:
        data = s->fg[0].lcdkey;
        break;
    case REG_LCDKEY1:
        data = s->fg[1].lcdkey;
        break;
    case REG_LCDALPHA:
        data = s->lcdalpha;
        break;
    case REG_LCDIPUR:
        data = s->lcdipur;
        break;
    case REG_LCDXYP0:
        data = s->fg[0].lcdxyp;
        break;
    case REG_LCDXYP1:
        data = s->fg[1].lcdxyp;
        break;
    case REG_LCDSIZE0:
        data = s->fg[0].lcdsize;
        break;
    case REG_LCDSIZE1:
        data = s->fg[1].lcdsize;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }
    trace_ingenic_lcd_read(addr, data);
    return data;
}

static void ingenic_lcd_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    IngenicLcd *s = INGENIC_LCD(opaque);
    trace_ingenic_lcd_write(addr, data);
    switch (addr) {
    case REG_LCDCFG:
        s->lcdcfg = data;
        break;
    case REG_LCDVSYNC:
        s->lcdvsync = data & 0x0fff0fff;
        break;
    case REG_LCDHSYNC:
        s->lcdhsync = data & 0x0fff0fff;
        break;
    case REG_LCDVAT:
        s->lcdvat = data & 0x0fff0fff;
        break;
    case REG_LCDDAH:
        s->lcddah = data & 0x0fff0fff;
        break;
    case REG_LCDDAV:
        s->lcddav = data & 0x0fff0fff;
        break;
    case REG_LCDCTRL: {
        bool en = (s->lcdctrl & BIT(3)) && ~(s->lcdctrl & BIT(4));
        bool en_next = (data & BIT(3)) && ~(data & BIT(4));
        s->lcdctrl = data & 0x3fffffff;
        if (en_next != en) {
            ingenic_lcd_enable(s, en_next);
            if (!(data & BIT(3)))
                s->lcdstate |= BIT(7);              // Quick disabled
            else if (data & BIT(4))
                s->lcdstate |= BIT(0);              // Normal disabled
            else
                s->lcdstate &= ~(BIT(7) | BIT(0));  // Enabled
        }
        ingenic_lcd_update_irq(s);
        break;
    }
    case REG_LCDSTATE:
        s->lcdstate = data & 0xbf;
        ingenic_lcd_update_irq(s);
        break;
    case REG_LCDDA0:
        s->desc[0].lcdda = data;
        break;
    case REG_LCDDA1:
        s->desc[1].lcdda = data;
        break;
    case REG_LCDRGBC:
        s->lcdrgbc = data & 0xc177;
        break;
    case REG_LCDOSDC:
        s->lcdosdc = data & 0xcc1f;
        break;
    case REG_LCDOSDCTRL:
        s->lcdosdctrl = data & 0x801f;
        break;
    case REG_LCDBGC:
        s->lcdbgc = data & 0x00ffffff;
        break;
    case REG_LCDKEY0:
        s->fg[0].lcdkey = data & 0xc0ffffff;
        break;
    case REG_LCDKEY1:
        s->fg[1].lcdkey = data & 0xc0ffffff;
        break;
    case REG_LCDALPHA:
        s->lcdalpha = data & 0xff;
        break;
    case REG_LCDIPUR:
        s->lcdipur = data & 0x80ffffff;
        break;
    case REG_LCDXYP0:
        s->fg[0].lcdxyp = data & 0x0fff0fff;
        break;
    case REG_LCDXYP1:
        s->fg[1].lcdxyp = data & 0x0fff0fff;
        break;
    case REG_LCDSIZE0:
        s->fg[0].lcdsize = data & 0x0fff0fff;
        break;
    case REG_LCDSIZE1:
        s->fg[1].lcdsize = data & 0x0fff0fff;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx " 0x%"PRIx64"\n",
                      __func__, addr, data);
        qmp_stop(NULL);
    }
}

static MemoryRegionOps lcd_ops = {
    .read = ingenic_lcd_read,
    .write = ingenic_lcd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ingenic_lcd_init(Object *obj)
{
    IngenicLcd *s = INGENIC_LCD(obj);
    memory_region_init_io(&s->mr, obj, &lcd_ops, s, "lcd", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    qdev_init_gpio_out_named(DEVICE(obj), &s->irq, "irq-out", 1);

    s->con = graphic_console_init(DEVICE(s), 0, &fb_ops, s);
}

static void ingenic_lcd_finalize(Object *obj)
{
}

static void ingenic_lcd_class_init(ObjectClass *class, void *data)
{
#if 0
    DeviceClass *dc = DEVICE_CLASS(class);
    dc->realize = bcm2835_fb_realize;
    dc->reset = bcm2835_fb_reset;
    dc->vmsd = &vmstate_bcm2835_fb;
#endif
    IngenicLcdClass *bch_class = INGENIC_LCD_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    resettable_class_set_parent_phases(rc,
                                       ingenic_lcd_reset,
                                       NULL,
                                       NULL,
                                       &bch_class->parent_phases);
}

OBJECT_DEFINE_TYPE(IngenicLcd, ingenic_lcd, INGENIC_LCD, SYS_BUS_DEVICE)

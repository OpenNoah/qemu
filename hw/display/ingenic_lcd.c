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

void qmp_stop(Error **errp);

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
            src++;
            r = *src++ & 0xfc;
            g = *src++ & 0xfc;
            b = *src++ & 0xfc;
            break;
        case 888:
            src++;
            r = *src++;
            g = *src++;
            b = *src++;
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
        cpu_physical_memory_read(da, &desc[0], sizeof(desc));
        s->desc[idesc].lcdda  = desc[0];
        s->desc[idesc].lcdsa  = desc[1];
        s->desc[idesc].lcdfid = desc[2];
        s->desc[idesc].lcdcmd = desc[3];
        if (s->lcdcfg & BIT(28)) {
            s->desc[idesc].lcdoffs    = desc[4];
            s->desc[idesc].lcdpw      = desc[5];
            s->desc[idesc].lcdcnum    = desc[6];
            s->desc[idesc].lcddessize = desc[7];
        }

        if (s->desc[idesc].lcdcmd & 0xf0000000) {
            qemu_log("%s: Unsupported CMD 0x%"PRIx32"\n",
                     __func__, s->desc[idesc].lcdcmd);
            qmp_stop(NULL);
            continue;
        } else if (s->lcdcfg & BIT(28)) {
            uint32_t xres = s->desc[idesc].lcddessize & 0xffff;
            uint32_t yres = s->desc[idesc].lcddessize >> 16;
            if (xres != s->xres || yres != s->yres) {
                qemu_log("%s: Descriptor size mismatch 0x%"PRIx32"\n",
                        __func__, s->desc[idesc].lcddessize);
                qmp_stop(NULL);
                continue;
            }
        }

#if 0
        qemu_log("%s: 0x%"PRIx32" 0x%"PRIx32" 0x%"PRIx32" 0x%"PRIx32
                 " 0x%"PRIx32" 0x%"PRIx32" 0x%"PRIx32" 0x%"PRIx32"\n",
                 __func__,
                 s->desc[idesc].lcdda,   s->desc[idesc].lcdsa,
                 s->desc[idesc].lcdfid,  s->desc[idesc].lcdcmd,
                 s->desc[idesc].lcdoffs, s->desc[idesc].lcdpw,
                 s->desc[idesc].lcdcnum, s->desc[idesc].lcddessize);
#endif

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
    if (!en) {
        // LCD controller disabled
        qemu_log("%s: LCD disabled\n", __func__);
        if (s->con)
            graphic_console_close(s->con);
        s->con = 0;
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

    qemu_log("%s: LCD enabled: %"PRIu32"x%"PRIu32" mode %"PRIu32"\n",
             __func__, s->xres, s->yres, s->mode);

    if (!s->xres || !s->yres || !s->mode) {
        qemu_log("%s: Unsupported configuration\n", __func__);
        qmp_stop(NULL);
        return;
    }

    if (!s->con) {
        s->con = graphic_console_init(DEVICE(s), 0, &fb_ops, s);
        qemu_console_resize(s->con, s->xres, s->yres);
    } else {
        qemu_log("%s: TODO %u\n", __func__, __LINE__);
        qmp_stop(NULL);
    }
}

static void ingenic_lcd_reset(Object *obj, ResetType type)
{
    qemu_log("%s enter\n", __func__);
    IngenicLcd *s = INGENIC_LCD(obj);
    (void)s;
    // Initial values
}

static uint64_t ingenic_lcd_read(void *opaque, hwaddr addr, unsigned size)
{
    IngenicLcd *s = INGENIC_LCD(opaque);
    uint64_t data = 0;
    switch (addr) {
    case 0x0000:
        data = s->lcdcfg;
        break;
    case 0x0004:
        data = s->lcdvsync;
        break;
    case 0x0008:
        data = s->lcdhsync;
        break;
    case 0x000c:
        data = s->lcdvat;
        break;
    case 0x0010:
        data = s->lcddah;
        break;
    case 0x0014:
        data = s->lcddav;
        break;
    case 0x0030:
        data = s->lcdctrl;
        break;
    case 0x0040:
        data = s->desc[0].lcdda;
        break;
    case 0x0050:
        data = s->desc[1].lcdda;
        break;
    case 0x0090:
        data = s->lcdrgbc;
        break;
    case 0x0104:
        data = s->lcdosdctrl;
        break;
    case 0x010c:
        data = s->lcdbgc;
        break;
    case 0x0110:
        data = s->fg[0].lcdkey;
        break;
    case 0x0114:
        data = s->fg[1].lcdkey;
        break;
    case 0x0118:
        data = s->lcdalpha;
        break;
    case 0x011c:
        data = s->lcdipur;
        break;
    case 0x0128:
        data = s->fg[0].lcdsize;
        break;
    case 0x012c:
        data = s->fg[1].lcdsize;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown address " HWADDR_FMT_plx "\n", __func__, addr);
        qmp_stop(NULL);
    }

    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);
    return data;
}

static void ingenic_lcd_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    qemu_log("%s: @ " HWADDR_FMT_plx "/%"PRIx32": 0x%"PRIx64"\n", __func__, addr, (uint32_t)size, data);

    IngenicLcd *s = INGENIC_LCD(opaque);
    uint32_t diff = data;
    switch (addr) {
    case 0x0000:
        s->lcdcfg = data;
        break;
    case 0x0004:
        s->lcdvsync = data & 0x0fff0fff;
        break;
    case 0x0008:
        s->lcdhsync = data & 0x0fff0fff;
        break;
    case 0x000c:
        s->lcdvat = data & 0x0fff0fff;
        break;
    case 0x0010:
        s->lcddah = data & 0x0fff0fff;
        break;
    case 0x0014:
        s->lcddav = data & 0x0fff0fff;
        break;
    case 0x0030:
        diff ^= s->lcdctrl;
        s->lcdctrl = data & 0x3fffffff;
        if (diff & BIT(3))
            ingenic_lcd_enable(s, s->lcdctrl & BIT(3));
        break;
    case 0x0040:
        s->desc[0].lcdda = data;
        break;
    case 0x0050:
        s->desc[1].lcdda = data;
        break;
    case 0x0090:
        s->lcdrgbc = data & 0xc177;
        break;
    case 0x0104:
        s->lcdosdctrl = data & 0x801f;
        break;
    case 0x010c:
        s->lcdbgc = data & 0x00ffffff;
        break;
    case 0x0110:
        s->fg[0].lcdkey = data & 0xc0ffffff;
        break;
    case 0x0114:
        s->fg[1].lcdkey = data & 0xc0ffffff;
        break;
    case 0x0118:
        s->lcdalpha = data & 0xff;
        break;
    case 0x011c:
        s->lcdipur = data & 0x80ffffff;
        break;
    case 0x0128:
        s->fg[0].lcdsize = data & 0x0fff0fff;
        break;
    case 0x012c:
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
    qemu_log("%s enter\n", __func__);
    IngenicLcd *s = INGENIC_LCD(obj);
    memory_region_init_io(&s->mr, OBJECT(s), &lcd_ops, s, "lcd", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
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

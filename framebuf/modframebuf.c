/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>

#include "py/runtime.h"
#include "py/binary.h"

#ifndef MICROPY_PY_FRAMEBUF
#define MICROPY_PY_FRAMEBUF (1)
#endif

#if MICROPY_PY_FRAMEBUF

#include "extmod/font_petme128_8x8.h"

#define SUPPORT_GFX_FONT (1)
#define SUPPORT_JPG (1)

#if SUPPORT_GFX_FONT
#include "gfxfont/gfxfont.h"
#include "utf8_rosetta.h"
#include "zlib/zlib.h"
#endif

#if SUPPORT_JPG
#include "tjpgd.h"
#include "extmod/vfs.h"
#include "py/stream.h"
#endif

typedef struct _mp_obj_framebuf_t {
    mp_obj_base_t base;
    mp_obj_t buf_obj; // need to store this to prevent GC from reclaiming buf
    void *buf;
    uint16_t width, height, stride;
    uint8_t format;
#if SUPPORT_GFX_FONT
    GFXfont *gfxFont;
#endif
} mp_obj_framebuf_t;

#if !MICROPY_ENABLE_DYNRUNTIME
STATIC const mp_obj_type_t mp_type_framebuf;
#endif

typedef void (*setpixel_t)(const mp_obj_framebuf_t *, unsigned int, unsigned int, uint32_t);
typedef uint32_t (*getpixel_t)(const mp_obj_framebuf_t *, unsigned int, unsigned int);
typedef void (*fill_rect_t)(const mp_obj_framebuf_t *, unsigned int, unsigned int, unsigned int, unsigned int, uint32_t);

typedef struct _mp_framebuf_p_t {
    setpixel_t setpixel;
    getpixel_t getpixel;
    fill_rect_t fill_rect;
} mp_framebuf_p_t;

// constants for formats
#define FRAMEBUF_MVLSB    (0)
#define FRAMEBUF_RGB565   (1)
#define FRAMEBUF_GS2_HMSB (5)
#define FRAMEBUF_GS4_HMSB (2)
#define FRAMEBUF_GS8      (6)
#define FRAMEBUF_MHLSB    (3)
#define FRAMEBUF_MHMSB    (4)
#define FRAMEBUF_GS4_HLSB (7)

// Functions for MHLSB and MHMSB

STATIC void mono_horiz_setpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, uint32_t col) {
    size_t index = (x + y * fb->stride) >> 3;
    unsigned int offset = fb->format == FRAMEBUF_MHMSB ? x & 0x07 : 7 - (x & 0x07);
    ((uint8_t *)fb->buf)[index] = (((uint8_t *)fb->buf)[index] & ~(0x01 << offset)) | ((col != 0) << offset);
}

STATIC uint32_t mono_horiz_getpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y) {
    size_t index = (x + y * fb->stride) >> 3;
    unsigned int offset = fb->format == FRAMEBUF_MHMSB ? x & 0x07 : 7 - (x & 0x07);
    return (((uint8_t *)fb->buf)[index] >> (offset)) & 0x01;
}

STATIC void mono_horiz_fill_rect(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, unsigned int w, unsigned int h, uint32_t col) {
    unsigned int reverse = fb->format == FRAMEBUF_MHMSB;
    unsigned int advance = fb->stride >> 3;
    while (w--) {
        uint8_t *b = &((uint8_t *)fb->buf)[(x >> 3) + y * advance];
        unsigned int offset = reverse ?  x & 7 : 7 - (x & 7);
        for (unsigned int hh = h; hh; --hh) {
            *b = (*b & ~(0x01 << offset)) | ((col != 0) << offset);
            b += advance;
        }
        ++x;
    }
}

// Functions for MVLSB format

STATIC void mvlsb_setpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, uint32_t col) {
    size_t index = (y >> 3) * fb->stride + x;
    uint8_t offset = y & 0x07;
    ((uint8_t *)fb->buf)[index] = (((uint8_t *)fb->buf)[index] & ~(0x01 << offset)) | ((col != 0) << offset);
}

STATIC uint32_t mvlsb_getpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y) {
    return (((uint8_t *)fb->buf)[(y >> 3) * fb->stride + x] >> (y & 0x07)) & 0x01;
}

STATIC void mvlsb_fill_rect(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, unsigned int w, unsigned int h, uint32_t col) {
    while (h--) {
        uint8_t *b = &((uint8_t *)fb->buf)[(y >> 3) * fb->stride + x];
        uint8_t offset = y & 0x07;
        for (unsigned int ww = w; ww; --ww) {
            *b = (*b & ~(0x01 << offset)) | ((col != 0) << offset);
            ++b;
        }
        ++y;
    }
}

// Functions for RGB565 format

STATIC void rgb565_setpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, uint32_t col) {
    ((uint16_t *)fb->buf)[x + y * fb->stride] = col;
}

STATIC uint32_t rgb565_getpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y) {
    return ((uint16_t *)fb->buf)[x + y * fb->stride];
}

STATIC void rgb565_fill_rect(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, unsigned int w, unsigned int h, uint32_t col) {
    uint16_t *b = &((uint16_t *)fb->buf)[x + y * fb->stride];
    while (h--) {
        for (unsigned int ww = w; ww; --ww) {
            *b++ = col;
        }
        b += fb->stride - w;
    }
}

// Functions for GS2_HMSB format

STATIC void gs2_hmsb_setpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, uint32_t col) {
    uint8_t *pixel = &((uint8_t *)fb->buf)[(x + y * fb->stride) >> 2];
    uint8_t shift = (x & 0x3) << 1;
    uint8_t mask = 0x3 << shift;
    uint8_t color = (col & 0x3) << shift;
    *pixel = color | (*pixel & (~mask));
}

STATIC uint32_t gs2_hmsb_getpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y) {
    uint8_t pixel = ((uint8_t *)fb->buf)[(x + y * fb->stride) >> 2];
    uint8_t shift = (x & 0x3) << 1;
    return (pixel >> shift) & 0x3;
}

STATIC void gs2_hmsb_fill_rect(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, unsigned int w, unsigned int h, uint32_t col) {
    for (unsigned int xx = x; xx < x + w; xx++) {
        for (unsigned int yy = y; yy < y + h; yy++) {
            gs2_hmsb_setpixel(fb, xx, yy, col);
        }
    }
}

// Functions for GS4_HMSB format

STATIC void gs4_hmsb_setpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, uint32_t col) {
    uint8_t *pixel = &((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1];

    if (x % 2) {
        *pixel = ((uint8_t)col & 0x0f) | (*pixel & 0xf0);
    } else {
        *pixel = ((uint8_t)col << 4) | (*pixel & 0x0f);
    }
}

STATIC uint32_t gs4_hmsb_getpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y) {
    if (x % 2) {
        return ((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1] & 0x0f;
    }

    return ((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1] >> 4;
}

STATIC void gs4_hmsb_fill_rect(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, unsigned int w, unsigned int h, uint32_t col) {
    col &= 0x0f;
    uint8_t *pixel_pair = &((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1];
    uint8_t col_shifted_left = col << 4;
    uint8_t col_pixel_pair = col_shifted_left | col;
    unsigned int pixel_count_till_next_line = (fb->stride - w) >> 1;
    bool odd_x = (x % 2 == 1);

    while (h--) {
        unsigned int ww = w;

        if (odd_x && ww > 0) {
            *pixel_pair = (*pixel_pair & 0xf0) | col;
            pixel_pair++;
            ww--;
        }

        memset(pixel_pair, col_pixel_pair, ww >> 1);
        pixel_pair += ww >> 1;

        if (ww % 2) {
            *pixel_pair = col_shifted_left | (*pixel_pair & 0x0f);
            if (!odd_x) {
                pixel_pair++;
            }
        }

        pixel_pair += pixel_count_till_next_line;
    }
}

// Functions for GS8 format

STATIC void gs8_setpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, uint32_t col) {
    uint8_t *pixel = &((uint8_t *)fb->buf)[(x + y * fb->stride)];
    *pixel = col & 0xff;
}

STATIC uint32_t gs8_getpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y) {
    return ((uint8_t *)fb->buf)[(x + y * fb->stride)];
}

STATIC void gs8_fill_rect(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, unsigned int w, unsigned int h, uint32_t col) {
    uint8_t *pixel = &((uint8_t *)fb->buf)[(x + y * fb->stride)];
    while (h--) {
        memset(pixel, col, w);
        pixel += fb->stride;
    }
}

// Functions for GS4_HLSB format

STATIC void gs4_hlsb_setpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, uint32_t col) {
    uint8_t *pixel = &((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1];

    if (x % 2) {
        *pixel = ((uint8_t)col << 4) | (*pixel & 0x0f);
    } else {
        *pixel = ((uint8_t)col & 0x0f) | (*pixel & 0xf0);
    }
}

STATIC uint32_t gs4_hlsb_getpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y) {
    if (x % 2) {
        return ((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1] >> 4;
    }

    return ((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1] & 0x0f;
}

STATIC void gs4_hlsb_fill_rect(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, unsigned int w, unsigned int h, uint32_t col) {
    col &= 0x0f;
    uint8_t *pixel_pair = &((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1];
    uint8_t col_shifted_left = col << 4;
    uint8_t col_pixel_pair = col_shifted_left | col;
    unsigned int pixel_count_till_next_line = (fb->stride - w) >> 1;
    bool odd_x = (x % 2 == 1);

    while (h--) {
        unsigned int ww = w;

        if (odd_x && ww > 0) {
            *pixel_pair = (*pixel_pair & 0x0f) | col_shifted_left;
            pixel_pair++;
            ww--;
        }

        memset(pixel_pair, col_pixel_pair, ww >> 1);
        pixel_pair += ww >> 1;

        if (ww % 2) {
            *pixel_pair = col | (*pixel_pair & 0xf0);
            if (!odd_x) {
                pixel_pair++;
            }
        }

        pixel_pair += pixel_count_till_next_line;
    }
}

STATIC mp_framebuf_p_t formats[] = {
    [FRAMEBUF_MVLSB] = {mvlsb_setpixel, mvlsb_getpixel, mvlsb_fill_rect},
    [FRAMEBUF_RGB565] = {rgb565_setpixel, rgb565_getpixel, rgb565_fill_rect},
    [FRAMEBUF_GS2_HMSB] = {gs2_hmsb_setpixel, gs2_hmsb_getpixel, gs2_hmsb_fill_rect},
    [FRAMEBUF_GS4_HMSB] = {gs4_hmsb_setpixel, gs4_hmsb_getpixel, gs4_hmsb_fill_rect},
    [FRAMEBUF_GS8] = {gs8_setpixel, gs8_getpixel, gs8_fill_rect},
    [FRAMEBUF_MHLSB] = {mono_horiz_setpixel, mono_horiz_getpixel, mono_horiz_fill_rect},
    [FRAMEBUF_MHMSB] = {mono_horiz_setpixel, mono_horiz_getpixel, mono_horiz_fill_rect},
    [FRAMEBUF_GS4_HLSB] = {gs4_hlsb_setpixel, gs4_hlsb_getpixel, gs4_hlsb_fill_rect},
};

STATIC inline void setpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y, uint32_t col) {
    formats[fb->format].setpixel(fb, x, y, col);
}

STATIC void setpixel_checked(const mp_obj_framebuf_t *fb, mp_int_t x, mp_int_t y, mp_int_t col, mp_int_t mask) {
    if (mask && 0 <= x && x < fb->width && 0 <= y && y < fb->height) {
        setpixel(fb, x, y, col);
    }
}

STATIC inline uint32_t getpixel(const mp_obj_framebuf_t *fb, unsigned int x, unsigned int y) {
    return formats[fb->format].getpixel(fb, x, y);
}

STATIC void fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    if (h < 1 || w < 1 || x + w <= 0 || y + h <= 0 || y >= fb->height || x >= fb->width) {
        // No operation needed.
        return;
    }

    // clip to the framebuffer
    int xend = MIN(fb->width, x + w);
    int yend = MIN(fb->height, y + h);
    x = MAX(x, 0);
    y = MAX(y, 0);

    formats[fb->format].fill_rect(fb, x, y, xend - x, yend - y, col);
}


#if SUPPORT_JPG
// User defined device identifier
typedef struct {
    // for file input function
    mp_obj_t fp; /* Input stream */

    // for buffer input function
    uint8_t *data;
    unsigned int data_index;
    unsigned int data_len;

    // for output
    uint8_t * fbuf; /* Output frame buffer */
    unsigned int wfbuf; /* Width of the frame buffer [pix] */
} IODEV;


const char *jd_errors[] = {
    "Succeeded",
    "Interrupted by output function",
    "Device error or wrong termination of input stream",
    "Insufficient memory pool for the image",
    "Insufficient stream input buffer",
    "Parameter error",
    "Data format error",
    "Right format but not supported",
    "Not supported JPEG standard"
};

static const uint8_t gs8_curve[] = {
    0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
    0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d,
    0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc1, 0xc2,
    0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4,
    0x00, 0x00, 0x00, 0x00, 0x5c, 0x3d, 0xcf, 0x3f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xa5, 0xa5, 0xa5, 0x58, 0x01, 0x00, 0x00,
    0x74, 0x31, 0xc9, 0x3f, 0x18, 0x00, 0x00, 0x00,
    0x6d, 0x61, 0x69, 0x6e, 0x00, 0x1a, 0x8f, 0x32,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

typedef uint32_t (*color_converts_t)(uint8_t, uint8_t, uint8_t);

STATIC uint32_t rgb888_to_gs8(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t gamme_val = (r * 38 + g * 75 + b * 15) >> 7;
    return gs8_curve[gamme_val];
}

STATIC uint32_t rgb888_to_gs4(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t gamme_val = (r * 38 + g * 75 + b * 15) >> 7;
    return (gs8_curve[gamme_val] >> 4);
}

STATIC color_converts_t converts[] = {
    [FRAMEBUF_MVLSB]    = NULL,
    [FRAMEBUF_RGB565]   = NULL,
    [FRAMEBUF_GS2_HMSB] = NULL,
    [FRAMEBUF_GS4_HMSB] = rgb888_to_gs4,
    [FRAMEBUF_GS8]      = rgb888_to_gs8,
    [FRAMEBUF_MHLSB]    = NULL,
    [FRAMEBUF_MHMSB]    = NULL,
    [FRAMEBUF_GS4_HLSB] = rgb888_to_gs4,
};

#endif


STATIC mp_obj_t framebuf_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args_in) {
    mp_arg_check_num(n_args, n_kw, 4, 5, false);

    mp_obj_framebuf_t *o = mp_obj_malloc(mp_obj_framebuf_t, type);
    o->buf_obj = args_in[0];

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args_in[0], &bufinfo, MP_BUFFER_WRITE);
    o->buf = bufinfo.buf;

    o->width = mp_obj_get_int(args_in[1]);
    o->height = mp_obj_get_int(args_in[2]);
    o->format = mp_obj_get_int(args_in[3]);
    if (n_args >= 5) {
        o->stride = mp_obj_get_int(args_in[4]);
    } else {
        o->stride = o->width;
    }

    switch (o->format) {
        case FRAMEBUF_MVLSB:
        case FRAMEBUF_RGB565:
            break;
        case FRAMEBUF_MHLSB:
        case FRAMEBUF_MHMSB:
            o->stride = (o->stride + 7) & ~7;
            break;
        case FRAMEBUF_GS2_HMSB:
            o->stride = (o->stride + 3) & ~3;
            break;
        case FRAMEBUF_GS4_HMSB:
        case FRAMEBUF_GS4_HLSB:
            o->stride = (o->stride + 1) & ~1;
            break;
        case FRAMEBUF_GS8:
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("invalid format"));
    }

#if SUPPORT_GFX_FONT
    o->gfxFont = NULL;
#endif

    return MP_OBJ_FROM_PTR(o);
}

STATIC void framebuf_args(const mp_obj_t *args_in, mp_int_t *args_out, int n) {
    for (int i = 0; i < n; ++i) {
        args_out[i] = mp_obj_get_int(args_in[i + 1]);
    }
}

STATIC mp_int_t framebuf_get_buffer(mp_obj_t self_in, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    (void)flags;
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    bufinfo->buf = self->buf;
    bufinfo->len = self->stride * self->height * (self->format == FRAMEBUF_RGB565 ? 2 : 1);
    bufinfo->typecode = 'B'; // view framebuf as bytes
    return 0;
}

STATIC mp_obj_t framebuf_fill(mp_obj_t self_in, mp_obj_t col_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t col = mp_obj_get_int(col_in);
    formats[self->format].fill_rect(self, 0, 0, self->width, self->height, col);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(framebuf_fill_obj, framebuf_fill);

STATIC mp_obj_t framebuf_fill_rect(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[5]; // x, y, w, h, col
    framebuf_args(args_in, args, 5);
    fill_rect(self, args[0], args[1], args[2], args[3], args[4]);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_fill_rect_obj, 6, 6, framebuf_fill_rect);

STATIC mp_obj_t framebuf_pixel(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t x = mp_obj_get_int(args_in[1]);
    mp_int_t y = mp_obj_get_int(args_in[2]);
    if (0 <= x && x < self->width && 0 <= y && y < self->height) {
        if (n_args == 3) {
            // get
            return MP_OBJ_NEW_SMALL_INT(getpixel(self, x, y));
        } else {
            // set
            setpixel(self, x, y, mp_obj_get_int(args_in[3]));
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_pixel_obj, 3, 4, framebuf_pixel);

STATIC mp_obj_t framebuf_hline(size_t n_args, const mp_obj_t *args_in) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[4]; // x, y, w, col
    framebuf_args(args_in, args, 4);

    fill_rect(self, args[0], args[1], args[2], 1, args[3]);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_hline_obj, 5, 5, framebuf_hline);

STATIC mp_obj_t framebuf_vline(size_t n_args, const mp_obj_t *args_in) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[4]; // x, y, h, col
    framebuf_args(args_in, args, 4);

    fill_rect(self, args[0], args[1], 1, args[2], args[3]);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_vline_obj, 5, 5, framebuf_vline);

STATIC mp_obj_t framebuf_rect(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[5]; // x, y, w, h, col
    framebuf_args(args_in, args, 5);
    if (n_args > 6 && mp_obj_is_true(args_in[6])) {
        fill_rect(self, args[0], args[1], args[2], args[3], args[4]);
    } else {
        fill_rect(self, args[0], args[1], args[2], 1, args[4]);
        fill_rect(self, args[0], args[1] + args[3] - 1, args[2], 1, args[4]);
        fill_rect(self, args[0], args[1], 1, args[3], args[4]);
        fill_rect(self, args[0] + args[2] - 1, args[1], 1, args[3], args[4]);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_rect_obj, 6, 7, framebuf_rect);

STATIC void line(const mp_obj_framebuf_t *fb, mp_int_t x1, mp_int_t y1, mp_int_t x2, mp_int_t y2, mp_int_t col) {
    mp_int_t dx = x2 - x1;
    mp_int_t sx;
    if (dx > 0) {
        sx = 1;
    } else {
        dx = -dx;
        sx = -1;
    }

    mp_int_t dy = y2 - y1;
    mp_int_t sy;
    if (dy > 0) {
        sy = 1;
    } else {
        dy = -dy;
        sy = -1;
    }

    bool steep;
    if (dy > dx) {
        mp_int_t temp;
        temp = x1;
        x1 = y1;
        y1 = temp;
        temp = dx;
        dx = dy;
        dy = temp;
        temp = sx;
        sx = sy;
        sy = temp;
        steep = true;
    } else {
        steep = false;
    }

    mp_int_t e = 2 * dy - dx;
    for (mp_int_t i = 0; i < dx; ++i) {
        if (steep) {
            if (0 <= y1 && y1 < fb->width && 0 <= x1 && x1 < fb->height) {
                setpixel(fb, y1, x1, col);
            }
        } else {
            if (0 <= x1 && x1 < fb->width && 0 <= y1 && y1 < fb->height) {
                setpixel(fb, x1, y1, col);
            }
        }
        while (e >= 0) {
            y1 += sy;
            e -= 2 * dx;
        }
        x1 += sx;
        e += 2 * dy;
    }

    if (0 <= x2 && x2 < fb->width && 0 <= y2 && y2 < fb->height) {
        setpixel(fb, x2, y2, col);
    }
}

STATIC mp_obj_t framebuf_line(size_t n_args, const mp_obj_t *args_in) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[5]; // x1, y1, x2, y2, col
    framebuf_args(args_in, args, 5);

    line(self, args[0], args[1], args[2], args[3], args[4]);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_line_obj, 6, 6, framebuf_line);

// Q2 Q1
// Q3 Q4
#define ELLIPSE_MASK_FILL (0x10)
#define ELLIPSE_MASK_ALL (0x0f)
#define ELLIPSE_MASK_Q1 (0x01)
#define ELLIPSE_MASK_Q2 (0x02)
#define ELLIPSE_MASK_Q3 (0x04)
#define ELLIPSE_MASK_Q4 (0x08)

STATIC void draw_ellipse_points(const mp_obj_framebuf_t *fb, mp_int_t cx, mp_int_t cy, mp_int_t x, mp_int_t y, mp_int_t col, mp_int_t mask) {
    if (mask & ELLIPSE_MASK_FILL) {
        if (mask & ELLIPSE_MASK_Q1) {
            fill_rect(fb, cx, cy - y, x + 1, 1, col);
        }
        if (mask & ELLIPSE_MASK_Q2) {
            fill_rect(fb, cx - x, cy - y, x + 1, 1, col);
        }
        if (mask & ELLIPSE_MASK_Q3) {
            fill_rect(fb, cx - x, cy + y, x + 1, 1, col);
        }
        if (mask & ELLIPSE_MASK_Q4) {
            fill_rect(fb, cx, cy + y, x + 1, 1, col);
        }
    } else {
        setpixel_checked(fb, cx + x, cy - y, col, mask & ELLIPSE_MASK_Q1);
        setpixel_checked(fb, cx - x, cy - y, col, mask & ELLIPSE_MASK_Q2);
        setpixel_checked(fb, cx - x, cy + y, col, mask & ELLIPSE_MASK_Q3);
        setpixel_checked(fb, cx + x, cy + y, col, mask & ELLIPSE_MASK_Q4);
    }
}

STATIC mp_obj_t framebuf_ellipse(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[5];
    framebuf_args(args_in, args, 5); // cx, cy, xradius, yradius, col
    mp_int_t mask = (n_args > 6 && mp_obj_is_true(args_in[6])) ? ELLIPSE_MASK_FILL : 0;
    if (n_args > 7) {
        mask |= mp_obj_get_int(args_in[7]) & ELLIPSE_MASK_ALL;
    } else {
        mask |= ELLIPSE_MASK_ALL;
    }
    mp_int_t two_asquare = 2 * args[2] * args[2];
    mp_int_t two_bsquare = 2 * args[3] * args[3];
    mp_int_t x = args[2];
    mp_int_t y = 0;
    mp_int_t xchange = args[3] * args[3] * (1 - 2 * args[2]);
    mp_int_t ychange = args[2] * args[2];
    mp_int_t ellipse_error = 0;
    mp_int_t stoppingx = two_bsquare * args[2];
    mp_int_t stoppingy = 0;
    while (stoppingx >= stoppingy) {   // 1st set of points,  y' > -1
        draw_ellipse_points(self, args[0], args[1], x, y, args[4], mask);
        y += 1;
        stoppingy += two_asquare;
        ellipse_error += ychange;
        ychange += two_asquare;
        if ((2 * ellipse_error + xchange) > 0) {
            x -= 1;
            stoppingx -= two_bsquare;
            ellipse_error += xchange;
            xchange += two_bsquare;
        }
    }
    // 1st point set is done start the 2nd set of points
    x = 0;
    y = args[3];
    xchange = args[3] * args[3];
    ychange = args[2] * args[2] * (1 - 2 * args[3]);
    ellipse_error = 0;
    stoppingx = 0;
    stoppingy = two_asquare * args[3];
    while (stoppingx <= stoppingy) {  // 2nd set of points, y' < -1
        draw_ellipse_points(self, args[0], args[1], x, y, args[4], mask);
        x += 1;
        stoppingx += two_bsquare;
        ellipse_error += xchange;
        xchange += two_bsquare;
        if ((2 * ellipse_error + ychange) > 0) {
            y -= 1;
            stoppingy -= two_asquare;
            ellipse_error += ychange;
            ychange += two_asquare;
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_ellipse_obj, 6, 8, framebuf_ellipse);

#if MICROPY_PY_ARRAY && !MICROPY_ENABLE_DYNRUNTIME
// TODO: poly needs mp_binary_get_size & mp_binary_get_val_array which aren't
// available in dynruntime.h yet.

STATIC mp_int_t poly_int(mp_buffer_info_t *bufinfo, size_t index) {
    return mp_obj_get_int(mp_binary_get_val_array(bufinfo->typecode, bufinfo->buf, index));
}

STATIC mp_obj_t framebuf_poly(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);

    mp_int_t x = mp_obj_get_int(args_in[1]);
    mp_int_t y = mp_obj_get_int(args_in[2]);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args_in[3], &bufinfo, MP_BUFFER_READ);
    // If an odd number of values was given, this rounds down to multiple of two.
    int n_poly = bufinfo.len / (mp_binary_get_size('@', bufinfo.typecode, NULL) * 2);

    if (n_poly == 0) {
        return mp_const_none;
    }

    mp_int_t col = mp_obj_get_int(args_in[4]);
    bool fill = n_args > 5 && mp_obj_is_true(args_in[5]);

    if (fill) {
        // This implements an integer version of http://alienryderflex.com/polygon_fill/

        // The idea is for each scan line, compute the sorted list of x
        // coordinates where the scan line intersects the polygon edges,
        // then fill between each resulting pair.

        // Restrict just to the scan lines that include the vertical extent of
        // this polygon.
        mp_int_t y_min = INT_MAX, y_max = INT_MIN;
        for (int i = 0; i < n_poly; i++) {
            mp_int_t py = poly_int(&bufinfo, i * 2 + 1);
            y_min = MIN(y_min, py);
            y_max = MAX(y_max, py);
        }

        for (mp_int_t row = y_min; row <= y_max; row++) {
            // Each node is the x coordinate where an edge crosses this scan line.
            mp_int_t nodes[n_poly];
            int n_nodes = 0;
            mp_int_t px1 = poly_int(&bufinfo, 0);
            mp_int_t py1 = poly_int(&bufinfo, 1);
            int i = n_poly * 2 - 1;
            do {
                mp_int_t py2 = poly_int(&bufinfo, i--);
                mp_int_t px2 = poly_int(&bufinfo, i--);

                // Don't include the bottom pixel of a given edge to avoid
                // duplicating the node with the start of the next edge. This
                // will miss some pixels on the boundary, and in particular
                // at a local minima or inflection point.
                if (py1 != py2 && ((py1 > row && py2 <= row) || (py1 <= row && py2 > row))) {
                    mp_int_t node = (32 * px1 + 32 * (px2 - px1) * (row - py1) / (py2 - py1) + 16) / 32;
                    nodes[n_nodes++] = node;
                } else if (row == MAX(py1, py2)) {
                    // At local-minima, try and manually fill in the pixels that get missed above.
                    if (py1 < py2) {
                        setpixel_checked(self, x + px2, y + py2, col, 1);
                    } else if (py2 < py1) {
                        setpixel_checked(self, x + px1, y + py1, col, 1);
                    } else {
                        // Even though this is a hline and would be faster to
                        // use fill_rect, use line() because it handles x2 <
                        // x1.
                        line(self, x + px1, y + py1, x + px2, y + py2, col);
                    }
                }

                px1 = px2;
                py1 = py2;
            } while (i >= 0);

            if (!n_nodes) {
                continue;
            }

            // Sort the nodes left-to-right (bubble-sort for code size).
            i = 0;
            while (i < n_nodes - 1) {
                if (nodes[i] > nodes[i + 1]) {
                    mp_int_t swap = nodes[i];
                    nodes[i] = nodes[i + 1];
                    nodes[i + 1] = swap;
                    if (i) {
                        i--;
                    }
                } else {
                    i++;
                }
            }

            // Fill between each pair of nodes.
            for (i = 0; i < n_nodes; i += 2) {
                fill_rect(self, x + nodes[i], y + row, (nodes[i + 1] - nodes[i]) + 1, 1, col);
            }
        }
    } else {
        // Outline only.
        mp_int_t px1 = poly_int(&bufinfo, 0);
        mp_int_t py1 = poly_int(&bufinfo, 1);
        int i = n_poly * 2 - 1;
        do {
            mp_int_t py2 = poly_int(&bufinfo, i--);
            mp_int_t px2 = poly_int(&bufinfo, i--);
            line(self, x + px1, y + py1, x + px2, y + py2, col);
            px1 = px2;
            py1 = py2;
        } while (i >= 0);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_poly_obj, 5, 6, framebuf_poly);
#endif // MICROPY_PY_ARRAY && !MICROPY_ENABLE_DYNRUNTIME

STATIC mp_obj_t framebuf_blit(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_obj_t source_in = mp_obj_cast_to_native_base(args_in[1], MP_OBJ_FROM_PTR(&mp_type_framebuf));
    if (source_in == MP_OBJ_NULL) {
        mp_raise_TypeError(NULL);
    }
    mp_obj_framebuf_t *source = MP_OBJ_TO_PTR(source_in);

    mp_int_t x = mp_obj_get_int(args_in[2]);
    mp_int_t y = mp_obj_get_int(args_in[3]);
    mp_int_t key = -1;
    if (n_args > 4) {
        key = mp_obj_get_int(args_in[4]);
    }
    mp_obj_framebuf_t *palette = NULL;
    if (n_args > 5 && args_in[5] != mp_const_none) {
        palette = MP_OBJ_TO_PTR(mp_obj_cast_to_native_base(args_in[5], MP_OBJ_FROM_PTR(&mp_type_framebuf)));
    }

    if (
        (x >= self->width) ||
        (y >= self->height) ||
        (-x >= source->width) ||
        (-y >= source->height)
        ) {
        // Out of bounds, no-op.
        return mp_const_none;
    }

    // Clip.
    int x0 = MAX(0, x);
    int y0 = MAX(0, y);
    int x1 = MAX(0, -x);
    int y1 = MAX(0, -y);
    int x0end = MIN(self->width, x + source->width);
    int y0end = MIN(self->height, y + source->height);

    for (; y0 < y0end; ++y0) {
        int cx1 = x1;
        for (int cx0 = x0; cx0 < x0end; ++cx0) {
            uint32_t col = getpixel(source, cx1, y1);
            if (palette) {
                col = getpixel(palette, col, 0);
            }
            if (col != (uint32_t)key) {
                setpixel(self, cx0, y0, col);
            }
            ++cx1;
        }
        ++y1;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_blit_obj, 4, 6, framebuf_blit);

STATIC mp_obj_t framebuf_scroll(mp_obj_t self_in, mp_obj_t xstep_in, mp_obj_t ystep_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t xstep = mp_obj_get_int(xstep_in);
    mp_int_t ystep = mp_obj_get_int(ystep_in);
    int sx, y, xend, yend, dx, dy;
    if (xstep < 0) {
        sx = 0;
        xend = self->width + xstep;
        if (xend <= 0) {
            return mp_const_none;
        }
        dx = 1;
    } else {
        sx = self->width - 1;
        xend = xstep - 1;
        if (xend >= sx) {
            return mp_const_none;
        }
        dx = -1;
    }
    if (ystep < 0) {
        y = 0;
        yend = self->height + ystep;
        if (yend <= 0) {
            return mp_const_none;
        }
        dy = 1;
    } else {
        y = self->height - 1;
        yend = ystep - 1;
        if (yend >= y) {
            return mp_const_none;
        }
        dy = -1;
    }
    for (; y != yend; y += dy) {
        for (int x = sx; x != xend; x += dx) {
            setpixel(self, x, y, getpixel(self, x - xstep, y - ystep));
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(framebuf_scroll_obj, framebuf_scroll);

STATIC mp_obj_t framebuf_text(size_t n_args, const mp_obj_t *args_in) {
    // extract arguments
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    const char *str = mp_obj_str_get_str(args_in[1]);
    mp_int_t x0 = mp_obj_get_int(args_in[2]);
    mp_int_t y0 = mp_obj_get_int(args_in[3]);
    mp_int_t col = 1;
    if (n_args >= 5) {
        col = mp_obj_get_int(args_in[4]);
    }

    // loop over chars
    for (; *str; ++str) {
        // get char and make sure its in range of font
        int chr = *(uint8_t *)str;
        if (chr < 32 || chr > 127) {
            chr = 127;
        }
        // get char data
        const uint8_t *chr_data = &font_petme128_8x8[(chr - 32) * 8];
        // loop over char data
        for (int j = 0; j < 8; j++, x0++) {
            if (0 <= x0 && x0 < self->width) { // clip x
                uint vline_data = chr_data[j]; // each byte is a column of 8 pixels, LSB at top
                for (int y = y0; vline_data; vline_data >>= 1, y++) { // scan over vertical column
                    if (vline_data & 1) { // only draw if pixel set
                        if (0 <= y && y < self->height) { // clip y
                            setpixel(self, x0, y, col);
                        }
                    }
                }
            }
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_text_obj, 4, 5, framebuf_text);

#if SUPPORT_GFX_FONT

STATIC mp_obj_t framebuf_gfx(size_t n_args, const mp_obj_t *args_in) {
    // extract arguments
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_obj_tuple_t *gfxFont = NULL;
    mp_buffer_info_t bufinfo;

    if (n_args >= 2) {
        if (args_in[1] == mp_const_none) {
            goto OUT;
        }
        gfxFont = MP_OBJ_TO_PTR(args_in[1]);
    } else {
        char describe[256] = { 0 };
        font_get_describe(self->gfxFont, describe, sizeof(describe));
        mp_printf(&mp_plat_print, "%s", describe);
        return mp_const_none;
    }

    if (self->gfxFont != NULL) {
        m_free(self->gfxFont->bitmap);
        m_free(self->gfxFont->glyph);
        m_free(self->gfxFont->intervals);
        m_free(self->gfxFont);
        self->gfxFont = NULL;
    }

    self->gfxFont = m_malloc(sizeof(GFXfont));
    if (self->gfxFont == NULL) {
        mp_warning(NULL, "memory allocation failed");
        return mp_const_none;
    }

    mp_get_buffer_raise(gfxFont->items[0], &bufinfo, MP_BUFFER_READ);
    self->gfxFont->bitmap = (uint8_t *)bufinfo.buf;
    if (self->gfxFont->bitmap == NULL) {
        mp_warning(NULL, "memory allocation failed");
        goto OUT_NO_BITMAP;
    }

    mp_obj_tuple_t *glyph_tuple = MP_OBJ_TO_PTR(gfxFont->items[1]);
    self->gfxFont->glyph = m_malloc(sizeof(GFXglyph) * glyph_tuple->len);
    if (self->gfxFont->glyph == NULL) {
        mp_warning(NULL, "memory allocation failed");
        goto OUT_NO_GLYPH;
    }

    for (size_t i = 0; i < glyph_tuple->len; i++) {
        mp_obj_tuple_t *glyph_items = MP_OBJ_TO_PTR(glyph_tuple->items[i]);
        self->gfxFont->glyph[i].width          = mp_obj_get_int(glyph_items->items[0]);
        self->gfxFont->glyph[i].height         = mp_obj_get_int(glyph_items->items[1]);
        self->gfxFont->glyph[i].xAdvance       = mp_obj_get_int(glyph_items->items[2]);
        self->gfxFont->glyph[i].left           = mp_obj_get_int(glyph_items->items[3]);
        self->gfxFont->glyph[i].top            = mp_obj_get_int(glyph_items->items[4]);
        self->gfxFont->glyph[i].compressedSize = mp_obj_get_int(glyph_items->items[5]);
        self->gfxFont->glyph[i].dataOffset     = mp_obj_get_int(glyph_items->items[6]);
    }

    mp_obj_tuple_t *intervals_tuple = MP_OBJ_TO_PTR(gfxFont->items[2]);
    self->gfxFont->intervals = m_malloc(sizeof(UnicodeInterval) * intervals_tuple->len);
    if (self->gfxFont->intervals == NULL) {
        mp_warning(NULL, "memory allocation failed");
        goto OUT_NO_INTERVALS;
    }

    for (size_t i = 0; i < intervals_tuple->len; i++) {
        mp_obj_tuple_t *intervals_items = MP_OBJ_TO_PTR(intervals_tuple->items[i]);
        self->gfxFont->intervals[i].first  = mp_obj_get_int(intervals_items->items[0]);
        self->gfxFont->intervals[i].last   = mp_obj_get_int(intervals_items->items[1]);
        self->gfxFont->intervals[i].offset = mp_obj_get_int(intervals_items->items[2]);
    }

    self->gfxFont->intervalCount = mp_obj_get_int(gfxFont->items[3]);
    self->gfxFont->compressed    = mp_obj_is_true(gfxFont->items[4]);
    self->gfxFont->yAdvance      = mp_obj_get_int(gfxFont->items[5]);
    self->gfxFont->ascender      = mp_obj_get_int(gfxFont->items[6]);
    self->gfxFont->descender     = mp_obj_get_int(gfxFont->items[7]);
    self->gfxFont->bpp           = mp_obj_get_int(gfxFont->items[8]);

    return mp_const_none;

OUT:
    m_free(self->gfxFont->intervals);
OUT_NO_INTERVALS:
    m_free(self->gfxFont->glyph);
OUT_NO_GLYPH:
    m_free(self->gfxFont->bitmap);
OUT_NO_BITMAP:
    m_free(self->gfxFont);
    self->gfxFont = NULL;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_gfx_obj, 1, 2, framebuf_gfx);

// #define MIN(x, y) ((x) < (y) ? (x) : (y))
// #define MAX(x, y) ((x) > (y) ? (x) : (y))

typedef uint32_t (*alpha_blend_t)(const FontProperties *, uint8_t , uint32_t);

// TODO: It also lacks a good anti-aliasing algorithm.
static uint32_t alpha_blend_rgb565(const FontProperties *props, uint8_t bpp, uint32_t alpha)
{
#if 1
    // https://blog.csdn.net/babyshan1/article/details/90747629
    uint16_t r = 0, g = 0, b = 0;
    uint16_t bpp_multiple = 1 << bpp;

    if ((props->fg_color == 0xffff) && (props->bg_color == 0)) {
        r = alpha;
        g = alpha;
        b = alpha;
    } else {
        if (alpha == (bpp_multiple -1)) {
            return props->fg_color;
        } else if (alpha == 0) {
            return props->bg_color;
        } else {
            uint8_t fg = (uint8_t)(props->fg_color & 0xF800 >> 8);
            uint8_t bg = (uint8_t)(props->bg_color & 0xF800 >> 8);
            r = ((int)(fg * alpha) + (bg * (bpp_multiple - alpha))) >> 8;

            fg = (uint8_t)(props->fg_color & 0x07E0 >> 3);
            bg = (uint8_t)(props->bg_color & 0x07E0 >> 3);
            g = ((int)(fg * alpha) + (bg * (bpp_multiple - alpha))) >> 8;

            fg = (uint8_t)((props->fg_color & 0x001F) << 3);
            bg = (uint8_t)((props->bg_color & 0x001F) << 3);
            b = ((int)(fg * alpha) + (bg * (bpp_multiple - alpha))) >> 8;
        }
    }

    return (((b >> 3) & 0x1F) << 0) | \
           (((g << 2) & 0x3F) << 5) | \
           (((r >> 3) & 0x1F) << 11);
#else
    // https://www.amobbs.com/thread-5676479-1-1.html
    float alpha_factor;

    uint16_t bpp_multiple = 1 << bpp;
    uint8_t color_r = (props->fg_color) >> 11 << 3;
    uint8_t color_g = (props->fg_color) << 5 >> 10 << 2;
    uint8_t color_b = (props->fg_color) << 3;

    uint8_t back_color_r = (props->bg_color) >> 11 << 3;
    uint8_t back_color_g = (props->bg_color) << 5 >> 10 << 2;
    uint8_t back_color_b = (props->bg_color) << 3;

    if (alpha == (bpp_multiple -1)) {
        return props->fg_color;
    } else if (alpha == 0) {
        return props->bg_color;
    } else {
        alpha_factor = 1.0 * alpha / (bpp_multiple -1);
        return ((((uint8_t)(alpha_factor * (color_r - back_color_r)) + back_color_r) >> 3) << 11) | \
               ((((uint8_t)(alpha_factor * (color_g - back_color_g)) + back_color_g) >> 2) << 5) | \
               ((((uint8_t)(alpha_factor * (color_b - back_color_b)) + back_color_b) >> 3));
    }
#endif
}


uint32_t gs4_alpha_blend(const FontProperties *props, uint8_t bpp, uint32_t alpha)
{
    uint16_t bpp_multiple = 1 << bpp;
    int32_t color_difference = (int32_t)props->fg_color - (int32_t)props->bg_color;
    uint8_t temp = MAX(0, MIN(15, props->bg_color + ((int32_t)alpha) * color_difference / (bpp_multiple - 1)));
    return temp;
}


STATIC alpha_blend_t alpha_blends[] = {
    [FRAMEBUF_MVLSB]    = gs4_alpha_blend,    // TODO
    [FRAMEBUF_RGB565]   = alpha_blend_rgb565,
    [FRAMEBUF_GS2_HMSB] = gs4_alpha_blend,    // TODO
    [FRAMEBUF_GS4_HMSB] = gs4_alpha_blend,
    [FRAMEBUF_GS8]      = gs4_alpha_blend,    // TODO
    [FRAMEBUF_MHLSB]    = gs4_alpha_blend,    // TODO
    [FRAMEBUF_MHMSB]    = gs4_alpha_blend,    // TODO
    [FRAMEBUF_GS4_HLSB] = gs4_alpha_blend,
};

// args:
//     0    1   2 3 4
//     self str x y pops
//
// TODO: Text Color
// TEST:
//   compressed:
//     before: 9302111 us
//     after: 9307873 us
//     consume: 5762 us
//   uncompressed:
//     before: 258058460 us
//     after: 258061035 us
//     consume: 2575 us
STATIC mp_obj_t framebuf_write(size_t n_args, const mp_obj_t *args_in) {
    // extract arguments
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    if (!self->gfxFont) {
        mp_warning(NULL, "no usable gfx font found");
        return mp_const_none;
    }

    const char *str = mp_obj_str_get_str(args_in[1]);
    mp_int_t x0 = mp_obj_get_int(args_in[2]);
    mp_int_t y0 = mp_obj_get_int(args_in[3]);
    FontProperties props;
    props.fg_color = 0x0000;
    props.bg_color = 0xFFFF;
    if (n_args >= 5) {
        mp_obj_tuple_t *props_in = MP_OBJ_TO_PTR(args_in[4]);
        props.fg_color = mp_obj_get_int(props_in->items[0]);
        props.bg_color = mp_obj_get_int(props_in->items[1]);
    }

    // draw char
    int32_t local_cursor_x = x0;
    int32_t local_cursor_y = y0;
    uint32_t cp;

    while ((cp = next_cp((uint8_t **)&str))) {
        GFXglyph *glyph = NULL;
        glyph = font_get_glyph(self->gfxFont, cp);
        if (glyph == NULL) {
            glyph = font_get_glyph(self->gfxFont, 0);
        }
        if (glyph == NULL) {
            mp_warning(NULL, "can't find glyph(@%d)", cp);
            continue ;
        }

        uint8_t *bitmap = glygp_get_bitmap(self->gfxFont, glyph);

        // x y --> glyph
        // xx yy --> framebuf
        for (int32_t y = 0; y < glyph->height; y++) {
            int32_t yy = local_cursor_y - glyph->top + y;
            if (yy < 0 || yy >= self->height) {
                continue;
            }
            int32_t start_pos = local_cursor_x + glyph->left;
            int32_t x = MAX(0, -start_pos);
            int32_t max_x = MIN(start_pos + glyph->width, self->width);
            for (int32_t xx = start_pos; xx < max_x; xx++) {
                uint32_t alpha = glygp_get_alpha(self->gfxFont, glyph, bitmap, x, y);
                uint32_t col = alpha_blends[self->format](&props, self->gfxFont->bpp, alpha);
                setpixel(self, xx, yy, col);
                x++;
            }
        }

        local_cursor_x += glyph->xAdvance;

        if (self->gfxFont->compressed) {
            m_free(bitmap);
        }
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_write_obj, 4, 5, framebuf_write);

STATIC mp_obj_t framebuf_get_text_size(size_t n_args, const mp_obj_t *args_in) {
    // extract arguments
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    const char *str = mp_obj_str_get_str(args_in[1]);

    int32_t w = 0, h = 0;
    mp_obj_t value[2];

    if (!self->gfxFont) {
        return mp_const_none;
    }

    font_get_str_szie(self->gfxFont, str, &w, &h);
    // mp_printf(&mp_plat_print, "w: %d, h: %d", w, h);

    value[0] = mp_obj_new_int(w);
    value[1] = mp_obj_new_int(h);

    return mp_obj_new_tuple(2, value);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_get_text_size_obj, 2, 2, framebuf_get_text_size);
#endif // SUPPORT_GFX_FONT

#if SUPPORT_JPG
// Returns number of bytes read (zero on error)
STATIC unsigned int buffer_in_func(JDEC *jd, uint8_t *buff, unsigned int nbyte) {
    IODEV *dev = (IODEV *) jd->device;

    mp_printf(&mp_plat_print, "buffer_in_func\n");

    if (dev->data_index + nbyte > dev->data_len) {
        nbyte = dev->data_len - dev->data_index;
    }

    if (buff) {
        memcpy(buff, (uint8_t *) (dev->data + dev->data_index), nbyte);
    }

    dev->data_index += nbyte;
    return nbyte;
}


// file input function
STATIC unsigned int file_in_func(JDEC *jd, uint8_t *buff, unsigned int nbyte) {
    IODEV *dev = (IODEV *)jd->device;
    unsigned int nread;
    int errcode;

    if (buff) { // Read data from input stream
        nread = (unsigned int)mp_stream_rw(dev->fp, buff, nbyte, &errcode, MP_STREAM_RW_READ);
        return nread;
    }

    // Remove data from input stream if buff was NULL
    // mp_seek(dev->fp, nbyte, SEEK_CUR);
    return 0;
}

STATIC int out_fast(JDEC *jd, void *bitmap, JRECT *rect) {
#if 0
    IODEV *dev = (IODEV *)jd->device;
    uint8_t *bitmap_ptr = (uint8_t *)bitmap;
    uint16_t x_offs = dev->x_offs;
    uint16_t y_offs = dev->y_offs;
    uint16_t x = 0;
    uint16_t y = 0;

    mp_printf(&mp_plat_print, "out_fast top: %d, bottom: %d, left: %d, right: %d\n",
        rect->top,
        rect->bottom,
        rect->left,
        rect->right
    );

    for (y = rect->top; y <= rect->bottom; y++) {
        for (x = rect->left; x <= rect->right; x++) {
            uint8_t r = *(bitmap_ptr++);
            uint8_t g = *(bitmap_ptr++);
            uint8_t b = *(bitmap_ptr++);
            uint32_t gamme_val = (r * 38 + g * 75 + b * 15) >> 7;
            // setpixel_checked(
            //     dev->framebuf_obj,
            //     x_offs + x,
            //     y_offs + y,
            //     (gs8_curve[gamme_val] >> 4),
            //     1
            // );
        }
    }
#endif

    IODEV *dev = (IODEV*)jd->device;   /* Session identifier (5th argument of jd_prepare function) */
    uint8_t *src, *dst;
    uint16_t y, bws;
    unsigned int bwd;

    /* Progress indicator */
    if (rect->left == 0) {
        printf("\r%lu%%", (rect->top << jd->scale) * 100UL / jd->height);
    }

    /* Copy the output image rectangle to the frame buffer */
    src = (uint8_t*)bitmap;                           /* Output bitmap */
    dst = dev->fbuf + 3 * (rect->top * dev->wfbuf + rect->left);  /* Left-top of rectangle in the frame buffer */
    bws = 3 * (rect->right - rect->left + 1);     /* Width of the rectangle [byte] */
    bwd = 3 * dev->wfbuf;                         /* Width of the frame buffer [byte] */
    for (y = rect->top; y <= rect->bottom; y++) {
        memcpy(dst, src, bws);   /* Copy a line */
        src += bws; dst += bwd;  /* Next line */
    }

    return 1; // Continue to decompress
}

STATIC mp_obj_t framebuf_jpg(size_t n_args, const mp_obj_t *args_in) {
    // extract arguments
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = 0;
    uint16_t y = 0;

    if (n_args >= 4) {
        y = mp_obj_get_int(args_in[3]);
        if (y > self->height) {
            return mp_const_none;
        }
    }

    if (n_args >= 3) {
        x = mp_obj_get_int(args_in[2]);
        if (x > self->width) {
            return mp_const_none;
        }
    }

    mp_buffer_info_t bufinfo;
    IODEV devid;
    JRESULT res;
    JDEC jdec;
    uint8_t *work = NULL;
    unsigned int (*input_func)(JDEC*, uint8_t*, unsigned int) = NULL;

    if (mp_obj_is_str(args_in[1])) {
        // const char *filename = mp_obj_str_get_str(args_in[1]);
        mp_obj_t vfs_args[2] = {
            args_in[1],
            MP_OBJ_NEW_QSTR(MP_QSTR_rb),
        };
        devid.fp = mp_vfs_open(MP_ARRAY_SIZE(vfs_args), &vfs_args[0], (mp_map_t *)&mp_const_empty_map);
        devid.data = MP_OBJ_NULL;
        devid.data_len = 0;
        input_func = file_in_func;
    } else if (mp_obj_is_type(args_in[1], &mp_type_bytes)) {
        mp_get_buffer_raise(args_in[1], &bufinfo, MP_BUFFER_READ);
        devid.data_index = 0;
        devid.data = bufinfo.buf;
        devid.data_len = bufinfo.len;
        devid.fp = MP_OBJ_NULL;
        input_func = buffer_in_func;
    } else {
        return mp_const_none;
    }

    work = (uint8_t *)m_malloc(3100);
    if (work == NULL) {
        goto OUT_OF_MEMORY;
    }

    res = jd_prepare(&jdec, input_func, work, 3100, &devid);
    if (res != JDR_OK) {
        goto OUT_PREPARE;
    }

    devid.fbuf = (uint8_t *)m_malloc(jdec.width * jdec.height * 3);
    devid.wfbuf = jdec.width;

    res = jd_decomp(&jdec, out_fast, 0);
    if (res != JDR_OK) {
        goto OUT_OF_DECOMP;
    }

    if (devid.fbuf != NULL) {
        uint8_t *bitmap_ptr = &devid.fbuf[0];
        for (size_t yy = 0; yy < jdec.width; yy++) {
            for (size_t xx = 0; xx < jdec.height; xx++) {
                uint8_t r = *(bitmap_ptr++);
                uint8_t g = *(bitmap_ptr++);
                uint8_t b = *(bitmap_ptr++);
                setpixel_checked(
                    self,
                    x + xx,
                    y + yy,
                    converts[self->format](r, g, b),
                    1
                );
            }
        }
    } else {
        mp_warning(NULL, "No Output frame buffer");
    }

    if (devid.fp != MP_OBJ_NULL) {
        mp_stream_close(devid.fp);
    }

    if (devid.fbuf != NULL) {
        m_free(devid.fbuf);
    }

    m_free(work);
    mp_obj_t value[2];
    value[0] = mp_obj_new_int(jdec.width);
    value[1] = mp_obj_new_int(jdec.height);
    return mp_obj_new_tuple(2, value);
OUT_OF_MEMORY:
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("out of memory(tjpgd work)"));
    return mp_const_none;
OUT_PREPARE:
    m_free(work);
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s(jd_prepare)"), jd_errors[res]);
    return mp_const_none;
OUT_OF_DECOMP:
    m_free(work);
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s(jd_decomp)"), jd_errors[res]);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_jpg_obj, 2, 4, framebuf_jpg);
#endif // SUPPORT_JPG

#if !MICROPY_ENABLE_DYNRUNTIME
STATIC const mp_rom_map_elem_t framebuf_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&framebuf_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_rect), MP_ROM_PTR(&framebuf_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&framebuf_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_hline), MP_ROM_PTR(&framebuf_hline_obj) },
    { MP_ROM_QSTR(MP_QSTR_vline), MP_ROM_PTR(&framebuf_vline_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&framebuf_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&framebuf_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_ellipse), MP_ROM_PTR(&framebuf_ellipse_obj) },
    #if MICROPY_PY_ARRAY
    { MP_ROM_QSTR(MP_QSTR_poly), MP_ROM_PTR(&framebuf_poly_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_blit), MP_ROM_PTR(&framebuf_blit_obj) },
    { MP_ROM_QSTR(MP_QSTR_scroll), MP_ROM_PTR(&framebuf_scroll_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&framebuf_text_obj) },
    #if SUPPORT_GFX_FONT
    { MP_ROM_QSTR(MP_QSTR_gfx), MP_ROM_PTR(&framebuf_gfx_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&framebuf_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_text_size), MP_ROM_PTR(&framebuf_get_text_size_obj) },
    #endif
    #if SUPPORT_JPG
    { MP_ROM_QSTR(MP_QSTR_jpg), MP_ROM_PTR(&framebuf_jpg_obj) },
    #endif
};
STATIC MP_DEFINE_CONST_DICT(framebuf_locals_dict, framebuf_locals_dict_table);

STATIC const mp_obj_type_t mp_type_framebuf = {
    { &mp_type_type },
    .name = MP_QSTR_FrameBuffer,
    .make_new = framebuf_make_new,
    .buffer_p = { .get_buffer = framebuf_get_buffer },
    .locals_dict = (mp_obj_dict_t *)&framebuf_locals_dict,
};
#endif

// this factory function is provided for backwards compatibility with old FrameBuffer1 class
STATIC mp_obj_t legacy_framebuffer1(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *o = mp_obj_malloc(mp_obj_framebuf_t, (mp_obj_type_t *)&mp_type_framebuf);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args_in[0], &bufinfo, MP_BUFFER_WRITE);
    o->buf = bufinfo.buf;

    o->width = mp_obj_get_int(args_in[1]);
    o->height = mp_obj_get_int(args_in[2]);
    o->format = FRAMEBUF_MVLSB;
    if (n_args >= 4) {
        o->stride = mp_obj_get_int(args_in[3]);
    } else {
        o->stride = o->width;
    }

    return MP_OBJ_FROM_PTR(o);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(legacy_framebuffer1_obj, 3, 4, legacy_framebuffer1);

#if !MICROPY_ENABLE_DYNRUNTIME
STATIC const mp_rom_map_elem_t framebuf_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_framebuf) },
    { MP_ROM_QSTR(MP_QSTR_FrameBuffer), MP_ROM_PTR(&mp_type_framebuf) },
    { MP_ROM_QSTR(MP_QSTR_FrameBuffer1), MP_ROM_PTR(&legacy_framebuffer1_obj) },
    { MP_ROM_QSTR(MP_QSTR_MVLSB), MP_ROM_INT(FRAMEBUF_MVLSB) },
    { MP_ROM_QSTR(MP_QSTR_MONO_VLSB), MP_ROM_INT(FRAMEBUF_MVLSB) },
    { MP_ROM_QSTR(MP_QSTR_RGB565), MP_ROM_INT(FRAMEBUF_RGB565) },
    { MP_ROM_QSTR(MP_QSTR_GS2_HMSB), MP_ROM_INT(FRAMEBUF_GS2_HMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS4_HMSB), MP_ROM_INT(FRAMEBUF_GS4_HMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS8), MP_ROM_INT(FRAMEBUF_GS8) },
    { MP_ROM_QSTR(MP_QSTR_MONO_HLSB), MP_ROM_INT(FRAMEBUF_MHLSB) },
    { MP_ROM_QSTR(MP_QSTR_MONO_HMSB), MP_ROM_INT(FRAMEBUF_MHMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS4_HLSB), MP_ROM_INT(FRAMEBUF_GS4_HLSB) },
};

STATIC MP_DEFINE_CONST_DICT(framebuf_module_globals, framebuf_module_globals_table);

const mp_obj_module_t mp_module_framebuf_plus = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&framebuf_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_framebuf_plus, mp_module_framebuf_plus);
#endif

#endif // MICROPY_PY_FRAMEBUF
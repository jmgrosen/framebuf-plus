#include "gfxfont.h"
#include "zlib/zlib.h"
#include "py/runtime.h"
#include "utf8_rosetta.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

typedef uint32_t (*getsize_t)(const GFXglyph *);
typedef uint8_t * (*getbitmap_t)(const GFXfont *font, const GFXglyph *);
typedef uint32_t (*getalpha_t)(const GFXglyph *, const uint8_t *, int32_t, int32_t);

typedef struct _glyph_p_t {
    getsize_t getsize;
    getbitmap_t getbitmap;
    getalpha_t getalpha;
} glyph_p_t;

// untested
static inline uint32_t getsize_1bpp(const GFXglyph *glyph) {
    return ((glyph->width / 8 + glyph->width % 8) * glyph->height);
}

// untested
static inline uint32_t getsize_2bpp(const GFXglyph *glyph) {
    return ((glyph->width / 4 + glyph->width % 4) * glyph->height);
}


static inline uint32_t getsize_4bpp(const GFXglyph *glyph) {
    return ((glyph->width / 2 + glyph->width % 2) * glyph->height);
}


static inline uint32_t getsize_8bpp(const GFXglyph *glyph) {
    return (glyph->width * glyph->height);
}

// untested
static uint8_t *getbitmap_1bpp(const GFXfont *font, const GFXglyph *glyph) {
    unsigned long bitmap_size = getsize_1bpp(glyph);
    uint8_t *bitmap = NULL;
    if (font->compressed) {
        bitmap = (uint8_t *)m_malloc(bitmap_size);
        uncompress(bitmap, &bitmap_size, &font->bitmap[glyph->dataOffset], glyph->compressedSize);
    } else {
        bitmap = &font->bitmap[glyph->dataOffset];
    }
    return bitmap;
}

// untested
static uint8_t *getbitmap_2bpp(const GFXfont *font, const GFXglyph *glyph) {
    unsigned long bitmap_size = getsize_2bpp(glyph);
    uint8_t *bitmap = NULL;
    if (font->compressed) {
        bitmap = (uint8_t *)m_malloc(bitmap_size);
        uncompress(bitmap, &bitmap_size, &font->bitmap[glyph->dataOffset], glyph->compressedSize);
    } else {
        bitmap = &font->bitmap[glyph->dataOffset];
    }
    return bitmap;
}


static uint8_t *getbitmap_4bpp(const GFXfont *font, const GFXglyph *glyph) {
    unsigned long bitmap_size = getsize_4bpp(glyph);
    uint8_t *bitmap = NULL;
    if (font->compressed) {
        bitmap = (uint8_t *)m_malloc(bitmap_size);
        uncompress(bitmap, &bitmap_size, &font->bitmap[glyph->dataOffset], glyph->compressedSize);
    } else {
        bitmap = &font->bitmap[glyph->dataOffset];
    }
    return bitmap;
}


static uint8_t *getbitmap_8bpp(const GFXfont *font, const GFXglyph *glyph) {
    unsigned long bitmap_size = getsize_8bpp(glyph);
    uint8_t *bitmap = NULL;
    if (font->compressed) {
        bitmap = (uint8_t *)m_malloc(bitmap_size);
        uncompress(bitmap, &bitmap_size, &font->bitmap[glyph->dataOffset], glyph->compressedSize);
    } else {
        bitmap = &font->bitmap[glyph->dataOffset];
    }
    return bitmap;
}

// untested
static inline uint32_t getalpha_1bpp(const GFXglyph *glyph, const uint8_t *bitmap, int32_t x, int32_t y)
{
    int32_t byte_width = (glyph->width / 8 + glyph->width % 8);
    uint8_t bm = bitmap[y * byte_width + x / 8];
    return ((bm >> (x / 8)) & 0x01);
}

// untested
static inline uint32_t getalpha_2bpp(const GFXglyph *glyph, const uint8_t *bitmap, int32_t x, int32_t y)
{
    int32_t byte_width = (glyph->width / 4 + glyph->width % 4);
    uint8_t bm = bitmap[y * byte_width + x / 4];
    return ((bm >> (2 * (x / 4))) & 0x03);
}


static inline uint32_t getalpha_4bpp(const GFXglyph *glyph, const uint8_t *bitmap, int32_t x, int32_t y)
{
    int32_t byte_width = (glyph->width / 2 + glyph->width % 2);
    uint8_t bm = bitmap[y * byte_width + x / 2];
    if ((x & 1) == 0) {
        bm = bm & 0xF;
    } else {
        bm = bm >> 4;
    }
    return bm;
}


static uint32_t getalpha_8bpp(const GFXglyph *glyph, const uint8_t *bitmap, int32_t x, int32_t y)
{
    return bitmap[y * glyph->width + x];
}


const static glyph_p_t glyph_p[] = {
    [GFX_FORMAT_1BPP] = {getsize_1bpp, getbitmap_1bpp, getalpha_1bpp},
    [GFX_FORMAT_2BPP] = {getsize_2bpp, getbitmap_2bpp, getalpha_2bpp},
    [GFX_FORMAT_4BPP] = {getsize_4bpp, getbitmap_4bpp, getalpha_4bpp},
    [GFX_FORMAT_8BPP] = {getsize_8bpp, getbitmap_8bpp, getalpha_8bpp},
};


static void get_char_bounds(
    const GFXfont *font,
    uint32_t cp,
    int32_t *x,
    int32_t *y,
    int32_t *minx,
    int32_t *miny,
    int32_t *maxx,
    int32_t *maxy)
{
    GFXglyph *glyph = font_get_glyph(font, cp);
    if (!glyph) {
        glyph = font_get_glyph(font, 0);
    }

    if (!glyph) return ;

    int32_t x1 = *x + glyph->left;
    int32_t y1 = *y + (glyph->top - glyph->height);
    int32_t x2 = x1 + glyph->width;
    int32_t y2 = y1 + glyph->height;

    // background needs to be taken into account
    if (x1 < *minx)
        *minx = x1;
    if (y1 < *miny)
        *miny = y1;
    if (x2 > *maxx)
        *maxx = x2;
    if (y2 > *maxy)
        *maxy = y2;

    *x += glyph->xAdvance;
}


void font_get_str_szie(const GFXfont *font, const char *str, int32_t *w, int32_t *h) {
    if (!str) {
        *w = 0;
        *h = 0;
        return ;
    }
    int32_t minx = 100000, miny = 100000, maxx = -1, maxy = -1;
    int32_t x = 200;
    int32_t y = 200;
    uint32_t c;
    while ((c = next_cp((uint8_t **)&str))) {
        get_char_bounds(font, c, &x, &y, &minx, &miny, &maxx, &maxy);
    }
    *w = maxx - MIN(x, minx);
    *h = maxy - miny;
}


GFXglyph *font_get_glyph(const GFXfont *font, uint32_t code_point)
{
    UnicodeInterval *intervals = font->intervals;
    for (int32_t i = 0; i < font->intervalCount; i++) {
        UnicodeInterval *interval = &intervals[i];
        if (code_point >= interval->first && code_point <= interval->last) {
            return &font->glyph[interval->offset + (code_point - interval->first)];
        }
        if (code_point < interval->first) {
            return NULL;
        }
    }
    return NULL;
}


void font_get_describe(const GFXfont *font, char *describe, int len)
{
    size_t pos = 0;

    pos += snprintf(&describe[pos], (len - pos), "BPP: %d\n", font->bpp);
    pos += snprintf(&describe[pos], (len - pos), "Unicode Range: \n");
    for (size_t i = 0; i < font->intervalCount; i++) {
        pos += snprintf(&describe[pos], (len - pos), "  0x%x-0x%x\n", font->intervals[i].first, font->intervals[i].last);
    }
    if (font->compressed) {
        pos += snprintf(&describe[pos], (len - pos), "Compressed: True\n");
    } else {
        pos += snprintf(&describe[pos], (len - pos), "Compressed: False\n");
    }
    pos += snprintf(&describe[pos], (len - pos), "Newline Distance: %d\n", font->yAdvance);
    pos += snprintf(&describe[pos], (len - pos), "Ascender: %d\n", font->ascender);
    pos += snprintf(&describe[pos], (len - pos), "Descender: %d\n", font->descender);
}


uint32_t glygp_get_bitmap_size(const GFXfont *font, const GFXglyph *glyph)
{
    return glyph_p[font->bpp].getsize(glyph);
}


uint8_t *glygp_get_bitmap(const GFXfont *font, const GFXglyph *glyph)
{
    return glyph_p[font->bpp].getbitmap(font, glyph);
}


uint32_t glygp_get_alpha(const GFXfont *font, const GFXglyph *glyph, const uint8_t *bitmap, int32_t x, int32_t y)
{
    return glyph_p[font->bpp].getalpha(glyph, bitmap, x, y);
}

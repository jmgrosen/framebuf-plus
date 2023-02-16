#ifndef _GFXFONT_H_
#define _GFXFONT_H_

#include <stdint.h>
#include <stdbool.h>

#define GFX_FORMAT_1BPP (1)
#define GFX_FORMAT_2BPP (2)
#define GFX_FORMAT_4BPP (4)
#define GFX_FORMAT_8BPP (8)

/**
 * @brief Font data stored PER GLYPH
 */
typedef struct {
    uint8_t width;           /** Bitmap dimensions in pixels */
    uint8_t height;          /** Bitmap dimensions in pixels */
    uint8_t xAdvance;        /** Distance to advance cursor (x axis) */
    int16_t left;            /** X dist from cursor pos to UL corner */
    int16_t top;             /** Y dist from cursor pos to UL corner */
    uint16_t compressedSize; /** Size of the zlib-compressed font data. */
    uint32_t dataOffset;     /** Pointer into GFXfont->bitmap */
} GFXglyph;

/**
 * @brief Glyph interval structure
 */
typedef struct {
    uint32_t first;  /** The first unicode code point of the interval */
    uint32_t last;   /** The last unicode code point of the interval */
    uint32_t offset; /** Index of the first code point into the glyph array */
} UnicodeInterval;

/**
 * @brief Data stored for FONT AS A WHOLE
 */
typedef struct {
    uint8_t         *bitmap;        /** Glyph bitmaps, concatenated */
    GFXglyph        *glyph;         /** Glyph array */
    UnicodeInterval *intervals;     /** Valid unicode intervals for this font */
    uint32_t         intervalCount; /** Number of unicode intervals. */
    bool             compressed;    /** Does this font use compressed glyph bitmaps? */
    uint8_t          yAdvance;      /** Newline distance (y axis) */
    int32_t          ascender;      /** Maximal height of a glyph above the base line */
    int32_t          descender;     /** Maximal height of a glyph below the base line */
    uint8_t          bpp;
} GFXfont;


/**
 * @brief Font properties.
 */
typedef struct {
    uint32_t fg_color; /** Foreground color */
    uint32_t bg_color; /** Background color */
} FontProperties;

void font_get_describe(const GFXfont *font, char *describe, int len);
void font_get_str_szie(const GFXfont *font, const char *str, int32_t *w, int32_t *h);
GFXglyph * font_get_glyph(const GFXfont *font, uint32_t code_point);

uint32_t glygp_get_bitmap_size(const GFXfont *font, const GFXglyph *glyph);
uint8_t *glygp_get_bitmap(const GFXfont *font, const GFXglyph *glyph);
uint32_t glygp_get_alpha(const GFXfont *font, const GFXglyph *glyph, const uint8_t *bitmap, int32_t x, int32_t y);

#endif // _GFXFONT_H_

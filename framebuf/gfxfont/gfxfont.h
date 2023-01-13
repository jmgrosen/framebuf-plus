#ifndef _GFXFONT_H_
#define _GFXFONT_H_

#include <stdint.h>
#include <stdbool.h>

// constants for formats
#define GFX_FORMAT_MVLSB    (0)
#define GFX_FORMAT_RGB565   (1)
#define GFX_FORMAT_GS2_HMSB (5)
#define GFX_FORMAT_GS4_HMSB (2)
#define GFX_FORMAT_GS8      (6)
#define GFX_FORMAT_MHLSB    (3)
#define GFX_FORMAT_MHMSB    (4)
#define GFX_FORMAT_GS4_HLSB (7)

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
    uint8_t          format;
} GFXfont;


/**
 * @brief Font properties.
 */
typedef struct {
    uint8_t foregroundColor: 4; /** Foreground color */
    uint8_t BackgroundColor: 4; /** Background color */
} FontProperties;

uint32_t nextCodepoint(uint8_t **str);

void getStringSzie(const GFXfont *font, const char *str, int32_t *w, int32_t *h);
GFXglyph * getGlyph(const GFXfont *font, uint32_t code_point);

#endif // _GFXFONT_H_

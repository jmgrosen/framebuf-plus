#include "gfxfont.h"
#include "zlib/zlib.h"

#include "py/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static inline int32_t min(int32_t x, int32_t y) {
    return x < y ? x : y;
}

static inline int32_t max(int32_t x, int32_t y) {
    return x > y ? x : y;
}


/**
 * @brief UTF-8 decode inspired from rosetta code
 *
 * https://rosettacode.org/wiki/UTF-8_encode_and_decode#C
 */
typedef struct {
    char mask;    /* char data will be bitwise AND with this */
    char lead;    /* start bytes of current char in utf-8 encoded character */
    uint32_t beg; /* beginning of codepoint range */
    uint32_t end; /* end of codepoint range */
    int bits_stored; /* the number of bits from the codepoint that fits in char */
}utf_t;

static const utf_t *utf[] = {
    /*             mask        lead        beg      end       bits */
    [0] = &(utf_t){0b00111111, 0b10000000, 0,       0,        6},
    [1] = &(utf_t){0b01111111, 0b00000000, 0000,    0177,     7},
    [2] = &(utf_t){0b00011111, 0b11000000, 0200,    03777,    5},
    [3] = &(utf_t){0b00001111, 0b11100000, 04000,   0177777,  4},
    [4] = &(utf_t){0b00000111, 0b11110000, 0200000, 04177777, 3},
    &(utf_t){0},
};

static int32_t utf8_len(const uint8_t ch)
{
    int32_t len = 0;
    for (const utf_t **u = utf; *u; ++u) {
        if ((ch & ~(*u)->mask) == (*u)->lead) {
            break;
        }
        ++len;
    }
    if (len > 4) {
        /* Malformed leading byte */
        assert("invalid unicode.");
    }
    return len;
}

uint32_t nextCodepoint(uint8_t **str)
{
    if (**str == 0) return 0;

    int32_t bytes = utf8_len(**str);
    uint8_t *chr = *str;
    *str += bytes;
    int32_t shift = utf[0]->bits_stored * (bytes - 1);
    uint32_t codep = (*chr++ & utf[bytes]->mask) << shift;

    for (int32_t i = 1; i < bytes; ++i, ++chr) {
        shift -= utf[0]->bits_stored;
        codep |= ((uint8_t)*chr & utf[0]->mask) << shift;
    }

    return codep;
}


static void get_char_bounds(const GFXfont *font,
                            uint32_t cp,
                            int32_t *x,
                            int32_t *y,
                            int32_t *minx,
                            int32_t *miny,
                            int32_t *maxx,
                            int32_t *maxy)
{
    GFXglyph *glyph = NULL;
    glyph = getGlyph(font, cp);

    if (!glyph) {
        glyph = getGlyph(font, 0);
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


void getStringSzie(const GFXfont *font, const char *str, int32_t *w, int32_t *h) {
    if (!str) {
        *w = 0;
        *h = 0;
        return ;
    }
    int32_t minx = 100000, miny = 100000, maxx = -1, maxy = -1;
    int32_t x = 200;
    int32_t y = 200;
    uint32_t c;
    while ((c = nextCodepoint((uint8_t **)&str)))
    {
        get_char_bounds(font, c, &x, &y, &minx, &miny, &maxx, &maxy);
    }
    *w = maxx - min(x, minx);
    *h = maxy - miny;
}


GFXglyph * getGlyph(const GFXfont *font, uint32_t code_point)
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
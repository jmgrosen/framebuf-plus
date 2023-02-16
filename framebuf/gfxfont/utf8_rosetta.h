#ifndef _UTF8_ROSETTA_H_
#define _UTF8_ROSETTA_H_

#include <stdint.h>
// https://rosettacode.org/wiki/UTF-8_encode_and_decode#C

/* All lengths are in bytes */
int codepoint_len(const uint32_t cp); /* len of associated utf-8 char */
int32_t utf8_len(const uint8_t ch);   /* len of utf-8 encoded char */

char *to_utf8(const uint32_t cp);
uint32_t to_cp(const char chr[4]);
uint32_t next_cp(uint8_t **str);

#endif

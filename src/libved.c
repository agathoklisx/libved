#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
/* This is for DT_* from dirent.h for readdir() as glibc defines these fields.
 * This is convenient as we avoid at least a lstat() and a couple of macro calls. */

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>

#include "libved.h"
#include "__libved.h"

private int str_eq (const char *sa, const char *sb) {
  const uchar *spa = (const uchar *) sa;
  const uchar *spb = (const uchar *) sb;
  for (; *spa == *spb; spa++, spb++)
    if (*spa == 0) return 1;

  return 0;
}

private int str_cmp_n (const char *sa, const char *sb, size_t n) {
  const uchar *spa = (const uchar *) sa;
  const uchar *spb = (const uchar *) sb;
  for (;n--; spa++, spb++) {
    if (*spa != *spb)
      return (*(uchar *) spa - *(uchar *) spb);

    if (*spa == 0) return 0;
  }

  return 0;
}

/* just for clarity */
private int str_eq_n  (const char *sa, const char *sb, size_t n) {
  return (0 == str_cmp_n (sa, sb, n));
}

/* the signature changed as in this namespace, size has been already computed */
private char *str_dup (const char *src, size_t len) {
  /* avoid recomputation */
  // size_t len = bytelen (src);
  char *dest = Alloc (len + 1);
  if (len) memcpy (dest, src, len);
  dest[len] = '\0';
  return dest;
}

private char *byte_in_str (const char *s, int c) {
  const char *sp = s;
  while (*sp != c) {
    if (*sp == 0) return NULL;
    sp++;
  }
  return (char *)sp;
}

private char *nullbyte_in_str (const char *s) {
  return byte_in_str (s, 0);
}

private char *str_trim_end (char *bytes, char c) {
  char *sp = nullbyte_in_str (bytes);
  sp--;

  while (sp >= bytes) {
    if (*sp isnot c) break;
    *sp = '\0';
    if (sp is bytes) break;
  }
  return bytes;
}

private char *str_substr (char *dest, size_t len, char *src, size_t src_len, size_t idx) {
  if (src_len < idx + len) {
    dest[0] = '\0';
    return NULL;
  }

  for (size_t i = 0; i < len; i++) dest[i] = src[i+idx];
  dest[len] = '\0';
  return dest;
}

private int ustring_charlen (uchar c) {
  if (c < 0x80) return 1;
  if ((c & 0xe0) is 0xc0) return 2;
  return 3 + ((c & 0xf0) isnot 0xe0);
}

private char *char_nth (char *bytes, int nth, int len) {
  int n = 0;
  int clen = 0;
  char *sp = bytes;

  for (int i = 0; i < len and n < nth; i++) {
    sp += clen;
    clen = (uchar) ustring_charlen (*sp);
    n++;
  }

  if (n isnot nth) return bytes;

  return sp;
}

private int char_num (char *bytes, int len) {
  int n = 0;
  int clen = 0;
  char *sp = bytes;

  for (int i = 0; i < len and *sp; i++) {
    sp += clen;
    clen = ustring_charlen ((uchar) *sp);
    n++;
  }

  return n;
}

private utf8 char_get_nth_code (char *bytes, int nth, int len) {
  char *sp = char_nth (bytes, nth, len);
  if (sp is bytes and nth isnot 1)
    return 0;

  return utf8_code (sp);
}

private int char_is_nth_at (char *bytes, int idx, int len) {
  if (idx >= len) return -1;

  int n = 0;
  int clen = 0;
  char *sp = bytes;

  for (int i = 0; i < len and i <= idx; i++) {
    sp += clen;
    ifnot (*sp) return -1;
    clen = ustring_charlen ((uchar) *sp);
    i += clen - 1;
    n++;
  }

  return n;
}

/* Unused and commented out, but stays as a reference as it looks that works
 * correctrly even for non-ascii composed strings.
 * 
 * private char *string_reverse_from_to (char *dest, char *src, int fidx, int lidx) {
 *   int len = lidx - fidx + 1;
 * 
 *   for (int i = 0; i < len; i++) dest[i] = ' ';
 * 
 *   int curidx = 0;
 *   int tlen = 0;
 * 
 *   uchar c;
 *   for (int i = fidx; i < len + fidx; i++) {
 *     c = src[i];
 *     tlen++;
 * 
 *     if (c < 0x80) {
 *       dest[len - 1 - curidx++] = c;
 *       continue;
 *     }
 * 
 *     int clen = ustring_charlen (c);
 *     tlen += clen - 1;
 * 
 *     for (int ii = 0; ii < clen; ii++) {
 *       uchar cc = src[i + ii];
 *       dest[(len - 1 - curidx) - (clen - ii) + 1] = cc;
 *     }
 * 
 *     curidx += clen;
 *     i += clen - 1;
 *   }
 * 
 *   dest[tlen] = '\0';
 *   return dest;
 * }
*/

/* A wcwidth() adjusted for this environment */

/*
 * https://github.com/termux/wcwidth
 * The MIT License (MIT)
 * Copyright (c) 2016 Fredrik Fornwall <fredrik@fornwall.net>
 * 
 * This license applies to parts originating from the
 * https://github.com/jquast/wcwidth repository:
 * 
 * The MIT License (MIT)
 * 
 * Copyright (c) 2014 Jeff Quast <contact@jeffquast.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

 /* NOTE (by myself): Markus Kuhn should be credited mostly here, as he (except
  * that he is the original author) is the one that helped as few others, the
  * stabilization of UTF8 (the above project does the same in their documents) */

struct width_interval {
  int start;
  int end;
};

// From https://github.com/jquast/wcwidth/blob/master/wcwidth/table_zero.py
// at commit 0d7de112202cc8b2ebe9232ff4a5c954f19d561a (2016-07-02):
static struct width_interval ZERO_WIDTH[] = {
  {0x0300, 0x036f},  // Combining Grave Accent  ..Combining Latin Small Le
  {0x0483, 0x0489},  // Combining Cyrillic Titlo..Combining Cyrillic Milli
  {0x0591, 0x05bd},  // Hebrew Accent Etnahta   ..Hebrew Point Meteg
  {0x05bf, 0x05bf},  // Hebrew Point Rafe       ..Hebrew Point Rafe
  {0x05c1, 0x05c2},  // Hebrew Point Shin Dot   ..Hebrew Point Sin Dot
  {0x05c4, 0x05c5},  // Hebrew Mark Upper Dot   ..Hebrew Mark Lower Dot
  {0x05c7, 0x05c7},  // Hebrew Point Qamats Qata..Hebrew Point Qamats Qata
  {0x0610, 0x061a},  // Arabic Sign Sallallahou ..Arabic Small Kasra
  {0x064b, 0x065f},  // Arabic Fathatan         ..Arabic Wavy Hamza Below
  {0x0670, 0x0670},  // Arabic Letter Superscrip..Arabic Letter Superscrip
  {0x06d6, 0x06dc},  // Arabic Small High Ligatu..Arabic Small High Seen
  {0x06df, 0x06e4},  // Arabic Small High Rounde..Arabic Small High Madda
  {0x06e7, 0x06e8},  // Arabic Small High Yeh   ..Arabic Small High Noon
  {0x06ea, 0x06ed},  // Arabic Empty Centre Low ..Arabic Small Low Meem
  {0x0711, 0x0711},  // Syriac Letter Superscrip..Syriac Letter Superscrip
  {0x0730, 0x074a},  // Syriac Pthaha Above     ..Syriac Barrekh
  {0x07a6, 0x07b0},  // Thaana Abafili          ..Thaana Sukun
  {0x07eb, 0x07f3},  // Nko Combining Sh||t High..Nko Combining Double Dot
  {0x0816, 0x0819},  // Samaritan Mark In       ..Samaritan Mark Dagesh
  {0x081b, 0x0823},  // Samaritan Mark Epentheti..Samaritan Vowel Sign A
  {0x0825, 0x0827},  // Samaritan Vowel Sign Sho..Samaritan Vowel Sign U
  {0x0829, 0x082d},  // Samaritan Vowel Sign Lon..Samaritan Mark Nequdaa
  {0x0859, 0x085b},  // Mandaic Affrication Mark..Mandaic Gemination Mark
  {0x08d4, 0x08e1},  // (nil)                   ..
  {0x08e3, 0x0902},  // Arabic Turned Damma Belo..Devanagari Sign Anusvara
  {0x093a, 0x093a},  // Devanagari Vowel Sign Oe..Devanagari Vowel Sign Oe
  {0x093c, 0x093c},  // Devanagari Sign Nukta   ..Devanagari Sign Nukta
  {0x0941, 0x0948},  // Devanagari Vowel Sign U ..Devanagari Vowel Sign Ai
  {0x094d, 0x094d},  // Devanagari Sign Virama  ..Devanagari Sign Virama
  {0x0951, 0x0957},  // Devanagari Stress Sign U..Devanagari Vowel Sign Uu
  {0x0962, 0x0963},  // Devanagari Vowel Sign Vo..Devanagari Vowel Sign Vo
  {0x0981, 0x0981},  // Bengali Sign Candrabindu..Bengali Sign Candrabindu
  {0x09bc, 0x09bc},  // Bengali Sign Nukta      ..Bengali Sign Nukta
  {0x09c1, 0x09c4},  // Bengali Vowel Sign U    ..Bengali Vowel Sign Vocal
  {0x09cd, 0x09cd},  // Bengali Sign Virama     ..Bengali Sign Virama
  {0x09e2, 0x09e3},  // Bengali Vowel Sign Vocal..Bengali Vowel Sign Vocal
  {0x0a01, 0x0a02},  // Gurmukhi Sign Adak Bindi..Gurmukhi Sign Bindi
  {0x0a3c, 0x0a3c},  // Gurmukhi Sign Nukta     ..Gurmukhi Sign Nukta
  {0x0a41, 0x0a42},  // Gurmukhi Vowel Sign U   ..Gurmukhi Vowel Sign Uu
  {0x0a47, 0x0a48},  // Gurmukhi Vowel Sign Ee  ..Gurmukhi Vowel Sign Ai
  {0x0a4b, 0x0a4d},  // Gurmukhi Vowel Sign Oo  ..Gurmukhi Sign Virama
  {0x0a51, 0x0a51},  // Gurmukhi Sign Udaat     ..Gurmukhi Sign Udaat
  {0x0a70, 0x0a71},  // Gurmukhi Tippi          ..Gurmukhi Addak
  {0x0a75, 0x0a75},  // Gurmukhi Sign Yakash    ..Gurmukhi Sign Yakash
  {0x0a81, 0x0a82},  // Gujarati Sign Candrabind..Gujarati Sign Anusvara
  {0x0abc, 0x0abc},  // Gujarati Sign Nukta     ..Gujarati Sign Nukta
  {0x0ac1, 0x0ac5},  // Gujarati Vowel Sign U   ..Gujarati Vowel Sign Cand
  {0x0ac7, 0x0ac8},  // Gujarati Vowel Sign E   ..Gujarati Vowel Sign Ai
  {0x0acd, 0x0acd},  // Gujarati Sign Virama    ..Gujarati Sign Virama
  {0x0ae2, 0x0ae3},  // Gujarati Vowel Sign Voca..Gujarati Vowel Sign Voca
  {0x0b01, 0x0b01},  // ||iya Sign Candrabindu  ..||iya Sign Candrabindu
  {0x0b3c, 0x0b3c},  // ||iya Sign Nukta        ..||iya Sign Nukta
  {0x0b3f, 0x0b3f},  // ||iya Vowel Sign I      ..||iya Vowel Sign I
  {0x0b41, 0x0b44},  // ||iya Vowel Sign U      ..||iya Vowel Sign Vocalic
  {0x0b4d, 0x0b4d},  // ||iya Sign Virama       ..||iya Sign Virama
  {0x0b56, 0x0b56},  // ||iya Ai Length Mark    ..||iya Ai Length Mark
  {0x0b62, 0x0b63},  // ||iya Vowel Sign Vocalic..||iya Vowel Sign Vocalic
  {0x0b82, 0x0b82},  // Tamil Sign Anusvara     ..Tamil Sign Anusvara
  {0x0bc0, 0x0bc0},  // Tamil Vowel Sign Ii     ..Tamil Vowel Sign Ii
  {0x0bcd, 0x0bcd},  // Tamil Sign Virama       ..Tamil Sign Virama
  {0x0c00, 0x0c00},  // Telugu Sign Combining Ca..Telugu Sign Combining Ca
  {0x0c3e, 0x0c40},  // Telugu Vowel Sign Aa    ..Telugu Vowel Sign Ii
  {0x0c46, 0x0c48},  // Telugu Vowel Sign E     ..Telugu Vowel Sign Ai
  {0x0c4a, 0x0c4d},  // Telugu Vowel Sign O     ..Telugu Sign Virama
  {0x0c55, 0x0c56},  // Telugu Length Mark      ..Telugu Ai Length Mark
  {0x0c62, 0x0c63},  // Telugu Vowel Sign Vocali..Telugu Vowel Sign Vocali
  {0x0c81, 0x0c81},  // Kannada Sign Candrabindu..Kannada Sign Candrabindu
  {0x0cbc, 0x0cbc},  // Kannada Sign Nukta      ..Kannada Sign Nukta
  {0x0cbf, 0x0cbf},  // Kannada Vowel Sign I    ..Kannada Vowel Sign I
  {0x0cc6, 0x0cc6},  // Kannada Vowel Sign E    ..Kannada Vowel Sign E
  {0x0ccc, 0x0ccd},  // Kannada Vowel Sign Au   ..Kannada Sign Virama
  {0x0ce2, 0x0ce3},  // Kannada Vowel Sign Vocal..Kannada Vowel Sign Vocal
  {0x0d01, 0x0d01},  // Malayalam Sign Candrabin..Malayalam Sign Candrabin
  {0x0d41, 0x0d44},  // Malayalam Vowel Sign U  ..Malayalam Vowel Sign Voc
  {0x0d4d, 0x0d4d},  // Malayalam Sign Virama   ..Malayalam Sign Virama
  {0x0d62, 0x0d63},  // Malayalam Vowel Sign Voc..Malayalam Vowel Sign Voc
  {0x0dca, 0x0dca},  // Sinhala Sign Al-lakuna  ..Sinhala Sign Al-lakuna
  {0x0dd2, 0x0dd4},  // Sinhala Vowel Sign Ketti..Sinhala Vowel Sign Ketti
  {0x0dd6, 0x0dd6},  // Sinhala Vowel Sign Diga ..Sinhala Vowel Sign Diga
  {0x0e31, 0x0e31},  // Thai Character Mai Han-a..Thai Character Mai Han-a
  {0x0e34, 0x0e3a},  // Thai Character Sara I   ..Thai Character Phinthu
  {0x0e47, 0x0e4e},  // Thai Character Maitaikhu..Thai Character Yamakkan
  {0x0eb1, 0x0eb1},  // Lao Vowel Sign Mai Kan  ..Lao Vowel Sign Mai Kan
  {0x0eb4, 0x0eb9},  // Lao Vowel Sign I        ..Lao Vowel Sign Uu
  {0x0ebb, 0x0ebc},  // Lao Vowel Sign Mai Kon  ..Lao Semivowel Sign Lo
  {0x0ec8, 0x0ecd},  // Lao Tone Mai Ek         ..Lao Niggahita
  {0x0f18, 0x0f19},  // Tibetan Astrological Sig..Tibetan Astrological Sig
  {0x0f35, 0x0f35},  // Tibetan Mark Ngas Bzung ..Tibetan Mark Ngas Bzung
  {0x0f37, 0x0f37},  // Tibetan Mark Ngas Bzung ..Tibetan Mark Ngas Bzung
  {0x0f39, 0x0f39},  // Tibetan Mark Tsa -phru  ..Tibetan Mark Tsa -phru
  {0x0f71, 0x0f7e},  // Tibetan Vowel Sign Aa   ..Tibetan Sign Rjes Su Nga
  {0x0f80, 0x0f84},  // Tibetan Vowel Sign Rever..Tibetan Mark Halanta
  {0x0f86, 0x0f87},  // Tibetan Sign Lci Rtags  ..Tibetan Sign Yang Rtags
  {0x0f8d, 0x0f97},  // Tibetan Subjoined Sign L..Tibetan Subjoined Letter
  {0x0f99, 0x0fbc},  // Tibetan Subjoined Letter..Tibetan Subjoined Letter
  {0x0fc6, 0x0fc6},  // Tibetan Symbol Padma Gda..Tibetan Symbol Padma Gda
  {0x102d, 0x1030},  // Myanmar Vowel Sign I    ..Myanmar Vowel Sign Uu
  {0x1032, 0x1037},  // Myanmar Vowel Sign Ai   ..Myanmar Sign Dot Below
  {0x1039, 0x103a},  // Myanmar Sign Virama     ..Myanmar Sign Asat
  {0x103d, 0x103e},  // Myanmar Consonant Sign M..Myanmar Consonant Sign M
  {0x1058, 0x1059},  // Myanmar Vowel Sign Vocal..Myanmar Vowel Sign Vocal
  {0x105e, 0x1060},  // Myanmar Consonant Sign M..Myanmar Consonant Sign M
  {0x1071, 0x1074},  // Myanmar Vowel Sign Geba ..Myanmar Vowel Sign Kayah
  {0x1082, 0x1082},  // Myanmar Consonant Sign S..Myanmar Consonant Sign S
  {0x1085, 0x1086},  // Myanmar Vowel Sign Shan ..Myanmar Vowel Sign Shan
  {0x108d, 0x108d},  // Myanmar Sign Shan Counci..Myanmar Sign Shan Counci
  {0x109d, 0x109d},  // Myanmar Vowel Sign Aiton..Myanmar Vowel Sign Aiton
  {0x135d, 0x135f},  // Ethiopic Combining Gemin..Ethiopic Combining Gemin
  {0x1712, 0x1714},  // Tagalog Vowel Sign I    ..Tagalog Sign Virama
  {0x1732, 0x1734},  // Hanunoo Vowel Sign I    ..Hanunoo Sign Pamudpod
  {0x1752, 0x1753},  // Buhid Vowel Sign I      ..Buhid Vowel Sign U
  {0x1772, 0x1773},  // Tagbanwa Vowel Sign I   ..Tagbanwa Vowel Sign U
  {0x17b4, 0x17b5},  // Khmer Vowel Inherent Aq ..Khmer Vowel Inherent Aa
  {0x17b7, 0x17bd},  // Khmer Vowel Sign I      ..Khmer Vowel Sign Ua
  {0x17c6, 0x17c6},  // Khmer Sign Nikahit      ..Khmer Sign Nikahit
  {0x17c9, 0x17d3},  // Khmer Sign Muusikatoan  ..Khmer Sign Bathamasat
  {0x17dd, 0x17dd},  // Khmer Sign Atthacan     ..Khmer Sign Atthacan
  {0x180b, 0x180d},  // Mongolian Free Variation..Mongolian Free Variation
  {0x1885, 0x1886},  // Mongolian Letter Ali Gal..Mongolian Letter Ali Gal
  {0x18a9, 0x18a9},  // Mongolian Letter Ali Gal..Mongolian Letter Ali Gal
  {0x1920, 0x1922},  // Limbu Vowel Sign A      ..Limbu Vowel Sign U
  {0x1927, 0x1928},  // Limbu Vowel Sign E      ..Limbu Vowel Sign O
  {0x1932, 0x1932},  // Limbu Small Letter Anusv..Limbu Small Letter Anusv
  {0x1939, 0x193b},  // Limbu Sign Mukphreng    ..Limbu Sign Sa-i
  {0x1a17, 0x1a18},  // Buginese Vowel Sign I   ..Buginese Vowel Sign U
  {0x1a1b, 0x1a1b},  // Buginese Vowel Sign Ae  ..Buginese Vowel Sign Ae
  {0x1a56, 0x1a56},  // Tai Tham Consonant Sign ..Tai Tham Consonant Sign
  {0x1a58, 0x1a5e},  // Tai Tham Sign Mai Kang L..Tai Tham Consonant Sign
  {0x1a60, 0x1a60},  // Tai Tham Sign Sakot     ..Tai Tham Sign Sakot
  {0x1a62, 0x1a62},  // Tai Tham Vowel Sign Mai ..Tai Tham Vowel Sign Mai
  {0x1a65, 0x1a6c},  // Tai Tham Vowel Sign I   ..Tai Tham Vowel Sign Oa B
  {0x1a73, 0x1a7c},  // Tai Tham Vowel Sign Oa A..Tai Tham Sign Khuen-lue
  {0x1a7f, 0x1a7f},  // Tai Tham Combining Crypt..Tai Tham Combining Crypt
  {0x1ab0, 0x1abe},  // Combining Doubled Circum..Combining Parentheses Ov
  {0x1b00, 0x1b03},  // Balinese Sign Ulu Ricem ..Balinese Sign Surang
  {0x1b34, 0x1b34},  // Balinese Sign Rerekan   ..Balinese Sign Rerekan
  {0x1b36, 0x1b3a},  // Balinese Vowel Sign Ulu ..Balinese Vowel Sign Ra R
  {0x1b3c, 0x1b3c},  // Balinese Vowel Sign La L..Balinese Vowel Sign La L
  {0x1b42, 0x1b42},  // Balinese Vowel Sign Pepe..Balinese Vowel Sign Pepe
  {0x1b6b, 0x1b73},  // Balinese Musical Symbol ..Balinese Musical Symbol
  {0x1b80, 0x1b81},  // Sundanese Sign Panyecek ..Sundanese Sign Panglayar
  {0x1ba2, 0x1ba5},  // Sundanese Consonant Sign..Sundanese Vowel Sign Pan
  {0x1ba8, 0x1ba9},  // Sundanese Vowel Sign Pam..Sundanese Vowel Sign Pan
  {0x1bab, 0x1bad},  // Sundanese Sign Virama   ..Sundanese Consonant Sign
  {0x1be6, 0x1be6},  // Batak Sign Tompi        ..Batak Sign Tompi
  {0x1be8, 0x1be9},  // Batak Vowel Sign Pakpak ..Batak Vowel Sign Ee
  {0x1bed, 0x1bed},  // Batak Vowel Sign Karo O ..Batak Vowel Sign Karo O
  {0x1bef, 0x1bf1},  // Batak Vowel Sign U F|| S..Batak Consonant Sign H
  {0x1c2c, 0x1c33},  // Lepcha Vowel Sign E     ..Lepcha Consonant Sign T
  {0x1c36, 0x1c37},  // Lepcha Sign Ran         ..Lepcha Sign Nukta
  {0x1cd0, 0x1cd2},  // Vedic Tone Karshana     ..Vedic Tone Prenkha
  {0x1cd4, 0x1ce0},  // Vedic Sign Yajurvedic Mi..Vedic Tone Rigvedic Kash
  {0x1ce2, 0x1ce8},  // Vedic Sign Visarga Svari..Vedic Sign Visarga Anuda
  {0x1ced, 0x1ced},  // Vedic Sign Tiryak       ..Vedic Sign Tiryak
  {0x1cf4, 0x1cf4},  // Vedic Tone Candra Above ..Vedic Tone Candra Above
  {0x1cf8, 0x1cf9},  // Vedic Tone Ring Above   ..Vedic Tone Double Ring A
  {0x1dc0, 0x1df5},  // Combining Dotted Grave A..Combining Up Tack Above
  {0x1dfb, 0x1dff},  // (nil)                   ..Combining Right Arrowhea
  {0x20d0, 0x20f0},  // Combining Left Harpoon A..Combining Asterisk Above
  {0x2cef, 0x2cf1},  // Coptic Combining Ni Abov..Coptic Combining Spiritu
  {0x2d7f, 0x2d7f},  // Tifinagh Consonant Joine..Tifinagh Consonant Joine
  {0x2de0, 0x2dff},  // Combining Cyrillic Lette..Combining Cyrillic Lette
  {0x302a, 0x302d},  // Ideographic Level Tone M..Ideographic Entering Ton
  {0x3099, 0x309a},  // Combining Katakana-hirag..Combining Katakana-hirag
  {0xa66f, 0xa672},  // Combining Cyrillic Vzmet..Combining Cyrillic Thous
  {0xa674, 0xa67d},  // Combining Cyrillic Lette..Combining Cyrillic Payer
  {0xa69e, 0xa69f},  // Combining Cyrillic Lette..Combining Cyrillic Lette
  {0xa6f0, 0xa6f1},  // Bamum Combining Mark Koq..Bamum Combining Mark Tuk
  {0xa802, 0xa802},  // Syloti Nagri Sign Dvisva..Syloti Nagri Sign Dvisva
  {0xa806, 0xa806},  // Syloti Nagri Sign Hasant..Syloti Nagri Sign Hasant
  {0xa80b, 0xa80b},  // Syloti Nagri Sign Anusva..Syloti Nagri Sign Anusva
  {0xa825, 0xa826},  // Syloti Nagri Vowel Sign ..Syloti Nagri Vowel Sign
  {0xa8c4, 0xa8c5},  // Saurashtra Sign Virama  ..
  {0xa8e0, 0xa8f1},  // Combining Devanagari Dig..Combining Devanagari Sig
  {0xa926, 0xa92d},  // Kayah Li Vowel Ue       ..Kayah Li Tone Calya Plop
  {0xa947, 0xa951},  // Rejang Vowel Sign I     ..Rejang Consonant Sign R
  {0xa980, 0xa982},  // Javanese Sign Panyangga ..Javanese Sign Layar
  {0xa9b3, 0xa9b3},  // Javanese Sign Cecak Telu..Javanese Sign Cecak Telu
  {0xa9b6, 0xa9b9},  // Javanese Vowel Sign Wulu..Javanese Vowel Sign Suku
  {0xa9bc, 0xa9bc},  // Javanese Vowel Sign Pepe..Javanese Vowel Sign Pepe
  {0xa9e5, 0xa9e5},  // Myanmar Sign Shan Saw   ..Myanmar Sign Shan Saw
  {0xaa29, 0xaa2e},  // Cham Vowel Sign Aa      ..Cham Vowel Sign Oe
  {0xaa31, 0xaa32},  // Cham Vowel Sign Au      ..Cham Vowel Sign Ue
  {0xaa35, 0xaa36},  // Cham Consonant Sign La  ..Cham Consonant Sign Wa
  {0xaa43, 0xaa43},  // Cham Consonant Sign Fina..Cham Consonant Sign Fina
  {0xaa4c, 0xaa4c},  // Cham Consonant Sign Fina..Cham Consonant Sign Fina
  {0xaa7c, 0xaa7c},  // Myanmar Sign Tai Laing T..Myanmar Sign Tai Laing T
  {0xaab0, 0xaab0},  // Tai Viet Mai Kang       ..Tai Viet Mai Kang
  {0xaab2, 0xaab4},  // Tai Viet Vowel I        ..Tai Viet Vowel U
  {0xaab7, 0xaab8},  // Tai Viet Mai Khit       ..Tai Viet Vowel Ia
  {0xaabe, 0xaabf},  // Tai Viet Vowel Am       ..Tai Viet Tone Mai Ek
  {0xaac1, 0xaac1},  // Tai Viet Tone Mai Tho   ..Tai Viet Tone Mai Tho
  {0xaaec, 0xaaed},  // Meetei Mayek Vowel Sign ..Meetei Mayek Vowel Sign
  {0xaaf6, 0xaaf6},  // Meetei Mayek Virama     ..Meetei Mayek Virama
  {0xabe5, 0xabe5},  // Meetei Mayek Vowel Sign ..Meetei Mayek Vowel Sign
  {0xabe8, 0xabe8},  // Meetei Mayek Vowel Sign ..Meetei Mayek Vowel Sign
  {0xabed, 0xabed},  // Meetei Mayek Apun Iyek  ..Meetei Mayek Apun Iyek
  {0xfb1e, 0xfb1e},  // Hebrew Point Judeo-spani..Hebrew Point Judeo-spani
  {0xfe00, 0xfe0f},  // Variation Select||-1    ..Variation Select||-16
  {0xfe20, 0xfe2f},  // Combining Ligature Left ..Combining Cyrillic Titlo
  {0x101fd, 0x101fd},  // Phaistos Disc Sign Combi..Phaistos Disc Sign Combi
  {0x102e0, 0x102e0},  // Coptic Epact Thousands M..Coptic Epact Thousands M
  {0x10376, 0x1037a},  // Combining Old Permic Let..Combining Old Permic Let
  {0x10a01, 0x10a03},  // Kharoshthi Vowel Sign I ..Kharoshthi Vowel Sign Vo
  {0x10a05, 0x10a06},  // Kharoshthi Vowel Sign E ..Kharoshthi Vowel Sign O
  {0x10a0c, 0x10a0f},  // Kharoshthi Vowel Length ..Kharoshthi Sign Visarga
  {0x10a38, 0x10a3a},  // Kharoshthi Sign Bar Abov..Kharoshthi Sign Dot Belo
  {0x10a3f, 0x10a3f},  // Kharoshthi Virama       ..Kharoshthi Virama
  {0x10ae5, 0x10ae6},  // Manichaean Abbreviation ..Manichaean Abbreviation
  {0x11001, 0x11001},  // Brahmi Sign Anusvara    ..Brahmi Sign Anusvara
  {0x11038, 0x11046},  // Brahmi Vowel Sign Aa    ..Brahmi Virama
  {0x1107f, 0x11081},  // Brahmi Number Joiner    ..Kaithi Sign Anusvara
  {0x110b3, 0x110b6},  // Kaithi Vowel Sign U     ..Kaithi Vowel Sign Ai
  {0x110b9, 0x110ba},  // Kaithi Sign Virama      ..Kaithi Sign Nukta
  {0x11100, 0x11102},  // Chakma Sign Candrabindu ..Chakma Sign Visarga
  {0x11127, 0x1112b},  // Chakma Vowel Sign A     ..Chakma Vowel Sign Uu
  {0x1112d, 0x11134},  // Chakma Vowel Sign Ai    ..Chakma Maayyaa
  {0x11173, 0x11173},  // Mahajani Sign Nukta     ..Mahajani Sign Nukta
  {0x11180, 0x11181},  // Sharada Sign Candrabindu..Sharada Sign Anusvara
  {0x111b6, 0x111be},  // Sharada Vowel Sign U    ..Sharada Vowel Sign O
  {0x111ca, 0x111cc},  // Sharada Sign Nukta      ..Sharada Extra Sh||t Vowe
  {0x1122f, 0x11231},  // Khojki Vowel Sign U     ..Khojki Vowel Sign Ai
  {0x11234, 0x11234},  // Khojki Sign Anusvara    ..Khojki Sign Anusvara
  {0x11236, 0x11237},  // Khojki Sign Nukta       ..Khojki Sign Shadda
  {0x1123e, 0x1123e},  // (nil)                   ..
  {0x112df, 0x112df},  // Khudawadi Sign Anusvara ..Khudawadi Sign Anusvara
  {0x112e3, 0x112ea},  // Khudawadi Vowel Sign U  ..Khudawadi Sign Virama
  {0x11300, 0x11301},  // Grantha Sign Combining A..Grantha Sign Candrabindu
  {0x1133c, 0x1133c},  // Grantha Sign Nukta      ..Grantha Sign Nukta
  {0x11340, 0x11340},  // Grantha Vowel Sign Ii   ..Grantha Vowel Sign Ii
  {0x11366, 0x1136c},  // Combining Grantha Digit ..Combining Grantha Digit
  {0x11370, 0x11374},  // Combining Grantha Letter..Combining Grantha Letter
  {0x11438, 0x1143f},  // (nil)                   ..
  {0x11442, 0x11444},  // (nil)                   ..
  {0x11446, 0x11446},  // (nil)                   ..
  {0x114b3, 0x114b8},  // Tirhuta Vowel Sign U    ..Tirhuta Vowel Sign Vocal
  {0x114ba, 0x114ba},  // Tirhuta Vowel Sign Sh||t..Tirhuta Vowel Sign Sh||t
  {0x114bf, 0x114c0},  // Tirhuta Sign Candrabindu..Tirhuta Sign Anusvara
  {0x114c2, 0x114c3},  // Tirhuta Sign Virama     ..Tirhuta Sign Nukta
  {0x115b2, 0x115b5},  // Siddham Vowel Sign U    ..Siddham Vowel Sign Vocal
  {0x115bc, 0x115bd},  // Siddham Sign Candrabindu..Siddham Sign Anusvara
  {0x115bf, 0x115c0},  // Siddham Sign Virama     ..Siddham Sign Nukta
  {0x115dc, 0x115dd},  // Siddham Vowel Sign Alter..Siddham Vowel Sign Alter
  {0x11633, 0x1163a},  // Modi Vowel Sign U       ..Modi Vowel Sign Ai
  {0x1163d, 0x1163d},  // Modi Sign Anusvara      ..Modi Sign Anusvara
  {0x1163f, 0x11640},  // Modi Sign Virama        ..Modi Sign Ardhacandra
  {0x116ab, 0x116ab},  // Takri Sign Anusvara     ..Takri Sign Anusvara
  {0x116ad, 0x116ad},  // Takri Vowel Sign Aa     ..Takri Vowel Sign Aa
  {0x116b0, 0x116b5},  // Takri Vowel Sign U      ..Takri Vowel Sign Au
  {0x116b7, 0x116b7},  // Takri Sign Nukta        ..Takri Sign Nukta
  {0x1171d, 0x1171f},  // Ahom Consonant Sign Medi..Ahom Consonant Sign Medi
  {0x11722, 0x11725},  // Ahom Vowel Sign I       ..Ahom Vowel Sign Uu
  {0x11727, 0x1172b},  // Ahom Vowel Sign Aw      ..Ahom Sign Killer
  {0x11c30, 0x11c36},  // (nil)                   ..
  {0x11c38, 0x11c3d},  // (nil)                   ..
  {0x11c3f, 0x11c3f},  // (nil)                   ..
  {0x11c92, 0x11ca7},  // (nil)                   ..
  {0x11caa, 0x11cb0},  // (nil)                   ..
  {0x11cb2, 0x11cb3},  // (nil)                   ..
  {0x11cb5, 0x11cb6},  // (nil)                   ..
  {0x16af0, 0x16af4},  // Bassa Vah Combining High..Bassa Vah Combining High
  {0x16b30, 0x16b36},  // Pahawh Hmong Mark Cim Tu..Pahawh Hmong Mark Cim Ta
  {0x16f8f, 0x16f92},  // Miao Tone Right         ..Miao Tone Below
  {0x1bc9d, 0x1bc9e},  // Duployan Thick Letter Se..Duployan Double Mark
  {0x1d167, 0x1d169},  // Musical Symbol Combining..Musical Symbol Combining
  {0x1d17b, 0x1d182},  // Musical Symbol Combining..Musical Symbol Combining
  {0x1d185, 0x1d18b},  // Musical Symbol Combining..Musical Symbol Combining
  {0x1d1aa, 0x1d1ad},  // Musical Symbol Combining..Musical Symbol Combining
  {0x1d242, 0x1d244},  // Combining Greek Musical ..Combining Greek Musical
  {0x1da00, 0x1da36},  // Signwriting Head Rim    ..Signwriting Air Sucking
  {0x1da3b, 0x1da6c},  // Signwriting Mouth Closed..Signwriting Excitement
  {0x1da75, 0x1da75},  // Signwriting Upper Body T..Signwriting Upper Body T
  {0x1da84, 0x1da84},  // Signwriting Location Hea..Signwriting Location Hea
  {0x1da9b, 0x1da9f},  // Signwriting Fill Modifie..Signwriting Fill Modifie
  {0x1daa1, 0x1daaf},  // Signwriting Rotation Mod..Signwriting Rotation Mod
  {0x1e000, 0x1e006},  // (nil)                   ..
  {0x1e008, 0x1e018},  // (nil)                   ..
  {0x1e01b, 0x1e021},  // (nil)                   ..
  {0x1e023, 0x1e024},  // (nil)                   ..
  {0x1e026, 0x1e02a},  // (nil)                   ..
  {0x1e8d0, 0x1e8d6},  // Mende Kikakui Combining ..Mende Kikakui Combining
  {0x1e944, 0x1e94a},  // (nil)                   ..
  {0xe0100, 0xe01ef},  // Variation Select||-17   ..Variation Select||-256
};

// https://github.com/jquast/wcwidth/blob/master/wcwidth/table_wide.py
// at commit 0d7de112202cc8b2ebe9232ff4a5c954f19d561a (2016-07-02):
static struct width_interval WIDE_EASTASIAN[] = {
  {0x1100, 0x115f},  // Hangul Choseong Kiyeok  ..Hangul Choseong Filler
  {0x231a, 0x231b},  // Watch                   ..Hourglass
  {0x2329, 0x232a},  // Left-pointing Angle Brac..Right-pointing Angle Bra
  {0x23e9, 0x23ec},  // Black Right-pointing Dou..Black Down-pointing Doub
  {0x23f0, 0x23f0},  // Alarm Clock             ..Alarm Clock
  {0x23f3, 0x23f3},  // Hourglass With Flowing S..Hourglass With Flowing S
  {0x25fd, 0x25fe},  // White Medium Small Squar..Black Medium Small Squar
  {0x2614, 0x2615},  // Umbrella With Rain Drops..Hot Beverage
  {0x2648, 0x2653},  // Aries                   ..Pisces
  {0x267f, 0x267f},  // Wheelchair Symbol       ..Wheelchair Symbol
  {0x2693, 0x2693},  // Anch||                  ..Anch||
  {0x26a1, 0x26a1},  // High Voltage Sign       ..High Voltage Sign
  {0x26aa, 0x26ab},  // Medium White Circle     ..Medium Black Circle
  {0x26bd, 0x26be},  // Soccer Ball             ..Baseball
  {0x26c4, 0x26c5},  // Snowman Without Snow    ..Sun Behind Cloud
  {0x26ce, 0x26ce},  // Ophiuchus               ..Ophiuchus
  {0x26d4, 0x26d4},  // No Entry                ..No Entry
  {0x26ea, 0x26ea},  // Church                  ..Church
  {0x26f2, 0x26f3},  // Fountain                ..Flag In Hole
  {0x26f5, 0x26f5},  // Sailboat                ..Sailboat
  {0x26fa, 0x26fa},  // Tent                    ..Tent
  {0x26fd, 0x26fd},  // Fuel Pump               ..Fuel Pump
  {0x2705, 0x2705},  // White Heavy Check Mark  ..White Heavy Check Mark
  {0x270a, 0x270b},  // Raised Fist             ..Raised Hand
  {0x2728, 0x2728},  // Sparkles                ..Sparkles
  {0x274c, 0x274c},  // Cross Mark              ..Cross Mark
  {0x274e, 0x274e},  // Negative Squared Cross M..Negative Squared Cross M
  {0x2753, 0x2755},  // Black Question Mark ||na..White Exclamation Mark O
  {0x2757, 0x2757},  // Heavy Exclamation Mark S..Heavy Exclamation Mark S
  {0x2795, 0x2797},  // Heavy Plus Sign         ..Heavy Division Sign
  {0x27b0, 0x27b0},  // Curly Loop              ..Curly Loop
  {0x27bf, 0x27bf},  // Double Curly Loop       ..Double Curly Loop
  {0x2b1b, 0x2b1c},  // Black Large Square      ..White Large Square
  {0x2b50, 0x2b50},  // White Medium Star       ..White Medium Star
  {0x2b55, 0x2b55},  // Heavy Large Circle      ..Heavy Large Circle
  {0x2e80, 0x2e99},  // Cjk Radical Repeat      ..Cjk Radical Rap
  {0x2e9b, 0x2ef3},  // Cjk Radical Choke       ..Cjk Radical C-simplified
  {0x2f00, 0x2fd5},  // Kangxi Radical One      ..Kangxi Radical Flute
  {0x2ff0, 0x2ffb},  // Ideographic Description ..Ideographic Description
  {0x3000, 0x303e},  // Ideographic Space       ..Ideographic Variation In
  {0x3041, 0x3096},  // Hiragana Letter Small A ..Hiragana Letter Small Ke
  {0x3099, 0x30ff},  // Combining Katakana-hirag..Katakana Digraph Koto
  {0x3105, 0x312d},  // Bopomofo Letter B       ..Bopomofo Letter Ih
  {0x3131, 0x318e},  // Hangul Letter Kiyeok    ..Hangul Letter Araeae
  {0x3190, 0x31ba},  // Ideographic Annotation L..Bopomofo Letter Zy
  {0x31c0, 0x31e3},  // Cjk Stroke T            ..Cjk Stroke Q
  {0x31f0, 0x321e},  // Katakana Letter Small Ku..Parenthesized K||ean Cha
  {0x3220, 0x3247},  // Parenthesized Ideograph ..Circled Ideograph Koto
  {0x3250, 0x32fe},  // Partnership Sign        ..Circled Katakana Wo
  {0x3300, 0x4dbf},  // Square Apaato           ..
  {0x4e00, 0xa48c},  // Cjk Unified Ideograph-4e..Yi Syllable Yyr
  {0xa490, 0xa4c6},  // Yi Radical Qot          ..Yi Radical Ke
  {0xa960, 0xa97c},  // Hangul Choseong Tikeut-m..Hangul Choseong Ssangyeo
  {0xac00, 0xd7a3},  // Hangul Syllable Ga      ..Hangul Syllable Hih
  {0xf900, 0xfaff},  // Cjk Compatibility Ideogr..
  {0xfe10, 0xfe19},  // Presentation F||m F|| Ve..Presentation F||m F|| Ve
  {0xfe30, 0xfe52},  // Presentation F||m F|| Ve..Small Full Stop
  {0xfe54, 0xfe66},  // Small Semicolon         ..Small Equals Sign
  {0xfe68, 0xfe6b},  // Small Reverse Solidus   ..Small Commercial At
  {0xff01, 0xff60},  // Fullwidth Exclamation Ma..Fullwidth Right White Pa
  {0xffe0, 0xffe6},  // Fullwidth Cent Sign     ..Fullwidth Won Sign
  {0x16fe0, 0x16fe0},  // (nil)                   ..
  {0x17000, 0x187ec},  // (nil)                   ..
  {0x18800, 0x18af2},  // (nil)                   ..
  {0x1b000, 0x1b001},  // Katakana Letter Archaic ..Hiragana Letter Archaic
  {0x1f004, 0x1f004},  // Mahjong Tile Red Dragon ..Mahjong Tile Red Dragon
  {0x1f0cf, 0x1f0cf},  // Playing Card Black Joker..Playing Card Black Joker
  {0x1f18e, 0x1f18e},  // Negative Squared Ab     ..Negative Squared Ab
  {0x1f191, 0x1f19a},  // Squared Cl              ..Squared Vs
  {0x1f200, 0x1f202},  // Square Hiragana Hoka    ..Squared Katakana Sa
  {0x1f210, 0x1f23b},  // Squared Cjk Unified Ideo..
  {0x1f240, 0x1f248},  // T||toise Shell Bracketed..T||toise Shell Bracketed
  {0x1f250, 0x1f251},  // Circled Ideograph Advant..Circled Ideograph Accept
  {0x1f300, 0x1f320},  // Cyclone                 ..Shooting Star
  {0x1f32d, 0x1f335},  // Hot Dog                 ..Cactus
  {0x1f337, 0x1f37c},  // Tulip                   ..Baby Bottle
  {0x1f37e, 0x1f393},  // Bottle With Popping C||k..Graduation Cap
  {0x1f3a0, 0x1f3ca},  // Carousel H||se          ..Swimmer
  {0x1f3cf, 0x1f3d3},  // Cricket Bat And Ball    ..Table Tennis Paddle And
  {0x1f3e0, 0x1f3f0},  // House Building          ..European Castle
  {0x1f3f4, 0x1f3f4},  // Waving Black Flag       ..Waving Black Flag
  {0x1f3f8, 0x1f43e},  // Badminton Racquet And Sh..Paw Prints
  {0x1f440, 0x1f440},  // Eyes                    ..Eyes
  {0x1f442, 0x1f4fc},  // Ear                     ..Videocassette
  {0x1f4ff, 0x1f53d},  // Prayer Beads            ..Down-pointing Small Red
  {0x1f54b, 0x1f54e},  // Kaaba                   ..Men||ah With Nine Branch
  {0x1f550, 0x1f567},  // Clock Face One Oclock   ..Clock Face Twelve-thirty
  {0x1f57a, 0x1f57a},  // (nil)                   ..
  {0x1f595, 0x1f596},  // Reversed Hand With Middl..Raised Hand With Part Be
  {0x1f5a4, 0x1f5a4},  // (nil)                   ..
  {0x1f5fb, 0x1f64f},  // Mount Fuji              ..Person With Folded Hands
  {0x1f680, 0x1f6c5},  // Rocket                  ..Left Luggage
  {0x1f6cc, 0x1f6cc},  // Sleeping Accommodation  ..Sleeping Accommodation
  {0x1f6d0, 0x1f6d2},  // Place Of W||ship        ..
  {0x1f6eb, 0x1f6ec},  // Airplane Departure      ..Airplane Arriving
  {0x1f6f4, 0x1f6f6},  // (nil)                   ..
  {0x1f910, 0x1f91e},  // Zipper-mouth Face       ..
  {0x1f920, 0x1f927},  // (nil)                   ..
  {0x1f930, 0x1f930},  // (nil)                   ..
  {0x1f933, 0x1f93e},  // (nil)                   ..
  {0x1f940, 0x1f94b},  // (nil)                   ..
  {0x1f950, 0x1f95e},  // (nil)                   ..
  {0x1f980, 0x1f991},  // Crab                    ..
  {0x1f9c0, 0x1f9c0},  // Cheese Wedge            ..Cheese Wedge
  {0x20000, 0x2fffd},  // Cjk Unified Ideograph-20..
  {0x30000, 0x3fffd},  // (nil)                   ..
};

private int intable (struct width_interval* table, int table_length, int c) {
  if (c < table[0].start) return 0;

  int bot = 0;
  int top = table_length - 1;
  while (top >= bot) {
    int mid = (bot + top) / 2;
    if (table[mid].end < c){
       bot = mid + 1;
    } else if (table[mid].start > c) {
       top = mid - 1;
    } else {
      return 1;
    }
  }
  return 0;
}

int cwidth (utf8 c) {
  if (c == 0 || c == 0x034F || (0x200B <= c && c <= 0x200F) ||
      c == 0x2028 || c == 0x2029 || (0x202A <= c && c <= 0x202E) ||
      (0x2060 <= c && c <= 0x2063)) {
    return 0;
  }

  if (0x07F <= c && c < 0x0A0) return 0;

  if (intable (ZERO_WIDTH, sizeof (ZERO_WIDTH) / sizeof(struct width_interval), c))
    return 0;

  return intable (WIDE_EASTASIAN, sizeof (WIDE_EASTASIAN) / sizeof(struct width_interval), c)
     ? 2 : 1;
}

private int char_utf8_width (char *s, int tabwidth) {
  if (s[0] >= ' ' and s[0] <= '~') return 1;
  if (s[0] is '\t') return tabwidth;
  return cwidth (utf8_code (s));
}

private void ustring_free_members (u8_t *u8) {
  ifnot (u8->num_items) return;
  u8char_t *it = u8->head;
  while (it) {
    u8char_t *tmp = it->next;
    free (it);
    it = tmp;
  }
  u8->num_items = 0;
  u8->cur_idx = -1;
  u8->head = u8->tail = u8->current = NULL;
}

private void ustring_free (u8_t *u8) {
  if (NULL is u8) return;
  ustring_free_members (u8);
  free (u8);
}

private u8_t *ustring_new (void) {
  return AllocType (u8);
}

private u8char_t *ustring_encode (u8_t *u8, char *bytes,
            size_t len, int clear_line, int tabwidth, int curidx) {
  if (clear_line) ustring_free_members (u8);

  ifnot (len) {
    u8->num_items = 0;
    return NULL;
  }

  int curpos = 0;

  char *sp = bytes;
  u8->len = 0;

  while (*sp) {
    uchar c = (uchar) *sp;
    u8char_t *chr = AllocType (u8char);
    chr->code = c;
    chr->width = chr->len = 1;
    chr->buf[0] = *sp;

    if (c < 0x80) {
      if (chr->code is '\t')
        chr->width += (tabwidth - 1);
      chr->buf[1] = '\0';
      goto push;
    }

    chr->buf[1] = *++sp;
    chr->len++;
    chr->code <<= 6; chr->code += (uchar) *sp;

    if ((c & 0xe0) is 0xc0) {
      chr->code -= offsetsFromUTF8[1];
      chr->width += (cwidth (chr->code) - 1);
      chr->buf[2] = '\0';
      goto push;
    }

    chr->buf[2] = *++sp;
    chr->len++;
    chr->code <<= 6; chr->code += (uchar) *sp;

    if ((c & 0xf0) is 0xe0) {
      chr->code -= offsetsFromUTF8[2];
      chr->width += (cwidth (chr->code) - 1);
      chr->buf[3] = '\0';
      goto push;
    }

    chr->buf[3] = *++sp;
    chr->buf[4] = '\0';
    chr->len++;
    chr->code <<= 6; chr->code += (uchar) *sp;
    chr->code -= offsetsFromUTF8[3];
    chr->width += (cwidth (chr->code) - 1);

push:
    current_list_append (u8, chr);
    if (curidx is u8->len or (u8->len + (chr->len - 1) is curidx))
      curpos = u8->cur_idx;

    u8->len += chr->len;
    sp++;
  }

  current_list_set (u8, curpos);
  return u8->current;
}

private utf8 ustring_get_code_at (char *src, size_t src_len, int idx, int *len) {
  if (idx >= (int) src_len) return -1;
  char *sp = src + idx;
  int code = 0;
  int i = 0;
  *len = 0;
  do {
    code <<= 6;
    code += (uchar) sp[i++];
    (*len)++;
  } while (sp[i] and IS_UTF8 (sp[i]));

  code -= offsetsFromUTF8[*len-1];
  return code;
}

private char *ustring_character (utf8 c, char *buf, int *len) {
  *len = 1;
  if (c < 0x80) {
    buf[0] = (char) c;
  } else if (c < 0x800) {
    buf[0] = (c >> 6) | 0xC0;
    buf[1] = (c & 0x3F) | 0x80;
    (*len)++;
  } else if (c < 0x10000) {
    buf[0] = (c >> 12) | 0xE0;
    buf[1] = ((c >> 6) & 0x3F) | 0x80;
    buf[2] = (c & 0x3F) | 0x80;
    (*len) += 2;
  } else if (c < 0x110000) {
    buf[0] = (c >> 18) | 0xF0;
    buf[1] = ((c >> 12) & 0x3F) | 0x80;
    buf[2] = ((c >> 6) & 0x3F) | 0x80;
    buf[3] = (c & 0x3F) | 0x80;
    (*len) += 3;
  } else
    return 0;

  buf[*len] = '\0';
  return buf;
}

/* almost all of this following 'case' code is from the utf8.h project:
 * https://github.com/sheredom/utf8.h.git 
 * This is free and unencumbered software released into the public domain.
 */

private utf8 ustring_to_lower (utf8 cp) {
  if (((0x0041 <= cp) && (0x005a >= cp)) ||
      ((0x00c0 <= cp) && (0x00d6 >= cp)) ||
      ((0x00d8 <= cp) && (0x00de >= cp)) ||
      ((0x0391 <= cp) && (0x03a1 >= cp)) ||
      ((0x03a3 <= cp) && (0x03ab >= cp))) {
    cp += 32;
  } else if (((0x0100 <= cp) && (0x012f >= cp)) ||
             ((0x0132 <= cp) && (0x0137 >= cp)) ||
             ((0x014a <= cp) && (0x0177 >= cp)) ||
             ((0x0182 <= cp) && (0x0185 >= cp)) ||
             ((0x01a0 <= cp) && (0x01a5 >= cp)) ||
             ((0x01de <= cp) && (0x01ef >= cp)) ||
             ((0x01f8 <= cp) && (0x021f >= cp)) ||
             ((0x0222 <= cp) && (0x0233 >= cp)) ||
             ((0x0246 <= cp) && (0x024f >= cp)) ||
             ((0x03d8 <= cp) && (0x03ef >= cp))) {
    cp |= 0x1;
  } else if (((0x0139 <= cp) && (0x0148 >= cp)) ||
             ((0x0179 <= cp) && (0x017e >= cp)) ||
             ((0x01af <= cp) && (0x01b0 >= cp)) ||
             ((0x01b3 <= cp) && (0x01b6 >= cp)) ||
             ((0x01cd <= cp) && (0x01dc >= cp))) {
    cp += 1;
    cp &= ~0x1;
  } else {
    switch (cp) {
    default: break;
    case 0x0178: cp = 0x00ff; break;
    case 0x0243: cp = 0x0180; break;
    case 0x018e: cp = 0x01dd; break;
    case 0x023d: cp = 0x019a; break;
    case 0x0220: cp = 0x019e; break;
    case 0x01b7: cp = 0x0292; break;
    case 0x01c4: cp = 0x01c6; break;
    case 0x01c7: cp = 0x01c9; break;
    case 0x01ca: cp = 0x01cc; break;
    case 0x01f1: cp = 0x01f3; break;
    case 0x01f7: cp = 0x01bf; break;
    case 0x0187: cp = 0x0188; break;
    case 0x018b: cp = 0x018c; break;
    case 0x0191: cp = 0x0192; break;
    case 0x0198: cp = 0x0199; break;
    case 0x01a7: cp = 0x01a8; break;
    case 0x01ac: cp = 0x01ad; break;
    case 0x01af: cp = 0x01b0; break;
    case 0x01b8: cp = 0x01b9; break;
    case 0x01bc: cp = 0x01bd; break;
    case 0x01f4: cp = 0x01f5; break;
    case 0x023b: cp = 0x023c; break;
    case 0x0241: cp = 0x0242; break;
    case 0x03fd: cp = 0x037b; break;
    case 0x03fe: cp = 0x037c; break;
    case 0x03ff: cp = 0x037d; break;
    case 0x037f: cp = 0x03f3; break;
    case 0x0386: cp = 0x03ac; break;
    case 0x0388: cp = 0x03ad; break;
    case 0x0389: cp = 0x03ae; break;
    case 0x038a: cp = 0x03af; break;
    case 0x038c: cp = 0x03cc; break;
    case 0x038e: cp = 0x03cd; break;
    case 0x038f: cp = 0x03ce; break;
    case 0x0370: cp = 0x0371; break;
    case 0x0372: cp = 0x0373; break;
    case 0x0376: cp = 0x0377; break;
    case 0x03f4: cp = 0x03d1; break;
    case 0x03cf: cp = 0x03d7; break;
    case 0x03f9: cp = 0x03f2; break;
    case 0x03f7: cp = 0x03f8; break;
    case 0x03fa: cp = 0x03fb; break;
    };
  }

  return cp;
}

private utf8 ustring_to_upper (utf8 cp) {
  if (((0x0061 <= cp) && (0x007a >= cp)) ||
      ((0x00e0 <= cp) && (0x00f6 >= cp)) ||
      ((0x00f8 <= cp) && (0x00fe >= cp)) ||
      ((0x03b1 <= cp) && (0x03c1 >= cp)) ||
      ((0x03c3 <= cp) && (0x03cb >= cp))) {
    cp -= 32;
  } else if (((0x0100 <= cp) && (0x012f >= cp)) ||
             ((0x0132 <= cp) && (0x0137 >= cp)) ||
             ((0x014a <= cp) && (0x0177 >= cp)) ||
             ((0x0182 <= cp) && (0x0185 >= cp)) ||
             ((0x01a0 <= cp) && (0x01a5 >= cp)) ||
             ((0x01de <= cp) && (0x01ef >= cp)) ||
             ((0x01f8 <= cp) && (0x021f >= cp)) ||
             ((0x0222 <= cp) && (0x0233 >= cp)) ||
             ((0x0246 <= cp) && (0x024f >= cp)) ||
             ((0x03d8 <= cp) && (0x03ef >= cp))) {
    cp &= ~0x1;
  } else if (((0x0139 <= cp) && (0x0148 >= cp)) ||
             ((0x0179 <= cp) && (0x017e >= cp)) ||
             ((0x01af <= cp) && (0x01b0 >= cp)) ||
             ((0x01b3 <= cp) && (0x01b6 >= cp)) ||
             ((0x01cd <= cp) && (0x01dc >= cp))) {
    cp -= 1;
    cp |= 0x1;
  } else {
    switch (cp) {
    default: break;
    case 0x00ff: cp = 0x0178; break;
    case 0x0180: cp = 0x0243; break;
    case 0x01dd: cp = 0x018e; break;
    case 0x019a: cp = 0x023d; break;
    case 0x019e: cp = 0x0220; break;
    case 0x0292: cp = 0x01b7; break;
    case 0x01c6: cp = 0x01c4; break;
    case 0x01c9: cp = 0x01c7; break;
    case 0x01cc: cp = 0x01ca; break;
    case 0x01f3: cp = 0x01f1; break;
    case 0x01bf: cp = 0x01f7; break;
    case 0x0188: cp = 0x0187; break;
    case 0x018c: cp = 0x018b; break;
    case 0x0192: cp = 0x0191; break;
    case 0x0199: cp = 0x0198; break;
    case 0x01a8: cp = 0x01a7; break;
    case 0x01ad: cp = 0x01ac; break;
    case 0x01b0: cp = 0x01af; break;
    case 0x01b9: cp = 0x01b8; break;
    case 0x01bd: cp = 0x01bc; break;
    case 0x01f5: cp = 0x01f4; break;
    case 0x023c: cp = 0x023b; break;
    case 0x0242: cp = 0x0241; break;
    case 0x037b: cp = 0x03fd; break;
    case 0x037c: cp = 0x03fe; break;
    case 0x037d: cp = 0x03ff; break;
    case 0x03f3: cp = 0x037f; break;
    case 0x03ac: cp = 0x0386; break;
    case 0x03ad: cp = 0x0388; break;
    case 0x03ae: cp = 0x0389; break;
    case 0x03af: cp = 0x038a; break;
    case 0x03cc: cp = 0x038c; break;
    case 0x03cd: cp = 0x038e; break;
    case 0x03ce: cp = 0x038f; break;
    case 0x0371: cp = 0x0370; break;
    case 0x0373: cp = 0x0372; break;
    case 0x0377: cp = 0x0376; break;
    case 0x03d1: cp = 0x03f4; break;
    case 0x03d7: cp = 0x03cf; break;
    case 0x03f2: cp = 0x03f9; break;
    case 0x03f8: cp = 0x03f7; break;
    case 0x03fb: cp = 0x03fa; break;
    };
  }

  return cp;
}

private int ustring_is_lower (utf8 chr) {
  return chr != ustring_to_upper (chr);
}

private int ustring_is_upper (utf8 chr) {
  return chr != ustring_to_lower (chr);
}

private int ustring_change_case (char *dest, char *src, size_t src_len, int to_type) {
  int idx = 0;
  int changed = 0;
  while (idx < (int) src_len) {
    int len = 0;
    utf8 c = ustring_get_code_at (src, src_len, idx, &len);
    if ((to_type is TO_LOWER ? ustring_is_upper : ustring_is_lower) (c)) {
        char buf[len];
        ustring_character ((to_type is TO_LOWER
           ? ustring_to_lower : ustring_to_upper) (c), buf, &len);
        for (int i = 0; i < len; i++)
           dest[idx] = buf[i];
        changed = 1;
    } else {
      for (int i = 0; i < len; i++)
        dest[idx] = src[idx];
    }

    idx += len;
  }

  dest[idx] = '\0';
  return changed;
}

/* This is itoa version 0.4, written by Lukás Chmela and released under GPLv3,
 * Original Source:  http://www.strudel.org.uk/itoa/
 */
private char *itoa (int value, char *result, int base) {
  if (base < 2 || base > 36) {
    *result = '\0';
    return result;
  }

  char *ptr = result, *ptr1 = result, tmp_char;
  int tmp_value;

  do {
    tmp_value = value;
    value /= base;
    *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"
        [35 + (tmp_value - (value * base))];
  } while (value);

  /* Apply negative sign */
  if (0 > tmp_value) *ptr++ = '-';

  *ptr-- = '\0';

  while (ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr--= *ptr1;
    *ptr1++ = tmp_char;
   }

  return result;
}

private void string_free (string_t *this) {
  if (this is NULL) return;
  if (this->mem_size) free (this->bytes);
  free (this);
}

private void string_clear (string_t *this) {
  ifnot (this->num_bytes) return;
  this->bytes[0] = '\0';
  this->num_bytes = 0;
}

private void string_clear_at (string_t *this, int idx) {
  if (0 > idx) idx += this->num_bytes;
  if (idx < 0) return;
  if (idx > (int) this->num_bytes) idx = this->num_bytes;
  this->bytes[idx] = '\0';
  this->num_bytes = idx;
}

private size_t string_align (size_t size) {
  size_t sz = 8 - (size % 8);
  sz = sizeof (char) * (size + (sz < 8 ? sz : 0));
  return sz;
}

private string_t *string_reallocate (string_t *this, size_t len) {
  size_t sz = string_align (this->mem_size + len + 1);
  this->bytes = Realloc (this->bytes, sz);
  this->mem_size = sz;
  return this;
}

private string_t *string_new (size_t len) {
  string_t *s = AllocType (string);
  size_t sz = (len <= 0 ? 8 : string_align (len));
  s->bytes = Alloc (sz);
  s->mem_size = sz;
  s->num_bytes = 0;
  s->bytes[0] = '\0';
  return s;
}

private string_t *string_new_with_len (const char *bytes, size_t len) {
  string_t *new = AllocType (string);
  size_t sz = string_align (len + 1);
  char *buf = Alloc (sz);
  memcpy (buf, bytes, len);
  buf[len] = '\0';
  new->bytes = buf;
  new->num_bytes = len;
  new->mem_size = sz;
  return new;
}

private string_t *string_new_with (const char *bytes) {
  size_t len = (NULL is bytes ? 0 : bytelen (bytes));
  return string_new_with_len (bytes, len); /* this succeeds even if bytes is NULL */
}

private string_t *string_new_with_fmt (const char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  size_t len = vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  return string_new_with_len (bytes, len);
}

private string_t *string_insert_at_with_len (string_t *this,
                     const char *bytes, int idx, size_t len) {
  size_t bts = this->num_bytes + len;
  if (bts >= this->mem_size) string_reallocate (this, bts - this->mem_size + 1);

  if (idx is (int) this->num_bytes) {
    memcpy (this->bytes + this->num_bytes, bytes, len);
  } else {
    memmove (this->bytes + idx + len, this->bytes + idx,
        this->num_bytes - idx);
    memcpy (this->bytes + idx, bytes, len);
  }

  this->num_bytes += len;
  this->bytes[this->num_bytes] = '\0';
  return this;
}

private string_t *string_insert_at (string_t *this, const char *bytes, int idx) {
   if (0 > idx) idx = this->num_bytes + idx + 1;
  if (idx < 0 or idx > (int) this->num_bytes) {
    tostderr ("ERROR THAT SHOULD NOT HAPPEN:\n"
              "string_insert_at (): index is out of range\n");
    tostderr (str_fmt ("this->bytes:\n%s\nlen: %zd\n(argument) index: %d\n",
         this->bytes, this->num_bytes, idx));
    return this;
  }

  size_t len = bytelen (bytes);
  ifnot (len) return this;

  return string_insert_at_with_len (this, bytes, idx, len);
}

private string_t *string_append_byte (string_t *this, char c) {
  int bts = this->mem_size - (this->num_bytes + 2);
  if (1 > bts) string_reallocate (this, 8);
  this->bytes[this->num_bytes++] = c;
  this->bytes[this->num_bytes] = '\0';
  return this;
}

private string_t *string_insert_byte_at (string_t *this, char c, int idx) {
  char buf[2]; buf[0] = c; buf[1] = '\0';
  return string_insert_at_with_len (this, buf, idx, 1);
}

private string_t *string_prepend_byte (string_t *this, char c) {
  int bts = this->mem_size - (this->num_bytes + 2);
  if (1 > bts) string_reallocate (this, 8);
  memmove (this->bytes + 1, this->bytes, this->num_bytes++);
  this->bytes[0] = c;
  this->bytes[this->num_bytes] = '\0';
  return this;
}

private string_t *string_append (string_t *this, const char *bytes) {
  return string_insert_at_with_len (this, bytes, this->num_bytes, bytelen (bytes));
}

private string_t *string_append_with_len (string_t *this, const char *bytes,
                                                                 size_t len) {
  return string_insert_at_with_len (this, bytes, this->num_bytes, len);
}

private string_t *string_prepend (string_t *this, const char *bytes) {
  return string_insert_at (this, bytes, 0);
}

private string_t *string_append_fmt (string_t *this, const char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  size_t len = vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  return string_insert_at_with_len (this, bytes, this->num_bytes, len);
}

private string_t *string_prepend_fmt (string_t *this, const char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  size_t len = vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  return string_insert_at_with_len (this, bytes, 0, len);
}

private int string_delete_numbytes_at (string_t *this, int num, int idx) {
  if (num < 0) return NOTOK;
  ifnot (num) return OK;

  if (idx < 0 or idx >= (int) this->num_bytes or
      idx + num > (int) this->num_bytes)
    return INDEX_ERROR;

  if (idx + num isnot (int) this->num_bytes)
    memmove (this->bytes + idx, this->bytes + idx + num,
        this->num_bytes - (idx + num - 1));

  this->num_bytes -= num;
  this->bytes[this->num_bytes] = '\0';
  return OK;
}

private string_t *string_replace_numbytes_at_with (
        string_t *this, int num, int idx, const char *bytes) {
  if (string_delete_numbytes_at (this, num, idx) isnot OK)
    return this;
  return string_insert_at (this, bytes, idx);
}

private string_t *string_replace_with (string_t *this, char *bytes) {
  string_clear (this);
  return string_append (this, bytes);
}

private string_t *string_replace_with_len (string_t *this, const char *bytes,
                                                                  size_t len) {
  string_clear (this);
  return string_insert_at_with_len (this, bytes, 0, len);
}

private string_t *string_replace_with_fmt (string_t *this, const char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  size_t len = vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  return string_replace_with_len (this, bytes, len);
}

private void vstr_clear (vstr_t *this) {
  vstring_t *vs = this->head;
  while (vs) {
    vstring_t *tmp = vs->next;
    string_free (vs->data);
    free (vs);
    vs = tmp;
  }
  this->head = this->tail = this->current = NULL;
  this->num_items = 0; this->cur_idx = -1;
}

private void vstr_free (vstr_t *this) {
  if (this is NULL) return;
  vstr_clear (this);
  free (this);
}

private vstr_t *vstr_new (void) {
  return AllocType (vstr);
}

private string_t *vstr_join (vstr_t *this, char *sep) {
  string_t *bytes = string_new (32);
  vstring_t *it = this->head;

  while (it) {
    string_append_fmt (bytes, "%s%s", it->data->bytes, sep);
    it = it->next;
  }

  if (this->num_items)
    string_clear_at (bytes, bytes->num_bytes -
        (NULL is sep ? 0 : bytelen (sep)));

  return bytes;
}

private void vstr_append_current_with (vstr_t *this, char *bytes) {
  vstring_t *vstr = AllocType (vstring);
  vstr->data = string_new_with (bytes);
  current_list_append (this, vstr);
}

private void vstr_append_with (vstr_t *this, char *bytes) {
  int cur_idx = this->cur_idx;
  if (cur_idx isnot this->num_items - 1)
    current_list_set (this, -1);

  vstr_append_current_with (this, bytes);
  current_list_set(this, cur_idx);
}

private void vstr_append_with_fmt (vstr_t *this, char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  vstr_append_with (this, bytes);
}

private void vstr_prepend_current_with (vstr_t *this, char *bytes) {
  vstring_t *vstr = AllocType (vstring);
  vstr->data = string_new_with (bytes);
  current_list_prepend (this, vstr);
}

private vstr_t *vstr_dup (vstr_t *this) {
  vstr_t *vs = vstr_new ();
  vstring_t *it = this->head;
  while (it) {
    vstr_append_current_with (vs, it->data->bytes);
    it = it->next;
  }

  current_list_set (vs, this->cur_idx);
  return vs;
}

private vstr_t *vstr_add_sort_and_uniq (vstr_t *this, char *bytes) {
  vstring_t *vs = AllocType (vstring);
  vs->data = string_new_with (bytes);

  int res = 1;

  if (this->head is NULL) {
    this->head = this->tail = vs;
    this->head->prev = this->head->next = NULL;
    goto theend;
  }

  res = strcmp (bytes, this->head->data->bytes);

  ifnot (res) goto theend;

  if (0 > res) {
    this->head->prev = vs;
    vs->next = this->head;
    this->head = vs;
    this->head->prev = NULL;
    goto theend;
  }

  if (this->num_items is 1) {
    res = 1;
    vs->prev = this->head;
    this->tail = vs;
    this->tail->next = NULL;
    this->head->next = vs;
    goto theend;
  }

  res = strcmp (bytes, this->tail->data->bytes);

  if (0 < res) {
    this->tail->next = vs;
    vs->prev = this->tail;
    this->tail = vs;
    this->tail->next = NULL;
    goto theend;
  } else ifnot (res) {
    goto theend;
  }

  vstring_t *it = this->head->next;

  while (it) {
    res = strcmp (bytes, it->data->bytes);
    ifnot (res) goto theend;

    if (0 > res) {
      it->prev->next = vs;
      it->prev->next->next = it;
      it->prev->next->prev = it->prev;
      it->prev = it->prev->next;
      it = it->prev;
      goto theend;
    }
    it = it->next;
  }

theend:
  if (res)
    this->num_items++;
  else {
    string_free (vs->data);
    free (vs);
  }

  return this;
}


public vstring_T __init_vstring__ (void) {
  return ClassInit (vstring,
    .self = SelfInit (vstring,
      .free = vstr_free,
      .clear = vstr_clear,
      .new = vstr_new,
      .join = vstr_join,
      .append_with_fmt = vstr_append_with_fmt,
      .cur = SubSelfInit (vstring, cur,
        .append_with = vstr_append_current_with
      ),
      .add = SubSelfInit (vstring, add,
        .sort_and_uniq = vstr_add_sort_and_uniq
      )
    )
  );
}

public void __deinit_vstring__ (vstring_T *this) {
  (void) this;
}

private vstr_t *str_chop (char *buf, char tok, vstr_t *tokstr,
                                     StrChop_cb cb, void *obj) {
  vstr_t *ts = tokstr;
  int ts_isnull = (NULL is ts);
  if (ts_isnull) ts = vstr_new ();

  char *sp = buf;
  char *p = sp;

  int end = 0;
  for (;;) {
    if (end) break;
    ifnot (*sp) {
      end = 1;
      goto tokenize;
    }

    if (*sp is tok) {
tokenize:;
      size_t len = sp - p;
      /* ifnot (len) {
         sp++; p = sp;
         continue;
      } */

      char s[len + 1];
      memcpy (s, p, len);
      s[len] = '\0';

      ifnot (NULL is cb) {
        int retval;
        if (STRCHOP_NOTOK is (retval = cb (ts, s, obj))) {
          if (ts_isnull) vstr_free (ts); /* this might bring issues (not */
          return NULL;                   /* with current code though) */
        }

        if (retval is STRCHOP_RETURN)
          return ts;
      }
      else
        vstr_append_current_with (ts, s);

      sp++;
      p = sp;
      continue;
    }

    sp++;
  }

  return ts;
}

/* unused
 * private vstr_t *str_tok (char *buf, char *tok, vstr_t *tokstr,
 *     void (*cb) (vstr_t *, char *, void *), void *obj) {
 *   vstr_t *ts = tokstr;
 *   if (NULL is ts) ts = vstr_new ();
 *
 *   char *src = str_dup (buf, bytelen (buf));
 *   char *sp = strtok (src, tok);
 *   while (sp) {
 *     ifnot (NULL is cb)
 *       cb (ts, sp, obj);
 *     else
 *       vstr_append_current_with (ts, sp);
 *     sp = strtok (NULL, tok);
 *   }
 *
 *   free (src);
 *   return ts;
 * }
 */

private int re_exec (regexp_t *re, char *bytes, size_t buf_len) {
  (void) buf_len;
  char *sp = strstr (bytes, re->pat->bytes);

  if (NULL is sp) {
    re->retval = RE_NO_MATCH;
    goto theend;
  }

  re->match_idx = sp - bytes;
  re->match_len = re->pat->num_bytes;
  re->match_ptr = bytes + re->match_idx;
  re->retval = re->match_idx + re->match_len;
  re->match = re->pat;

theend:
  return re->retval;
}

private void re_reset_captures (regexp_t *re) {
  re->match_len = re->match_idx = 0;
  re->match_ptr = NULL;

  ifnot (re->flags & RE_PATTERN_IS_STRING_LITERAL) {
    if (re->match isnot NULL) {
      string_free (re->match);
      re->match = NULL;
    }
  }

  if (re->cap is NULL) return;
  for (int i = 0; i < re->num_caps; i++) {
    if (NULL is re->cap[i]) continue;
    free (re->cap[i]);
    re->cap[i] = NULL;
  }
}

private void re_free_captures (regexp_t *re) {
  re_reset_captures (re);
  if (re->cap is NULL) return;
  free (re->cap);
  re->cap = NULL;
}

private void re_allocate_captures (regexp_t *re, int num) {
  re->num_caps = (0 > num ? 0 : num);
  re->cap = Alloc (sizeof (capture_t) * re->num_caps);
}

private void re_free_pat (regexp_t *re) {
  if (NULL is re->pat) return;
  string_free (re->pat);
  re->pat = NULL;
}

private void re_free (regexp_t *re) {
  re_free_pat (re);
  re_free_captures (re);
  free (re);
}

private int re_compile (regexp_t *re) {
  re->flags |= RE_PATTERN_IS_STRING_LITERAL;
  return OK;
}

private regexp_t *re_new (char *pat, int flags, int num_caps, int (*compile) (regexp_t *)) {
  regexp_t *re = AllocType (regexp);
  re->flags |= flags;
  re->pat = string_new_with (pat);
  compile (re);
  re_allocate_captures (re, num_caps);
  re->match = NULL;
  return re;
}

private string_t *re_parse_substitute (regexp_t *re, char *sub, char *replace_buf) {
  string_t *substr = string_new (16);
  char *sub_p = sub;
  while (*sub_p) {
    switch (*sub_p) {
      case '\\':
        if (*(sub_p + 1) is 0) {
          strcpy (re->errmsg, "awaiting escaped char, found (null byte) 0");
          goto theerror;
        }

        switch (*++sub_p) {
          case '&':
            string_append_byte (substr, '&');
            sub_p++;
            continue;

          case '\\':
            string_append_byte (substr, '\\');
            sub_p++;
            continue;

          case 's':
            string_append_byte (substr, ' ');
            sub_p++;
            continue;

          default:
            snprintf (re->errmsg, 256, "awaiting \\,&,s, got %d [%c]", *sub_p, *sub_p);
            goto theerror;
        }

      case '&':
        string_append (substr, replace_buf);
        break;

      default:
        string_append_byte (substr, *sub_p);
     }
    sub_p++;
  }

  return substr;

theerror:
  string_free (substr);
  return NULL;
}

public re_T __init_re__ (void) {
  return ClassInit (re,
    .self = SelfInit (re,
      .exec = re_exec,
      .new = re_new,
      .free = re_free,
      .free_captures = re_free_captures,
      .allocate_captures = re_allocate_captures,
      .reset_captures = re_reset_captures,
      .free_pat = re_free_pat,
      .parse_substitute = re_parse_substitute,
      .compile = re_compile,
    )
  );
}

private int file_is_reg (const char *fname) {
  struct stat st;
  if (NOTOK is stat (fname, &st)) return 0;
  return S_ISREG (st.st_mode);
}

private int file_exists (const char *fname) {
  return (0 is access (fname, F_OK));
}

private int file_is_executable (const char *fname) {
  return (0 is access (fname, F_OK|X_OK));
}

private int file_is_readable (const char *fname) {
  return (0 is access (fname, R_OK));
}

private int file_is_elf (const char *file) {
  int fd = open (file, O_RDONLY);
  if (-1 is fd) return 0;
  int retval = 0;
  char buf[8];
  ssize_t bts = fd_read (fd, buf, 4);
  if (bts < 4) goto theend;
  buf[bts] = '\0';
  retval = str_eq (buf + 1, "ELF");
theend:
  close (fd);
  return retval;
}

private vstr_t *file_readlines (char *file, vstr_t *lines,
                                 FileReadLines_cb cb, void *obj) {
  vstr_t *llines = lines;
  if (NULL is llines) llines = vstr_new ();
  if (-1 is access (file, F_OK|R_OK)) goto theend;
  FILE *fp = fopen (file, "r");
  char *buf = NULL;
  size_t len;
  ssize_t nread;

  if (cb isnot NULL) {
    int num = 0;
    while (-1 isnot (nread = getline (&buf, &len, fp))) {
      cb (llines, buf, nread, ++num, obj);
    }
  } else {
    while (-1 isnot (nread = getline (&buf, &len, fp))) {
      buf[nread - 1] = '\0';
      vstr_append_current_with (llines, buf);
    }
  }

  fclose (fp);
  ifnot (buf is NULL) free (buf);

theend:
  return llines;
}

private ssize_t __file_write__ (char *fname, char *bytes, ssize_t size, char *mode) {
  if (size < 0) size = bytelen (bytes);
  if (size <= 0) return NOTOK;

  FILE *fp = fopen (fname, mode);
  if (NULL is fp) return NOTOK;
  size = fwrite (bytes, 1, size, fp);
  fclose (fp);
  return size;
}

private ssize_t file_write (char *fname, char *bytes, ssize_t size) {
  return __file_write__ (fname, bytes, size, "w");
}

private ssize_t file_append (char *fname, char *bytes, ssize_t size) {
  return __file_write__ (fname, bytes, size, "a+");
}

public file_T __init_file__ (void) {
  return ClassInit (file,
    .self = SelfInit (file,
      .exists = file_exists,
      .is_readable = file_is_readable,
      .is_executable = file_is_executable,
      .is_elf = file_is_elf,
      .is_reg = file_is_reg,
      .readlines = file_readlines,
      .write = file_write,
      .append = file_append
    )
  );
}

private int is_directory (char *dname) {
  struct stat st;
  if (NOTOK is stat (dname, &st)) return 0;
  return S_ISDIR (st.st_mode);
}

private char *dir_current (void) {
  size_t size = 64;
  char *buf = Alloc (size);
  char *dir = NULL;

  while ((dir = getcwd (buf, size)) is NULL) {
    if (errno isnot ERANGE) break;
    size += (size / 2);
    buf = Realloc (buf, size);
  }

  return dir;
}

private char *buf_get_current_dir (buf_t *this, int new_allocation) {
  char *cwd = dir_current ();
  if (NULL is cwd) {
    MSG_ERRNO (MSG_CAN_NOT_DETERMINATE_CURRENT_DIR);
    free (cwd);
    return NULL;
  }

  if (new_allocation) return cwd;

  My(String).replace_with ($my(shared_str), cwd);
  free (cwd);
  return $my(shared_str)->bytes;
}

private void dirlist_free (dirlist_t *dlist) {
  vstr_free (dlist->list);
  free (dlist);
}

private dirlist_t *dirlist (char *dir, int flags) {
  (void) flags;
  if (NULL is dir) return NULL;
  ifnot (is_directory (dir)) return NULL;

  DIR *dh = NULL;
  if (NULL is (dh = opendir (dir))) return NULL;
  struct dirent *dp;

  size_t len;

  dirlist_t *dlist = AllocType (dirlist);
  dlist->free = dirlist_free;
  dlist->list = vstr_new ();
  strncpy (dlist->dir, dir, PATH_MAX - 1);

  while (1) {
    errno = 0;

    if (NULL is (dp = readdir (dh)))
      break;

    len = bytelen (dp->d_name);

    if (len < 3 and dp->d_name[0] is '.')
      if (len is 1 or dp->d_name[1] is '.')
        continue;

    vstring_t *vstr = AllocType (vstring);
    vstr->data = string_new_with (dp->d_name);
/* continue logic (though not sure where to store) */
    switch (dp->d_type) {
      case DT_DIR:
        string_append_byte (vstr->data, DIR_SEP);
    }

    current_list_append (dlist->list, vstr);
  }

  closedir (dh);
  return dlist;
}

public dir_T __init_dir__ (void) {
  return ClassInit (dir,
    .self = SelfInit (dir,
      .list = dirlist,
      .current = dir_current
    )
  );
}

private void tmpfname_free (tmpname_t *this) {
  ifnot (this) return;
  ifnot (NULL is this->fname) {
    unlink (this->fname->bytes);
    string_free (this->fname);
    this->fname = NULL;
  }
  free (this);
}

private tmpname_t *tmpfname (char *dname, char *prefix) {
  static unsigned int seed = 12252;
  ifnot (is_directory (dname)) return NULL;

  tmpname_t *this = NULL;

  char bpid[6];
  pid_t pid = getpid ();
  itoa ((int) pid, bpid, 10);

  int len = bytelen (dname) + bytelen (bpid) + bytelen (prefix) + 10;

  char name[len];
  snprintf (name, len, "%s/%s-%s.xxxxxx", dname, prefix, bpid);

  srand ((unsigned int) time (NULL) + (unsigned int) pid + seed++);

  dirlist_t *dlist = dirlist (dname, 0);
  if (NULL is dlist) return NULL;

  int
    found = 0,
    loops = 0,
    max_loops = 1024,
    inner_loops = 0,
    max_inner_loops = 1024;
  char c;

  while (1) {
again:
    found = 0;
    if (++loops is max_loops) goto theend;

    for (int i = 0; i < 6; i++) {
      inner_loops = 0;
      while (1) {
        if (++inner_loops is max_inner_loops) goto theend;

        c = (char) (rand () % 123);
        if ((c <= 'z' and c >= 'a') or (c >= '0' and c <= '9') or
            (c >= 'A' and c <= 'Z') or c is '_') {
          name[len - i - 2] = c;
          break;
        }
      }
    }

    vstring_t *it = dlist->list->head;
    while (it) {
      if (str_eq (name, it->data->bytes)) goto again;
      it = it->next;
    }

    found = 1;
    break;
  }

  ifnot (found) goto theend;

  this = Alloc (sizeof (tmpname_t));
  this->fd = open (name, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);

  if (-1 is this->fd) goto theerror;
  if (-1 is fchmod (this->fd, 0600)) {
    close (this->fd);
    goto theerror;
  }

  this->fname = string_new_with (name);
  goto theend;

theerror:
  ifnot (NULL is this) {
    tmpfname_free (this);
    this = NULL;
  }

theend:
  dlist->free (dlist);
  return this;
}

private char *path_basename (char *name) {
  ifnot (name) return name;
  char *p = nullbyte_in_str (name);
  if (p is NULL) p = name + bytelen (name) + 1;
  while (p > name and IsNotDirSep (*(p - 1))) --p;
  return p;
}

private char *path_extname (char *name) {
  ifnot (name) return name;
  char *p = nullbyte_in_str (name);
  if (p is NULL) p = name + bytelen (name) + 1;
  while (p > name and (*(p - 1) isnot '.')) --p;
  if (p is name) return "";
  p--;
  return p;
}

private char *path_dirname (char *name) {
  size_t len = bytelen (name);
  char *dname = NULL;
  if (name is NULL or 0 is len) {
    dname = Alloc (2); dname[0] = '.'; dname[1] = '\0';
    return dname;
  }

  char *sep = name + len;

  /* trailing slashes */
  while (sep isnot name) {
    ifnot (IS_DIR_SEP (*sep)) break;
    sep--;
  }

  /* first found */
  while (sep isnot name) {
    if (IS_DIR_SEP (*sep)) break;
    sep--;
  }

  /* trim again */
  while (sep isnot name) {
    ifnot (IS_DIR_SEP (*sep)) break;
    sep--;
  }

  if (sep is name) {
    dname = Alloc (2);
    dname[0] = (IS_DIR_SEP (*name)) ? DIR_SEP : '.'; dname[1] = '\0';
    return dname;
  }

  len = sep - name + 1;
  dname = Alloc (len + 1); memcpy (dname, name, len); dname[len] = '\0';
  return dname;
}

private int path_is_absolute (char *path) {
  return IS_PATH_ABS (path);
}

public path_T __init_path__ (void) {
  return ClassInit (path,
    .self = SelfInit (path,
      .basename = path_basename,
      .extname = path_extname,
      .dirname = path_dirname,
      .is_absolute = path_is_absolute
    )
  );
}

#define CONTINUE_ON_EXPECTED_ERRNO(fd__)  \
  if (errno == EINTR) continue;           \
  if (errno == EAGAIN) {                  \
    struct timeval tv;                    \
    fd_set read_fd;                       \
    FD_ZERO(&read_fd);                    \
    FD_SET(fd, &read_fd);                 \
    tv.tv_sec = 0;                        \
    tv.tv_usec = 100000;                  \
    select (fd__ + 1, &read_fd, NULL, NULL, &tv); \
    continue;                             \
   } do {} while (0)

private int fd_read (int fd, char *buf, size_t len) {
  if (1 > len) return NOTOK;

  char *s = buf;
  ssize_t bts;
  int tbts = 0;

  while (1) {
    if (NOTOK is (bts = read (fd, s, len))) {
      CONTINUE_ON_EXPECTED_ERRNO (fd);
      return NOTOK;
    }

    tbts += bts;
    if (tbts is (int) len or bts is 0) break;

    s += bts;
  }

  buf[tbts] = '\0';
  return bts;
}

private int fd_write (int fd, char *buf, int len) {
  int retval = len;
  int bts;

  while (len > 0) {
    if (NOTOK is (bts = write (fd, buf, len))) {
      CONTINUE_ON_EXPECTED_ERRNO (fd);
      return NOTOK;
    }

    len -= bts;
    buf += bts;
  }

  return retval;
}

#define SEND_ESC_SEQ(fd, seq) fd_write ((fd), seq, seq ## _LEN)
#define TERM_SEND_ESC_SEQ(seq) fd_write ($my(out_fd), seq, seq ## _LEN)

private void term_screen_save (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_SCREEN_SAVE);
}

private void term_screen_restore (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_SCREEN_RESTORE);
}

private void term_screen_clear (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_SCREEN_CLEAR);
}

private void term_screen_clear_eol (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_LINE_CLR_EOL);
}

private void term_cursor_hide (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_CURSOR_HIDE);
}

private void term_cursor_show (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_CURSOR_SHOW);
}

private void term_cursor_restore (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_CURSOR_RESTORE);
}

private int term_cursor_get_ptr_pos (term_t *this, int *row, int *col) {
  if (NOTOK is TERM_SEND_ESC_SEQ (TERM_GET_PTR_POS))
    return NOTOK;

  char buf[32];
  uint i = 0;
  int bts;
  while (i < sizeof (buf) - 1) {
    if (NOTOK is (bts = fd_read ($my(in_fd), buf + i, 1)) or bts is 0)
      return NOTOK;

    if (buf[i] is 'R') break;
    i++;
  }

  buf[i] = '\0';

  if (buf[0] isnot ESCAPE_KEY or buf[1] isnot '[' or
      2 isnot sscanf (buf + 2, "%d;%d", row, col))
    return NOTOK;

  return OK;
}

private void term_cursor_set_ptr_pos (term_t *this, int row, int col) {
  char ptr[32];
  snprintf (ptr, 32, TERM_GOTO_PTR_POS_FMT, row, col);
  fd_write ($my(out_fd), ptr, bytelen (ptr));
}

private void term_free (term_t *this) {
  if (NULL is this) return;

  ifnot (NULL is $myprop) {
    ifnot (NULL is $my(name)) {
      free ($my(name));
      $my(name) = NULL;
    }
    free ($myprop);
    $myprop = NULL;
  }
  free (this); this = NULL;
}

private term_t *term_new (void) {
  term_t *this = AllocType (term);
  $myprop = AllocProp (term);
  $my(lines) = 24;
  $my(columns) = 78;
  $my(out_fd) = STDOUT_FILENO;
  $my(in_fd) = STDIN_FILENO;
  $my(mode) = 'o';
  return this;
}

private void term_init_size (term_t *this, int *rows, int *cols) {
  struct winsize wsiz;

  do {
    if (OK is ioctl ($my(out_fd), TIOCGWINSZ, &wsiz)) {
      $my(lines) = (int) wsiz.ws_row;
      $my(columns) = (int) wsiz.ws_col;
      return;
    }
  } while (errno is EINTR);

  int orig_row, orig_col;
  term_cursor_get_ptr_pos (this, &orig_row, &orig_col);

  TERM_SEND_ESC_SEQ (TERM_LAST_RIGHT_CORNER);
  term_cursor_get_ptr_pos (this, rows, cols);
  term_cursor_set_ptr_pos (this, orig_row, orig_col);
}

private int term_sane_mode (term_t *this) {
  if ($my(mode) is 's') return OK;
   if (isnotatty ($my(in_fd))) return NOTOK;

  struct termios mode;
  while (NOTOK is tcgetattr ($my(in_fd), &mode))
    if (errno isnot EINTR) return NOTOK;

  mode.c_iflag |= (BRKINT|INLCR|ICRNL|IXON|ISTRIP);
  mode.c_iflag &= ~(IGNBRK|INLCR|IGNCR|IXOFF);
  mode.c_oflag |= (OPOST|ONLCR);
  mode.c_lflag |= (ECHO|ECHOE|ECHOK|ECHOCTL|ISIG|ICANON|IEXTEN);
  mode.c_lflag &= ~(ECHONL|NOFLSH|XCASE|TOSTOP|ECHOPRT);
  mode.c_cc[VEOF] = 'D'^64; // splitvt
  mode.c_cc[VMIN] = 1;   /* 0 */
  mode.c_cc[VTIME] = 0;  /* 1 */

  while (NOTOK is tcsetattr ($my(in_fd), TCSAFLUSH, &mode))
    if (errno isnot EINTR) return NOTOK;

  $my(mode) = 's';
  return OK;
}

private int term_raw_mode (term_t *this) {
   if ($my(mode) is 'r') return OK;
   if (isnotatty ($my(in_fd))) return NOTOK;

  while (NOTOK is tcgetattr ($my(in_fd), &$my(orig_mode)))
    if (errno isnot EINTR) return NOTOK;

  $my(raw_mode) = $my(orig_mode);
  $my(raw_mode).c_iflag &= ~(INLCR|ICRNL|IXON|ISTRIP);
  $my(raw_mode).c_cflag |= (CS8);
  $my(raw_mode).c_oflag &= ~(OPOST);
  $my(raw_mode).c_lflag &= ~(ECHO|ISIG|ICANON|IEXTEN);
  $my(raw_mode).c_lflag &= NOFLSH;
  $my(raw_mode).c_cc[VEOF] = 1;
  $my(raw_mode).c_cc[VMIN] = 0;   /* 1 */
  $my(raw_mode).c_cc[VTIME] = 1;  /* 0 */

  while (NOTOK is tcsetattr ($my(in_fd), TCSAFLUSH, &$my(raw_mode)))
    ifnot (errno is EINTR) return NOTOK;

  $my(mode) = 'r';
  return OK;
}

private int term_orig_mode (term_t *this) {
  if ($my(mode) is 'o') return OK;
  if (isnotatty ($my(in_fd))) return NOTOK;

  while (NOTOK is tcsetattr ($my(in_fd), TCSAFLUSH, &$my(orig_mode)))
    ifnot (errno is EINTR) return NOTOK;

  $my(mode) = 'o';
  return OK;
}

private int *term_get_dim (term_t *this, int *dim) {
  dim[0] = $my(lines);
  dim[1] = $my(columns);
  return dim;
}

private void term_set_name (term_t *this) {
  $my(name) = NULL;
}

private int term_set_mode (term_t *this, char mode) {
  switch (mode) {
    case 'o': return term_orig_mode (this);
    case 's': return term_sane_mode (this);
    case 'r': return term_raw_mode (this);
  }
  return NOTOK;
}

private int term_set (term_t *this) {
  if (NOTOK is term_set_mode (this, 'r')) return NOTOK;
  term_cursor_get_ptr_pos (this,  &$my(orig_curs_row_pos), &$my(orig_curs_col_pos));
  term_init_size (this, &$my(lines), &$my(columns));
  term_screen_save (this);
  term_screen_clear (this);
  return OK;
}

private int term_reset (term_t *this) {
  term_set_mode (this, 's');
  term_cursor_set_ptr_pos (this, $my(orig_curs_row_pos), $my(orig_curs_col_pos));
  term_screen_restore (this);
  return OK;
}

/* this is an extended version of the same function of
 * the kilo editor at https://github.com/antirez/kilo.git
 */
private utf8 term_get_input (term_t *this) {
  char c;
  int n;
  char buf[5];

  while (0 is (n = fd_read ($my(in_fd), buf, 1)));

  if (n is NOTOK) return NOTOK;

  c = buf[0];

  switch (c) {
    case ESCAPE_KEY:
      if (fd_read ($my(in_fd), buf, 1) is 0)
         return ESCAPE_KEY;

      /* recent (revailed through CTRL-[other than CTRL sequence]) and unused */
      if ('z' >= buf[0] and buf[0] >= 'a') {
        /*
         * if (fd_read ($my(in_fd), buf + 1, 1) is 0)
         *   return ALT(buf[0]);
         * else
         */
        return 0;
      }

      if (buf[0] isnot '[' && buf[0] isnot 'O')
        return 0; // 65535 + buf[0];

      ifnot (fd_read ($my(in_fd), buf + 1, 1))
        return ESCAPE_KEY;

      if (buf[0] == '[') {
        if ('0' <= buf[1] && buf[1] <= '9') {
          ifnot (fd_read ($my(in_fd), buf + 2, 1))
             return ESCAPE_KEY;

          if (buf[2] == '~') {
            switch (buf[1]) {
              case '1': return HOME_KEY;
              case '2': return INSERT_KEY;
              case '3': return DELETE_KEY;
              case '4': return END_KEY;
              case '5': return PAGE_UP_KEY;
              case '6': return PAGE_DOWN_KEY;
              case '7': return HOME_KEY;
              case '8': return END_KEY;
              default: return 0;
            }
          } else if (buf[1] == '1') {
            if (fd_read ($my(in_fd), buf, 1) is 0)
              return ESCAPE_KEY;

            switch (buf[2]) {
              case '1': return FN_KEY(1);
              case '2': return FN_KEY(2);
              case '3': return FN_KEY(3);
              case '4': return FN_KEY(4);
              case '5': return FN_KEY(5);
              case '7': return FN_KEY(6);
              case '8': return FN_KEY(7);
              case '9': return FN_KEY(8);
              default: return 0;
            }
          } else if (buf[1] == '2') {
            if (fd_read ($my(in_fd), buf, 1) is 0)
              return ESCAPE_KEY;

            switch (buf[2]) {
              case '0': return FN_KEY(9);
              case '1': return FN_KEY(10);
              case '3': return FN_KEY(11);
              case '4': return FN_KEY(12);
              default: return 0;
            }
          } else { /* CTRL_[key other than CTRL sequence] */
                   /* lower case */
            if (buf[2] == 'h')
              return INSERT_KEY; /* sample/test (logically return 0) */
            else
              return 0;
          }
        } else if (buf[1] == '[') {
          if (fd_read ($my(in_fd), buf, 1) is 0)
            return ESCAPE_KEY;

          switch (buf[0]) {
            case 'A': return FN_KEY(1);
            case 'B': return FN_KEY(2);
            case 'C': return FN_KEY(3);
            case 'D': return FN_KEY(4);
            case 'E': return FN_KEY(5);

            default: return 0;
          }
        } else {
          switch (buf[1]) {
            case 'A': return ARROW_UP_KEY;
            case 'B': return ARROW_DOWN_KEY;
            case 'C': return ARROW_RIGHT_KEY;
            case 'D': return ARROW_LEFT_KEY;
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            case 'P': return DELETE_KEY;

            default: return 0;
          }
        }
      } else if (buf[0] == 'O') {
        switch (buf[1]) {
          case 'A': return ARROW_UP_KEY;
          case 'B': return ARROW_DOWN_KEY;
          case 'C': return ARROW_RIGHT_KEY;
          case 'D': return ARROW_LEFT_KEY;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
          case 'P': return FN_KEY(1);
          case 'Q': return FN_KEY(2);
          case 'R': return FN_KEY(3);
          case 'S': return FN_KEY(4);

          default: return 0;
        }
      }
    break;

  default:
    if (c < 0) {
      int len = ustring_charlen ((uchar) c);
      utf8 code = 0;
      code += (uchar) c;

      int idx;
      int invalid = 0;
      char cc;

      for (idx = 0; idx < len - 1; idx++) {
        if (0 >= fd_read ($my(in_fd), &cc, 1))
          return NOTOK;

        if (isnotutf8 ((uchar) cc)) {
          invalid = 1;
        } else {
          code <<= 6;
          code += (uchar) cc;
        }
      }

      if (invalid) return NOTOK;

      code -= offsetsFromUTF8[len-1];
      return code;
    }

    if (127 == c) return BACKSPACE_KEY;

    return c;
  }

  return NOTOK;
}

private video_t *video_new (int fd, int rows, int cols, int first_row, int first_col) {
  video_t *this = AllocType (video);

  loop (rows) {
    vstring_t *row = AllocType (vstring);
    row->data = string_new_with_len (" ", 1);
    current_list_append (this, row);
  }

  this->fd = fd;
  this->first_row = first_row;
  this->first_col = first_col;
  this->num_rows = rows;
  this->num_cols = cols;
  this->last_row = first_row + rows - 1;
  this->row_pos = this->first_row;
  this->col_pos = 1;
  this->render = string_new (cols);
  this->tmp_render = string_new (cols);
  this->rows = Alloc (sizeof (int) * this->num_rows);
  return this;
}

private void video_free (video_t *this) {
  if (this is NULL) return;

  vstring_t *it = this->head;

  while (it isnot NULL) {
    vstring_t *next = it->next;
    string_free (it->data);
    free (it);
    it = next;
  }

  string_free (this->render);
  string_free (this->tmp_render);
  free (this->rows);
  free (this);
}

private void video_flush (video_t *this, string_t *render) {
  fd_write (this->fd, render->bytes, render->num_bytes);
}

private void video_render_set_from_to (video_t *this, int frow, int lrow) {
  int fidx = frow - 1; int lidx = lrow - 1;

  string_append (this->render, TERM_CURSOR_HIDE);
  while (fidx <= lidx) {
    current_list_set (this, fidx++);
    string_append_fmt (this->render, TERM_GOTO_PTR_POS_FMT "%s%s%s",
        this->cur_idx + 1, this->first_col, TERM_LINE_CLR_EOL,
        this->current->data->bytes, TERM_BOL);
  }

  string_append (this->render, TERM_CURSOR_SHOW);
}

private void video_draw_at (video_t *this, int at) {
  int idx = at - 1;

  if (current_list_set(this, idx) is INDEX_ERROR) return;

  vstring_t *row = this->current;
  string_replace_with_fmt (this->tmp_render,
      "%s" TERM_GOTO_PTR_POS_FMT "%s%s%s" TERM_GOTO_PTR_POS_FMT,
      TERM_CURSOR_HIDE, at, this->first_col, TERM_LINE_CLR_EOL,
      row->data->bytes, TERM_CURSOR_SHOW, this->row_pos, this->col_pos);

  video_flush (this, this->tmp_render);
}

private void video_draw_all (video_t *this) {
  string_clear (this->tmp_render);
  string_append_fmt (this->tmp_render,
      "%s%s" TERM_SCROLL_REGION_FMT "\033[0m\033[1m",
   TERM_CURSOR_HIDE, TERM_FIRST_LEFT_CORNER, 0, this->num_rows);

  int num_times = this->last_row;
  vstring_t *row = this->head;

  loop (num_times - 1) {
    string_append_fmt (this->tmp_render, "%s%s%s", TERM_LINE_CLR_EOL,
        row->data->bytes, TERM_BOL);
    row = row->next;
  }

  string_append_fmt (this->tmp_render, "%s" TERM_GOTO_PTR_POS_FMT,
     TERM_CURSOR_SHOW, this->row_pos, this->col_pos);

  video_flush (this, this->tmp_render);
}

private void video_set_row_with (video_t *this, int idx, char *bytes) {
  if (current_list_set (this, idx) is INDEX_ERROR) return;
  vstring_t *row = this->current;
  string_replace_with (row->data, bytes);
}

private void video_row_hl_at (video_t *this, int idx, int color,
                                               int fidx, int lidx) {
  if (current_list_set (this, idx) is INDEX_ERROR) return;
  vstring_t *row = this->current;
  if (fidx >= (int) row->data->num_bytes) return;
  if (lidx >= (int) row->data->num_bytes) lidx = row->data->num_bytes - 1;

  string_insert_at_with_len (row->data, TERM_COLOR_RESET, lidx, TERM_COLOR_RESET_LEN);
  string_insert_at_with_len (row->data, TERM_MAKE_COLOR(color), fidx, TERM_SET_COLOR_FMT_LEN);
  string_replace_with_fmt (this->tmp_render, TERM_GOTO_PTR_POS_FMT, idx + 1, 1);
  string_append_with_len (this->tmp_render, row->data->bytes, row->data->num_bytes);

  video_flush (this, this->tmp_render);
}

private void video_resume_painted_rows (video_t *this) {
  int row = 0, i = 0;
  while (0 isnot (row = this->rows[i++])) video_draw_at (this, row);
  this->rows[0] = 0;
}

private video_t *video_paint_rows_with (video_t *this, int row, int f_col, int l_col, char *bytes) {
  int last_row = this->last_row - 3;
  int first_row = row < 1 or row > last_row ? last_row : row;
  int last_col = l_col < 0 or l_col > this->num_cols ? this->num_cols : l_col;
  int first_col = f_col < 0 or f_col > this->num_cols ? 1 : f_col;
  char *f_p = 0;
  f_p = bytes;
  char *l_p = bytes;

  vstr_t *vsa = vstr_new ();

  while (l_p) {
    vstring_t *vs = AllocType (vstring);
    vs->data = string_new (8);

    l_p = strstr (l_p, "\n");

    if (NULL is l_p) {
      string_append (vs->data, f_p);
    } else {
      char line[l_p - f_p];
      int i = 0;
      while (f_p < l_p) line[i++] = *f_p++;
      f_p++; l_p++;
      line[i] = '\0';
      string_append (vs->data, line);
    }

    current_list_append (vsa, vs);
  }

  int num_rows = vsa->num_items;
  int i = 0; while (i < num_rows - 1 and first_row > 2) {i++; first_row--;};
  num_rows = i + 1;
  int num_chars = last_col - first_col + 1;

  string_clear (this->tmp_render);
  string_append_fmt (this->tmp_render, "%s" TERM_SET_COLOR_FMT, TERM_CURSOR_HIDE, COLOR_BOX);
  vstring_t *it = vsa->head;
  i = 0;
  while (i < num_rows) {
    this->rows[i] = (i + first_row);
    string_append_fmt (this->tmp_render, TERM_GOTO_PTR_POS_FMT, first_row + i++, first_col);
    int num = 0; int idx = 0;
    while (num++ < num_chars and idx < (int) it->data->num_bytes) {
      int clen = ustring_charlen ((uchar) it->data->bytes[idx]);
      for (int li = 0; li < clen; li++)
        string_append_byte (this->tmp_render, it->data->bytes[idx + li]);
      idx += clen;
    }

    while (num++ <= num_chars) string_append_byte (this->tmp_render, ' ');

    it = it->next;
  }

  this->rows[num_rows] = 0;

  string_append_fmt (this->tmp_render, "%s%s", TERM_COLOR_RESET, TERM_CURSOR_SHOW);
  video_flush (this, this->tmp_render);
  vstr_free (vsa);
  return this;
}

private void menu_free_list (menu_t *this) {
  if (this->state & MENU_LIST_IS_ALLOCATED) {
    vstr_free (this->list);
    this->state &= ~MENU_LIST_IS_ALLOCATED;
  }
}

private void menu_free (menu_t *this) {
  menu_free_list (this);
  string_free (this->header);
  free (this);
}

private menu_t *menu_new (ed_t *this, int first_row, int last_row, int first_col,
                                 MenuProcessList_cb cb, char *pat, size_t patlen) {
  menu_t *menu = AllocType (menu);
  menu->fd = $my(video)->fd;
  menu->first_row = first_row;
  menu->orig_first_row = first_row;
  menu->min_first_row = 3;
  menu->last_row = last_row;
  menu->num_rows = menu->last_row - menu->first_row + 1;
  menu->orig_num_rows = menu->num_rows;
  menu->first_col = first_col + 1;
  menu->num_cols = $my(dim)->num_cols;
  menu->cur_video = $my(video);
  menu->process_list = cb;
  menu->state |= (MENU_INIT|RL_IS_VISIBLE);
  menu->space_selects = 1;
  menu->header = string_new (8);
  menu->this = self(get.current_buf);
  ifnot (NULL is pat) {
    memcpy (menu->pat, pat, patlen);
    menu->patlen = patlen;
  }

  menu->retval = menu->process_list (menu);
  return menu;
}

private void menu_clear (menu_t *menu) {
  if (menu->header->num_bytes)
    video_draw_at (menu->cur_video, menu->first_row - 1);

  for (int i = 0;i < menu->num_rows; i++)
    video_draw_at (menu->cur_video, menu->first_row + i);
}

private int rline_menu_at_beg (rline_t **rl) {
  switch ((*rl)->c) {
    case ESCAPE_KEY:
    case '\r':
    case ARROW_LEFT_KEY:
    case ARROW_RIGHT_KEY:
    case ARROW_UP_KEY:
    case ARROW_DOWN_KEY:
    case '\t':

      (*rl)->state |= RL_POST_PROCESS;
      return RL_POST_PROCESS;
  }

  (*rl)->state |= RL_OK;
  return RL_OK;
}

private int rline_menu_at_end (rline_t **rl) {
  menu_t *menu = (menu_t *) (*rl)->object;

  switch ((*rl)->c) {
    case BACKSPACE_KEY:
      if (menu->clear_and_continue_on_backspace) {
        menu_clear (menu);
       (*rl)->state |= RL_CONTINUE;
       return RL_CONTINUE;
      }
      break;
  }

  (*rl)->state |= RL_BREAK;
  return RL_BREAK;
}

private char *menu_create (ed_t *this, menu_t *menu) {
  rline_t *rl = rline_new (this, $my(term), My(Input).get, $my(prompt_row),
      1, $my(dim)->num_cols, $my(video));
  rl->at_beg = rline_menu_at_beg;
  rl->at_end = rline_menu_at_end;
  rl->object = (void *) menu;
  rl->state |= RL_CURSOR_HIDE;
  rl->prompt_char = 0;

  if (menu->state & RL_IS_VISIBLE) rl->state &= ~RL_IS_VISIBLE;

  BYTES_TO_RLINE (rl, menu->pat, menu->patlen);

init_list:;
  if (menu->state & MENU_REINIT_LIST) {
    menu->state &= ~MENU_REINIT_LIST;
    menu_clear (menu);
    menu->first_row = menu->orig_first_row;
  }

  char *match = NULL;
  int cur_idx = 0;
  int maxlen = 0;
  int vcol_pos = 1;
  int vrow_pos = 0;
  int frow_idx = 0;
  int num_rows = 0;
  int num_cols = 0;
  int mod = 0;
  int num = 1;
  int rend_rows = menu->orig_num_rows;
  int first_row = menu->first_row;
  int first_col = menu->first_col;
  int orig_beh  = menu->return_if_one_item;

  ifnot (menu->list->num_items) goto theend;

  vstring_t *it = menu->list->head;

  if (menu->list->num_items is 1)
    if (menu->return_if_one_item) {
      menu->c = '\r';
      goto handle_char;
    }

  while (it) {
    if ((int) it->data->num_bytes > maxlen) maxlen = it->data->num_bytes;
    it = it->next;
  }

  ifnot (maxlen) goto theend;
  maxlen++;

  int avail_cols = menu->num_cols - menu->first_col;

  if (maxlen < avail_cols) {
    num = avail_cols / maxlen;
    if ((num - 1) + (maxlen * num) > avail_cols)
      num--;
  } else {
    num = 1;
    maxlen = avail_cols;
  }

  mod = menu->list->num_items % num;
  num_cols = (num * maxlen);
  num_rows = (menu->list->num_items / num) + (mod isnot 0);
  // +  (menu->header->num_bytes isnot 0);

  if (num_rows > rend_rows) {
    while (first_row > menu->min_first_row and num_rows > rend_rows) {
      first_row--;
      rend_rows++;
    }
  } else {
    rend_rows = num_rows;
  }

  menu->num_rows = rend_rows;
  menu->first_row = first_row;

  char *fmt =  str_fmt ("\033[%%dm%%-%ds%%s", maxlen);

  vrow_pos = first_row;

  for (;;) {
    it = menu->list->head;
    for (int i = 0; i < frow_idx; i++)
      for (int j = 0; j < num; j++)
        it = it->next;

    string_t *render = string_new_with (TERM_CURSOR_HIDE);
    if (menu->header->num_bytes) {
      string_append_fmt (render, TERM_GOTO_PTR_POS_FMT TERM_SET_COLOR_FMT,
         first_row - 1, first_col, COLOR_MENU_HEADER);

      if ((int) menu->header->num_bytes > num_cols) {
        My(String).clear_at (menu->header, num_cols - 1);
      } else
      while ((int) menu->header->num_bytes < num_cols)
        My(String).append_byte (menu->header, ' ');

      string_append_fmt (render, "%s%s", menu->header->bytes, TERM_COLOR_RESET);
    }

    int ridx, iidx = 0; int start_row = 0;
    for (ridx = 0; ridx < rend_rows; ridx++) {
      start_row = first_row + ridx;
      string_append_fmt (render, TERM_GOTO_PTR_POS_FMT, start_row, first_col);

      for (iidx = 0; iidx < num and iidx + (ridx  * num) + (frow_idx * num) < menu->list->num_items; iidx++) {
        int len = ((int) it->data->num_bytes > maxlen ? maxlen : (int) it->data->num_bytes);
        char item[len+1]; memcpy(item, it->data->bytes, len); item[len] = '\0';
        int color = (iidx + (ridx * num) + (frow_idx * num) is cur_idx)
           ? COLOR_MENU_SEL : COLOR_MENU_BG;
        string_append_fmt (render, fmt, color, item, TERM_COLOR_RESET);
        it = it->next;
      }

      if (mod)
        for (int i = mod + 1; i < num; i++)
          for (int j = 0; j < maxlen; j++)
            string_append_byte (render, ' ');
   }

//    string_append (render, TERM_CURSOR_SHOW);

    fd_write (menu->fd, render->bytes, render->num_bytes);

    string_free (render);

    menu->c = rline_edit (rl)->c;
    if (menu->state & MENU_QUIT) goto theend;

handle_char:
    switch (menu->c) {
      case ESCAPE_KEY: goto theend;

      case '\r':
      case ' ':
        if (' ' is menu->c and menu->space_selects is 0)
          goto insert_char;

        it = menu->list->head;
        for (int i = 0; i < cur_idx; i++) it = it->next;
        match = it->data->bytes;
        goto theend;

      case ARROW_LEFT_KEY:
        ifnot (0 is cur_idx) {
          if (vcol_pos > 1) {
            cur_idx--;
            vcol_pos--;
            continue;
          }

          cur_idx += (num - vcol_pos);
          vcol_pos += (num - vcol_pos);
        }

        //__fallthrough__;

      case ARROW_UP_KEY:
        if (vrow_pos is first_row) {
          ifnot (frow_idx) {
            cur_idx = menu->list->num_items - 1;
            vcol_pos = 1 + (mod ? mod - 1 : 0);
            frow_idx = num_rows - rend_rows;
            vrow_pos = first_row + rend_rows - 1;
            continue;
          }

          frow_idx--;
        } else
          vrow_pos--;

        cur_idx -= num;
        continue;

      case '\t':
      case ARROW_RIGHT_KEY:
         if (vcol_pos isnot num and cur_idx < menu->list->num_items - 1) {
           cur_idx++;
           vcol_pos++;
           continue;
         }

        cur_idx -= (vcol_pos - 1);
        vcol_pos = 1;

        //__fallthrough__;

      case ARROW_DOWN_KEY:
        if (vrow_pos is menu->last_row or (vrow_pos - first_row + 1 is num_rows)) {
          if (cur_idx + (num - vcol_pos) + 1 >= menu->list->num_items) {
            frow_idx = 0;
            vrow_pos = first_row;
            cur_idx = 0;
            vcol_pos = 1;
            continue;
          }

          cur_idx += num;
          while (cur_idx >= menu->list->num_items) {cur_idx--; vcol_pos--;}
          frow_idx++;
          continue;
        }

        cur_idx += num;
        while (cur_idx >= menu->list->num_items) {cur_idx--; vcol_pos--;}
        vrow_pos++;
        continue;

      case PAGE_UP_KEY: {
          int i = rend_rows;
          while (i--) {
            if (frow_idx > 0) {
              frow_idx--;
              cur_idx -= num;
            } else break;
          }
        }

        continue;

      case PAGE_DOWN_KEY: {
          int i = rend_rows;
          while (i--) {
            if (frow_idx < num_rows - rend_rows) {
              frow_idx++;
              cur_idx += num;
            } else break;
          }
        }

        continue;

      default:
insert_char:
        {
          string_t *p = vstr_join (rl->line, "");
          if (rl->line->tail->data->bytes[0] is ' ')
            string_clear_at (p, p->num_bytes - 1);

          ifnot (str_eq (menu->pat, p->bytes)) {
            menu->patlen = p->num_bytes;
            strcpy (menu->pat, p->bytes);
            menu->pat[menu->patlen] = '\0';
          }

          string_free (p);
        }

        menu->process_list (menu);

        if (menu->state & MENU_QUIT) goto theend;

        if (menu->list->num_items is 1)
          if (menu->return_if_one_item) {
            menu->c = '\r';
            goto handle_char;
          }

        /* this it can be change in the callback, and is intented for backspace */
        menu->return_if_one_item = orig_beh;

        if (menu->state & MENU_REINIT_LIST) goto init_list;

        continue;
    }
  }

theend:
  menu_clear (menu);
  rline_free (rl);
  return match;
}

private void ved_menu_free (ed_t *ed, menu_t *this) {
  (void) ed;
  menu_free (this);
}

private menu_t *ved_menu_new (ed_t *this, buf_t *buf, MenuProcessList_cb cb) {
  menu_t *menu = menu_new (this, $my(video)->row_pos, $my(prompt_row) - 2,
    $my(video)->col_pos, cb, NULL, 0);
  menu->this = buf;
  menu->return_if_one_item = 1;
  return menu;
}

private string_t *buf_input_box (buf_t *this, int row, int col,
                            int abort_on_escape, char *buf) {
  string_t *str = NULL;
  rline_t *rl = rline_new ($my(root), $my(term_ptr), My(Input).get, row,
      col, $my(dim)->num_cols - col + 1, $my(video));
  rl->opts &= ~RL_OPT_HAS_HISTORY_COMPLETION;
  rl->opts &= ~RL_OPT_HAS_TAB_COMPLETION;
  rl->prompt_char = 0;

  ifnot (NULL is buf)
    BYTES_TO_RLINE (rl, buf, (int) bytelen (buf));

  utf8 c;
  for (;;) {
     c = rline_edit (rl)->c;
     switch (c) {
       case ESCAPE_KEY:
          if (abort_on_escape) {
            str = string_new (1);
            goto theend;
          }
       case '\r': str = rline_get_string (rl); goto theend;
     }
  }

theend:
  My(String).clear_at (str, -1);
  rline_free (rl);
  return str;
}

private dim_t *dim_set (dim_t *dim, int f_row, int l_row,
                                    int f_col, int l_col) {
  if (NULL is dim)
    dim = AllocType (dim);

  dim->first_row = f_row;
  dim->last_row = l_row;
  dim->num_rows = l_row - f_row + 1;
  dim->first_col = f_col;
  dim->last_col = l_col;
  dim->num_cols = l_col - f_col + 1;
  return dim;
}

private dim_t **ed_dims_init (ed_t *this, int num_frames) {
  (void) this;
  return Alloc (sizeof (dim_t *) * num_frames);
}

private dim_t *ed_dim_new (ed_t *this, int f_row, int l_row, int f_col, int l_col) {
  (void) this;
  dim_t *dim = AllocType (dim);
  return dim_set (dim, f_row, l_row, f_col, l_col);
}

private dim_t **ed_dim_calc (ed_t *this, int num_rows, int num_frames,
                                     int min_rows, int has_dividers) {
  int reserved = $my(has_topline) + $my(has_msgline) + $my(has_promptline);
  int dividers = has_dividers ? num_frames - 1 : 0;
  int rows = (num_frames * min_rows) + dividers + reserved;

  if (num_rows < rows) return NULL;

  rows = (num_rows - dividers - reserved) / num_frames;
  int mod = (num_rows - dividers - reserved) % num_frames;

  dim_t **dims = self(dims_init, num_frames);

  for (int i = 0; i < num_frames; i++) {
    dims[i] = AllocType (dim);
    dims[i]->num_rows = rows + (i is 0 ? mod : 0);
    dims[i]->first_row = i is 0
      ? 1 + $my(has_topline)
      : dims[i-1]->last_row + has_dividers + 1;
    dims[i]->last_row = dims[i]->first_row + dims[i]->num_rows - 1;
    dims[i]->first_col = $my(dim)->first_col;
    dims[i]->last_col  = $my(dim)->last_col;
    dims[i]->num_cols  = $my(dim)->num_cols;
  }

  return dims;
}

#define state_cp(v__, a__)                                 \
  (v__)->video_first_row = (a__)->video_first_row;         \
  (v__)->video_first_row_idx = (a__)->video_first_row_idx; \
  (v__)->row_pos = (a__)->row_pos;                         \
  (v__)->col_pos = (a__)->col_pos;                         \
  (v__)->cur_col_idx = (a__)->cur_col_idx;                 \
  (v__)->first_col_idx = (a__)->first_col_idx;             \
  (v__)->cur_idx = (a__)->cur_idx;                         \
  (v__)->idx = (a__)->idx

#define state_set(v__)                                   \
  (v__)->video_first_row = $my(video_first_row);         \
  (v__)->video_first_row_idx = $my(video_first_row_idx); \
  (v__)->row_pos = $my(cur_video_row);                   \
  (v__)->col_pos = $my(cur_video_col);                   \
  (v__)->cur_col_idx = $mycur(cur_col_idx);              \
  (v__)->first_col_idx = $mycur(first_col_idx);          \
  (v__)->cur_idx = this->cur_idx

#define state_restore(s__)                               \
  $mycur(first_col_idx) = (s__)->first_col_idx;          \
  $mycur(cur_col_idx) = (s__)->cur_col_idx;              \
  $my(video)->row_pos = (s__)->row_pos;                  \
  $my(video)->col_pos = (s__)->col_pos;                  \
  $my(video_first_row_idx) = (s__)->video_first_row_idx; \
  $my(video_first_row) = (s__)->video_first_row;         \
  $my(cur_video_row) = (s__)->row_pos;                   \
  $my(cur_video_col) = (s__)->col_pos

private int buf_mark_restore (buf_t *this, mark_t *mark) {
  if (mark->video_first_row is NULL) return NOTHING_TODO;
  if (mark->cur_idx is this->cur_idx) return NOTHING_TODO;
  if (mark->cur_idx >= this->num_items) return NOTHING_TODO;

  self(cur.set, mark->cur_idx);
  state_restore (mark);

  if ($mycur(first_col_idx) or $mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes) {
    $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;
    $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
  }

  return DONE;
}

private void buf_adjust_view (buf_t *this) {
  $my(video_first_row) = this->current;
  $my(video_first_row_idx) = this->cur_idx;
  int num = (ONE_PAGE / 2);

  while ($my(video_first_row_idx) and num) {
    $my(video_first_row_idx)--;
    num--;
    $my(video_first_row) = $my(video_first_row)->prev;
  }

  $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;

  $my(video)->row_pos = $my(cur_video_row) =
      $my(dim)->first_row + ((ONE_PAGE / 2) - num);
  $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
}

private void buf_adjust_marks (buf_t *this, int type, int fidx, int lidx) {
  for (int m = 0; m < NUM_MARKS; m++) {
    mark_t *mark = &$my(marks)[m];
    if (mark->video_first_row is NULL) continue;
    if (mark->cur_idx < fidx) continue;
    if (fidx <= mark->cur_idx and mark->cur_idx <= lidx) {
      mark->video_first_row = NULL;
      continue;
    }

    if (type is DELETE_LINE)
      if (fidx is 0 and mark->video_first_row_idx is 0)
        mark->video_first_row = this->head;

    int lcount = lidx - fidx + (type is DELETE_LINE);
    int idx = this->cur_idx;

    mark->video_first_row_idx = idx;
    mark->video_first_row = this->current;

    if (type is DELETE_LINE)
      mark->cur_idx -= lcount;
    else
      mark->cur_idx += lcount;

    while (idx++ < mark->cur_idx) {
      mark->video_first_row_idx++;
      mark->video_first_row = mark->video_first_row->next;
    }

    mark->row_pos = $my(dim)->first_row;

    idx = 5 < $my(dim)->num_rows ? 5 : $my(dim)->num_rows;
    while (mark->video_first_row_idx > 0 and idx--) {
      if (NULL is mark->video_first_row or NULL is mark->video_first_row->prev)
        break;
      mark->video_first_row = mark->video_first_row->prev;
      mark->video_first_row_idx--;
      mark->row_pos++;
    }
  }
}

private vchar_t *buf_get_line_nth (line_t *line, int idx) {
  line->current = line->head;
  int i = 0;
  while (line->current) {
    if (i is idx or (i + (line->current->len - 1) is idx))
      return line->current;

    i += line->current->len;
    line->current = line->current->next;
  }

  if (i is idx or (i + (line->tail->len - 1) is idx))
    return line->tail;
  return NULL;
}

private vchar_t *buf_get_line_idx (line_t *line, int idx) {
  int cidx = current_list_set (line, idx);
  if (cidx is INDEX_ERROR) return NULL;
  return line->current;
}

private char *buf_get_line_data (buf_t *this, line_t *line) {
  ifnot (line->num_items) return "";
  My(String).clear ($my(shared_str));
  vchar_t *it = line->head;
  while (it) {
    My(String).append ($my(shared_str), it->buf);
    it = it->next;
  }
  return $my(shared_str)->bytes;
}

private int ved_buf_adjust_col (buf_t *this, int nth, int isatend) {
  if (this->current is NULL) return 1;

  ustring_encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
      CLEAR, $my(ftype)->tabwidth, $mycur(cur_col_idx));

  int hasno_len = ($mycur(data)->num_bytes is 0 or NULL is $my(line));
  if (hasno_len) isatend = 0;
  if (0 is isatend and (hasno_len or nth <= 1 or
      (int) $mycur(data)->num_bytes is $my(line)->head->len)) {
    $mycur(cur_col_idx) = $mycur(first_col_idx) = 0;
    $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;

    if ($mycur(data)->num_bytes)
      $my(video)->col_pos = $my(cur_video_col) = $my(cur_video_col) +
          (0 is IS_MODE (INSERT_MODE) ? $my(line)->head->width - 1 : 0);

    return $my(video)->col_pos;
  }

  vchar_t *it;
  int clen = 0;
  /* some heuristics, this can never be perfect, unless a specific keybinding|set */
  if (isatend and
      ($my(prev_nth_ptr_pos) > $my(line)->num_items or
      ($my(line)->num_items < ($my(prev_nth_ptr_pos) + 20) and
       $my(prev_num_items) > 1))) {
    clen = $my(line)->tail->len;
    $mycur(cur_col_idx) = $mycur(data)->num_bytes - clen;
  } else {
    int idx = 0;
    int num = 1;
    it = $my(line)->head;
    while (it and (idx < (int) $mycur(data)->num_bytes and num < nth)) {
      clen = it->len;
      idx += clen;
      it = it->next;
      num++;
    }

    if (idx >= (int) $mycur(data)->num_bytes)
      $mycur(cur_col_idx) = $mycur(data)->num_bytes - clen;
    else
      $mycur(cur_col_idx) = idx - (num < nth ? clen : 0);
  }

  it = buf_get_line_nth ($my(line), $mycur(cur_col_idx));
  int cur_width = it->width;
  int col_pos = cur_width; /* cur_col_idx char */;
  int idx = $mycur(cur_col_idx);

  it = it->prev;
  while (it and idx and (col_pos < $my(dim)->num_cols)) {
    idx -= it->len;
    col_pos += it->width;
    it = it->prev;
  }

  $mycur(first_col_idx) = idx;
  $my(video)->col_pos = $my(cur_video_col) = col_pos -
       (IS_MODE (INSERT_MODE) ? (cur_width - 1) : 0);

  return $my(video)->col_pos;
}

private action_t *buf_action_new (buf_t *this) {
  (void) this;
  return AllocType (action);
}

private void buf_free_action (buf_t *this, action_t *action) {
  (void) this;
  act_t *act = stack_pop (action, act_t);
  while (act) {
    free (act->bytes);
    free (act);
    act = stack_pop (action, act_t);
  }

  free (action);
}

#define vundo_set(act, type__)                           \
  (act)->type = (type__);                                \
  state_set(act)

#define vundo_restore(act)                               \
  state_restore(act)

private void buf_vundo_init (buf_t *this) {
  if (NULL is $my(undo)) $my(undo) = AllocType (undo);
  if (NULL is $my(redo)) $my(redo) = AllocType (undo);
}

private action_t *vundo_pop (buf_t *this) {
  return current_list_pop ($my(undo), action_t);
}

private action_t *redo_pop (buf_t *this) {
  if ($my(redo)->head is NULL) return NULL;
  return current_list_pop ($my(redo), action_t);
}

private void redo_clear (buf_t *this) {
  if ($my(redo)->head is NULL) return;

  action_t *action = redo_pop (this);
  while (action) {
    buf_free_action (this, action);
    action = redo_pop (this);
  }

  $my(redo)->num_items = 0; $my(redo)->cur_idx = 0;
  $my(redo)->head = $my(redo)->tail = $my(redo)->current = NULL;
}

private void undo_clear (buf_t *this) {
  if ($my(undo)->head is NULL) return;
  action_t *action = vundo_pop (this);
  while (action isnot NULL) {
    buf_free_action (this, action);
    action = vundo_pop (this);
  }
  $my(undo)->num_items = 0; $my(undo)->cur_idx = 0;
  $my(undo)->head = $my(undo)->tail = $my(undo)->current = NULL;
}

private void vundo_clear (buf_t *this) {
  undo_clear (this);
  redo_clear (this);
}

private void vundo_push (buf_t *this, action_t *action) {
  if ($my(undo)->num_items > $myroots(max_num_undo_entries)) {
    action_t *tmp = list_pop_tail ($my(undo), action_t);
    buf_free_action  (this, tmp);
  }

  ifnot ($my(undo)->state & VUNDO_RESET)
    redo_clear (this);
  else
    $my(undo)->state &= ~VUNDO_RESET;

  current_list_prepend ($my(undo), action);
}

private void redo_push (buf_t *this, action_t *action) {
  if ($my(redo)->num_items > $myroots(max_num_undo_entries)) {
    action_t *tmp = list_pop_tail ($my(redo), action_t);
    buf_free_action  (this, tmp);
  }

  current_list_prepend ($my(redo), action);
}

private int vundo_insert (buf_t *this, act_t *act, action_t *redoact) {
  ifnot (this->num_items) return DONE;

  act_t *ract = AllocType (act);
  self(cur.set, act->idx);
  buf_adjust_view (this);
  ract->type = DELETE_LINE;
  vundo_set (ract, DELETE_LINE);
  ract->idx = this->cur_idx;
  ract->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (redoact, ract);

  self(cur.delete);

  if (this->num_items) {
    self(cur.set, act->cur_idx);
    buf_adjust_marks (this, DELETE_LINE, act->idx, act->idx);
    vundo_restore (act);
  }

  return DONE;
}

private int vundo_delete_line (buf_t *this, act_t *act, action_t *redoact) {
  act_t *ract = AllocType (act);
  row_t *row = self(row.new_with, act->bytes);
  if (this->num_items) {
    if (act->idx >= this->num_items) {
      self(cur.set, this->num_items - 1);
      buf_adjust_view (this);
      vundo_set (ract, INSERT_LINE);
      self(cur.append, row);
      ract->idx = this->cur_idx;
    } else {
      self(cur.set, act->idx);
      buf_adjust_view (this);
      vundo_set (ract, INSERT_LINE);
      self(cur.prepend, row);
      ract->idx = this->cur_idx;
    }
    stack_push (redoact, ract);
    buf_adjust_marks (this, INSERT_LINE, act->idx, act->idx + 1);
    vundo_restore (act);
  } else {
    this->head = row;
    this->tail = row;
    this->cur_idx = 0;
    this->current = this->head;
    this->num_items = 1;
    buf_adjust_view (this);
    vundo_set (ract, INSERT_LINE);
    stack_push (redoact, ract);
    //  $my(video_first_row_idx) = this->cur_idx;
    //  self(cur.append, row);
  }

  if ($my(video_first_row_idx) is this->cur_idx)
    $my(video_first_row) = this->current;

  return DONE;
}

private int vundo_replace_line (buf_t *this, act_t *act, action_t *redoact) {
  self(cur.set, act->idx);

  act_t *ract = AllocType (act);
  self(set.row.idx, act->idx, act->row_pos - $my(dim)->first_row, act->col_pos);
  vundo_set (ract, REPLACE_LINE);
  ract->idx = this->cur_idx;
  ract->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (redoact, ract);

  My(String).replace_with ($mycur(data), act->bytes);

  vundo_restore (act);
  return DONE;
}

private int vundo (buf_t *this, utf8 com) {
  action_t *action = NULL;
  if (com is 'u')
    action = vundo_pop (this);
  else
    action = redo_pop (this);

  if (NULL is action) return NOTHING_TODO;

  act_t *act = stack_pop (action, act_t);

  action_t *redoact = AllocType (action);

  while (act) {
    if (act->type is DELETE_LINE)
      vundo_delete_line (this, act, redoact);
    else
      if (act->type is REPLACE_LINE)
        vundo_replace_line (this, act, redoact);
      else
        vundo_insert (this, act, redoact);

    free (act->bytes);
    free (act);
    act = stack_pop (action, act_t);
  }

  if (com is 'u')
    redo_push (this, redoact);
  else {
    $my(undo)->state |= VUNDO_RESET;
    vundo_push (this, redoact);
  }

  free (action);
  self(draw);
  return DONE;
}

private void buf_action_set_current (buf_t *this, action_t *action, int type) {
  act_t *act = AllocType (act);
  vundo_set (act, type);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (action, act);
}

private void buf_action_set_with (buf_t *this, action_t *action,
                     int type, int idx, char *bytes, size_t len) {
  act_t *act = AllocType (act);
  vundo_set (act, type);
  act->idx = ((idx < 0 or idx >= this->num_items) ? this->cur_idx : idx);
  if (NULL is bytes)
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  else
    act->bytes = str_dup (bytes, len);
  stack_push (action, act);
}

private void buf_action_push (buf_t *this, action_t *action) {
  vundo_push (this, action);
}

/* a highlight theme derived from tte editor, fork of kilo editor,
 * adjusted and enhanced for the environment
 * written by itself (the very first lines) (iirc somehow after the middle days
 * of February) 
 */

private char *buf_syn_parser (buf_t *, char *, int, int, row_t *);
private ftype_t *buf_syn_init (buf_t *);
private ftype_t *buf_syn_init_c (buf_t *);

char *default_extensions[] = {".txt", NULL};

char *c_extensions[] = {".c", ".h", ".cpp", ".hpp", ".cc", NULL};
char *c_keywords[] = {
    "is I", "isnot I", "or I", "and I", "if I", "for I", "return I", "else I",
    "ifnot I", "private I", "self I", "this V", "NULL K", "OK K", "NOTOK K",
    "switch I", "while I", "break I", "continue I", "do I", "default I", "goto I",
    "case I", "free F", "$my V", "My V", "$mycur V", "uint T", "int T", "size_t T",
    "utf8 T", "char T", "uchar T", "sizeof T", "void T", "$from V", "thisp V",
    "$myprop V", "theend I", "theerror E", "UNSET N", "ZERO N", "#define M",
    "#endif M", "#error M", "#ifdef M", "#ifndef M", "#undef M", "#if M", "#else I",
    "#include M", "struct T", "union T", "typedef I", "Alloc T", "Realloc T", "AllocType T",
    "$myroots V", "$myparents V", "public I", "mutable I", "loop I", "forever I",
    "static I", "enum T", "bool T","long T", "double T", "float T", "unsigned T",
    "extern I", "signed T", "volatile T", "register T", "union T", "const T", "auto T",
    NULL
};

char c_singleline_comment[] = "//";
char c_multiline_comment_start[] = "/*";
char c_multiline_comment_end[] = "*/";
char c_multiline_comment_continuation[] = " * ";
char c_operators[] = "+:?-%*^><=|&~.()[]{}!";
char *NULL_ARRAY[] = {NULL};

syn_t HL_DB[] = {
   {
     "txt", NULL_ARRAY, default_extensions, NULL_ARRAY,
     NULL, NULL,
     NULL, NULL, NULL, NULL,
     HL_STRINGS_NO, HL_NUMBERS_NO, buf_syn_parser, buf_syn_init, 0, NULL, NULL,
  },
  {
    "c", NULL_ARRAY, c_extensions, NULL_ARRAY,
    c_keywords, c_operators,
    c_singleline_comment, c_multiline_comment_start, c_multiline_comment_end,
    c_multiline_comment_continuation,
    HL_STRINGS, HL_NUMBERS,
    buf_syn_parser, buf_syn_init_c, 0, NULL, NULL,
  }
};

#define IsSeparator(c)                          \
  ((c) is ' ' or (c) is '\t' or (c) is '\0' or  \
   byte_in_str (",.()+-/=*~%<>[]:;}", (c)) isnot NULL)

#define IGNORE(c) ((c) > '~' || (c) <= ' ')

#define ADD_COLORED_CHAR(_c, _clr) My(String).append_fmt ($my(shared_str), \
  TERM_SET_COLOR_FMT "%c" TERM_COLOR_RESET,(_clr), (_c))

#define ADD_INVERTED_CHAR(_c, _clr) My(String).append_fmt ($my(shared_str), \
  TERM_INVERTED "%s%c" TERM_COLOR_RESET, TERM_MAKE_COLOR ((_clr)), (_c))

#define SYN_HAS_OPEN_COMMENT       (1 << 0)
#define SYN_HAS_SINGLELINE_COMMENT (1 << 1)
#define SYN_HAS_MULTILINE_COMMENT  (1 << 2)

private int buf_syn_has_mlcmnt (buf_t *this, row_t *row) {
  int found = 0;
  row_t *it = row->prev;

  for (int i = 0; i < MAX_BACKTRACK_LINES_FOR_ML_COMMENTS and it; i++) {
    if (NULL isnot strstr (it->data->bytes, $my(syn)->multiline_comment_end))
      break;

    char *sp = strstr (it->data->bytes, $my(syn)->multiline_comment_start);

    if (NULL isnot sp)
      if (sp is it->data->bytes or
          it->data->bytes[sp - it->data->bytes - 1] is ' ' or
          it->data->bytes[sp - it->data->bytes - 1] is '\t') {
        found = 1;
        break;
      }

    ifnot (NULL is $my(syn)->multiline_comment_continuation)
      if (str_eq_n (it->data->bytes, $my(syn)->multiline_comment_continuation, bytelen ($my(syn)->multiline_comment_continuation))) {
        found = 1;
        break;
      }

    it = it->prev;
  }

  return found;
}

/* Sorry but the highlight system is ridicolous simple (line by line), but is fast and works for me in C */
private char *buf_syn_parser (buf_t *this, char *line, int len, int index, row_t *row) {
  (void) index;

  ifnot (len) return line;

  /* make sure to reset, in the case the last character on previus line
   * is in e.g., a string. Do it here instead with an "if" later on */
  My(String).replace_with_len ($my(shared_str), TERM_COLOR_RESET, TERM_COLOR_RESET_LEN);

  char *m_cmnt_p = NULL;
  int m_cmnt_idx = -1;
  int has_mlcmnt  = 0;

  ifnot (NULL is $my(syn)->multiline_comment_start) {
    m_cmnt_p = strstr (line, $my(syn)->multiline_comment_start);
    m_cmnt_idx = (NULL is m_cmnt_p ? -1 : m_cmnt_p - line);

    if (m_cmnt_idx > 0)
      if (line[m_cmnt_idx-1] isnot ' ' and line[m_cmnt_idx-1] isnot '\t') {
        m_cmnt_idx = -1;
        m_cmnt_p = NULL;
      }

    if (m_cmnt_idx is -1) has_mlcmnt = buf_syn_has_mlcmnt (this, row);
  }

  char *s_cmnt_p = NULL;
  int s_cmnt_idx = -1;

  if (m_cmnt_idx is -1)
    ifnot (NULL is $my(syn)->singleline_comment) {
      s_cmnt_p = strstr (line, $my(syn)->singleline_comment);
      s_cmnt_idx = (NULL is s_cmnt_p ? -1 : s_cmnt_p - line);
    }

  uchar c;
  int idx = 0;

  for (idx = 0; idx < len; idx++) {
    c = line[idx];
    goto parse_char;

parse_comment:
    if ($my(syn)->state & SYN_HAS_OPEN_COMMENT) {
      $my(syn)->state &= ~SYN_HAS_OPEN_COMMENT;
      int diff = len;
      My(String).append_fmt ($my(shared_str), "%s%s", TERM_MAKE_COLOR (HL_COMMENT), TERM_ITALIC);

      if ($my(syn)->state & SYN_HAS_MULTILINE_COMMENT) {
        $my(syn)->state &= ~SYN_HAS_MULTILINE_COMMENT;
        char *sp = strstr (line + idx, $my(syn)->multiline_comment_end);
        if (sp is NULL) {
          while (idx < len)
            My(String).append_byte ($my(shared_str), line[idx++]);

          My(String).append ($my(shared_str), TERM_COLOR_RESET);
          goto theend;
        }

        diff = idx + (sp - (line + idx)) + (int) bytelen ($my(syn)->multiline_comment_end);

      } else
        $my(syn)->state &= ~SYN_HAS_SINGLELINE_COMMENT;

      while (idx < diff) My(String).append_byte ($my(shared_str), line[idx++]);
      My(String).append ($my(shared_str), TERM_COLOR_RESET);
      if (idx is len) goto theend;
      c = line[idx];
    }

parse_char:
    while (IGNORE (c)) {
      if (c is ' ') {
        if (idx + 1 is (int) row->data->num_bytes) {
          ADD_INVERTED_CHAR (' ', HL_TRAILING_WS);
        } else
          My(String).append_byte ($my(shared_str), c);
      } else
        if (c is '\t') {
          for (int i = 0; i < $my(ftype)->tabwidth; i++)
            My(String).append_byte ($my(shared_str), ' ');
        } else
          if ((c < ' ' and (c isnot 0 and c isnot 0x0a)) or c is 0x7f) {
            ADD_INVERTED_CHAR ('?', HL_ERROR);
          }
          else {
            My(String).append_byte ($my(shared_str),  c);
          }

      if (++idx is len) goto theend;
      c = line[idx];
    }

    if (has_mlcmnt or idx is m_cmnt_idx) {
      has_mlcmnt = 0;
      $my(syn)->state |= (SYN_HAS_OPEN_COMMENT|SYN_HAS_MULTILINE_COMMENT);
      goto parse_comment;
    }

    if (idx is s_cmnt_idx)
      if (s_cmnt_idx is 0 or line[s_cmnt_idx-1] is ' ' or line[s_cmnt_idx-1] is '\t') {
        $my(syn)->state |= (SYN_HAS_OPEN_COMMENT|SYN_HAS_SINGLELINE_COMMENT);
        goto parse_comment;
      }

    ifnot (NULL is $my(syn->operators)) {
      int lidx = idx;
      while (NULL isnot byte_in_str ($my(syn)->operators, c)) {
        ADD_COLORED_CHAR (c, HL_OPERATOR);
        if (++idx is len) goto theend;
        c = line[idx];
      }

      if (idx isnot lidx) goto parse_char;
    }

    if ($my(syn)->hl_strings)
      if (c is '"' or c is '\'') {
        My(String).append_fmt ($my(shared_str),
          TERM_SET_COLOR_FMT "%c" TERM_SET_COLOR_FMT "%s",
            HL_STRING_DELIM, c, HL_STRING, TERM_ITALIC);
        char openc = c;
        while (++idx < len) {
          char prevc = c;
          c = line[idx];
          if (c is openc and (prevc isnot '\\' or (prevc is '\\' and line[idx-2] is '\\'))) {
            ADD_COLORED_CHAR (c, HL_STRING_DELIM);
            goto next_char;
          }

          My(String).append_byte ($my(shared_str),  c);
        }

        goto theend;
      }

    if ($my(syn)->hl_numbers)
      if (IS_DIGIT (c)) {
        ADD_COLORED_CHAR (c, HL_NUMBER);
        while (++idx < len) {
          c = line[idx];
          if (0 is IS_DIGIT (c) and 0 is IsAlsoANumber (c))
            goto parse_char;

          ADD_COLORED_CHAR (c, HL_NUMBER);
        }

        goto theend;
      }

    if (NULL isnot $my(syn)->keywords and (idx is 0 or IsSeparator (line[idx-1])))
      for (int j = 0; $my(syn)->keywords[j] isnot NULL; j++) {
        int kw_len = $my(syn)->keywords_len[j];
        // int is_glob = $my(syn)->keywords[j][kw_len] is '*';if (is_glob) kw_len--;

        if (str_eq_n (&line[idx], $my(syn)->keywords[j], kw_len) and
           IsSeparator (line[idx + kw_len])) {
         // (0 is is_glob ? IsSeparator (line[idx + kw_len]) : 1)) {
          int color = $my(syn)->keywords_colors[j];
          My(String).append ($my(shared_str), TERM_MAKE_COLOR (color));

          for (int i = 0; i < kw_len; i++)
            My(String).append_byte ($my(shared_str), line[idx+i]);

          idx += (kw_len - 1);

          // ifnot (is_glob) {
          My(String).append ($my(shared_str), TERM_COLOR_RESET);
          goto next_char;
          // }
          /*
          idx++;
          while (idx < len) My(String).append_byte ($my(shared_str), line[idx++]);
          My(String).append ($my(shared_str), TERM_COLOR_RESET);
          goto theend;
          */
        }
      }

    My(String).append_byte ($my(shared_str),  c);

next_char:;
  }

theend:
  strncpy (line, $my(shared_str)->bytes, $my(shared_str)->num_bytes);
  line[$my(shared_str)->num_bytes] = '\0';
  return line;
}

private string_t *buf_ftype_autoindent (buf_t *this, row_t *row) {
  (void) row;
  $my(shared_int) = 0; // needed by the caller
  char s[$my(shared_int) + 1];
  for (int i = 0; i < $my(shared_int); i++) s[i] = ' ';
  s[$my(shared_int)] = '\0';
  My(String).replace_with ($my(shared_str), s);
  return $my(shared_str);
}

private string_t *buf_autoindent_c (buf_t *this, row_t *row) {
  do {
    if (row->data->num_bytes < 2 or row->data->bytes[0] is '}') {
      $my(shared_int) = 0;
      break;
    }

    int ws = 0;
    char *line = row->data->bytes;
    while (*line and *line++ is ' ') ws++;
    if (ws is (int) row->data->num_bytes) {
      $my(shared_int) = 0;
      break;
    }

    int len = row->data->num_bytes - ws - 2;
    if (len > 0) while (len--) line++;
    if (*line is '\n') line--;
    if (*line is ';' or *line is ',')
      $my(shared_int) = ws;
    else
      $my(shared_int) = ws + $my(ftype)->shiftwidth;
  } while (0);

  char s[$my(shared_int) + 1];
  for (int i = 0; i < $my(shared_int) ; i++) s[i] = ' ';
  s[$my(shared_int)] = '\0';
  My(String).replace_with ($my(shared_str), s);
  return $my(shared_str);
}

private ftype_t *buf_ftype_init (buf_t *this, int ftype, Indent_cb indent_cb) {
  $my(ftype) = AllocType (ftype);

  if (ftype >= $myroots(num_syntaxes) or ftype < 0) ftype = 0;

  $my(syn) = &$myroots(syntaxes)[ftype];
  strcpy ($my(ftype)->name, $my(syn)->filetype);
  $my(ftype)->autochdir = 1;
  $my(ftype)->shiftwidth = 0;
  $my(ftype)->tabwidth = TABWIDTH;

  $my(ftype)->clear_blanklines = CLEAR_BLANKLINES;
  $my(ftype)->tab_indents = TAB_ON_INSERT_MODE_INDENTS;
  $my(ftype)->cr_on_normal_is_like_insert_mode = CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE;
  $my(ftype)->backspace_on_normal_is_like_insert_mode = BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE;
  $my(ftype)->backspace_on_first_idx_remove_trailing_spaces = BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES;
  $my(ftype)->small_e_on_normal_goes_insert_mode = SMALL_E_ON_NORMAL_GOES_INSERT_MODE;
  $my(ftype)->space_on_normal_is_like_insert_mode = SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE;
  $my(ftype)->read_from_shell = READ_FROM_SHELL;

  $my(ftype)->autoindent = (NULL is indent_cb ? buf_ftype_autoindent : indent_cb);
  strcpy ($my(ftype)->on_emptyline, "~");
  return $my(ftype);
}

private char *ftype_on_open_fname_under_cursor_c (char *fname,
                                size_t len, size_t stack_size) {
  if (len < 8 or (*fname isnot '<' and fname[len-1] isnot '>'))
    return fname;

  char incl_dir[] = {"/usr/include"};
  size_t tlen = len + bytelen (incl_dir) + 1 - 2; // + / - <>

  if (tlen + 1 > stack_size) return fname;

  char t[tlen + 1];
  snprintf (t, tlen + 1, "%s/%s", incl_dir, fname + 1);
  for (size_t i = 0; i < tlen; i++) fname[i] = t[i];
  fname[tlen] = '\0';

  return fname;
}

private int ed_syn_get_ftype_idx (ed_t *this, char *name) {
  for (int i = 0; i < $my(num_syntaxes); i++) {
    if (str_eq ($my(syntaxes)[i].filetype, name))
      return i;
  }

  return 0;
}

private ftype_t *buf_syn_init_c (buf_t *this) {
  int idx = ed_syn_get_ftype_idx ($my(root), "c");
  ftype_t *ft = buf_ftype_init (this, idx, buf_autoindent_c);
  ft->shiftwidth = 2;
  ft->tab_indents = C_TAB_ON_INSERT_MODE_INDENTS;
  ft->on_open_fname_under_cursor = ftype_on_open_fname_under_cursor_c;
  return ft;
}

private ftype_t *buf_syn_init (buf_t *this) {
  return buf_ftype_init (this, FTYPE_DEFAULT, buf_ftype_autoindent);
}

private void buf_set_ftype (buf_t *this, int ftype) {
  if (FTYPE_DEFAULT < ftype and ftype < $myroots(num_syntaxes)) {
    $my(ftype) = $myroots(syntaxes)[ftype].init (this);
    return;
  }

  for (int i = 0; i < $myroots(num_syntaxes); i++) {
    int j = 0;
    while ($myroots(syntaxes)[i].filenames[j])
      if (str_eq ($myroots(syntaxes)[i].filenames[j++], $my(basename))) {
        $my(ftype) = $myroots(syntaxes)[i].init (this);
        return;
      }

    if (NULL is $my(extname)) continue;

    j = 0;
    while ($myroots(syntaxes)[i].extensions[j])
      if (str_eq ($myroots(syntaxes)[i].extensions[j++], $my(extname))) {
        $my(ftype) = $myroots(syntaxes)[i].init (this);
        return;
      }

    if (NULL is this->head or this->head->data->num_bytes < 2) continue;

    j = 0;
    while ($myroots(syntaxes)[i].shebangs[j]) {
      if (str_eq_n ($myroots(syntaxes)[i].shebangs[j], this->head->data->bytes,
          bytelen ($myroots(syntaxes)[i].shebangs[j]))) {
        $my(ftype) = $myroots(syntaxes)[i].init (this);
        return;
      }

      j++; /* gcc complains (and probably for a right) if j++ at the end of the
            * conditional expression (even if it is right) */
     }
  }

  $my(ftype) = buf_syn_init (this);
}

private void buf_set_modified (buf_t *this) {
  $my(flags) |= BUF_IS_MODIFIED;
}

private row_t *buf_row_new_with (buf_t *this, const char *bytes) {
  row_t *row = AllocType (row);
  string_t *data = My(String).new_with (bytes);
  row->data = data;
  return row;
}

private row_t *buf_row_new_with_len (buf_t *this, const char *bytes, size_t len) {
  row_t *row = AllocType (row);
  string_t *data = My(String).new_with_len (bytes, len);
  row->data = data;
  return row;
}

private int buf_get_row_col_idx (buf_t *this, row_t *row) {
  (void) this;
  return row->cur_col_idx;
}

private row_t *buf_get_row_at (buf_t *this, int idx) {
  return list_get_at (this, row_t, idx);
}

private row_t *buf_get_row_current (buf_t *this) {
  return this->current;
}

private int buf_get_row_current_col_idx (buf_t *this) {
  return $mycur(cur_col_idx);
}

private string_t *buf_get_row_current_bytes (buf_t *this) {
  return $mycur(data);
}

private void buf_free_row (buf_t *this, row_t *row) {
  if (row is NULL) return;
  My(String).free (row->data);
  free (row);
}

private void buf_free_line (buf_t *this) {
  if (this is NULL or $myprop is NULL or $my(line) is NULL) return;
  ustring_free_members ($my(line));
  free ($my(line));
}

private void buf_free_ftype (buf_t *this) {
  if (this is NULL or $myprop is NULL or $my(ftype) is NULL) return;
  free ($my(ftype));
}

private void buf_free_jumps (buf_t *this) {
  jump_t *jump = $my(jumps)->head;
  while (jump) {
    jump_t *tmp = jump->next;
    free (jump->mark);
    free (jump);
    jump = tmp;
  }

  free ($my(jumps));
}

private void buf_jumps_init (buf_t *this) {
  if (NULL is $my(jumps)) {
    $my(jumps) = AllocType (jumps);
    $my(jumps)->old_idx = -1;
  }
}

private void buf_jump_push (buf_t *this, mark_t *mark) {
  jump_t *jump = AllocType (jump);
  mark_t *lmark = AllocType (mark);
  state_cp (lmark, mark);
  jump->mark = lmark;

  if ($my(jumps)->num_items >= 20) {
    jump_t *tmp = stack_pop_tail ($my(jumps), jump_t);
    free (tmp->mark);
    free (tmp);
    $my(jumps)->num_items--;
  }

  stack_push ($my(jumps), jump);
  $my(jumps)->num_items++;
  $my(jumps)->cur_idx = 0;
}

private int mark_get_idx (int c) {
  char marks[] = MARKS; /* this is for tcc */
  char *m = byte_in_str (marks, c);
  if (NULL is m) return -1;
  return m - marks;
}

private int mark_set (buf_t *this, int mark) {
  if (mark < 0) {
    mark = mark_get_idx (My(Input).get ($my(term_ptr)));
    if (-1 is mark) return NOTHING_TODO;
  }

  state_set (&$my(marks)[mark]);
  $my(marks)[mark].cur_idx = this->cur_idx;

  if (mark isnot MARK_UNAMED)
    MSG("set [%c] mark", MARKS[mark]);

  buf_jump_push (this, &$my(marks)[mark]);
  return DONE;
}

private int mark_goto (buf_t *this) {
  int c = mark_get_idx (My(Input).get ($my(term_ptr)));
  if (-1 is c) return NOTHING_TODO;

  mark_t *mark = &$my(marks)[c];
  mark_t t;  state_set (&t);  t.cur_idx = this->cur_idx;

  if (NOTHING_TODO is buf_mark_restore (this, mark))
    return NOTHING_TODO;

  $my(marks)[MARK_UNAMED] = t;

  self(draw);
  return DONE;
}

private int buf_jump (buf_t *this, int dir) {
  ifnot ($my(jumps)->num_items) return NOTHING_TODO;

  jump_t *jump = $my(jumps)->head;
  if (dir is LEFT_DIRECTION) {
    for (int i = 0; i + 1 < $my(jumps)->cur_idx; i++)
      jump = jump->next;
    $my(jumps)->old_idx = $my(jumps)->cur_idx;
    if ($my(jumps)->cur_idx) $my(jumps)->cur_idx--;
    goto theend;
  }

  if ($my(jumps)->cur_idx + 1 is $my(jumps)->num_items and
      $my(jumps)->old_idx + 1 is $my(jumps)->num_items)
    return NOTHING_TODO;

  for (int i = 0; i < $my(jumps)->cur_idx; i++)
    jump = jump->next;

  if ($my(jumps)->cur_idx is 0) { // and $my(jumps)->num_items is 1) {
    mark_t m; state_set (&m); m.cur_idx = this->cur_idx;
    buf_jump_push (this, &m);
  }

  $my(jumps)->old_idx = $my(jumps)->cur_idx;
  if ($my(jumps)->cur_idx + 1 isnot $my(jumps)->num_items)
    $my(jumps)->cur_idx++;

theend:
  state_set (&$my(marks)[MARK_UNAMED]);
  $my(marks)[MARK_UNAMED].cur_idx = this->cur_idx;

  buf_mark_restore (this, jump->mark);
  self(draw);
  return DONE;
}

private void buf_iter_free (buf_t *unused, bufiter_t *this) {
  (void) unused;
  if (NULL is this) return;
  free (this);
}

private bufiter_t *buf_iter_new (buf_t *this, int start_idx) {
  bufiter_t *it = AllocType (bufiter);
  if (start_idx < 0) start_idx = this->cur_idx;

  it->row = self(get.row.at, start_idx);
  it->num_lines = this->num_items - start_idx;
  if (it->row) {
    it->line = it->row->data;
    it->idx = start_idx;
    it->col_idx = it->row->cur_col_idx;
  } else {
    it->line = NULL;
    it->idx = -1;
  }
  return it;
}

private bufiter_t *buf_iter_next (buf_t *unused, bufiter_t *this) {
  (void) unused;
  if (this->row) this->row = this->row->next;
  if (this->row) {
    this->line = this->row->data;
    this->idx++;
    this->num_lines--;
    this->col_idx = this->row->cur_col_idx;
  } else this->line = NULL;
  return this;
}

private void buf_free_undo (buf_t *this) {
  vundo_clear (this);
  free ($my(undo));
  free ($my(redo));
  return;
  action_t *action = vundo_pop (this);
  while (action isnot NULL) {
    buf_free_action (this, action);
    action = vundo_pop (this);
  }

  free ($my(undo));

  action = $my(redo)->head;
  while (action isnot NULL) {
    action_t *tmp = action->next;
    buf_free_action (this, action);
    action = tmp;
  }

  free ($my(redo));
}

/* from slang sources slsh/slsh.c
 * Copyright (C) 2005-2017,2018 John E. Davis
 * 
 * This file is part of the S-Lang Library.
 * 
 * The S-Lang Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

private char *vsys_stat_mode_to_string (char *mode_string, mode_t mode) {
  if (S_ISREG(mode)) mode_string[0] = '-';
  else if (S_ISDIR(mode)) mode_string[0] = 'd';
  else if (S_ISLNK(mode)) mode_string[0] = 'l';
  else if (S_ISCHR(mode)) mode_string[0] = 'c';
  else if (S_ISFIFO(mode)) mode_string[0] = 'p';
  else if (S_ISSOCK(mode)) mode_string[0] = 's';
  else if (S_ISBLK(mode)) mode_string[0] = 'b';

  if (mode & S_IRUSR) mode_string[1] = 'r'; else mode_string[1] = '-';
  if (mode & S_IWUSR) mode_string[2] = 'w'; else mode_string[2] = '-';
  if (mode & S_IXUSR) mode_string[3] = 'x'; else mode_string[3] = '-';
  if (mode & S_ISUID) mode_string[3] = 's';

  if (mode & S_IRGRP) mode_string[4] = 'r'; else mode_string[4] = '-';
  if (mode & S_IWGRP) mode_string[5] = 'w'; else mode_string[5] = '-';
  if (mode & S_IXGRP) mode_string[6] = 'x'; else mode_string[6] = '-';
  if (mode & S_ISGID) mode_string[6] = 'g';

  if (mode & S_IROTH) mode_string[7] = 'r'; else mode_string[7] = '-';
  if (mode & S_IWOTH) mode_string[8] = 'w'; else mode_string[8] = '-';
  if (mode & S_IXOTH) mode_string[9] = 'x'; else mode_string[9] = '-';
  if (mode & S_ISVTX) mode_string[9] = 't';

  mode_string[10] = '\0';
  return mode_string;
}

public vsys_T __init_vsys__ (void) {
  return ClassInit (vsys,
    .self = SelfInit (vsys,
      .which = vsys_which,
      .stat = SubSelfInit (vsys, stat,
        .mode_to_string = vsys_stat_mode_to_string
      )
    )
  );
}

public void __deinit_vsys__ (vsys_T *this) {
  (void) this;
}

private venv_t *env_new () {
  venv_t *env = AllocType (venv);
  env->pid = getpid ();
  env->uid = getuid ();
  env->gid = getgid ();

  char *hdir = getenv ("HOME");
  if (NULL is hdir)
    env->home_dir = string_new (1);
  else {
    env->home_dir = string_new_with (hdir);
    if (hdir[env->home_dir->num_bytes - 1] is DIR_SEP)
      string_clear_at (env->home_dir, env->home_dir->num_bytes - 1);
  }

  char *term_name = getenv ("TERM");
  if (NULL is term_name) {
    fprintf (stderr, "TERM environment variable isn't set\n");
    env->term_name = string_new (1);
  } else
    env->term_name = string_new_with (term_name);

#ifndef TMPDIR
  char tmpt[PATH_MAX];
  char *dtmpt = path_dirname (realpath (__FILE__, tmpt));
  env->data_dir = string_new_with_fmt ("%s/%s", dtmpt, THIS_SYS_TMP_DIR);
  free (dtmpt);
#else
  char tmpdir[] = TMPDIR;
#endif

  int fexists = file_exists (tmpdir);
  ifnot (fexists) {
    if (-1 is mkdir (tmpdir, S_IRWXU)) {
      fprintf (stderr, "Fatal Error: Cannot create %s directory\n", tmpdir);
      exit (errno);
    }
  }

  if (-1 is access (tmpdir, R_OK)) {
    fprintf (stderr, "Fatal Error: %s, temp directory, Is Not Readable\n", tmpdir);
    exit (errno);
  }

  if (-1 is access (tmpdir, W_OK)) {
    fprintf (stderr, "Fatal Error: %s, temp directory, Is Not Writable\n", tmpdir);
    exit (errno);
  }

  if (-1 is access (tmpdir, W_OK)) {
    fprintf (stderr, "Fatal Error: %s, temp directory, Has Not Execution Bits\n", tmpdir);
    exit (errno);
  }

  struct stat st;
  if (-1 is stat (tmpdir, &st)) {
    fprintf (stderr, "Fatal Error: %s, temp directory, Can not stat()\n", tmpdir);
    exit (errno);
  }

  ifnot (S_ISDIR (st.st_mode)) {
    fprintf (stderr, "Fatal Error: %s, temp directory, Is Not A Directory\n", tmpdir);
    exit (errno);
  }

  char mode_string[12];
  vsys_stat_mode_to_string (mode_string, st.st_mode);
  ifnot (str_eq (mode_string, "drwx------")) {
    fprintf (stderr, "Warning: tmp directory |%s| permissions is not 0700 or drwx------\n",
       tmpdir);
   }

  env->tmp_dir = string_new_with (tmpdir);

#ifndef VED_DATA_DIR
  char tmpd[PATH_MAX];
  char *dtmpd = path_dirname (realpath (__FILE__, tmpd));
  env->data_dir = string_new_with_fmt ("%s/%s", dtmpd, THIS_SYS_DATA_DIR);
  free (dtmpd);
#else
  env->data_dir = string_new_with (VED_DATA_DIR);
#endif

  char *path = getenv ("PATH");
  env->diff_exec = vsys_which ("diff", path);
  env->xclip_exec = vsys_which ("xclip", path);
  env->path = (path is NULL) ? NULL : string_new_with (path);
  return env;
}

private void env_free (venv_t **env) {
  if (NULL is env) return;
  string_free ((*env)->home_dir);
  string_free ((*env)->tmp_dir);
  string_free ((*env)->data_dir);
  ifnot (NULL is (*env)->diff_exec) string_free ((*env)->diff_exec);
  ifnot (NULL is (*env)->xclip_exec) string_free ((*env)->xclip_exec);
  ifnot (NULL is (*env)->path) string_free ((*env)->path);
  ifnot (NULL is (*env)->term_name) string_free ((*env)->term_name);
  free (*env); *env = NULL;
}

private string_t *venv_get (ed_t *this, char *name) {
  if (str_eq (name, "term_name")) return $my(env)->term_name;
  if (str_eq (name, "path")) return $my(env)->path;
  if (str_eq (name, "home_dir")) return $my(env)->home_dir;
  if (str_eq (name, "tmp_dir")) return $my(env)->tmp_dir;
  if (str_eq (name, "data_dir")) return $my(env)->data_dir;
  if (str_eq (name, "diff_exec")) return $my(env)->diff_exec;
  if (str_eq (name, "xclip_exec")) return $my(env)->xclip_exec;
  return NULL;
}

private string_t *venv_set (ed_t *this, char *name, char *val) {
  if (str_eq (name, "term_name"))
    return My(String).replace_with ($my(env)->term_name, val);
  if (str_eq (name, "path"))
    return My(String).replace_with ($my(env)->path, val);
  if (str_eq (name, "home_dir"))
    return My(String).replace_with ($my(env)->home_dir, val);
  if (str_eq (name, "tmp_dir"))
    return My(String).replace_with ($my(env)->tmp_dir, val);
  return NULL;
}

public venv_T __init_venv__ (void) {
  return ClassInit (venv,
    .self = SelfInit (venv,
      .get = venv_get
    )
  );
}

public void __deinit_venv__ (venv_T *this) {
  (void) this;
}

private void history_free (hist_t **hist) {
  if (NULL is hist) return;
  h_search_t *hs = (*hist)->search;
  if (NULL isnot hs) {
    histitem_t *hitem = hs->head;

    while (hitem isnot NULL) {
      histitem_t *tmp = hitem->next;
      string_free (hitem->data);
      free (hitem);
      hitem = tmp;
    }
  }

  h_rline_t *hrl = (*hist)->rline;
  h_rlineitem_t *it = hrl->head;
  while (it isnot NULL) {
    h_rlineitem_t *tmp = it->next;
    rline_free (it->data);
    free (it);
    it = tmp;
  }

  free (hrl);
  free (hs); (*hist)->search = NULL;
  free (*hist); *hist = NULL;
}

private void buf_set_video_first_row (buf_t *this, int idx) {
  if (idx >= this->num_items or idx < 0 or idx is $my(video_first_row_idx))
    return;

  if ($my(video_first_row_idx) < 0) {
    $my(video_first_row) = this->head;
    $my(video_first_row_idx) = 0;
  }

  int num;
  if (idx < $my(video_first_row_idx)) {
    num = $my(video_first_row_idx) - idx;
    loop (num) $my(video_first_row) = $my(video_first_row)->prev;
    $my(video_first_row_idx) -= num;
  } else {
    num = idx - $my(video_first_row_idx);
    loop (num) $my(video_first_row) = $my(video_first_row)->next;
    $my(video_first_row_idx) += num;
   }
}


private row_t *buf_current_prepend (buf_t *this, row_t *row) {
  return current_list_prepend (this, row);
}

private row_t *buf_current_append (buf_t *this, row_t *row) {
  return current_list_append (this, row);
}

private row_t *buf_append_with (buf_t *this, char *bytes) {
  row_t *row = self(row.new_with, bytes);
  int cur_idx = this->cur_idx;
  self(cur.set, this->num_items - 1);
  current_list_append (this, row);
  self(cur.set, cur_idx);
  return row;
}

private row_t *buf_current_prepend_with(buf_t *this, char *bytes) {
  row_t *row = self(row.new_with, bytes);
  return current_list_prepend (this, row);
}

private row_t *buf_current_append_with (buf_t *this, char *bytes) {
  row_t *row = self(row.new_with, bytes);
  return current_list_append (this, row);
}

private row_t *buf_current_append_with_len (buf_t *this, char *bytes, size_t len) {
  row_t *row = self(row.new_with_len, bytes, len);
  return current_list_append (this, row);
}

private row_t *buf_current_replace_with (buf_t *this, char *bytes) {
  My(String).replace_with (this->current->data, bytes);
  return this->current;
}

private row_t *__buf_current_delete (buf_t *this, row_t **row) {
  if (this->current is NULL) return NULL;

  *row = this->current;

  if (this->num_items is 1) {
    this->current = NULL;
    this->head = NULL;
    this->tail = NULL;
    this->cur_idx = -1;
    goto theend;
  }

  if (this->cur_idx is 0) {
    this->current = this->current->next;
    this->current->prev = NULL;
    this->head = this->current;
    goto theend;
  }

  if (this->cur_idx + 1 is this->num_items) {
    this->cur_idx--;
    this->current = this->current->prev;
    this->current->next = NULL;
    this->tail = this->current;
    goto theend;
  }

  this->current->prev->next = this->current->next;
  this->current->next->prev = this->current->prev;
  this->current = this->current->next;

theend:
  this->num_items--;
  return this->current;
}

private row_t *buf_current_delete (buf_t *this) {
  row_t *row;
  __buf_current_delete (this, &row);

  if (row isnot NULL) self(free.row, row);

  return this->current;
}

private row_t *buf_current_pop (buf_t *this) {
  row_t *row;  __buf_current_delete (this, &row);
  return row;
}

private row_t *buf_current_pop_next (buf_t *this) {
  if (this->current->next is NULL) return NULL;

  this->current = this->current->next;
  this->cur_idx++;
  row_t *row; __buf_current_delete (this, &row);
  ifnot (this->cur_idx + 1 is this->num_items) {
    this->current = this->current->prev;
    this->cur_idx--;
  }

  return row;
}

private char *buf_get_fname (buf_t *this) {
  return $my(fname);
}

private int buf_get_current_video_row (buf_t *this) {
  return $my(cur_video_row);
}

private int buf_get_current_video_col (buf_t *this) {
  return $my(cur_video_col);
}

private size_t buf_get_num_lines (buf_t *this) {
  return this->num_items;
}

private int buf_get_prop_tabwidth (buf_t *this) {
  return $my(ftype)->tabwidth;
}

private int buf_set_row_idx (buf_t *this, int idx, int ofs, int col) {
  do {
    idx = current_list_set (this, idx);
    if (idx is INDEX_ERROR) {
      idx = 0;
      continue;
    }
  } while (0);

  $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
  $my(video_first_row) = this->current;
  $my(video_first_row_idx) = idx;

  for (int i = 0; i < ofs and idx-- > 0; i++) {
    $my(video_first_row_idx)--;
    $my(video_first_row) = $my(video_first_row)->prev;
    $my(video)->row_pos = $my(cur_video_row) =
        $my(cur_video_row) + 1;
  }

  if (col > $my(dim)->num_cols) col = 1;
  $my(video)->col_pos = $my(cur_video_col) = col;
  return this->cur_idx;
}

private int buf_current_set (buf_t *this, int idx) {
  return current_list_set (this, idx);
}

private void buf_set_mode (buf_t *this, char *mode) {
  strcpy ($my(mode), mode);
}

private void buf_set_normal_cb (buf_t *this, BufNormalBeg_cb cb) {
  this->on_normal_beg = cb;
}

private void buf_set_show_statusline (buf_t *this, int val) {
  $my(show_statusline) = val;
}

private void *mem_should_realloc (void *obj, size_t allocated, size_t len) {
  if (len > allocated) return Realloc (obj, len);
  return obj;
}

private void buf_set_as_non_existant (buf_t *this) {
  $my(basename) = $my(fname); $my(extname) = NULL;
  $my(cwd) = dir_current ();
  $my(flags) &= ~FILE_EXISTS;
}

private void buf_set_as_unamed (buf_t *this) {
  size_t len = bytelen (UNAMED);
  /* static size_t len = bytelen (UNAMED); fails on tcc with:
   * error: initializer element is not constant
   */
  $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
  strcpy ($my(fname), UNAMED);
  self(set.as.non_existant);
}

private int buf_set_fname (buf_t *this, char *filename) {
  int is_null = (NULL is filename);
  int is_unamed = (is_null ? 0 : str_eq (filename, UNAMED));
  size_t len = ((is_null or is_unamed) ? 0 : bytelen (filename));

  if (is_null or 0 is len or is_unamed) {
    buf_set_as_unamed (this);
    return OK;
  }

  string_t *fname = $my(shared_str);
  My(String).replace_with_len (fname, filename, len);

  for (int i = len - 1; i > 0 /* at least a char */; i--) {
    ifnot (fname->bytes[i] is DIR_SEP) break;
    My(String).clear_at (fname, i);
    len--;
  }

  if ($my(flags) & BUF_IS_SPECIAL) {
    $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
    strncpy ($my(fname), fname->bytes, len + 1);
    self(set.as.non_existant);
    return OK;
    /* this stays as a reference as tcc segfault'ed, when jumping to the label
     * (the old code had fname as char * that it should be freed and there
     * was a clean-up goto)
     * The reason was that the variable (fname) was considered uninitialized,
     * when it wasn't, and it should do here the goto's job
     * free (fname);
     * return retval;
     */
  }

  int fname_exists = file_exists (fname->bytes);
  int is_abs = IS_PATH_ABS (fname->bytes);

  if (fname_exists) {
    ifnot (file_is_reg (fname->bytes)) {
      VED_MSG_ERROR(MSG_FILE_EXISTS_BUT_IS_NOT_A_REGULAR_FILE, fname->bytes);
      buf_set_as_unamed (this);
      return NOTOK;
    }

    if (file_is_elf (fname->bytes)) {
      VED_MSG_ERROR(MSG_FILE_EXISTS_BUT_IS_AN_OBJECT_FILE, fname->bytes);
      buf_set_as_unamed (this);
      return NOTOK;
    }

    $my(flags) |= FILE_EXISTS;

    ifnot (is_abs)
      goto concat_with_cwd;
    else {
      $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
      strncpy ($my(fname), fname->bytes, len + 1);
    }
  } else {
    $my(flags) &= ~FILE_EXISTS;
    if (is_abs) {
      $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
      strncpy ($my(fname), fname->bytes, len + 1);
    } else {
concat_with_cwd:;
      char *cwd = dir_current ();
      len += bytelen (cwd) + 1;
      char tmp[len + 1]; snprintf (tmp, len + 1, "%s/%s", cwd, fname->bytes);
      $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
      /* $my(fname) = realpath (tmp, NULL); aborts with invalid argument on tcc */
      realpath (tmp, $my(fname));
      free (cwd);
    }
  }

  buf_t *buf = My(Ed).get.bufname ($my(root), $my(fname));
  if (buf isnot NULL) {
    VED_MSG_ERROR(MSG_FILE_IS_LOADED_IN_ANOTHER_BUFFER, $my(fname));
    $my(flags) |= BUF_IS_RDONLY;
  }

  if ($my(flags) & FILE_EXISTS) stat ($my(fname), &$my(st));

  $my(basename) = path_basename ($my(fname));
  $my(extname) = path_extname ($my(fname));
  $my(cwd) = path_dirname ($my(fname));

  return OK;
}

private int buf_on_no_length (buf_t *this) {
  buf_current_append_with (this, " ");
  My(String).clear_at (this->current->data, 0);
  return OK;
}

private ssize_t ed_readline_from_fp (char **line, size_t *len, FILE *fp) {
  ssize_t nread;
  if (-1 is (nread = getline (line, len, fp))) return -1;
  if (nread and ((*line)[nread - 1] is '\n' or (*line)[nread - 1] is '\r')) {
    (*line)[nread - 1] = '\0';
    nread--;
  }

  return nread;
}

private ssize_t buf_read_fname (buf_t *this) {
  if ($my(fname) is NULL or str_eq ($my(fname), UNAMED)) return NOTOK;

  FILE *fp = fopen ($my(fname), "r");

  if (fp is NULL) {
     if (EACCES is errno) {
      if (file_exists ($my(fname))) {
        $my(flags) |= FILE_EXISTS;
        $my(flags) &= ~(FILE_IS_READABLE|FILE_IS_WRITABLE);
        VED_MSG_ERROR(MSG_FILE_IS_NOT_READABLE, $my(fname));
        return NOTOK;
      } else
       return OK;

    } else {
      MSG_ERRNO(errno);
      return NOTOK;
    }
  }

  fstat (fileno (fp), &$my(st));
  $my(flags) |= (FILE_EXISTS|FILE_IS_READABLE);

  if (OK is access ($my(fname), W_OK))
    $my(flags) &= ~FILE_IS_WRITABLE;
  else
    $my(flags) |= FILE_IS_WRITABLE;

  $my(flags) |= S_ISREG ($my(st).st_mode);

  size_t t_len = 0;

  ifnot ($my(flags) & FILE_IS_REGULAR) {
    t_len = NOTOK;
    goto theend;
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t nread;

  while (-1 isnot (nread = ed_readline_from_fp (&line, &len, fp))) {
    buf_current_append_with (this, line);
    t_len += nread;
  }

  if (line isnot NULL) free (line);

theend:
  fclose (fp);
  return t_len;
}

private ssize_t buf_init_fname (buf_t *this, char *fname) {
  ssize_t retval = buf_set_fname (this, fname);
//  if (NOTOK is retval) return NOTOK;

  if ($my(flags) & FILE_EXISTS) {
    retval = self(read.fname);

    ifnot (this->num_items)
      retval = buf_on_no_length (this);
  } else
    retval = buf_on_no_length (this);

  $my(video_first_row_idx) = 0;
  $my(video_first_row) = this->head;
  $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
  $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;

  return retval;
}

private void win_adjust_buf_dim (win_t *w) {
  buf_t *this = w->head;
  while (this) {
    $my(dim) = $from(w, frames_dim)[$my(at_frame)];
    $my(statusline_row) = $my(dim)->last_row;
    $my(video_first_row_idx) = this->cur_idx;
    $my(video_first_row) = this->current;
    $my(cur_video_row) = $my(dim)->first_row;
    for (int i = 0; i < 4 and $my(video_first_row_idx) and
        i < $my(dim)->num_rows - 1; i++) {
      $my(video_first_row_idx)--; $my(video_first_row) = $my(video_first_row)->prev;
      $my(cur_video_row)++;
    }

    this = this->next;
  }
}

private void win_set_has_dividers (win_t *this, int val) {
  $my(has_dividers) = val;
}

private void win_set_video_dividers (win_t *this) {
  ifnot ($my(has_dividers)) return;
  if ($my(num_frames) - 1 < 1) return;

  int len = $my(dim)->num_cols + TERM_SET_COLOR_FMT_LEN + TERM_COLOR_RESET_LEN;
  char line[len + 1];
  snprintf (line, TERM_SET_COLOR_FMT_LEN + 1, TERM_SET_COLOR_FMT, COLOR_DIVIDER);
  for (int i = 0; i < $my(dim)->num_cols; i++)
    line[i + TERM_SET_COLOR_FMT_LEN] = '_';

  snprintf (line + TERM_SET_COLOR_FMT_LEN + $my(dim)->num_cols, len, "%s",
      TERM_COLOR_RESET);

  line[len+1] = '\0';
   for (int i = 0; i < $my(num_frames) - 1; i++) {
     My(Video).set_with ($my(video), $my(frames_dim)[i]->last_row,
       line);
  }
}

private int win_add_frame (win_t *this) {
  if ($my(num_frames) is $my(max_frames)) return NOTHING_TODO;

  for (int i = 0; i < $my(num_frames); i++) free ($my(frames_dim)[i]);
  free ($my(frames_dim));

  $my(num_frames)++;
  self(dim_calc);
  self(adjust.buf_dim);
  self(set.video_dividers);
  return DONE;
}

private int win_delete_frame (win_t *this, int idx) {
  if ($my(num_frames) is 1) return NOTHING_TODO;
  if ($my(num_frames) is $my(min_frames)) return NOTHING_TODO;

  for (int i = 0; i < $my(num_frames); i++) free ($my(frames_dim)[i]);
  free ($my(frames_dim));

  $my(num_frames)--;
  self(dim_calc);

  buf_t *it = this->head;
  while (it isnot NULL) {
    if ($from(it, at_frame) > $my(num_frames) - 1)
        $from(it, at_frame) = $my(num_frames) - 1;
    else
      if ($from(it, at_frame) >= idx)
        $from(it, at_frame)--;

    it = it->next;
  }

  if ($my(cur_frame) >= $my(num_frames))
    $my(cur_frame) = $my(num_frames) - 1;

  self(adjust.buf_dim);
  self(set.video_dividers);
   return DONE;
}

private buf_t *win_frame_change (win_t* w, int frame, int draw) {
  if (frame < FIRST_FRAME or frame > WIN_LAST_FRAME(w)) return NULL;
  int idx = 0;
  buf_t *this = w->head;
  while (this) {
    if ($my(at_frame) is frame and $my(flags) & BUF_IS_VISIBLE) {
      My(Win).set.current_buf ($my(parent), idx, draw);
      WIN_CUR_FRAME(w) = frame;
      return w->current;
    }

    idx++;
    this = this->next;
  }

  return NULL;
}

private int ved_buf_on_normal_beg (buf_t **thisp, utf8 com, int *range, int regidx) {
  (void) thisp; (void) com; (void) range; (void) regidx;
  return 0;
}

private int ved_buf_on_normal_end (buf_t **thisp, utf8 com, int *range, int regidx) {
  (void) thisp; (void) com; (void) range; (void) regidx;
  return 0;
}

private void buf_free_rows (buf_t *this) {
  row_t *row = this->head;
  while (row) {
    row_t *next = row->next;
    buf_free_row (this, row);
    row = next;
  }
}

private void buf_free (buf_t *this) {
  if (this is NULL) return;
  buf_free_rows (this);

  if (this is NULL or $myprop is NULL) return;

  if ($my(fname) isnot NULL) free ($my(fname));

  free ($my(cwd));

  My(String).free ($my(statusline));
  My(String).free ($my(promptline));
  My(String).free ($my(shared_str));

  buf_free_line (this);
  buf_free_ftype (this);
  buf_free_undo (this);
  buf_free_jumps (this);

  free ($myprop);
  free (this);
}

private buf_t *win_buf_init (win_t *w, int at_frame, int flags) {
  buf_t *this = AllocType (buf);
  $myprop = AllocProp (buf);

  $my(parent) = w;
  $my(root) = $myparents(parent);

  $my(Ed) = $myparents(Ed);
  $my(Me) = $myparents(Buf);
  $my(Win)= $myparents(Me);
  $my(Cstring) = $myparents(Cstring);
  $my(Vstring) = $myparents(Vstring);
  $my(Ustring) = $myparents(Ustring);
  $my(String) = $myparents(String);
  $my(Re) = $myparents(Re);
  $my(Msg) = $myparents(Msg);
  $my(Error) = $myparents(Error);
  $my(Video) = $myparents(Video);
  $my(Term) = $myparents(Term);
  $my(Input) = $myparents(Input);
  $my(Cursor) = $myparents(Cursor);
  $my(Screen)= $myparents(Screen);
  $my(File) = $myparents(File);
  $my(Path) = $myparents(Path);
  $my(Dir) = $myparents(Dir);
  $my(Rline) = $myparents(Rline);
  $my(Vsys) = $myparents(Vsys);
  $my(Venv) = $myparents(Venv);

  $my(term_ptr) = $myroots(term);
  $my(msg_row_ptr) = &$myroots(msg_row);
  $my(prompt_row_ptr) = &$myroots(prompt_row);
  $my(history) = $myroots(history);
  $my(last_insert) = $myroots(last_insert);
  $my(regs) = &$myroots(regs)[0];
  $my(video) = $myroots(video);

  buf_vundo_init (this);
  buf_jumps_init (this);

  $my(shared_str) = My(String).new (128);
  $my(statusline) = My(String).new (64);
  $my(promptline) = My(String).new (64);
  $my(line) = AllocType(line);

  $my(flags) = flags;
  $my(flags) &= ~BUF_IS_MODIFIED;
  $my(flags) &= ~BUF_IS_VISIBLE;

  self(set.mode, NORMAL_MODE);

  $my(fname) = Alloc (PATH_MAX + 1);

  for (int i = 0; i < NUM_MARKS; i++)
    $my(marks)[i] = (mark_t) {.mark = MARKS[i], .video_first_row = NULL};

  $my(at_frame) = at_frame;
  $my(dim) = $myparents(frames_dim)[$my(at_frame)];
  $my(statusline_row) = $my(dim->last_row);
  $my(show_statusline) = 1;

  this->on_normal_beg = ved_buf_on_normal_beg;
  this->on_normal_end = ved_buf_on_normal_end;

  return this;
}

private buf_t *win_buf_new (win_t *w, char *fname, int at_frame, int flags) {
  buf_t *this = win_buf_init (w, at_frame, flags);

  self(init_fname, fname);
  self(set.ftype, FTYPE_DEFAULT);

  self(set.row.idx, 0, NO_OFFSET, 1);

  return this;
}

private void win_set_min_rows (win_t *this, int rows) {
  $my(min_rows) = rows;
}

private void win_set_num_frames (win_t *this, int num_frames) {
  if (num_frames > $my(max_frames)) return;
  $my(num_frames) = num_frames;
}

private buf_t *win_set_current_buf (win_t *w, int idx, int draw) {
  buf_t *that = w->current;
  int cur_idx = w->cur_idx;
  int cur_frame = $from(w, cur_frame);
  buf_t *this = NULL;

  if (idx is cur_idx) {
    this = that;
    goto change;
  }

  int lidx = current_list_set (w, idx);

  if (lidx is INDEX_ERROR) return NULL;

  if (cur_idx isnot lidx) w->prev_idx = cur_idx;

  this = w->current;

  if (cur_frame is $my(at_frame))
    $from(that, flags) &= ~BUF_IS_VISIBLE;
  else
    $from(w, cur_frame) = $my(at_frame);

change:
  $my(flags) |= BUF_IS_VISIBLE;
  $my(video)->row_pos = $my(cur_video_row);
  $my(video)->col_pos = $my(cur_video_col);

  if ($my(ftype)->autochdir)
    if (-1 is chdir ($my(cwd)))
      MSG_ERRNO(errno);

  if ($my(flags) & FILE_EXISTS) {
    struct stat st;
    if (-1 is stat ($my(fname), &st)) {
      VED_MSG_ERROR(MSG_FILE_REMOVED_FROM_FILESYSTEM);
    } else {
      if ($my(st).st_mtim.tv_sec isnot st.st_mtim.tv_sec)
        VED_MSG_ERROR(MSG_FILE_HAS_BEEN_MODIFIED);
    }
  }

  if (draw) self(draw);

  return this;
}

private buf_t *win_get_current_buf (win_t *w) {
  return w->current;
}

private int win_get_current_buf_idx (win_t *w) {
  return w->cur_idx;
}

private buf_t *win_get_buf_by_idx (win_t *w, int idx) {
  if (w is NULL) return NULL;

  if (0 > idx) idx += w->num_items;

  if (idx < 0 || idx >= w->num_items) return NULL;

  buf_t *this = w->head;
  loop (idx) this = this->next;

  return this;
}

private buf_t *win_get_buf_by_name (win_t *w, const char *fname, int *idx) {
  if (w is NULL or fname is NULL) return NULL;
  *idx = 0;
  buf_t *this = w->head;
  while (this) {
    if ($my(fname) isnot NULL)
      if (str_eq ($my(fname), fname)) return this;

    this = this->next;
    *idx += 1;
  }

  return NULL;
}

private int win_get_num_buf (win_t *w) {
  return w->num_items;
}

private int win_get_num_rows (win_t *this) {
  return $my(dim)->num_rows;
}

private int win_get_num_cols (win_t *this) {
  return $my(dim)->num_cols;
}

private int win_append_buf (win_t *this, buf_t *buf) {
  current_list_append (this, buf);
  return this->cur_idx;
}

private int win_pop_current_buf (win_t *this) {
  ifnot (this->num_items) return NOTOK;
  if ($from(this->current, flags) & BUF_IS_SPECIAL) return NOTOK;
  int prev_idx = this->prev_idx;
  buf_t *tmp = current_list_pop (this, buf_t);
  buf_free (tmp);
  if (this->num_items is 1)
    this->prev_idx = this->cur_idx;
  else
    if (prev_idx >= this->cur_idx) this->prev_idx--;
  return this->cur_idx;
}

private void win_draw (win_t *w) {
  char line[$from(w, dim->num_cols) + 1];
  for (int i = 0; i < $from(w, dim->num_cols); i++) line[i] = ' ';
  line[$from(w, dim->num_cols)] = '\0';

//  if (w->num_items is 0) return;

  for (int i = $from(w, dim->first_row) - 1; i < $from(w, dim->last_row); i++) {
    $from(w, Video->self).set_with ($from(w, video), i, line);
  }

  buf_t *this = w->head;
  My(Win).set.video_dividers ($my(parent));

  while (this) {
    if ($my(flags) & BUF_IS_VISIBLE) {
      if (this is w->current)
        My(Ed).set.topline (this);

      self(to.video);
    }
    this = this->next;
  }

  this = w->head;
  My(Video).Draw.all ($my(video));
}

private void ved_draw_current_win (ed_t *this) {
  win_draw (this->current);
}

private void win_free (win_t *this) {
  buf_t *buf = this->head;
  buf_t *next;

  while (buf) {
    next = buf->next;
    buf_free (buf);
    buf = next;
  }

  if ($myprop isnot NULL) {
    free ($my(name));

    if ($my(dim) isnot NULL)
      free ($my(dim));

    if ($my(frames_dim) isnot NULL) {
      for (int i = 0; i < $my(num_frames); i++) free ($my(frames_dim)[i]);

      free ($my(frames_dim));
    }

    free ($myprop);
  }

 free (this);
}

private dim_t **win_dim_calc_cb (win_t *this, int num_rows, int num_frames,
                                            int min_rows, int has_dividers) {
  return ed_dim_calc ($my(parent), num_rows, num_frames, min_rows, has_dividers);
}

private dim_t **win_dim_calc (win_t *this) {
  $my(frames_dim) = this->dim_calc (this, $my(dim)->num_rows, $my(num_frames),
     $my(min_rows), $my(has_dividers));
  return $my(frames_dim);
}

private win_t *ed_win_init (ed_t *ed, char *name, WinDimCalc_cb dim_calc_cb) {
  win_t *this = AllocType (win);
  $myprop = AllocProp (win);
  if (NULL is name) {
    int num = ed->name_gen / 26;
    $my(name) = Alloc (num * sizeof (char *) + 1);
    for (int i = 0; i < num; i++) $my(name)[i] = 'a' + (ed->name_gen++ % 26);
    $my(name)[num] = '\0';
  } else
    $my(name) = str_dup (name, bytelen (name));

  $my(parent) = ed;

  $my(Ed) = $myparents(Me);
  $my(Me) = &$myparents(Me)->Win;
  $my(Buf) = &$myparents(Me)->Buf;
  $my(Cstring) = $myparents(Cstring);
  $my(Vstring) = $myparents(Vstring);
  $my(Ustring) = $myparents(Ustring);
  $my(String) =$myparents(String);
  $my(Re) = $myparents(Re);
  $my(Msg) = $myparents(Msg);
  $my(Error) = $myparents(Error);
  $my(Video) = $myparents(Video);
  $my(Term) = $myparents(Term);
  $my(Input) = $myparents(Input);
  $my(Cursor) = $myparents(Cursor);
  $my(Screen)= $myparents(Screen);
  $my(File) = $myparents(File);
  $my(Path) = $myparents(Path);
  $my(Dir) = $myparents(Dir);
  $my(Rline) = $myparents(Rline);
  $my(Vsys) = $myparents(Vsys);
  $my(Venv) = $myparents(Venv);

  $my(video) = $myparents(video);
  $my(min_rows) = 1;
  $my(has_topline) = $myparents(has_topline);
  $my(has_msgline) = $myparents(has_msgline);
  $my(has_promptline) = $myparents(has_promptline);
  $my(type) = VED_WIN_NORMAL_TYPE;
  $my(has_dividers) = 1;

  $my(dim) = ed_dim_new (ed,
      $myparents(dim)->first_row,
      $myparents(dim)->num_rows,
      $myparents(dim)->first_col,
      $myparents(dim)->num_cols);

  $my(cur_frame) = 0;
  $my(min_frames) = 1;
  $my(num_frames) = 1;
  $my(max_frames) = MAX_FRAMES;

  this->dim_calc = (NULL isnot dim_calc_cb ? dim_calc_cb : win_dim_calc_cb);
  return this;
}

private win_t *ed_win_new (ed_t *ed, char *name, int num_frames) {
  win_t *this = ed_win_init (ed, name, win_dim_calc_cb);
  $my(num_frames) = num_frames;
  self(dim_calc);
  self(set.video_dividers);
  return this;
}

private win_t *ed_win_new_special (ed_t *ed, char *name, int num_frames) {
  win_t *this = ed_win_new (ed, name, num_frames);
  $my(type) = VED_WIN_SPECIAL_TYPE;
  return this;
}

private void ed_win_readjust_size (ed_t *ed, win_t *this) {
  ifnot (NULL is $my(dim)) {
    free ($my(dim));
    $my(dim) = NULL;
  }

  $my(dim) = ed_dim_new (ed,
    $myparents(dim)->first_row, $myparents(dim)->num_rows,
    $myparents(dim)->first_col, $myparents(dim)->num_cols);

  ifnot (NULL is $my(frames_dim)) {
    for (int i = 0; i < $my(num_frames); i++) free ($my(frames_dim)[i]);
    free ($my(frames_dim));
    $my(frames_dim) = NULL;
  }

  self(dim_calc);
  self(adjust.buf_dim);
  self(set.video_dividers);
  $my(video)->row_pos = $from(this->current, cur_video_row);
  $my(video)->col_pos = $from(this->current, cur_video_col);
  if (this is $my(parent)->current) {
    My(Video).set_with ($my(video), $myparents(prompt_row), " ");
    My(Video).set_with ($my(video), $myparents(msg_row), " ");
    self(draw);
  }
}

private void ed_check_msg_status (buf_t *this) {
  if ($myroots(msg_send) is 1)
    $myroots(msg_send)++;
  else if (2 is $myroots(msg_send)) {
    My(Video).set_with ($my(video), *$my(msg_row_ptr) - 1, " ");
    My(Video).Draw.row_at ($my(video), *$my(msg_row_ptr));
    $myroots(msg_send) = 0;
  }
}

private buf_t *ed_get_buf (ed_t *this, char *wname, char *bname) {
  int idx;
  win_t *w = self(get.win_by_name, wname, &idx);
  ifnot (w) return NULL;
  return My(Win).get.buf_by_name (w, bname, &idx);
}

private int ed_change_buf (ed_t *this, buf_t **thisp, char *wname, char *bname) {
  self(win.change, thisp, NO_COMMAND, wname, NO_OPTION, NO_FORCE);
  return ved_buf_change_bufname (thisp, bname);
}

private buf_t *ved_special_buf (ed_t *this, char *wname, char *bname,
    int num_frames, int at_frame) {
  int idx;
  win_t *w = self(get.win_by_name, wname, &idx);
  if (NULL is w) {
    w = self(win.new_special, wname, num_frames);
    self(append.win, w);
  }

  buf_t *buf = My(Win).get.buf_by_name (w, bname, &idx);
  if (NULL is buf) {
    buf = My(Win).buf.new (w, bname, at_frame, BUF_IS_PAGER|BUF_IS_RDONLY|BUF_IS_SPECIAL);
    My(Win).append_buf (w, buf);
  }

  return buf;
}

private buf_t *ved_msg_buf (ed_t *this) {
  return ved_special_buf (this, VED_MSG_WIN, VED_MSG_BUF, 1, 0);
}

private buf_t *ved_diff_buf (ed_t *this) {
  return ved_special_buf (this, VED_DIFF_WIN, VED_DIFF_BUF, 1, 0);
}

private buf_t *ved_search_buf (ed_t *root) {
  buf_t *this = ved_special_buf (root, VED_SEARCH_WIN, VED_SEARCH_BUF, 2, 1);
  this->on_normal_beg = ved_grep_on_normal;
  $my(is_sticked) = 1;
  $myparents(cur_frame) = 1;
  My(Win).set.current_buf ($my(parent), 0, DRAW);
  return this;
}

private buf_t *ved_scratch_buf (ed_t *this) {
  int idx;
  win_t *w = self(get.win_by_name, VED_SCRATCH_WIN, &idx);
  if (NULL is w) {
    w = self(win.new_special, VED_SCRATCH_WIN, 1);
    self(append.win, w);
  }

  buf_t *buf = My(Win).get.buf_by_name (w, VED_SCRATCH_BUF, &idx);
  if (NULL is buf) {
    buf = My(Win).buf.new (w, VED_SCRATCH_BUF, 0, BUF_IS_PAGER|BUF_IS_RDONLY|BUF_IS_SPECIAL);
    My(Win).append_buf (w, buf);
  }

  return buf;
}

private void ved_append_toscratch (ed_t *this, int clear_first, char *bytes) {
  buf_t *buf = self(buf.get, VED_SCRATCH_WIN, VED_SCRATCH_BUF);
  if (NULL is buf)
    buf = ved_scratch_buf (this);

  if (clear_first) My(Buf).clear (buf);

  vstr_t *lines = str_chop (bytes, '\n', NULL, NO_CB_FN, NULL);
  vstring_t *it = lines->head;

  int ifclear = 0;
  while (it) {
    if (clear_first and 0 is ifclear++)
      My(Buf).cur.replace_with (buf, it->data->bytes);
    else
      My(Buf).append_with (buf, it->data->bytes);
    it = it->next;
  }
  vstr_free (lines);
}

private void ved_append_toscratch_fmt (ed_t *this, int clear_first, char *fmt, ...) {
  char bytes[MAXLINE];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  ved_append_toscratch (this, clear_first, bytes);
}

private int ved_scratch (ed_t *this, buf_t **bufp, int at_eof) {
  self(buf.change, bufp, VED_SCRATCH_WIN, VED_SCRATCH_BUF);
  ifnot (str_eq ($from((*bufp), fname), VED_SCRATCH_BUF))
   (*bufp) = ved_scratch_buf (this);

  if (at_eof) ved_normal_eof (*bufp, DRAW);
  else My(Buf).draw (*bufp);

  return DONE;
}

private int ved_messages (ed_t *this, buf_t **bufp, int at_eof) {
  self(buf.change, bufp, VED_MSG_WIN, VED_MSG_BUF);
  ifnot (str_eq ($from((*bufp), fname), VED_MSG_BUF))
    return NOTHING_TODO;

  if (at_eof) ved_normal_eof (*bufp, DRAW);
  else My(Buf).draw (*bufp);

  return DONE;
}

private int ved_append_message_cb (vstr_t *str, char *tok, void *obj) {
  (void) str;
  buf_t *this = (buf_t *) obj;
  self(append_with, tok);
  return OK;
}

private void ved_append_message (ed_t *this, char *msg) {
  buf_t *buf = self(buf.get, VED_MSG_WIN, VED_MSG_BUF);
  ifnot (buf) return;
  vstr_t unused;
  str_chop (msg, '\n', &unused, ved_append_message_cb, (void *) buf);
}

private void ved_append_message_fmt (ed_t *this, char *fmt, ...) {
  char bytes[MAXLINE];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  ved_append_message (this, bytes);
}

private char *ed_msg_fmt (ed_t *this, int msgid, ...) {
  char fmt[MAXERRLEN]; fmt[0] = '%'; fmt[1] = 's'; fmt[2] = '\0';
  char pat[16]; snprintf (pat, 16, "%d:", msgid);
  char *sp = strstr (ED_MSGS_FMT, pat);
  if (sp isnot NULL) {
    int i;
    for (i = 0; i < (int) bytelen (pat); i++) sp++;
    for (i = 0; *sp and *sp isnot '.'; sp++) fmt[i++] = *sp;
    fmt[i] = '\0';
  }

  char bytes[MAXLINE];
  va_list ap;
  va_start(ap, msgid);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  My(String).replace_with ($my(ed_str), bytes);
  return $my(ed_str)->bytes;
}

private int ed_fmt_string_with_numchars (ed_t *this, string_t *dest,
   int clear_dest, char *src, size_t src_len, int tabwidth, int num_cols) {

  int numchars = 0;
  if (clear_dest) My(String).clear (dest);

  if (src_len <= 0) src_len = bytelen (src);

  ifnot (src_len) return numchars;

  vchar_t *it = ustring_encode ($my(uline), src, src_len, CLEAR, tabwidth, 0);

  vchar_t *tmp = it;

  while (it and numchars < num_cols) {
    if (My(Cstring).eq (it->buf, "\t")) {
      for (int i = 0; i < it->width; i++)
        My(String).append_byte (dest, ' ');
    } else
      My(String).append (dest, it->buf);

    numchars += it->width;
    tmp = it;
    it = it->next;
  }

  it = tmp;
  while (it and numchars > num_cols) {
    if (My(Cstring).eq (it->buf, "\t")) {
      for (int i = 0; i < it->width; i++) {
        if (numchars-- is num_cols)
          return numchars;
        My(String).clear_at (dest, dest->num_bytes - 1);
      }

      goto thenext_it;
    }

    My(String).clear_at (dest, (dest->num_bytes - it->len));
    numchars -= it->width;

thenext_it:
    it = it->prev;
  }

  return numchars;
}

private void ed_msg_write (ed_t *this, char *msg) {
  self(append.message, msg);
}

private void ed_msg_set (ed_t *this, int color, int msg_flags, char *msg,
                                                        size_t maybe_len) {
  int wt_msg = (msg_flags & MSG_SET_TO_MSG_BUF or
               (msg_flags & MSG_SET_TO_MSG_LINE) is UNSET);
  if (wt_msg)
    self(append.message, msg);

  int clear = (msg_flags & MSG_SET_RESET or
              (msg_flags & MSG_SET_APPEND) is UNSET);
  if (clear)
    $my(msg_numchars) = 0;

  $my(msg_numchars) += ed_fmt_string_with_numchars (this, $my(msgline),
      (clear ? CLEAR : DONOT_CLEAR), msg, maybe_len,
      $my(msg_tabwidth), $my(dim)->num_cols - $my(msg_numchars));

  int close = (msg_flags & MSG_SET_CLOSE or
              (msg_flags & MSG_SET_OPEN) is UNSET);
  if (close)
    for (int i = $my(msg_numchars); i < $my(dim)->num_cols; i++)
      My(String).append_byte ($my(msgline), ' ');

  int set_color = (msg_flags & MSG_SET_COLOR);
  if (set_color) {
    My(String).prepend ($my(msgline), TERM_MAKE_COLOR(color));
    My(String).append($my(msgline), TERM_COLOR_RESET);
  }

  My(Video).set_with ($my(video), $my(msg_row) - 1, $my(msgline)->bytes);

  int draw = (msg_flags & MSG_SET_DRAW);
  if (draw)
    My(Video).Draw.row_at ($my(video), $my(msg_row));

  $my(msg_send) = 1;
}

private void ed_msg_set_fmt (ed_t *this, int color, int msg_flags, char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  size_t len = vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  ed_msg_set (this, color, msg_flags, bytes, len);
}

private void ed_msg_send (ed_t *this, int color, char *msg) {
  ed_msg_set (this, color, MSG_SET_DRAW|MSG_SET_COLOR, msg, -1);
}

private void ed_msg_error (ed_t *this, char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  size_t len = vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  ed_msg_set (this, COLOR_ERROR, MSG_SET_DRAW|MSG_SET_COLOR, bytes, len);
}

private void ed_msg_send_fmt (ed_t *this, int color, char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  size_t len = vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  ed_msg_set (this, color, MSG_SET_DRAW|MSG_SET_COLOR, bytes, len);
}

private char *ed_error_string (ed_t *this, int err) {
  char ebuf[MAXERRLEN];
  ebuf[0] = '\0';
  char epat[16];
  snprintf (epat, 16, "%d:", err);

  char *sp = strstr (SYS_ERRORS, epat);
  if (sp is NULL) {
    snprintf (epat, 16, "%d:",  err);
    sp = strstr (ED_ERRORS, epat);
  }

  if (sp is NULL) return NULL;
  int i;
  for (i = 0; *sp is ' ' or i <= (int) bytelen (epat); i++) sp++;
  for (i = 0; *sp and *sp isnot ' '; sp++) ebuf[i++] = *sp;
  for (; *sp and *sp is ' '; sp++);
  ebuf[i++] = ':'; ebuf[i++] = ' ';
  for (; *sp and *sp isnot ','; sp++) ebuf[i++] = *sp;
  ebuf[i] = '\0';

  My(String).replace_with ($my(ed_str), ebuf);
  return $my(ed_str)->bytes;
}

private void buf_set_topline (buf_t *this) {
  time_t tim = time (NULL);
  struct tm *tm = localtime (&tim);

  My(String).replace_with_fmt ($myroots(topline), "[%s] ftype (%s) [pid %d]",
    $my(mode), $my(ftype)->name, $myroots(env)->pid);

  char tmnow[32];
  strftime (tmnow, sizeof (tmnow), "[%a %d %H:%M:%S]", tm);
  int pad = $my(dim->num_cols) - $myroots(topline)->num_bytes - bytelen (tmnow);
  if (pad > 0)
    loop (pad) My(String).append ($myroots(topline), " ");

  My(String).append_fmt ($myroots(topline), "%s%s", tmnow, TERM_COLOR_RESET);
  My(String).prepend_fmt ($myroots(topline), TERM_SET_COLOR_FMT, COLOR_TOPLINE);
  My(Video).set_with ($my(video), 0, $myroots(topline)->bytes);
}

private void buf_set_draw_topline (buf_t *this) {
  My(Ed).set.topline (this);
  My(Video).Draw.row_at ($my(video), 1);
}

private void buf_set_statusline (buf_t *this) {
  if ($my(dim->num_rows) is 1 or (
      $my(show_statusline) is 0 and 0 is IS_MODE (INSERT_MODE))) {
    My(String).replace_with ($my(statusline), " ");
    My(Video).set_with ($my(video), $my(statusline_row) - 1, $my(statusline)->bytes);
    return;
  }

  int cur_code = 0;
  if ($mycur(cur_col_idx) < (int) $mycur(data)->num_bytes) {
    cur_code = CUR_UTF8_CODE;
  }

  My(String).replace_with_fmt ($my(statusline),
    TERM_SET_COLOR_FMT "%s (line: %d/%d idx: %d len: %d chr: %d)",
    COLOR_STATUSLINE, $my(basename), this->cur_idx + 1, this->num_items,
    $mycur(cur_col_idx), $mycur(data)->num_bytes, cur_code);

  My(String).clear_at ($my(statusline), $my(dim)->num_cols + TERM_SET_COLOR_FMT_LEN);
  My(String).append_fmt ($my(statusline), "%s", TERM_COLOR_RESET);
  My(Video).set_with ($my(video), $my(statusline_row) - 1, $my(statusline)->bytes);
}

private void buf_set_draw_statusline (buf_t *this) {
  buf_set_statusline (this);
  My(Video).Draw.row_at ($my(video), $my(statusline_row));
}

private string_t *get_current_number (buf_t *this, int *fidx) {
  if ($mycur(data)->num_bytes is 0) return NULL;

  string_t *nb = string_new (8);
  int type = 'd';
  int issign = 0;

  int orig_idx = $mycur(cur_col_idx);
  int idx = orig_idx;
  uchar c;

  while (idx >= 0) {
    c = $mycur(data)->bytes[idx--];

    if (type is 'x') {
      if ('0' isnot c) goto theerror;
      string_prepend_byte (nb, c);
      *fidx = idx + 1;
      break;
    }

    if (IS_DIGIT (c) or IsAlsoAHex (c)) {
      string_prepend_byte (nb, c);
      continue;
    }

    if (c is 'x') {
      if (type is 'x') goto theerror;
      type = 'x';
      string_prepend_byte (nb, c);
      continue;
    }

    if (c is '-') { issign = 1; idx--; }
    *fidx = idx + 2;
    break;
  }

  int cur_idx = nb->num_bytes - 1;

  idx = orig_idx + 1;
  while (idx < (int) $mycur(data)->num_bytes) {
    c = $mycur(data)->bytes[idx++];

    if (IS_DIGIT (c) or IsAlsoAHex (c)) {
      string_append_byte (nb, c);
      continue;
    }

    if (c is 'x') {
      if (type is 'x') goto theerror;
      if (nb->num_bytes isnot 1 and nb->bytes[0] isnot '0') goto theerror;
      string_append_byte (nb, c);
      type = 'x';
      continue;
    }

    break;
  }

  if (nb->num_bytes is 0) goto theerror;

  if (type is 'x') {
    if (nb->num_bytes < 3) goto theerror;
    goto theend;
  }

  idx = cur_idx + 1;

  while (idx < (int) nb->num_bytes and IS_DIGIT (nb->bytes[idx])) idx++;
  if (idx < (int) nb->num_bytes) My(String).clear_at (nb, idx);

  int num = 0;
  idx = cur_idx;
  while (idx >= 0) {
    if (IsAlsoAHex (nb->bytes[idx])) {
      num = idx + 1;
      *fidx = orig_idx - (cur_idx - idx) + 1;
      break;
    }
    idx--;
  }

  if (num) My(String).delete_numbytes_at (nb, num, 0);
  ifnot (nb->num_bytes) goto theerror;

  idx = 0;
  if (nb->bytes[idx++] is '0') {
    while (idx <= (int) nb->num_bytes - 1) {
      if (nb->bytes[idx] > '8') {
        My(String).clear_at (nb, idx);
        break;
      }
      idx++;
    }
    if (nb->num_bytes < 2) goto theerror;

    type = 'o';
    goto theend;
  }

  goto theend;

theerror:
  string_free (nb);
  return NULL;

theend:
  if (issign) string_prepend_byte (nb, '-');
  string_prepend_byte (nb, type);
  return nb;
}

private char *str_extract_word_at (char *bytes, size_t bsize, char *word, size_t wsize,
   char *Nwtype, size_t Nwsize, int cur_idx, int *fidx, int *lidx) {
  if (NULL is bytes or 0 is bsize or (int) bsize <= cur_idx) {
    *lidx = cur_idx;
    return NULL;
  }

  if (IS_SPACE (bytes[cur_idx]) or
      IS_CNTRL (bytes[cur_idx]) or
      NULL isnot memchr (Nwtype, bytes[cur_idx], Nwsize)) {
    *lidx = cur_idx;
    return NULL;
  }

  while (cur_idx > 0 and
      IS_SPACE (bytes[cur_idx]) is 0 and
      IS_CNTRL (bytes[cur_idx]) is 0 and
      NULL is memchr (Nwtype, bytes[cur_idx], Nwsize))
    cur_idx--;

  if (cur_idx isnot 0 or (
      IS_SPACE (bytes[cur_idx]) or
      IS_CNTRL (bytes[cur_idx]) or
      NULL isnot memchr (Nwtype, bytes[cur_idx], Nwsize)))
    cur_idx++;

  *fidx = cur_idx;

  int widx = 0;
  while (cur_idx < (int) bsize and
      IS_SPACE (bytes[cur_idx]) is 0 and
      IS_CNTRL (bytes[cur_idx]) is 0 and
      NULL is memchr (Nwtype, bytes[cur_idx], Nwsize) and
      widx <= (int) wsize)
    word[widx++] = bytes[cur_idx++];

  *lidx = cur_idx - 1;

  word[widx] = '\0';

  return word;
}

private char *get_current_word (buf_t *this, char *word, char *Nwtype,
                                  int Nwtypelen, int *fidx, int *lidx) {
  return str_extract_word_at ($mycur(data)->bytes, $mycur(data)->num_bytes,
    word, MAXWORD, Nwtype, Nwtypelen, $mycur (cur_col_idx), fidx, lidx);
}

#define SEARCH_UPDATE_ROW(idx)                    \
({                                                \
  int idx__ = (idx);                              \
  if (sch->dir is -1) {                           \
    if (idx__ is 0) {                             \
      idx__ = this->num_items - 1;                \
      sch->row = this->tail;                      \
    } else {                                      \
      idx__--;                                    \
      sch->row = sch->row->prev;                  \
    }                                             \
  } else {                                        \
    if (idx__ is this->num_items - 1) {           \
      idx__ = 0;                                  \
      sch->row = this->head;                      \
    } else {                                      \
      idx__++;                                    \
      sch->row = sch->row->next;                  \
    }                                             \
  }                                               \
                                                  \
  idx__;                                          \
})

#define SEARCH_FREE                               \
({                                                \
  My(String).free (sch->pat);                     \
  stack_free (sch, sch_t);                        \
  ifnot (NULL is sch->prefix) free (sch->prefix); \
  ifnot (NULL is sch->match) free (sch->match);   \
  free (sch);                                     \
})

#define SEARCH_PUSH(idx_, row_)                   \
  sch_t *s_ = AllocType (sch);                    \
  s_->idx = (idx_);                               \
  s_->row  = (row_);                              \
  stack_push (sch, s_)

private int __ved_search__ (buf_t *this, search_t *sch) {
  int retval = NOTOK;
  int idx = sch->cur_idx;
  int flags = 0;
  regexp_t *re = My(Re).new (sch->pat->bytes, flags, RE_MAX_NUM_CAPTURES, My(Re).compile);

  sch->found = 0;
  SEARCH_PUSH (sch->cur_idx, sch->row);

  do {
    int ret = My(Re).exec (re, sch->row->data->bytes, sch->row->data->num_bytes);
    if (ret is RE_UNBALANCED_BRACKETS_ERROR) {
      MSG_ERRNO (RE_UNBALANCED_BRACKETS_ERROR);
      break;
    }

    if (0 <= ret) {
      sch->idx = idx;
      sch->found = 1;
      sch->col = re->match_idx;
      sch->match = Alloc ((sizeof (char) * re->match_len) + 1);
      memcpy (sch->match, re->match_ptr, re->match_len);
      sch->match[re->match_len] = '\0';

      sch->prefix = Alloc ((sizeof (char) * sch->col) + 1);
      memcpy (sch->prefix, sch->row->data->bytes, sch->col);
      sch->prefix[sch->col] = '\0';
      sch->end = sch->row->data->bytes + re->retval;
      retval = OK;
      goto theend;
    }

    idx = SEARCH_UPDATE_ROW (idx);

  } while (idx isnot sch->cur_idx);

theend:
  My(Re).free (re);
  return retval;
}

private int rline_search_at_beg (rline_t **rl) {
  switch ((*rl)->c) {
    case ESCAPE_KEY:
    case '\r':
    case CTRL('n'):
    case CTRL('p'):
    case ARROW_UP_KEY:
    case ARROW_DOWN_KEY:

    case '\t':
    (*rl)->state |= RL_POST_PROCESS;
    return RL_POST_PROCESS;
  }

  (*rl)->state |= RL_OK;
  return RL_OK;
}

private void ved_search_history_push (ed_t *this, char *bytes, size_t len) {
  if ($my(max_num_hist_entries) < $my(history)->search->num_items) {
    histitem_t *tmp = list_pop_tail ($my(history)->search, histitem_t);
    string_free (tmp->data);
    free (tmp);
  }

  histitem_t *h = AllocType (histitem);
  h->data = string_new_with_len (bytes, len);
  list_push ($my(history)->search, h);
}

private int ved_search (buf_t *this, char com) {
  if (this->num_items is 0) return NOTHING_TODO;

  int toggle = 0;
  int hist_called = 0; /* if this variable declared a little bit before the for loop
  * gcc will complain for "maybe used uninitialized" because the code can jump to
  * the search label which is inside the for loop, clang do not warn for this, the
  * code works but valgrind reports uninitialized value */

  search_t *sch = AllocType (search);
  sch->found = 0;
  sch->prefix = sch->match = NULL;
  sch->row = this->current;

  if (com is '/' or com is '*' or com is 'n') sch->dir = 1;
  else sch->dir = -1;

  sch->cur_idx = SEARCH_UPDATE_ROW (this->cur_idx);

  histitem_t *his = $my(history)->search->head;

  MSG(" ");

  rline_t *rl = rline_new ($my(root), $my(term_ptr), My(Input).get,
      *$my(prompt_row_ptr), 1, $my(dim)->num_cols, $my(video));
  rl->at_beg = rline_search_at_beg;
  rl->at_end = rline_break;

  rl->prompt_char = (com is '*' or com is '/') ? '/' : '?';

  if (com is '*' or com is '#') {
    com = '*' is com ? '/' : '?';

    char word[MAXWORD]; int fidx, lidx;
    get_current_word (this, word, Notword, Notword_len, &fidx, &lidx);
    sch->pat = My(String).new_with (word);
    if (sch->pat->num_bytes) {
      BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);
      rline_write_and_break (rl);
      goto search;
    }
  } else {
    if (com is 'n' or com is 'N') {
      if ($my(history)->search->num_items is 0) return NOTHING_TODO;
      sch->pat = My(String).new_with (his->data->bytes);
      BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);

      com = 'n' is com ? '/' : '?';
      rl->prompt_char = com;
      rline_write_and_break (rl);
      goto search;
    } else
      sch->pat = My(String).new (1);
  }

  for (;;) {
    utf8 c = rline_edit (rl)->c;
    string_t *p = vstr_join (rl->line, "");

    if (rl->line->tail->data->bytes[0] is ' ')
      string_clear_at (p, p->num_bytes - 1);

    if (str_eq (sch->pat->bytes, p->bytes)) {
      string_free (p);
    } else {
      string_replace_with (sch->pat, p->bytes);
      string_free (p);

search:
      ifnot (NULL is sch->prefix) { free (sch->prefix); sch->prefix = NULL; }
      ifnot (NULL is sch->match) { free (sch->match); sch->match = NULL; }
      __ved_search__ (this, sch);
      if (toggle) {
        sch->dir = (sch->dir is 1) ? -1 : 1;
        toggle = 0;
      }

      if (sch->found) {
        ed_msg_set_fmt ($my(root), COLOR_NORMAL, MSG_SET_OPEN, "|%d %d|%s",
            sch->idx + 1, sch->col, sch->prefix);

        size_t hl_idx = $myroots(msgline)->num_bytes;

        ed_msg_set_fmt ($my(root), COLOR_NORMAL, MSG_SET_APPEND|MSG_SET_CLOSE, "%s%s",
            sch->match, sch->end);

        video_row_hl_at ($my(video), *$my(msg_row_ptr) - 1, COLOR_RED,
            hl_idx, hl_idx + bytelen(sch->match));

        sch->cur_idx = sch->idx;
      } else {
        sch_t *s = stack_pop (sch, sch_t);
        if (NULL isnot s) {
          sch->cur_idx = s->idx;
          sch->row = s->row;
          free (s);
        }

      MSG(" ");
      }

      continue;
    }

    switch (c) {

      case ESCAPE_KEY:
        sch->found = 0;

      case '\r':
        goto theend;

      case CTRL('n'):
        if (sch->found) {
          sch->cur_idx = SEARCH_UPDATE_ROW (sch->idx);
          goto search;
        }

        continue;

      case CTRL('p'):
        if (sch->found) {
          sch->dir = (sch->dir is 1) ? -1 : 1;
          sch->cur_idx = SEARCH_UPDATE_ROW (sch->idx);
          toggle = 1;
          goto search;
        }

        continue;

      case ARROW_UP_KEY:
      case ARROW_DOWN_KEY:
        if ($my(history)->search->num_items is 0) continue;
        if (c is ARROW_DOWN_KEY) {
          if (his->prev isnot NULL)
            his = his->prev;
          else
            his = $my(history)->search->tail;
          hist_called++;
        } else {
          if (hist_called) {
            if (his->next isnot NULL)
              his = his->next;
            else
              his = $my(history)->search->head;
          } else hist_called++;
        }

        My(String).replace_with (sch->pat, his->data->bytes);

        rl->state |= RL_CLEAR_FREE_LINE;
        rline_clear (rl);
        BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);
        goto search;

      default:
        continue;
    }
  }

theend:
  if (sch->found) {
    ved_search_history_push ($my(root), sch->pat->bytes, sch->pat->num_bytes);
    ved_normal_goto_linenr (this, sch->idx + 1, DRAW);
  }

  MSG(" ");

  rline_clear (rl);
  rline_free (rl);

  SEARCH_FREE;
  return DONE;
}

private int ved_grep_on_normal (buf_t **thisp, utf8 com, int *range, int regidx) {
  (void) range; (void) regidx;
  buf_t *this = *thisp;

  if (com isnot '\r' and com isnot 'q') return 0;
  if (com is 'q') {
    if (NOTHING_TODO is ed_win_change ($my(root), thisp, VED_COM_WIN_CHANGE_PREV_FOCUSED, NULL, NO_OPTION, NO_FORCE))
      return EXIT;
    return -1;
  }

  this = *thisp;
  ved_normal_bol (this);
  return 1 + ved_open_fname_under_cursor (thisp, 0, OPEN_FILE_IFNOT_EXISTS, DONOT_REOPEN_FILE_IF_LOADED);
}

private int ved_search_file (buf_t *this, char *fname, regexp_t *re) {
  FILE *fp = fopen (fname, "r");
  if (fp is NULL) return NOTOK;

  size_t fnlen = bytelen (fname);
  char *line = NULL;
  size_t len = 0;
  size_t nread = 0;
  int idx = 0;
  while (-1 isnot (int) (nread = ed_readline_from_fp (&line, &len, fp))) {
    idx++;
    int ret = My(Re).exec (re, line, nread);
    if (ret is RE_UNBALANCED_BRACKETS_ERROR) {
      MSG_ERRNO (RE_UNBALANCED_BRACKETS_ERROR);
      break;
    }
    if (ret is RE_NO_MATCH) continue;

    size_t blen = (fnlen) + 32 + nread;
    char bytes[blen + 1];
    snprintf (bytes, blen, "%s|%d col %d| %s", fname, idx, re->match_idx, line);
    buf_current_append_with (this, bytes);
    My(Re).reset_captures (re);
  }

  fclose (fp);
  if (line isnot NULL) free (line);
  return 0;
}

private int ved_grep (buf_t **thisp, char *pat, vstr_t *fnames) {
  buf_t *this = *thisp;
  int idx = 0;
  win_t *w = My(Ed).get.win_by_name ($my(root), VED_SEARCH_WIN, &idx);
  this = My(Win).get.buf_by_name (w, VED_SEARCH_BUF, &idx);
  if (this is NULL) return NOTHING_TODO;
  self(clear);
  My(String).replace_with_fmt ($mycur(data), "searching for %s", pat);
  int flags = 0;
  regexp_t *re = My(Re).new (pat, flags, RE_MAX_NUM_CAPTURES, My(Re).compile);

  vstring_t *it = fnames->head;
  char *dname = it is NULL ? NULL : it->data->bytes;

  while (it) {
    char *fname = it->data->bytes;
    if (file_exists (fname))
      ifnot (is_directory (fname))
        if (file_is_readable (fname))
          ved_search_file (this, fname, re);
    it = it->next;
  }

  ifnot (NULL is dname) {
    free ($my(cwd));
    if (*dname is '.' or *dname isnot DIR_SEP)
      $my(cwd) = dir_current ();
    else
      $my(cwd) = path_dirname (dname);
  }

  My(Re).free (re);

  if (this->num_items is 1) return NOTHING_TODO;

  self(set.video_first_row, 0);
  self(cur.set, 0);
  $my(video)->row_pos = $my(cur_video_row);
  $my(video)->col_pos = $my(cur_video_col);
  ved_normal_down (this, 1, DONOT_ADJUST_COL, DONOT_DRAW);
  ed_change_buf ($my(root), thisp, VED_SEARCH_WIN, VED_SEARCH_BUF);
  return DONE;
}

private int buf_substitute (buf_t *this, char *pat, char *sub, int global,
int interactive, int fidx, int lidx) {
  int retval = NOTHING_TODO;
  ifnot (this->num_items) return retval;

  string_t *substr = NULL;
  int flags = 0;
  regexp_t *re = My(Re).new (pat, flags, RE_MAX_NUM_CAPTURES, My(Re).compile);

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (action, act);

  row_t *it = this->head;
  int idx = 0;
  while (idx < fidx) {idx++; it = it->next;}

  while (idx++ <= lidx) {
    int bidx = 0;

searchandsub:;
    int done_substitution = 0;
    My(Re).reset_captures (re);

    if (0 > My(Re).exec (re, it->data->bytes + bidx,
        it->data->num_bytes - bidx)) goto thenext;

    ifnot (NULL is substr) string_free (substr);
    if (NULL is (substr = My(Re).parse_substitute (re, sub, re->match->bytes))) {
      MSG_ERROR("Error: %s", re->errmsg);
      goto theend;
    }

    if (interactive) {
      char prefix[bidx + re->match_idx + 1];
      memcpy (prefix, it->data->bytes, bidx + re->match_idx);
      prefix[bidx + re->match_idx] = '\0';
      utf8 chars[] = {'y', 'Y', 'n', 'N', 'q', 'Q', 'a', 'A', 'c', 'C'};
      char qu[MAXLINE]; /* using str_fmt (a statement expression) causes  */
                        /* messages for uninitialized value[s] (on clang) */
      snprintf (qu, MAXLINE,
        "|match at line %d byte idx %d|\n"
        "%s%s%s%s%s\n"
        "|substitution string|\n"
        "%s%s%s\n"
        "replace? yY[es]|nN[o] replace all?aA[ll], continue next line? cC[ontinue], quit? qQ[uit]\n",
         idx, re->match_idx + bidx, prefix, TERM_MAKE_COLOR(COLOR_MENU_SEL), re->match->bytes,
         TERM_MAKE_COLOR(COLOR_MENU_BG), re->match_ptr + re->match_len,
         TERM_MAKE_COLOR(COLOR_MENU_SEL), substr->bytes, TERM_MAKE_COLOR(COLOR_MENU_BG));

      utf8 c =  quest (this, qu, chars, ARRLEN(chars));

      switch (c) {
        case 'n': case 'N': goto if_global;
        case 'q': case 'Q': goto theend;
        case 'c': case 'C': goto thenext;
        case 'a': case 'A': interactive = 0;
      }
    }

    act = AllocType (act);
    vundo_set (act, REPLACE_LINE);
    act->idx = idx - 1;
    act->bytes = str_dup (it->data->bytes, it->data->num_bytes);
    stack_push (action, act);
    string_replace_numbytes_at_with (it->data, re->match_len, re->match_idx + bidx,
      substr->bytes);
    done_substitution = 1;

    retval = DONE;

if_global:
    if (global) {
      int len = (done_substitution ? (int) substr->num_bytes : 1);
      bidx += (re->match_idx + len);
      if (bidx >= (int) it->data->num_bytes) goto thenext;
      goto searchandsub;
    }

thenext:
    it = it->next;
  }

theend:
  if (retval is DONE) {
    $my(flags) |= BUF_IS_MODIFIED;
    vundo_push (this, action);
    if ($mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes)
      ved_normal_eol (this);
    self(draw);
  } else
    buf_free_action (this, action);

  My(Re).free (re);
  ifnot (NULL is substr) string_free (substr);

  return retval;
}

private FILE *ed_file_pointer_from_X (ed_t *this, int target) {
  if (NULL is $my(env)->xclip_exec) return NULL;
  if (NULL is getenv ("DISPLAY")) return NULL;
  size_t len = $my(env)->xclip_exec->num_bytes + 32;
  char command[len];
  snprintf (command, len, "%s -o -selection %s", $my(env)->xclip_exec->bytes,
      (target is X_PRIMARY) ? "primary" : "clipboard");

  return popen (command, "r");
}

private void ed_selection_to_X (ed_t *this, char *bytes, size_t len, int target) {
  if (NULL is $my(env)->xclip_exec) return;
  if (NULL is getenv ("DISPLAY")) return;

  size_t ex_len = $my(env)->xclip_exec->num_bytes + 32;
  char command[ex_len + 32];
  snprintf (command, ex_len + 32, "%s -i -selection %s 2>/dev/null",
    $my(env)->xclip_exec->bytes, (target is X_PRIMARY) ? "primary" : "clipboard");

  FILE *fp = popen (command, "w");
  if (NULL is fp) return;
  fwrite (bytes, 1, len, fp);
  pclose (fp);
}

private int ed_selection_to_X_word_actions_cb (buf_t **thisp, int fidx, int lidx,
                                bufiter_t *it, char *word, utf8 c, char *action) {
  (void) it; (void) action;
  buf_t *this = *thisp;
  ed_selection_to_X ($my(root), word, lidx - fidx + 1,
  	('*' is c ? X_PRIMARY : X_CLIPBOARD));
  return DONE;
}

private void ed_register_free (ed_t *this, rg_t *rg) {
  reg_t *reg = rg->head;
  while (reg) {
    reg_t *tmp = reg->next;
    My(String).free (reg->data);
    free (reg);
    reg = tmp;
  }

  rg->head = NULL;
}

private rg_t *ed_register_new (ed_t *this, int regidx) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];
  rg_t *rg = &$my(regs)[regidx];
  self(free_reg, rg);
  return rg;
}

private void ed_register_init_all (ed_t *this) {
  for (int i = 0; i < NUM_REGISTERS; i++)
    $my(regs)[i] = (rg_t) {.reg = REGISTERS[i]};

  $my(regs)[REG_RDONLY].head = AllocType (reg);
  $my(regs)[REG_RDONLY].head->data = My(String).new_with ("     ");
  $my(regs)[REG_RDONLY].head->next = NULL;
  $my(regs)[REG_RDONLY].head->prev = NULL;
}

private int ed_register_get_idx (ed_t *this, int c) {
  (void) this;
  if (c is 0x17) c = '^';
  char regs[] = REGISTERS; /* this is for tcc */
  char *r = byte_in_str (regs, c);
  return (NULL isnot r) ? (r - regs) : -1;
}

private rg_t *ed_register_push (ed_t *this, int regidx, int type, reg_t *reg) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];
  rg_t *rg = &$my(regs)[regidx];
  rg->reg = regidx;
  rg->type = type;
  stack_push (rg, reg);
  return rg;
}

private rg_t *ed_register_push_r (ed_t *this, int regidx, int type, reg_t *reg) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];
  rg_t *rg = &$my(regs)[regidx];
  reg_t *it = rg->head;
  if (it is NULL)
    return ed_register_push (this, regidx, type, reg);

  rg->reg = regidx;
  rg->type = type;

  while (it) {
    if (it->next is NULL) break;
    it = it->next;
  }

  reg->prev = it;
  reg->next = NULL;
  it->next = reg;

  return rg;
}

private rg_t *ed_register_push_with (ed_t *this, int regidx, int type, char *bytes, int dir) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];
  reg_t *reg = AllocType (reg);
  reg->data = My(String).new_with (bytes);
  if (dir is REVERSE_ORDER)
    return ed_register_push_r (this, regidx, type, reg);
  return ed_register_push (this, regidx, type, reg);
}

private int ed_register_special_set (ed_t *this, buf_t *buf, int regidx) {
  if (REG_SEARCH > regidx or (regidx > REG_EXPR and regidx < REG_CURWORD))
    return NOTHING_TODO;

  switch (regidx) {
    case REG_SEARCH:
      {
        histitem_t *his = $my(history)->search->head;
        if (NULL is his) return NOTOK;
        ed_register_push_with (this, regidx, CHARWISE, his->data->bytes, DEFAULT_ORDER);
        return DONE;
      }

    case REG_PROMPT:
      if ($my(history)->rline->num_items is 0) return NOTOK;
      {
        h_rlineitem_t *it = $my(history)->rline->head;
        if (NULL is it) return ERROR;
        string_t *str = vstr_join (it->data->line, "");
        ed_register_push_with (this, regidx, CHARWISE, str->bytes, DEFAULT_ORDER);

        string_free (str);
        return DONE;
      }

    case REG_FNAME:
      ed_register_new (this, regidx);
      ed_register_push_with (this, regidx, CHARWISE, $from(buf, fname), DEFAULT_ORDER);
      return DONE;

    case REG_PLUS:
    case REG_STAR:
      {
        FILE *fp = ed_file_pointer_from_X (this, (REG_STAR is regidx) ? X_PRIMARY : X_CLIPBOARD);
        if (NULL is fp) return ERROR;
        ed_register_new (this, regidx);
        size_t len = 0;
        char *line = NULL;

        /* while (-1 isnot ed_readline_from_fp (&line, &len, fp)) { */
        /* do it by hand to look for new lines and proper set the type */
        ssize_t nread;
        int type = CHARWISE;
        while (-1 isnot (nread = getline (&line, &len, fp))) {
          if (nread) {
            if (line[nread - 1] is '\n' or line[nread - 1] is '\r') {
              line[nread - 1] = '\0';
              type = LINEWISE;
            }

            ed_register_push_with (this, regidx, type, line, REVERSE_ORDER);
            //ed_register_push_with (this, regidx, type, line, DEFAULT_ORDER);
          }
        }

        if (line isnot NULL) free (line);
        pclose (fp);
        return DONE;
      }

    case REG_CURWORD:
      {
        char word[MAXWORD]; int fidx, lidx;
        ifnot (NULL is get_current_word (buf, word, Notword, Notword_len,
            &fidx, &lidx)) {
          ed_register_new (this, regidx);
          ed_register_push_with (this, regidx, CHARWISE, word, DEFAULT_ORDER);
        } else
          return ERROR;
      }
      return DONE;

    case REG_EXPR:
    case REG_BLACKHOLE:
      return NOTOK;
  }

  return NOTHING_TODO;
}

/*
private rg_t *ed_register_set (ed_t *this, int regidx, int type, reg_t *reg) {
  ed_register_new (this, regidx);
  return ed_register_push (this, regidx, type, reg);
}
*/

private rg_t *ed_register_set_with (ed_t *this, int regidx, int type, char *bytes, int dir) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];

  ed_register_new (this, regidx);
  reg_t *reg = AllocType (reg);
  reg->data = My(String).new_with (bytes);
  if (-1 is dir)
    return ed_register_push_r (this, regidx, type, reg);
  return ed_register_push (this, regidx, type, reg);
}

private utf8 quest (buf_t *this, char *qu, utf8 *chs, int len) {
  video_paint_rows_with ($my(video), -1, -1, -1, qu);
  SEND_ESC_SEQ ($my(video)->fd, TERM_CURSOR_HIDE);
  utf8 c;
  for (;;) {
    c = My(Input).get ($my(term_ptr));
    for (int i = 0; i < len; i++)
      if (chs[i] is c) goto theend;
  }

theend:
  video_resume_painted_rows ($my(video));
  SEND_ESC_SEQ ($my(video)->fd, TERM_CURSOR_SHOW);
  return c;
}

private utf8 ved_question (ed_t *this, char *qu, utf8 *chs, int len) {
  return quest (this->current->current, qu, chs, len);
}

private char *buf_parse_line (buf_t *this, row_t *row, char *line, int idx) {
  ustring_encode ($my(line), row->data->bytes, row->data->num_bytes,
      CLEAR, $my(ftype)->tabwidth, row->first_col_idx);

  int numchars = 0;
  int j = 0;

  vchar_t *it = $my(line)->current;
  vchar_t *tmp = it;
  while (it and numchars < $my(dim)->num_cols) {
    for (int i = 0; i < it->len; i++) line[j++] = it->buf[i];
    numchars += it->width;
    tmp = it;
    it = it->next;
  }

  it = tmp;
  while (numchars > $my(dim)->num_cols) {
    j -= it->len;
    numchars -= it->width;
    it = it->prev;
  }

  line[j] = '\0';
  return $my(syn)->parse (this, line, j, idx, row);
}

private void buf_draw_cur_row (buf_t *this) {
  char line[MAXLINE];
  buf_parse_line (this, this->current, line, this->cur_idx);
  My(Video).set_with ($my(video), $my(video)->row_pos - 1, line);
  My(Video).Draw.row_at ($my(video), $my(video)->row_pos);
  buf_set_draw_statusline (this);
  My(Cursor).set_pos ($my(term_ptr), $my(video)->row_pos, $my(video)->col_pos);
}

private void buf_to_video (buf_t *this) {
  row_t *row = $my(video_first_row);
  int idx = $my(video_first_row_idx);
  char line[MAXLINE];

  int i;
  for (i = $my(dim)->first_row - 1; i < $my(statusline_row) - 1; i++) {
    if (row is NULL) break;
    buf_parse_line (this, row, line, idx++);
    My(Video).set_with ($my(video), i, line);
    row = row->next;
  }

  while (i < $my(statusline_row) - 1)
    My(Video).set_with ($my(video), i++, $my(ftype)->on_emptyline);

  buf_set_statusline (this);
}

private void buf_flush (buf_t *this) {
  video_render_set_from_to ($my(video), $my(dim)->first_row, $my(statusline_row));
  My(String).append_fmt ($my(video)->render, TERM_GOTO_PTR_POS_FMT,
      $my(video)->row_pos, $my(video)->col_pos);
  video_flush ($my(video), $my(video)->render);
}

private void buf_draw (buf_t *this) {
  My(String).clear ($my(video)->render);
  My(Ed).set.topline (this);
  video_render_set_from_to ($my(video), 1, 1);
  self(to.video);
  self(flush);
}

private int ved_diff (buf_t **thisp, int to_stdout) {
#if (HAS_SHELL_COMMANDS == 0)
  return NOTOK;
#endif
  buf_t *this = *thisp;
  if (NULL is $myroots(env)->diff_exec) {
    My(Msg).error ($my(root), "diff executable can not be found in $PATH");
    return NOTOK;
  }

  ifnot (file_exists ($my(fname))) return NOTOK;
  tmpname_t *tmpn = tmpfname ($myroots(env)->tmp_dir->bytes, $my(basename));
  if (NULL is tmpn or -1 is tmpn->fd) return NOTOK;
  ved_write_to_fname (this, tmpn->fname->bytes, DONOT_APPEND, 0, this->num_items - 1, FORCE, VERBOSE_OFF);
  size_t len = $myroots(env)->diff_exec->num_bytes + tmpn->fname->num_bytes +
       bytelen ($my(fname)) + 6;
  char com[len];
  snprintf (com, len, "%s -u %s %s", $myroots(env)->diff_exec->bytes, tmpn->fname->bytes,
    $my(fname));
  com[len] = '\0';

  int retval = NOTHING_TODO;
  if (to_stdout)
    retval = My(Ed).sh.popen ($my(root), this, com, 0, 0, NULL);
  else {
    this = My(Ed).buf.get ($my(root), VED_DIFF_WIN, VED_DIFF_BUF);
    if (this) {
      self(clear);
      retval = My(Ed).sh.popen ($my(root), this, com, 1, 0, NULL);
      retval = (retval == 1 ? OK : (retval == 0 ? 1 : retval));
      ifnot (retval) {     // diff returns 1 when files differ
        My(Ed).buf.change ($my(root), thisp, VED_DIFF_WIN, VED_DIFF_BUF);
        retval = OK;
      }
    }
  }

  tmpfname_free (tmpn);

  return retval;
}

private int ved_quit (ed_t *ed, int force) {
  int retval = EXIT;
  if (force) return retval;

  win_t *w = ed->head;

 while (w) {
    if ($from(w, type) is VED_WIN_SPECIAL_TYPE) goto winnext;

    buf_t *this = w->head;
    while (this isnot NULL) {
      if ($my(flags) & BUF_IS_SPECIAL) goto bufnext;
      ifnot ($my(flags) & BUF_IS_MODIFIED) goto bufnext;
      utf8 chars[] = {'y', 'Y', 'n', 'N', 'c', 'C','d'};
thequest:;
      utf8 c = quest (this, str_fmt (
         "%s has been modified since last change\n"
         "continue writing? [yY|nN], [cC]ansel, unified [d]iff?",
         $my(fname)), chars, ARRLEN (chars));
      switch (c) {
        case 'y':
        case 'Y':
          self(write, FORCE);
        case 'n':
        case 'N':
          break;
        case 'c':
        case 'C':
          retval = NOTHING_TODO;
          goto theend;
        case 'd':
          ved_diff (&this, 1);
          goto thequest;
      }

bufnext:
      this = this->next;
    }

winnext:
    w = w->next;
  }

theend:
  return retval;
}

private void ved_on_blankline (buf_t *this) {
  ifnot ($my(ftype)->clear_blanklines) return;

  int lineisblank = 1;
  for (int i = 0; i < (int) $mycur(data)->num_bytes; i++) {
    if ($mycur(data)->bytes[i] isnot ' ') {
      lineisblank = 0;
      break;
    }
  }

  if (lineisblank) {
    My(String).replace_with ($mycur(data), "");
    $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
    $mycur(cur_col_idx) = $mycur(first_col_idx) = 0;
  }
}

private int ved_normal_right (buf_t *this, int count, int draw) {
  int is_ins_mode = IS_MODE (INSERT_MODE);
  if ($mycur(cur_col_idx) is ((int) $mycur(data)->num_bytes -
      ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]) +
      is_ins_mode) or 0 is $mycur(data)->num_bytes or
      $mycur(data)->bytes[$mycur(cur_col_idx)] is 0 or
      $mycur(data)->bytes[$mycur(cur_col_idx)] is '\n')
    return NOTHING_TODO;

  ustring_encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
      CLEAR, $my(ftype)->tabwidth, $mycur(cur_col_idx));

  vchar_t *it = buf_get_line_nth ($my(line), $mycur(cur_col_idx));

  while (count-- and it) {
    if (it->code is '\n' or it is $my(line)->tail) break;

    $mycur(cur_col_idx) += it->len;

    if ($my(video)->col_pos is $my(dim)->num_cols) {
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
      $mycur(first_col_idx) = $mycur(cur_col_idx);
      ifnot (is_ins_mode)
        if (it->next)
           $my(video)->col_pos = $my(cur_video_col) =
               $my(video)->col_pos + (it->next->width - 1);
    } else {
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->col_pos + 1;
      ifnot (is_ins_mode) {
        if (it->next) {
           $my(video)->col_pos = $my(cur_video_col) =
               $my(video)->col_pos + (it->next->width - 1);

          if ($my(video)->col_pos > $my(dim)->num_cols) {
            $my(video)->col_pos = $my(cur_video_col) = it->next->width;
            $mycur(first_col_idx) = $mycur(cur_col_idx);
          }
        }
      }

      if (is_ins_mode)
        $my(video)->col_pos = $my(cur_video_col) =
             $my(video)->col_pos + (it->width - 1);
    }
    it = it->next;
  }

  if (draw) self(draw_cur_row);
  return DONE;
}

private int ved_normal_noblnk (buf_t *this) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  if ($mycur(cur_col_idx) is 0 and 0 is IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)]))
    return NOTHING_TODO;

  $mycur(cur_col_idx) = 0;
  $my(video)->col_pos = $my(cur_video_col) =
      char_utf8_width ($mycur(data)->bytes, $my(ftype)->tabwidth);

  int i = 0;
  for (; i < (int) $mycur(data)->num_bytes; i++) {
    if (IS_SPACE ($mycur(data)->bytes[i])) {
      ved_normal_right (this, 1, DONOT_DRAW);
      continue;
    }

    break;
  }

  if (i) self(draw);

  return DONE;
}

private int ved_normal_eol (buf_t *this) {
  if ($mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes)
    ved_normal_bol (this);
  return ved_normal_right (this, $mycur(data)->num_bytes * 4, DRAW);
}

private int ved_normal_left (buf_t *this, int count, int draw) {
  int is_ins_mode = IS_MODE (INSERT_MODE);
  if ($mycur(cur_col_idx) is 0) {
    if ($my(cur_video_col) isnot 1 and $mycur(data)->bytes[0] is 0)
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
    return NOTHING_TODO;
  }

  ustring_encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
      CLEAR, $my(ftype)->tabwidth, $mycur(cur_col_idx));

  vchar_t *it = buf_get_line_nth ($my(line), $mycur(cur_col_idx));

  if (it is NULL) {
      it = $my(line)->tail;
  } else     /* otherwise it->prev is null and pass the loop */
    ifnot ($my(line)->num_items is 1)  // and is_ins_mode)
      it = it->prev;

  int curcol = $mycur(first_col_idx);

  while (it and count-- and $mycur(cur_col_idx)) {
    int len = it->len;
    $mycur(cur_col_idx) -= len;
     vchar_t *fcol = buf_get_line_nth ($my(line), $mycur(first_col_idx));

    if ($my(cur_video_col) is 1 or $my(cur_video_col) - fcol->width is 0) {
      ifnot ($mycur(first_col_idx)) return NOTHING_TODO;

      int num = 0;
      vchar_t *tmp = NULL; // can not be NULL
      while ($mycur(first_col_idx) and fcol and num < $my(dim)->num_cols) {
        $mycur(first_col_idx) -= fcol->len;
        num += fcol->width;
        tmp = fcol;
        fcol = fcol->prev;
      }

      if (num > $my(dim)->num_cols) {
        $mycur(first_col_idx) += tmp->len;
        num -= tmp->width;
      }

      $my(video)->col_pos = $my(cur_video_col) = num;

      ifnot (is_ins_mode)
        $my(video)->col_pos = $my(cur_video_col) =
            $my(video)->col_pos + (it->width - 1);

      if (is_ins_mode and 0 is $mycur(first_col_idx))
        $my(video)->col_pos = $my(cur_video_col) =
            $my(video)->col_pos - (tmp->width) + 1;
    } else {
      int width = $my(cur_video_col) - 1;
      if (is_ins_mode)
        width -= it->width - 1;
      else
        if (it->next) width -= (it->next->width) - 1;

      ifnot (width) {
        width = 1;
        $mycur(first_col_idx) -= it->len;
      }

      $my(video)->col_pos = $my(cur_video_col) = width;
    }

    it = it->prev;
  }

  ifnot (draw) return DONE;

  if ($mycur(first_col_idx) isnot curcol) {
    self(draw_cur_row);
  } else {
    buf_set_draw_statusline (this);
    My(Cursor).set_pos ($my(term_ptr), $my(video)->row_pos, $my(video)->col_pos);
  }

  return DONE;
}

private int ved_normal_bol (buf_t *this) {
  ifnot ($mycur(cur_col_idx)) return NOTHING_TODO;
  $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;
  $my(video)->col_pos = $my(cur_video_col) =
      char_utf8_width ($mycur(data)->bytes, $my(ftype)->tabwidth);

  self(draw_cur_row);
  return DONE;
}

private int ved_normal_end_word (buf_t **thisp, int count, int run_insert_mode, int draw) {
  buf_t *this = *thisp;
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  int cur_idx = $mycur(cur_col_idx);
  int retval = NOTHING_TODO;
  for (int i = 0; i < count; i++) {
    while (($mycur(cur_col_idx) isnot (int) $mycur(data)->num_bytes - 1) and
          (0 is (IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)]))))
      retval = ved_normal_right (this, 1, DONOT_DRAW);

    if (NOTHING_TODO is retval) break;
    if (NOTHING_TODO is (retval = ved_normal_right (this, 1, DONOT_DRAW))) break;
  }

  if (cur_idx is $mycur(cur_col_idx)) return NOTHING_TODO;
  if (retval is DONE) ved_normal_left (this, 1, DONOT_DRAW);
  if (run_insert_mode) {
    self(draw);
    return ved_insert (&this, 'i');
  }

  if (IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)]))
    ved_normal_left (this, 1, DONOT_DRAW);

  if (draw) self(draw);
  return DONE;
}

#define THIS_LINE_PTR_IS_AT_NTH_POS                                          \
({                                                                           \
  ustring_encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,   \
      CLEAR, $my(ftype)->tabwidth, $mycur(cur_col_idx) -                     \
      ($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes and              \
       IS_MODE(INSERT_MODE) ? 1 : 0));                                       \
  int nth__ = $my(line)->cur_idx + 1;                                        \
  $my(prev_nth_ptr_pos) = nth__;                                             \
  $my(prev_num_items) = $my(line)->num_items;                                \
  do {                                                                       \
    if (0 is $mycur(data)->num_bytes or                                      \
        0 is $mycur(data)->bytes[0] or                                       \
        '\n' is $mycur(data)->bytes[0]) {                                    \
       nth__ = $my(nth_ptr_pos);                                             \
       break;                                                                \
     }                                                                       \
     if ($my(line)->num_items is 1)                                          \
       nth__ = $my(nth_ptr_pos);                                             \
     else if ($my(line)->current is $my(line)->tail)                         \
       $my(state) |= PTR_IS_AT_EOL;                                          \
     else                                                                    \
       $my(state) &= ~PTR_IS_AT_EOL;                                         \
     $my(nth_ptr_pos) = nth__;                                               \
  } while (0);                                                               \
  nth__;                                                                     \
})

private int ved_normal_up (buf_t *this, int count, int adjust_col, int draw) {
  int currow_idx = this->cur_idx;

  if (0 is currow_idx or 0 is count) return NOTHING_TODO;
  if (count > currow_idx) count = currow_idx;

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  ved_on_blankline (this);

  currow_idx -= count;
  self(cur.set, currow_idx);

  int col_pos = adjust_col ? ved_buf_adjust_col (this, nth, isatend) : $my(video)->first_col;
  int row = $my(video)->row_pos;
  int orig_count = count;

  if (row > count)
    while (count && row - count >= $my(dim)->first_row)
      count--;

  ifnot (adjust_col) $mycur(cur_col_idx) = 0;

  if (count) {
    if (count <= $my(video_first_row_idx)) {
      self(set.video_first_row, $my(video_first_row_idx) - count);
      $my(video)->col_pos = $my(cur_video_col) = col_pos;
    } else {
      self(set.video_first_row, currow_idx);
      $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
      $my(video)->col_pos = $my(cur_video_col) = col_pos;
    }
  } else {
    $my(video)->row_pos = $my(cur_video_row) = row - orig_count;
    $my(video)->col_pos = $my(cur_video_col) = col_pos;
    if (draw) self(draw_cur_row);
    return DONE;
  }

  if (draw) self(draw);
  return DONE;
}

private int ved_normal_down (buf_t *this, int count, int adjust_col, int draw) {
  int currow_idx = this->cur_idx;
  if (this->num_items - 1 is currow_idx)
    return NOTHING_TODO;

  if (count + currow_idx >= this->num_items)
    count = this->num_items - currow_idx - 1;

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  ved_on_blankline (this);

  currow_idx += count;

  self(cur.set, currow_idx);

  int col_pos = adjust_col ? ved_buf_adjust_col (this, nth, isatend) : $my(video)->first_col;

  int row = $my(video)->row_pos;
  int orig_count = count;

  while (count && count + row < $my(statusline_row)) count--;

  ifnot (adjust_col) $mycur(cur_col_idx) = 0;

  if (count) {
    self(set.video_first_row, $my(video_first_row_idx) + count);
    $my(video)->col_pos = $my(cur_video_col) = col_pos;
  } else {
    $my(video)->row_pos = $my(cur_video_row) = row + orig_count;
    $my(video)->col_pos = $my(cur_video_col) = col_pos;
    if (draw) self(draw_cur_row);
    return DONE;
  }

  if (draw) self(draw);
  return DONE;
}

private int ved_normal_page_down (buf_t *this, int count) {
  if (this->num_items < ONE_PAGE
      or this->num_items - $my(video_first_row_idx) < ONE_PAGE + 1)
    return NOTHING_TODO;

  mark_set (this, MARK_UNAMED);

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  int row = $my(video)->row_pos;

  int frow = $my(video_first_row_idx);
  int currow_idx = this->cur_idx;

  while (count--) {
    frow += ONE_PAGE; currow_idx += ONE_PAGE;
    if (frow >= this->num_items or currow_idx >= this->num_items) {
      frow = this->num_items - ONE_PAGE;
      row = $my(dim->first_row) + 1;
      currow_idx = frow + 1;
      break;
      }
  }

  ved_on_blankline (this);

  self(cur.set, currow_idx);
  self(set.video_first_row, frow);
  $my(video)->row_pos = $my(cur_video_row) = row;
  $my(video)->col_pos = $my(cur_video_col) = ved_buf_adjust_col (this, nth, isatend);
  self(draw);
  return DONE;
}

private int ved_normal_page_up (buf_t *this, int count) {
  if ($my(video_first_row_idx) is 0 or this->num_items < ONE_PAGE)
    return NOTHING_TODO;

  mark_set (this, MARK_UNAMED);

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  int row = $my(video)->row_pos;
  int frow = $my(video_first_row_idx);
  int curlnr = this->cur_idx;

  while (count--) {
    frow -= ONE_PAGE;
    if (frow <= 0) {
      curlnr = 0 > frow ? 0 : curlnr - ONE_PAGE;
      row = frow < 0 ? $my(dim)->first_row : row;
      frow = 0;
      break;
      }

    curlnr -= ONE_PAGE;
  }

  ved_on_blankline (this);

  self(cur.set, curlnr);
  self(set.video_first_row, frow);
  $my(video)->row_pos = $my(cur_video_row) = row;
  $my(video)->col_pos = $my(cur_video_col) = ved_buf_adjust_col (this, nth, isatend);

  self(draw);
  return DONE;
}

private int ved_normal_bof (buf_t *this, int draw) {
  if (this->cur_idx is 0) return NOTHING_TODO;

  mark_set (this, MARK_UNAMED);

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  ved_on_blankline (this);

  self(set.video_first_row, 0);
  self(cur.set, 0);

  $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
  $my(video)->col_pos = $my(cur_video_col) = ved_buf_adjust_col (this, nth, isatend);

  if (draw) self(draw);
  return DONE;
}

private int ved_normal_eof (buf_t *this, int draw) {
  if ($my(video_first_row_idx) is this->num_items - 1) {
    ifnot (draw)
      return NOTHING_TODO;
    else
      goto draw;
  }

  mark_set (this, MARK_UNAMED);

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  ved_on_blankline (this);

  self(cur.set, this->num_items - 1);

  $my(video)->col_pos = $my(cur_video_col) = ved_buf_adjust_col (this, nth, isatend);

  if (this->num_items < ONE_PAGE) {
    $my(video)->row_pos = $my(cur_video_row) =
        ($my(dim)->first_row + this->num_items) - 1;
    if (draw) goto draw;
    buf_set_draw_statusline (this);
    My(Cursor).set_pos ($my(term_ptr), $my(video)->row_pos, $my(video)->col_pos);
    return DONE;
  } else {
    self(set.video_first_row, this->num_items - (ONE_PAGE));
    $my(video)->row_pos = $my(cur_video_row) = $my(statusline_row) - 1;
  }

draw:
  self(draw);
  return DONE;
}

private int ved_normal_goto_linenr (buf_t *this, int lnr, int draw) {
  int currow_idx = this->cur_idx;

  if (lnr <= 0 or lnr is currow_idx + 1 or lnr > this->num_items)
    return NOTHING_TODO;

  mark_set (this, MARK_UNAMED);

  if (lnr < currow_idx + 1)
    return ved_normal_up (this, currow_idx - lnr + 1, ADJUST_COL, draw);

  return ved_normal_down (this, lnr - currow_idx - 1, ADJUST_COL, draw);
}

private int ved_normal_replace_char (buf_t *this) {
  if ($mycur(data)->num_bytes is 0) return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (action, act);
  vundo_push (this, action);

  utf8 c = My(Input).get ($my(term_ptr));
  char buf[5]; int len;
  ustring_character (c, buf, &len);
  int clen = ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]);
  My(String).replace_numbytes_at_with ($mycur(data), clen,
    $mycur(cur_col_idx), buf);

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_normal_delete_eol (buf_t *this, int regidx) {
  int clen = ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]);
  if ($mycur(data)->num_bytes is 0)
    // or $mycur(cur_col_idx) is (int) $mycur(data)->num_bytes - clen)
    return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (action, act);
  vundo_push (this, action);

  int len = $mycur(data)->num_bytes - $mycur(cur_col_idx);
  char buf[len + 1];
  memcpy (buf, $mycur(data)->bytes + $mycur(cur_col_idx), len);

  buf[len] = '\0';
  ed_register_set_with ($my(root), regidx, CHARWISE, buf, 0);

  My(String).delete_numbytes_at ($mycur(data),
     $mycur(data)->num_bytes - $mycur(cur_col_idx), $mycur(cur_col_idx));

  $mycur(cur_col_idx) -= clen;
  if ($mycur(cur_col_idx) is 0)
    $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
  else
    if (0 is $mycur(first_col_idx))
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->col_pos - 1;
    else {
      if (0 is $mycur(first_col_idx) - clen)
        $mycur(first_col_idx) -= clen;
      else
        $my(video)->col_pos = $my(cur_video_col) = $my(video)->col_pos - 1;
    }

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_insert_new_line (buf_t **thisp, utf8 com) {
  buf_t *this = *thisp;
  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, INSERT_LINE);

  int new_idx = this->cur_idx + ('o' is com ? 1 : -1);
  int currow_idx = this->cur_idx;

  if ('o' is com) {
    self(cur.append_with, " ");
    My(String).clear ($mycur(data));
    act->idx = this->cur_idx;
    this->current = this->current->prev;
    this->cur_idx--;
    ved_normal_down (this, 1, DONOT_ADJUST_COL, DONOT_DRAW);
  } else {
    self(cur.prepend_with, " ");
    My(String).clear ($mycur(data));
    act->idx = this->cur_idx;

    if ($my(video_first_row_idx) is this->cur_idx and 0 is this->cur_idx) {
      $my(video_first_row_idx)--;
      $my(video_first_row) = $my(video_first_row)->prev;
    }

    this->current = this->current->next;
    this->cur_idx++;
    if ($my(video)->row_pos isnot $my(dim)->first_row)
      $my(video)->row_pos = $my(cur_video_row) = $my(video)->row_pos + 1;

    ved_normal_up (this, 1, ADJUST_COL, DONOT_DRAW);
  }

  stack_push (action, act);
  vundo_push (this, action);

  if (currow_idx > new_idx) {int t = new_idx; new_idx = currow_idx; currow_idx = t;}
  buf_adjust_marks (this, INSERT_LINE, currow_idx, new_idx);

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw);
  return ved_insert (thisp, com);
}

private int ved_join (buf_t *this) {
  if (this->num_items is 0 or this->num_items - 1 is this->cur_idx)
    return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (action, act);

  row_t *row = buf_current_pop_next (this);
  act_t *act2 = AllocType (act);
  vundo_set (act2, DELETE_LINE);
  act2->idx = this->cur_idx + 1;
  act2->bytes = str_dup (row->data->bytes, row->data->num_bytes);

  stack_push (action, act2);
  vundo_push (this, action);

  if (row->data->num_bytes isnot 0) {
    RM_TRAILING_NEW_LINE;
    char *sp = row->data->bytes;
    if (*sp is ' ') { /* MAYBE string_trim_beg() */
      while (*++sp and *sp is ' '); /* at least a space */
      My(String).delete_numbytes_at (row->data, (sp - row->data->bytes) - 1, 1);
    }
    My(String).append ($mycur(data), row->data->bytes);
  }

  buf_adjust_marks (this, DELETE_LINE, this->cur_idx, this->cur_idx + 1);
  $my(flags) |= BUF_IS_MODIFIED;
  self(free.row, row);
  self(draw);
  return DONE;
}

private int ved_normal_delete (buf_t *this, int count, int regidx) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  action_t *action = NULL;  act_t *act = NULL;

  int is_norm_mode = IS_MODE (NORMAL_MODE);
  int perfom_undo = is_norm_mode or IS_MODE (VISUAL_MODE_CW) or IS_MODE (VISUAL_MODE_BW);

  if (perfom_undo) {
    action = AllocType (action);
    act = AllocType (act);
    vundo_set (act, REPLACE_LINE);
    act->idx = this->cur_idx;
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  }

  int clen = ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]);
  int len = 0;

  int calc_width = perfom_undo;
  int width = 0;

  while (count--) {
    len += clen;
    if ($mycur(cur_col_idx) + clen is (int) $mycur(data)->num_bytes)
      break;

    if (calc_width) {
      int lwidth = char_utf8_width ($mycur(data)->bytes + $mycur(cur_col_idx),
        $my(ftype)->tabwidth);
      if (lwidth > 1) width += lwidth - 1;
    }

    clen = ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx) + len]);
  }

  char buf[len + 1];
  memcpy (buf, $mycur(data)->bytes + $mycur(cur_col_idx), len);
  buf[len] = '\0';
  My(String).delete_numbytes_at ($mycur(data), len, $mycur(cur_col_idx));

  if (calc_width and 0 isnot width)
    $my(video)->col_pos = $my(cur_video_col) =
      $my(cur_video_col) - width;

  if ($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes + (0 is is_norm_mode))
    ved_normal_left (this, 1, DONOT_DRAW);
  else {
    int lwidth = char_utf8_width ($mycur(data)->bytes + $mycur(cur_col_idx),
        $my(ftype)->tabwidth);
    if (lwidth > 1) {
      if (lwidth + $my(cur_video_col) > $my(dim)->num_cols) {
        $mycur(first_col_idx) = $mycur(cur_col_idx);
        $my(video)->col_pos = $my(cur_video_col) = lwidth;
      } else
        $my(video)->col_pos = $my(cur_video_col) =
         $my(cur_video_col) + lwidth - 1;
    }
  }

  if (perfom_undo) {
    act->cur_col_idx = $mycur(cur_col_idx);
    act->first_col_idx = $mycur(first_col_idx);

    stack_push (action, act);
    vundo_push (this, action);
    ed_register_set_with ($my(root), regidx, CHARWISE, buf, 0);
  }

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_inc_dec_char (buf_t *this, int count, utf8 com) {
  utf8 c = CUR_UTF8_CODE;
  if (CTRL('a') is com)
    c += count;
  else
    c -= count;

  /* this needs accuracy, so dont give that illusion and be strict to what you know */
  if (c < ' ' or (c > 126 and c < 161) or (c > 254 and c < 902) or c > 974)
    return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->num_bytes = $mycur(data)->num_bytes;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  action = stack_push (action, act);
  vundo_push (this, action);

  char ch[5]; int len; ustring_character (c, ch, &len);
  int clen = ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]);
  My(String).replace_numbytes_at_with ($mycur(data), clen, $mycur(cur_col_idx), ch);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_word_math (buf_t *this, int count, utf8 com) {
  int fidx = 0;

  string_t *word = get_current_number (this, &fidx);
  if (NULL is word) return ved_inc_dec_char (this, count, com);

  int nr = 0;
  char *p = word->bytes;
  int type = *p++;

  if (type is 'd') nr = atoi (p);
  else if (type is 'o')  nr = strtol (p, NULL, 8);
  else nr = strtol (p, NULL, 16);

  if (CTRL('a') is com) nr += count;
  else nr -= count;

  char new[32]; new[0] = '\0';
  if (type is 'd')
    itoa (nr, new, 10);
  else {
    char s[16];
    if (type is 'o') itoa (nr, s, 8);
    else itoa (nr, s, 16);
    snprintf (new, 32, "0%s%s", ('x' is type ? "x" : ""), s);
  }

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->num_bytes = $mycur(data)->num_bytes;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  action = stack_push (action, act);
  vundo_push (this, action);

  My(String).replace_numbytes_at_with ($mycur(data), word->num_bytes - 1, fidx, new);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);

  string_free (word);
  return DONE;
}

private int ved_complete_word_callback (menu_t *menu) {
  buf_t *this = menu->this;

  int idx = 0;
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;

    idx = $mycur(cur_col_idx) - ($mycur(cur_col_idx) isnot 0);

    while (idx >= 0 and (
         IS_SPACE ($mycur(data)->bytes[idx]) is 0 and
         IS_CNTRL ($mycur(data)->bytes[idx]) is 0 and
         NULL is memchr (Notword, $mycur(data)->bytes[idx], Notword_len)))
      idx--;

    $my(shared_int) = idx + 1;

    idx++;

    menu->pat[0] = '\0';
    menu->patlen = 0;

    while (idx < $mycur(cur_col_idx)) {
      menu->pat[menu->patlen++] = $mycur(data)->bytes[idx];
      idx++;
    }

    while ($mycur(data)->bytes[idx]) {
      if (IS_SPACE ($mycur(data)->bytes[idx]) or
          NULL isnot memchr (Notword, $mycur(data)->bytes[idx], Notword_len))
        break;
      menu->pat[menu->patlen++] = $mycur(data)->bytes[idx];
      idx++;
    }

    menu->pat[menu->patlen] = '\0';
  } else
    menu_free_list (menu);

  vstr_t *words = vstr_new ();

  row_t *row = this->head;
  idx = -1;

  while (row isnot NULL) {
    if (row->data->bytes and ++idx isnot this->cur_idx) {
      char *p = strstr (row->data->bytes, menu->pat);
      if (NULL isnot p) {
        char word[MAXWORDLEN];
        int lidx = 0;
        while (*p and (
            IS_SPACE (*p) is 0 and
            IS_CNTRL (*p) is 0 and
            NULL is memchr (Notword, *p, Notword_len))) {
          word[lidx++] = *p++;
        }

        word[lidx] = '\0';
        vstr_add_sort_and_uniq (words, word);
      }
    }
    row = row->next;
  }

  ifnot (words->num_items) {
    free (words);
    menu->state |= MENU_QUIT;
    return NOTHING_TODO;
  }

  menu->list = words;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private int ved_complete_word (buf_t **thisp) {
  buf_t *this = *thisp;
  int retval = DONE;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2, $my(video)->col_pos,
  ved_complete_word_callback, NULL, 0);
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;
  //menu->space_selects = 0;

  int orig_patlen = menu->patlen;

  char *word = menu_create ($my(root), menu);
  if (word isnot NULL) {
    action_t *action = AllocType (action);
    act_t *act = AllocType (act);
    vundo_set (act, REPLACE_LINE);
    act->idx = this->cur_idx;
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
    stack_push (action, act);
    vundo_push (this, action);
    My(String).delete_numbytes_at ($mycur(data), orig_patlen, $my(shared_int));
    My(String).insert_at ($mycur(data), word, $my(shared_int));
    ved_normal_end_word (thisp, 1, 0, DONOT_DRAW);
    ved_normal_right (this, 1, DONOT_DRAW);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_cur_row);
  }

theend:
  menu_free (menu);
  return retval;
}

private int ved_complete_line_callback (menu_t *menu) {
  buf_t *this = menu->this;

  int idx = 0;

  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
    menu->pat[0] = '\0';
    menu->patlen = 0;

    char *s = $mycur(data)->bytes;

    while (IS_SPACE (*s)) { s++; idx++; }
    int clen = ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]);

    while (idx++ < $mycur(cur_col_idx) + (clen - 1))
      menu->pat[menu->patlen++] = *s++;

    menu->pat[menu->patlen] = '\0';
  } else
    menu_free_list (menu);

  vstr_t *lines = vstr_new ();

  row_t *row = this->head;
  idx = 0;

  while (row isnot NULL) {
    if (row->data->bytes and idx++ isnot this->cur_idx) {
      char *p = strstr (row->data->bytes, menu->pat);
      if (NULL isnot p) {
        if (p is row->data->bytes or ({
            int i = p - row->data->bytes;
            int found = 1;
            while (i--) {
              if (IS_SPACE (row->data->bytes[i]) is 0) {
                found = 0; break;
              }
            }
            found;}))
        vstr_add_sort_and_uniq (lines, row->data->bytes);
       }
    }
    row = row->next;
  }

  ifnot (lines->num_items) {
    free (lines);
    menu->state |= MENU_QUIT;
    return NOTHING_TODO;
  }

  menu->list = lines;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private int ved_complete_line (buf_t *this) {
  int retval = DONE;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2, $my(video)->col_pos,
      ved_complete_line_callback, NULL, 0);
  //menu->space_selects = 0;
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *line = menu_create ($my(root), menu);

  if (line isnot NULL) {
    action_t *action = AllocType (action);
    act_t *act = AllocType (act);
    vundo_set (act, REPLACE_LINE);
    act->idx = this->cur_idx;
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
    stack_push (action, act);
    vundo_push (this, action);
    My(String).clear ($mycur(data));
    My(String).append ($mycur(data), line);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_cur_row);
  }

theend:
  menu_free (menu);
  return retval;
}

private int ved_win_only (buf_t *this) {
  if (1 is $myparents(num_frames)) return NOTHING_TODO;

  for (int i = $myparents(num_frames) - 1; i > 0; i--)
    win_delete_frame ($my(parent), i);

  buf_t *it = $my(parent)->head;
  int idx = 0;
  while (it isnot NULL) {
    if (idx++ isnot $my(parent)->cur_idx)
      $from(it, flags) &= ~BUF_IS_VISIBLE;
    else {
      $my(video)->row_pos = $my(cur_video_row);
      $my(video)->col_pos = $my(cur_video_col);
    }

    it = it->next;
  }

  My(Win).draw ($my(parent));
  return DONE;
}

private int ved_complete_word_actions_cb (menu_t *menu) {
  buf_t *this = menu->this;
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  ifnot (menu->patlen)
    menu->list = vstr_dup ($myroots(word_actions));
  else {
    menu->list = vstr_new ();
    vstring_t *it = $myroots(word_actions)->head;
    while (it) {
      if (str_eq_n (it->data->bytes, menu->pat, menu->patlen))
        vstr_add_sort_and_uniq (menu->list, it->data->bytes);
      it = it->next;
    }
  }

  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private utf8 ved_complete_word_actions (buf_t *this, char *action) {
  int retval = DONE;
  utf8 c = ESCAPE_KEY;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
    $my(video)->col_pos, ved_complete_word_actions_cb, NULL, 0);
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;
  menu->this = this;
  menu->return_if_one_item = 1;
  char *item = menu_create ($my(root), menu);
  if (item isnot NULL) {
    c = *item;
    char *tmp = item;
    int i = 0;
    for (; i < MAX_WORD_ACTION_LEN - 1 and *tmp; i++)
      action[i] = *tmp++;
    action[i] = '\0';
  }

theend:                                     /* avoid list (de|re)allocation */
  //menu->state &= ~MENU_LIST_IS_ALLOCATED; /* list will be free'd at ed_free() */
  menu_free (menu);
  return c;
}

private int ved_normal_handle_W (buf_t **thisp) {
  buf_t *this = *thisp;
  char action[MAX_WORD_ACTION_LEN];
  utf8 c = ved_complete_word_actions (this, action);
  if (c is ESCAPE_KEY) return NOTHING_TODO;

  char word[MAXWORD]; int fidx, lidx;
  if (NULL is get_current_word (this, word, Notword, Notword_len, &fidx, &lidx))
    return NOTHING_TODO;

  int retval = NOTHING_TODO;
  for (int i = 0; i < $myroots(word_actions_chars_len); i++)
    if (c is $myroots(word_actions_chars)[i]) {
      bufiter_t *it = self(iter.new, this->cur_idx);
      retval = $myroots(word_actions_cb)[i] (thisp, fidx, lidx, it, word, c, action);
      self(iter.free, it);
    }

  return retval;
}

private int ved_normal_handle_ctrl_w (buf_t **thisp) {
  buf_t *this = *thisp;

  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case CTRL('w'):
    case ARROW_DOWN_KEY:
    case 'j':
    case 'w':
       {
         int frame = WIN_CUR_FRAME($my(parent)) + 1;
         if (frame > WIN_LAST_FRAME($my(parent))) frame = FIRST_FRAME;
         this = My(Win).frame.change ($my(parent), frame, DRAW);
         if (NULL isnot this) {
           *thisp = this;
           return DONE;
         }
       }
       break;

    case ARROW_UP_KEY:
    case 'k':
       {
         int frame = WIN_CUR_FRAME($my(parent)) - 1;
         if (frame < FIRST_FRAME) frame = WIN_LAST_FRAME($my(parent));
         this = My(Win).frame.change ($my(parent), frame, DRAW);
         if (NULL isnot this) {
           *thisp = this;
           return DONE;
         }
       }
       break;

    case 'o':
      return ved_win_only (this);

    case 'n':
      return ved_enew_fname (thisp, UNAMED);

    case 's':
      return ved_split (thisp, UNAMED);

    case 'l':
    case ARROW_RIGHT_KEY:
      return ed_win_change ($my(root), thisp, VED_COM_WIN_CHANGE_NEXT, NULL, NO_OPTION, NO_FORCE);

    case 'h':
    case ARROW_LEFT_KEY:
      return ed_win_change ($my(root), thisp, VED_COM_WIN_CHANGE_PREV, NULL, NO_OPTION, NO_FORCE);

    case '`':
      return ed_win_change ($my(root), thisp, VED_COM_WIN_CHANGE_PREV_FOCUSED, NULL, NO_OPTION, NO_FORCE);

    default:
      break;
  }

  return NOTHING_TODO;
}


private int ved_normal_handle_g (buf_t **thisp, int count) {
  buf_t *this = *thisp;

  if (1 isnot count) return ved_normal_goto_linenr (this, count, DRAW);

  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case 'g':
      return ved_normal_bof (this, DRAW);

    case 'f':
      return ved_open_fname_under_cursor (thisp, str_eq ($my(fname), VED_MSG_BUF)
         ? 0 : AT_CURRENT_FRAME, OPEN_FILE_IFNOT_EXISTS, DONOT_REOPEN_FILE_IF_LOADED);

    default:
      for (int i = 0; i < $myroots(num_on_normal_g_cbs); i++) {
        int retval = $myroots(on_normal_g_cbs)[i] (thisp, c);
        if (retval isnot NO_CALLBACK_FUNCTION) return retval;
      }

      return NOTHING_TODO;
  }

  return ved_normal_goto_linenr (this, count, DRAW);
}

private int ved_normal_handle_G (buf_t *this, int count) {
  if (1 isnot count)
    return ved_normal_goto_linenr (this, count, DRAW);

  return ved_normal_eof (this, DONOT_DRAW);
}

private int ved_normal_handle_comma (buf_t **thisp) {
  buf_t *this = *thisp;
  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case 'n':
      return ved_buf_change (thisp, VED_COM_BUF_CHANGE_NEXT);

    case 'm':
      return ved_buf_change (thisp, VED_COM_BUF_CHANGE_PREV);

    case ',':
      return ved_buf_change (thisp, VED_COM_BUF_CHANGE_PREV_FOCUSED);

    case '.':
      return ed_win_change ($my(root), thisp, VED_COM_WIN_CHANGE_PREV_FOCUSED, NULL, NO_OPTION, NO_FORCE);

    case '/':
      return ed_win_change ($my(root), thisp, VED_COM_WIN_CHANGE_NEXT, NULL, NO_OPTION, NO_FORCE);
  }

  return NOTHING_TODO;
}

private int ved_handle_ctrl_x (buf_t **thisp) {
  buf_t *this = *thisp;
  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case 'l':
    case CTRL('l'):
      return ved_complete_line (this);

    case 'f':
    case CTRL('f'):
      return ved_insert_complete_filename (thisp);
  }

  return NOTHING_TODO;
}

private int ved_delete_word (buf_t *this, int regidx) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  char word[MAXWORD]; int fidx, lidx;
  if (NULL is get_current_word (this, word, Notword, Notword_len, &fidx, &lidx))
    return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);

  int len = $mycur(cur_col_idx) - fidx;
  char buf[len + 1];
  memcpy (buf, $mycur(data)->bytes + fidx, len);
  buf[len] = '\0';

  act->cur_col_idx = $mycur(cur_col_idx);
  act->first_col_idx = $mycur(first_col_idx);

  ved_normal_left (this, char_num (buf, len), DONOT_DRAW);
  My(String).delete_numbytes_at ($mycur(data), bytelen (word), fidx);

  stack_push (action, act);
  vundo_push (this, action);
  ed_register_set_with ($my(root), regidx, CHARWISE, word, 0);

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_delete_line (buf_t *this, int count, int regidx) {
  if (count > this->num_items - this->cur_idx)
    count = this->num_items - this->cur_idx;

  int currow_idx = this->cur_idx;

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  action_t *action = AllocType (action);
  rg_t *rg = ed_register_new ($my(root), regidx);

  int fidx = this->cur_idx;
  int lidx = fidx + count - 1;
                                       /* optimization for large buffers */
  int perfom_reg = (regidx isnot REG_UNAMED or count < ($my(dim)->num_rows * 3));

  for (int idx = fidx; idx <= lidx; idx++) {
    act_t *act = AllocType (act);
    vundo_set (act, DELETE_LINE);
    act->idx = this->cur_idx;
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
    stack_push (action, act);

    /* with large buffers, this really slowdown a lot the operation */
    if (perfom_reg) {
      rg = ed_register_push_with (
        $my(root), regidx, LINEWISE, $mycur(data)->bytes, REVERSE_ORDER);
      rg->cur_col_idx = $mycur(cur_col_idx);
      rg->first_col_idx = $mycur(first_col_idx);
      rg->col_pos = $my(cur_video_col);
    }

    if (NULL is self(cur.delete)) break;
  }

  if (this->num_items is 0) buf_on_no_length (this);

  $my(video)->col_pos = $my(cur_video_col) = ved_buf_adjust_col (this, nth, isatend);
  buf_adjust_marks (this, DELETE_LINE, fidx, lidx);

  if (this->num_items is 1 and $my(cur_video_row) isnot $my(dim)->first_row)
    $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;


  if (this->cur_idx is currow_idx) {
    if ($my(video_first_row_idx) < fidx) {
       goto theend;
    } else {
      $my(video_first_row) = this->current;
      $my(video_first_row_idx) = this->cur_idx;
      goto theend;
    }
  }

  if ($my(video_first_row_idx) > this->cur_idx or
      $my(video_first_row_idx) < this->cur_idx)
    buf_adjust_view (this);

theend:
  $my(flags) |= BUF_IS_MODIFIED;
  if (perfom_reg) MSG("deleted into register [%c]", REGISTERS[regidx]);
  self(draw);

  vundo_push (this, action);
  return DONE;
}

private int ved_normal_change_case (buf_t *this) {
  utf8 c = CUR_UTF8_CODE;
  char buf[5]; int len;
  action_t *action;
  act_t *act;

  if ('a' <= c and c <= 'z')
    buf[0] = c - ('a' - 'A');
  else if ('A' <= c and c <= 'Z')
    buf[0] = c + ('a' - 'A');
  else {
    char *p = byte_in_str (CASE_A, $mycur(data)->bytes[$mycur(cur_col_idx)]);
    if (NULL is p) {
      if (c > 0x80) {
        utf8 new;
        if (ustring_is_lower (c))
          new = ustring_to_upper (c);
        else
          new = ustring_to_lower (c);

        if (new is c) goto theend;
        ustring_character (new, buf, &len);
        goto setaction;
      } else
        goto theend;
    }

    buf[0] = CASE_B[p-CASE_A];
  }

  buf[1] = '\0';

setaction:
  action = AllocType (action);
  act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  My(String).replace_numbytes_at_with ($mycur(data), bytelen (buf), $mycur(cur_col_idx), buf);
  stack_push (action, act);
  vundo_push (this, action);
  self(draw_cur_row);

theend:
  $my(flags) |= BUF_IS_MODIFIED;
  ved_normal_right (this, 1, DRAW);
  return DONE;
}

private int ved_indent (buf_t *this, int count, utf8 com) {
  if (com is '<') {
    if ($mycur(data)->num_bytes is 0 or (IS_SPACE ($mycur(data)->bytes[0]) is 0))
      return NOTHING_TODO;
  } else
    if (str_eq (VISUAL_MODE_LW, $my(mode)))
      ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);

  int shiftwidth = ($my(ftype)->shiftwidth ? $my(ftype)->shiftwidth : DEFAULT_SHIFTWIDTH);

  if ((shiftwidth * count) > $my(dim)->num_cols)
    count = $my(dim)->num_cols / shiftwidth;

  if (com is '>') {
    int len = shiftwidth * count;
    char s[len + 1];
    int i;
    for (i = 0; i < len; i++) { s[i] = ' '; } s[i] = '\0';
    My(String).prepend ($mycur(data), s);
  } else {
    char *s = $mycur (data)->bytes;
    int i = 0;
    while (IS_SPACE (*s) and i < shiftwidth * count) {
      s++; i++;
    }

    My(String).clear ($mycur(data));
    My(String).append ($mycur(data), s);
    if ($mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes) {
      $mycur(cur_col_idx) = $mycur(first_col_idx) = 0;
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
      ved_normal_eol (this);
    }
  }

  stack_push (action, act);
  vundo_push (this, action);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_normal_Yank (buf_t *this, int count, int regidx) {
  if (count > this->num_items - this->cur_idx)
    count = this->num_items - this->cur_idx;

  int currow_idx = this->cur_idx;

  rg_t *rg = NULL;

  if (regidx isnot REG_STAR and regidx isnot REG_PLUS) {
    rg = ed_register_new ($my(root), regidx);
  }

  My(String).clear ($my(shared_str));

  for (int i = 0; i < count; i++) {
    self(cur.set, (currow_idx + count - 1) - i);
    if (regidx isnot REG_STAR and regidx isnot REG_PLUS) {
      rg = ed_register_push_with (
          $my(root), regidx, LINEWISE, $mycur(data)->bytes, DEFAULT_ORDER);
      rg->cur_col_idx = $mycur(cur_col_idx);
      rg->first_col_idx = $mycur(first_col_idx);
      rg->col_pos = $my(cur_video_col);
    } else {
      My(String).prepend_fmt ($my(shared_str), "%s\n", $mycur(data)->bytes);
    }
  }

  if (regidx is REG_STAR or regidx is REG_PLUS)
    ed_selection_to_X ($my(root), $my(shared_str)->bytes, $my(shared_str)->num_bytes,
        (REG_STAR is regidx ? X_PRIMARY : X_CLIPBOARD));

  MSG("yanked [linewise] into register [%c]", REGISTERS[regidx]);
  return DONE;
}

private int ved_normal_yank (buf_t *this, int count, int regidx) {
  if (count > (int) $mycur(data)->num_bytes - $mycur(cur_col_idx))
    count = $mycur(data)->num_bytes - $mycur(cur_col_idx);

  char buf[(count * 4) + 1];
  char *bytes = $mycur(data)->bytes + $mycur(cur_col_idx);

  int bufidx = 0;
  for (int i = 0; i < count and *bytes; i++) {
    int clen = ustring_charlen ((uchar) *bytes);
    loop (clen) buf[bufidx + $i] = bytes[$i];
    bufidx += clen;
    bytes += clen;
    i += clen - 1;
  }

  buf[bufidx] = '\0';

  if (regidx isnot REG_STAR and regidx isnot REG_PLUS) {
    ed_register_set_with ($my(root), regidx, CHARWISE, buf, 0);
  } else
    ed_selection_to_X ($my(root), buf, bufidx, (REG_STAR is regidx
        ? X_PRIMARY : X_CLIPBOARD));

  MSG("yanked [charwise] into register [%c]", REGISTERS[regidx]);
  return DONE;
}

private int ved_normal_put (buf_t *this, int regidx, utf8 com) {
  if (ERROR is ed_register_special_set ($my(root), this, regidx))
    return NOTHING_TODO;

  rg_t *rg = &$my(regs)[regidx];
  reg_t *reg = rg->head;

  if (NULL is reg) return NOTHING_TODO;

  action_t *action = AllocType (action);

  row_t *currow = this->current;
  int currow_idx = this->cur_idx;

  if (com is 'P') {
    reg->prev = NULL;
    for (;;) {
      if (NULL is reg->next) break;
      reg_t *t = reg; reg = reg->next; reg->prev = t;
    }
  }

  int linewise_num = 0;

  while (reg isnot NULL) {
    act_t *act = AllocType (act);
    if (rg->type is LINEWISE) {
      row_t *row = self(row.new_with, reg->data->bytes);
      vundo_set (act, INSERT_LINE);
      linewise_num++;

      if ('p' is com)
        current_list_append (this, row);
      else
        current_list_prepend (this, row);

      act->idx = this->cur_idx;
    } else {
      vundo_set (act, REPLACE_LINE);
      act->idx = this->cur_idx;
      act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
      My(String).insert_at ($mycur(data), reg->data->bytes, $mycur(cur_col_idx) +
        (('P' is com or 0 is $mycur(data)->num_bytes) ? 0 :
          ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)])));
    }

    stack_push (action, act);
    if (com is 'P')
      reg = reg->prev;
    else
      reg = reg->next;
  }

  vundo_push (this, action);

  if (rg->type is LINEWISE) {

    buf_adjust_marks (this, INSERT_LINE, this->cur_idx, this->cur_idx + linewise_num);

    if ('p' is com) {
      this->current = currow;
      this->cur_idx = currow_idx;
    } else {
      if ($my(video_first_row_idx) is this->cur_idx)
        $my(video_first_row) = this->current;

      $mycur(cur_col_idx) = rg->cur_col_idx;
      $mycur(first_col_idx) = rg->first_col_idx;
      $my(video)->col_pos = $my(cur_video_col) = rg->col_pos;
    }
  }

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw);
  return DONE;
}

private int ved_normal_handle_c (buf_t *this, int count, int regidx) {
  (void) count;
  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case 'w':
      ved_delete_word (this, regidx);
      return ved_insert (&this, 'c');
  }

  return NOTHING_TODO;
}

private int ved_normal_handle_d (buf_t *this, int count, int reg) {
  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case 'G':
    case END_KEY:
      count = (this->num_items) - this->cur_idx;
      break;
    case 'g':
    case HOME_KEY:
      count = this->cur_idx + 1;
      ved_normal_bof (this, DRAW);
  }

  switch (c) {
    case 'G':
    case END_KEY:
    case 'g':
    case HOME_KEY:
    case 'd':
      return ved_delete_line (this, count, reg);

    case 'w':
      return ved_delete_word (this, reg);

    default:
      return NOTHING_TODO;
   }
}

private int insert_change_line (buf_t *this, utf8 c, action_t **action) {
  if ($mycur(data)->num_bytes) RM_TRAILING_NEW_LINE;

  if (c is ARROW_UP_KEY) ved_normal_up (this, 1, ADJUST_COL, DRAW);
  else if (c is ARROW_DOWN_KEY) ved_normal_down (this, 1, ADJUST_COL, DRAW);
  else if (c is PAGE_UP_KEY) ved_normal_page_up (this, 1);
  else if (c is PAGE_DOWN_KEY) ved_normal_page_down (this, 1);
  else {
    int isatend = $mycur(cur_col_idx) is (int) $mycur(data)->num_bytes;
    if (isatend) {
      self(cur.append_with, $my(ftype)->autoindent (this, this->current)->bytes);
    } else {
      char bytes[MAXLINE];
      int len = $mycur(data)->num_bytes - $mycur(cur_col_idx);
      memcpy (bytes, $mycur(data)->bytes + $mycur(cur_col_idx), len);
      bytes[len] = '\0';
      My(String).clear_at ($mycur(data), $mycur(cur_col_idx));
      self(cur.append_with, str_fmt ("%s%s",
          $my(ftype)->autoindent (this, this->current)->bytes, bytes));
    }

    this->current = this->current->prev;
    this->cur_idx--;
    buf_adjust_marks (this, INSERT_LINE, this->cur_idx, this->cur_idx + 1);

    act_t *act = AllocType (act);
    vundo_set (act, INSERT_LINE);

    $my(cur_video_col) = $my(video)->col_pos = $my(video)->first_col;
    $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;
    ved_normal_down (this, 1, DONOT_ADJUST_COL, 0);
    if ($mycur(data)->num_bytes) ADD_TRAILING_NEW_LINE;
    ved_normal_right (this, $my(shared_int) + isatend, DONOT_DRAW);

    act->idx = this->cur_idx;
    stack_push (*action, act);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw);
    return DONE;
  }

  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (*action, act);
  $my(flags) |= BUF_IS_MODIFIED;
  return DONE;
}

private int ved_insert_character (buf_t *this, utf8 *c) {
  int has_pop_pup = 1;
  *c = BUF_GET_AS_NUMBER (has_pop_pup, $my(video)->row_pos - 1,
      $my(video)->col_pos + 1, $my(video)->num_cols, "utf8 code:");
  ifnot (*c) return NOTHING_TODO;
  if (0x07F <= *c and *c < 0x0A0) return NOTHING_TODO;
  if (*c < ' ' and '\t' isnot *c) return NOTHING_TODO;
  $my(state) |= ACCEPT_TAB_WHEN_INSERT;
  return NEWCHAR;
}

private int ved_insert_handle_ud_line_completion (buf_t *this, utf8 *c) {
  char *line;
  int len = 0;
  int up_or_down_line = *c is CTRL('y');

  if (up_or_down_line)
    ifnot (this->num_items)
      return NOTHING_TODO;
    else {
      line = $mycur(prev)->data->bytes;
      len = $mycur(prev)->data->num_bytes;
    }
  else
    if (this->cur_idx is this->num_items - 1)
      return NOTHING_TODO;
    else {
      line = $mycur(next)->data->bytes;
      len =  $mycur(next)->data->num_bytes;
    }

  if (len is 0) return NOTHING_TODO;

  int nolen = 0 is $mycur(data)->num_bytes or 0 is $mycur(data)->bytes[0]
    or '\n' is $mycur(data)->bytes[0];
  int nth = 0;
  if (nolen)
    nth = -1;
  else {
    ustring_encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
        CLEAR, $my(ftype)->tabwidth, $mycur(cur_col_idx));
    if (0 is $mycur(data)->bytes[$mycur(cur_col_idx)])
      nth = $my(line)->num_items + 1;
    else
      nth = $my(line)->cur_idx + 1;
  }

  ustring_encode ($my(line), line, len, CLEAR, $my(ftype)->tabwidth, 0);
  if ($my(line)->num_items < nth) return NOTHING_TODO;
  int n = 1;

  vchar_t *it = $my(line)->head;
  while (++n <= nth) it = it->next;

  ifnot (it->code) return NOTHING_TODO;

  *c = it->code;

  return NEWCHAR;
}

private int ved_insert_last_insert (buf_t *this) {
  if ($my(last_insert)->num_bytes is 0) return NOTHING_TODO;
  My(String).insert_at ($mycur(data), $my(last_insert)->bytes, $mycur(cur_col_idx));
  ved_normal_right (this, char_num ($my(last_insert)->bytes, $my(last_insert)->num_bytes), 1);
  self(draw_cur_row);
  return DONE;
}

typedef void (*draw_buf) (buf_t *);
typedef void (*draw_cur_row) (buf_t *);

private void buf_draw_void (buf_t *this) {(void) this;}
private void buf_draw_cur_row_void (buf_t *this) {(void) this;}

#define VISUAL_ADJUST_IDXS(vis__)           \
do {                                        \
  if ((vis__).fidx > (vis__).lidx) {        \
    int t = (vis__).fidx;                   \
    (vis__).fidx = (vis__).lidx;            \
    (vis__).lidx = t;                       \
  }                                         \
} while (0)

#define VISUAL_RESTORE_STATE(vis__, mark__) \
   VISUAL_ADJUST_IDXS(vis__);               \
   state_restore (&(mark__));               \
   self(cur.set, (mark__).cur_idx);         \
   this->cur_idx = (mark__).cur_idx

#define VISUAL_ADJUST_COL(idx)                                                      \
({                                                                                  \
  ustring_encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,         \
      CLEAR, $my(ftype)->tabwidth, idx);                                            \
  int nth_ = $my(line)->cur_idx + 1;                                                \
    ved_normal_bol (this);                                                          \
    ved_normal_right (this, nth_ - 1, DRAW);                                        \
})

#define VISUAL_INIT_FUN(fmode, parse_fun)                                           \
  char prev_mode[bytelen ($my(mode)) + 1];                                          \
  memcpy (prev_mode, $my(mode), sizeof (prev_mode));                                \
  self(set.mode, (fmode));                                                          \
  buf_set_draw_topline (this);                                                      \
  mark_t mark; state_set (&mark); mark.cur_idx = this->cur_idx;                     \
  draw_buf dbuf = $self(draw);                                                      \
  draw_cur_row drow = $self(draw_cur_row);                                          \
  $self(draw) = buf_draw_cur_row_void;                                              \
  $self(draw_cur_row) = buf_draw_cur_row_void;                                      \
  int reg = -1;  int count = 1;  (void) count;                                      \
  $my(vis)[1] = (vis_t) {                                                           \
    .fidx = this->cur_idx, .lidx = this->cur_idx, .orig_syn_parser = $my(syn)->parse};                                  \
 $my(vis)[0] = (vis_t) {                                                            \
    .fidx = $mycur(cur_col_idx), .lidx = $mycur(cur_col_idx), .orig_syn_parser = $my(syn)->parse};\
  $my(syn)->parse = (parse_fun)

#define VIS_HNDL_CASE_REG(reg)                                              \
  case '"':                                                                 \
    if (-1 isnot (reg)) goto theend;                                        \
    (reg) = ed_register_get_idx ($my(root), My(Input).get ($my(term_ptr))); \
    continue

#define VIS_HNDL_CASE_INT(count)                                            \
  case '1'...'9':                                                           \
    {                                                                       \
      char intbuf[8];                                                       \
      intbuf[0] = c;                                                        \
      int idx = 1;                                                          \
      c = BUF_GET_NUMBER (intbuf, idx);                                     \
      if (idx is MAX_COUNT_DIGITS) goto handle_char;                        \
      intbuf[idx] = '\0';                                                   \
      count = atoi (intbuf);                                                \
                                                                            \
      goto handle_char;                                                     \
    }                                                                       \
                                                                            \
    continue

private char *ved_syn_parse_visual_lw (buf_t *this, char *line, int len, int idx, row_t *currow) {
  (void) len;

  if ((idx is $my(vis)[0].fidx) or
      (idx > $my(vis)[0].fidx and $my(vis)[0].fidx < $my(vis)[0].lidx and
       idx <= $my(vis)[0].lidx) or
      (idx < $my(vis)[0].fidx and $my(vis)[0].fidx > $my(vis)[0].lidx and
       idx >= $my(vis)[0].lidx) or
      (idx > $my(vis)[0].lidx and $my(vis)[0].lidx < $my(vis)[0].fidx and
       idx < $my(vis)[0].fidx)) {

    ustring_encode ($my(line), line, len,
        CLEAR, $my(ftype)->tabwidth, currow->first_col_idx);

    vchar_t *it = $my(line)->current;

    My(String).replace_with_fmt ($my(shared_str), "%s%s",
         TERM_LINE_CLR_EOL, TERM_MAKE_COLOR(HL_VISUAL));

    int num = 0;

    while (num < $my(dim)->num_cols and it) {
      if (it->buf[0] is '\t') goto handle_tab;
      num += it->width;
      My(String).append ($my(shared_str), it->buf);
      goto next;

handle_tab:
      for (int i = 0; i < $my(ftype)->tabwidth and num < $my(dim)->num_cols; i++) {
        num++;
        My(String).append_byte ($my(shared_str), ' ');
      }

next:
      it = it->next;
    }

    My(String).append ($my(shared_str), TERM_COLOR_RESET);
    strncpy (line, $my(shared_str)->bytes, $my(shared_str)->num_bytes);
    line[$my(shared_str)->num_bytes] = '\0';
    return $my(shared_str)->bytes;
  }

  return $my(vis)[0].orig_syn_parser (this, line, len, idx, currow);
}

private int ed_lw_mode_cb (buf_t **thisp, int frow, int lrow, vstr_t *vstr, utf8 c) {
  (void) thisp; (void) vstr; (void) c; (void) frow; (void) lrow;
  return 1;
}

private int ved_visual_token_cb (vstr_t *str, char *tok, void *menu_o) {
  menu_t *menu = (menu_t *) menu_o;
  if (menu->patlen)
    ifnot (str_eq_n (tok, menu->pat, menu->patlen)) return OK;

  vstr_append_current_with (str, tok);
  return OK;
}

private int ved_visual_complete_actions_cb (menu_t *menu) {
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  buf_t *this = menu->this;

  vstr_t *items;
  if (IS_MODE(VISUAL_MODE_CW))
    items = str_chop ($myroots(cw_mode_actions), '\n', NULL, ved_visual_token_cb, menu);
  else if (IS_MODE(VISUAL_MODE_LW))
    items = str_chop ($myroots(lw_mode_actions), '\n', NULL, ved_visual_token_cb, menu);
  else
    items = str_chop (
      "insert text in front of the selected block\n"
      "change/replace selected block\n"
      "delete selected block\n", '\n', NULL,
      ved_visual_token_cb, menu);

  menu->list = items;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private utf8 ved_visual_complete_actions (buf_t *this, char *action) {
  int retval = DONE;
  utf8 c = ESCAPE_KEY;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
    $my(video)->col_pos, ved_visual_complete_actions_cb, NULL, 0);
  menu->this = this;
  menu->return_if_one_item = 1;

  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *item = menu_create ($my(root), menu);
  if (item isnot NULL) {
    c = *item;
    char *tmp = item;
    int i = 0;
    for (; i < MAX_WORD_ACTION_LEN - 1 and *tmp; i++)
      action[i] = *tmp++;
    action[i] = '\0';
  }

theend:
  menu_free (menu);
  return c;
}

private int ved_normal_visual_lw (buf_t **thisp) {
  buf_t *this = *thisp;
  VISUAL_INIT_FUN (VISUAL_MODE_LW, ved_syn_parse_visual_lw);
  $my(vis)[0] = $my(vis)[1];

  int goto_cb = 0;
  utf8 c;
  char vis_action[MAX_WORD_ACTION_LEN];
  vis_action[0] = '\0';

  for (;;) {
    $my(vis)[0].lidx = this->cur_idx;
    dbuf (this);

    c = My(Input).get ($my(term_ptr));

handle_char:
    switch (c) {
      case '\t':
        c = ved_visual_complete_actions (this, vis_action);
        goto handle_char;

      VIS_HNDL_CASE_REG(reg);
      VIS_HNDL_CASE_INT(count);

      case ARROW_DOWN_KEY:
        ved_normal_down (this, 1, ADJUST_COL, DONOT_DRAW);
        continue;

      case ARROW_UP_KEY:
        ved_normal_up (this, 1, ADJUST_COL, DONOT_DRAW);
        continue;

      case PAGE_DOWN_KEY:
        ved_normal_page_down (this, 1);
        continue;

      case PAGE_UP_KEY:
        ved_normal_page_up (this, 1);
        continue;

      case ESCAPE_KEY:
        VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        goto theend;

      case HOME_KEY:
        ved_normal_bof (this, DONOT_DRAW);
        continue;

      case 'G':
      case END_KEY:
        ved_normal_eof (this, DONOT_DRAW);
        continue;

      case '>':
      case '<':
        if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        } else {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
          self(cur.set, $my(vis)[0].fidx);
          this->cur_idx = $my(vis)[0].fidx;
        }

        {
          action_t *action = AllocType (action);

          for (int i = $my(vis)[0].fidx; i <= $my(vis)[0].lidx; i++) {
            if (DONE is ved_indent (this, count, c)) {
              action_t *laction = vundo_pop (this);
              act_t *act = stack_pop (laction, act_t);
              stack_push (action, act);
              free (laction);
            }

            this->current = this->current->next;
            this->cur_idx++;
          }

          vundo_push (this, action);

          VISUAL_RESTORE_STATE ($my(vis)[1], mark);
          if (c is '<' and $mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes - 1) {
            ved_normal_noblnk (this);
            VISUAL_ADJUST_COL ($mycur(cur_col_idx));
          }
        }

        goto theend;

      case 'd':
      case 'y':
      case 'Y':
        if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        } else {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
          self(cur.set, $my(vis)[0].fidx);
          this->cur_idx = $my(vis)[0].fidx;
        }

        if (-1 is reg or c is 'Y') reg = (c is 'Y' ? REG_STAR : REG_UNAMED);

        if (c is 'd')
          ved_delete_line (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);
        else {
          ved_normal_Yank (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);
          VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        }

        goto theend;

      case 's':
        VISUAL_ADJUST_IDXS($my(vis)[0]);
        {
          rline_t *rl = ved_rline_new ($my(root), $my(term_ptr), My(Input).get,
              *$my(prompt_row_ptr), 1, $my(dim)->num_cols, $my(video));
          string_t *str = My(String).new_with_fmt ("substitute --range=%d,%d --global -i --pat=",
              $my(vis)[0].fidx + 1, $my(vis)[0].lidx + 1);
          BYTES_TO_RLINE (rl, str->bytes, (int) str->num_bytes);
          rline_write_and_break (rl);
          ved_rline (&this, rl);
          My(String).free (str);
        }
        goto theend;

      case 'w':
        VISUAL_ADJUST_IDXS($my(vis)[0]);
        {
          rline_t *rl = ved_rline_new ($my(root), $my(term_ptr), My(Input).get,
              *$my(prompt_row_ptr), 1, $my(dim)->num_cols, $my(video));
          string_t *str = My(String).new_with_fmt ("write --range=%d,%d ",
              $my(vis)[0].fidx + 1, $my(vis)[0].lidx + 1);
          BYTES_TO_RLINE (rl, str->bytes, (int) str->num_bytes);
          rline_write_and_break (rl);
          ved_rline (&this, rl);
          My(String).free (str);
        }
        goto theend;

      case '+':
      case '*':
        if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        } else {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
          self(cur.set, $my(vis)[0].fidx);
          this->cur_idx = $my(vis)[0].fidx;
        }

        {
          row_t *row = this->current;
          vstr_t *rows = vstr_new ();
          for (int ii = $my(vis)[0].fidx; ii <= $my(vis)[0].lidx; ii++) {
            vstr_append_current_with (rows, row->data->bytes);
            row = row->next;
          }
          string_t *str = vstr_join (rows, "\n");
          ed_selection_to_X ($my(root), str->bytes, str->num_bytes,
              ('*' is c ? X_PRIMARY : X_CLIPBOARD));
          string_free (str);
          vstr_free (rows);
          goto theend;
        }

      default:
        for (int i = 0; i < $myroots(lw_mode_chars_len); i++)
          if (c is $myroots(lw_mode_chars)[i]) {
            if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
              VISUAL_RESTORE_STATE ($my(vis)[0], mark);
            } else {
              VISUAL_ADJUST_IDXS($my(vis)[0]);
              self(cur.set, $my(vis)[0].fidx);
              this->cur_idx = $my(vis)[0].fidx;
            }

            goto_cb = 1;
            goto theend;

            callback:;

            row_t *row = this->current;
            vstr_t *rows = vstr_new ();
            for (int ii = $my(vis)[0].fidx; ii <= $my(vis)[0].lidx; ii++) {
              vstr_append_current_with (rows, row->data->bytes);
              row = row->next;
            }

            $myroots(lw_mode_cb) (thisp, $my(vis)[0].fidx, $my(vis)[0].lidx, rows, c);
            vstr_free (rows);
            goto thereturn;
          }

        continue;
    }
  }

theend:
  $my(syn)->parse = $my(vis)[0].orig_syn_parser;
  $self(draw) = dbuf;
  $self(draw_cur_row) = drow;
  self(set.mode, prev_mode);
  self(draw);

  if (goto_cb) goto callback;

thereturn:
  return DONE;
}

private char *ved_syn_parse_visual_line (buf_t *this, char *line, int len, row_t *currow) {
  ifnot (len) return line;

  int fidx = $my(vis)[0].fidx;
  int lidx = $my(vis)[0].lidx;
  if (fidx > lidx)
    {int t = fidx; fidx = lidx; lidx = t;}

  ustring_encode ($my(line), line, len,
      CLEAR, $my(ftype)->tabwidth, currow->first_col_idx);

  vchar_t *it = $my(line)->current;

  My(String).replace_with ($my(shared_str), TERM_LINE_CLR_EOL);

  int num = 0;
  int idx = currow->first_col_idx;

  if (idx > fidx)
    My(String).append_fmt ($my(shared_str), "%s", TERM_MAKE_COLOR(HL_VISUAL));

  while (num < $my(dim)->num_cols and it) {
    if (idx is fidx)
      My(String).append_fmt ($my(shared_str), "%s", TERM_MAKE_COLOR(HL_VISUAL));

    if (it->buf[0] is '\t') goto handle_tab;

    num += it->width; // (artifact) if a character occupies > 1 width and is the last
    // char, the code will do the wrong thing and probably will mess up the screen

    My(String).append ($my(shared_str), it->buf);

    goto next;

handle_tab:
    for (int i = 0; i < $my(ftype)->tabwidth and num < $my(dim)->num_cols; i++) {
      num++;
      My(String).append_byte ($my(shared_str), ' ');
    }

next:
    if (idx is lidx)
      My(String).append ($my(shared_str), TERM_COLOR_RESET);
    idx += it->len;
    it = it->next;
  }

  strncpy (line, $my(shared_str)->bytes, $my(shared_str)->num_bytes);
  line[$my(shared_str)->num_bytes] = '\0';
  return $my(shared_str)->bytes;
}

private char *ved_syn_parse_visual_cw (buf_t *this, char *line, int len, int idx, row_t *row) {
  (void) idx;
  return ved_syn_parse_visual_line (this, line, len, row);
}

private int ed_cw_mode_cb (buf_t **thisp, int fcol, int lcol, string_t *str, utf8 c, char *action) {
  (void) thisp; (void) str; (void) c; (void) fcol; (void) lcol; (void) action;
  return 1;
}

private int ved_normal_visual_cw (buf_t **thisp) {
  buf_t *this = *thisp;

  VISUAL_INIT_FUN (VISUAL_MODE_CW, ved_syn_parse_visual_cw);

  $my(vis)[1] = $my(vis)[0];
  $my(vis)[0] = (vis_t) {.fidx = $mycur(cur_col_idx), .lidx = $mycur(cur_col_idx),
     .orig_syn_parser = $my(vis)[1].orig_syn_parser};

  int goto_cb = 0;
  char vis_action[MAX_WORD_ACTION_LEN];
  vis_action[0] = '\0';

  for (;;) {
    $my(vis)[0].lidx = $mycur(cur_col_idx);
    drow (this);
    utf8 c = My(Input).get ($my(term_ptr));

handle_char:
    switch (c) {
      case '\t':
        c = ved_visual_complete_actions (this, vis_action);
        goto handle_char;

      VIS_HNDL_CASE_REG(reg);
      VIS_HNDL_CASE_INT(count);

      case ARROW_LEFT_KEY:
        ved_normal_left (this, 1, DONOT_DRAW);
        continue;

      case ARROW_RIGHT_KEY:
        ved_normal_right (this, 1, DONOT_DRAW);
        continue;

      case ESCAPE_KEY:
        goto theend;

      case HOME_KEY:
        ved_normal_bol (this);
        continue;

      case END_KEY:
        ved_normal_eol (this);
        continue;

      case 'e':
        VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        {
          int len = $my(vis)[0].lidx - $my(vis)[0].fidx + 1;
          char fname[len + 1];
          strncpy (fname, $mycur(data)->bytes + $my(vis)[0].fidx, len);
          fname[len] = '\0';
          ifnot (file_exists (fname)) goto theend;
          ved_edit_fname ($my(parent), thisp, fname, $myparents(cur_frame), 0, 0, 0);
        }

        goto theend;

      case 'd':
      case 'x':
      case 'y':
      case 'Y':
        if (-1 is reg or c is 'Y') reg = (c is 'Y' ? REG_STAR : REG_UNAMED);

        if ($my(vis)[0].lidx < $my(vis)[0].fidx) {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
        } else {   /* MACRO BLOCKS ARE EVIL */
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        }

        if (c is 'd' or c is 'x')
          ved_normal_delete (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);
        else
          ved_normal_yank (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);

        goto theend;

      case '+':
      case '*':
        if ($my(vis)[0].lidx < $my(vis)[0].fidx) {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
        } else {   /* MACRO BLOCKS ARE EVIL */
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        }

        {
          string_t *str = string_new (($my(vis)[0].lidx - $my(vis)[0].fidx) + 2);
          for (int ii = $my(vis)[0].fidx; ii <= $my(vis)[0].lidx; ii++)
            string_append_byte (str, $mycur(data)->bytes[ii]);
          ed_selection_to_X ($my(root), str->bytes, str->num_bytes,
              ('*' is c ? X_PRIMARY : X_CLIPBOARD));
          string_free (str);
          goto theend;
        }

      default:
        for (int i = 0; i < $myroots(cw_mode_chars_len); i++)
          if (c is $myroots(cw_mode_chars)[i]) {
            if ($my(vis)[0].lidx < $my(vis)[0].fidx) {
              VISUAL_ADJUST_IDXS($my(vis)[0]);
            } else {   /* MACRO BLOCKS ARE EVIL */
              VISUAL_RESTORE_STATE ($my(vis)[0], mark);
            }

            goto_cb = 1;
            goto theend;
            callback:;

            string_t *str = string_new (($my(vis)[0].lidx - $my(vis)[0].fidx) + 2);
            for (int ii = $my(vis)[0].fidx; ii <= $my(vis)[0].lidx; ii++)
              string_append_byte (str, $mycur(data)->bytes[ii]);

            $myroots(cw_mode_cb) (thisp, $my(vis)[0].fidx, $my(vis)[0].lidx, str, c, vis_action);
            string_free (str);
            goto thereturn;
          }

        continue;

    }
  }

theend:
  $my(syn)->parse = $my(vis)[0].orig_syn_parser;
  $self(draw) = dbuf;
  $self(draw_cur_row) = drow;
  self(set.mode, prev_mode);
  this = *thisp;
  self(draw);

  if (goto_cb) goto callback;

thereturn:
  return DONE;
}

private char *ved_syn_parse_visual_bw (buf_t *this, char *line, int len, int idx, row_t *row) {
  (void) len;

  if ((idx is $my(vis)[1].fidx) or
      (idx > $my(vis)[1].fidx and $my(vis)[1].fidx < $my(vis)[1].lidx and
       idx <= $my(vis)[1].lidx) or
      (idx < $my(vis)[1].fidx and $my(vis)[1].fidx > $my(vis)[1].lidx and
       idx >= $my(vis)[1].lidx) or
      (idx > $my(vis)[1].lidx and $my(vis)[1].lidx < $my(vis)[1].fidx and
       idx < $my(vis)[1].fidx)) {
    return ved_syn_parse_visual_line (this, line, len, row);
  }

  return $my(vis)[0].orig_syn_parser (this, line, len, idx, row);
}


private int ved_normal_visual_bw (buf_t *this) {
  VISUAL_INIT_FUN (VISUAL_MODE_BW, ved_syn_parse_visual_bw);

  char vis_action[MAX_WORD_ACTION_LEN];
  vis_action[0] = '\0';

  for (;;) {
    $my(vis)[1].lidx = this->cur_idx;
    $my(vis)[0].lidx = $mycur(cur_col_idx);
    dbuf (this);

    utf8 c = My(Input).get ($my(term_ptr));

handle_char:
    switch (c) {
      case '\t':
        c = ved_visual_complete_actions (this, vis_action);
        goto handle_char;

      VIS_HNDL_CASE_REG(reg);
      VIS_HNDL_CASE_INT(count);

      case ARROW_DOWN_KEY:
        ved_normal_down (this, 1, ADJUST_COL, DONOT_DRAW);
        continue;

      case ARROW_UP_KEY:
        ved_normal_up (this, 1, ADJUST_COL, DONOT_DRAW);
        continue;

      case PAGE_DOWN_KEY:
        ved_normal_page_down (this, 1);
        continue;

      case PAGE_UP_KEY:
        ved_normal_page_up (this, 1);
        continue;

      case ESCAPE_KEY:
        VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        goto theend;

      case HOME_KEY:
        ved_normal_bof (this, DONOT_DRAW);
        continue;

      case 'G':
      case END_KEY:
        ved_normal_eof (this, DONOT_DRAW);
        continue;

      case ARROW_LEFT_KEY:
        ved_normal_left (this, 1, DONOT_DRAW);
        continue;

      case ARROW_RIGHT_KEY:
        ved_normal_right (this, 1, DONOT_DRAW);
        continue;

      case 'i':
      case 'I':
      case 'c':
        VISUAL_ADJUST_IDXS($my(vis)[0]);
        {
          int row = $my(cur_video_row) - (2 < $my(cur_video_row));
          int index = $my(vis)[1].lidx;
          if (index > $my(vis)[1].fidx)
            while (row > 2 and index > $my(vis)[1].fidx) {
              row--; index--;
            }

          string_t *str = self(input_box, row, $my(vis)[0].fidx + 1,
              DONOT_ABORT_ON_ESCAPE, NULL);

          action_t *action = AllocType (action);
          action_t *baction =AllocType (action);

          for (int idx = $my(vis)[1].fidx; idx <= $my(vis)[1].lidx; idx++) {
            self(cur.set, idx);
            buf_adjust_view (this);
            VISUAL_ADJUST_COL ($my(vis)[0].fidx);

            if (c is 'c') {
              if ((int) $mycur(data)->num_bytes < $my(vis)[0].fidx)
                continue;
              else
                ved_normal_delete (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, REG_BLACKHOLE);

              action_t *paction = vundo_pop (this);
              act_t *act = stack_pop (paction, act_t);
              stack_push (baction, act);
              free (paction);
            } else {
              act_t *act = AllocType (act);
              vundo_set (act, REPLACE_LINE);
              act->idx = this->cur_idx;
              act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
              stack_push (baction, act);
            }

            My(String).insert_at ($mycur(data), str->bytes, $my(vis)[0].fidx);
            $my(flags) |= BUF_IS_MODIFIED;
          }

          My(String).free (str);

          act_t *bact = stack_pop (baction, act_t);
          while (bact isnot NULL) {
            stack_push (action, bact);
            bact = stack_pop (baction, act_t);
          }

          free (baction);
          vundo_push (this, action);
        }

        VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        goto theend;

      case 'x':
      case 'd':
        if (-1 is reg) reg = REG_UNAMED;
        {
          action_t *action = AllocType (action);
          action_t *baction =AllocType (action);

          VISUAL_ADJUST_IDXS($my(vis)[0]);
          VISUAL_ADJUST_IDXS($my(vis)[1]);

          for (int idx = $my(vis)[1].fidx; idx <= $my(vis)[1].lidx; idx++) {
            self(cur.set, idx);
            buf_adjust_view (this);
            VISUAL_ADJUST_COL ($my(vis)[0].fidx);
            if ((int) $mycur(data)->num_bytes < $my(vis)[0].fidx + 1) continue;
            int lidx__ = (int) $mycur(data)->num_bytes < $my(vis)[0].lidx ?
              (int) $mycur(data)->num_bytes : $my(vis)[0].lidx;
            ved_normal_delete (this, lidx__ - $my(vis)[0].fidx + 1, reg);
            action_t *paction = vundo_pop (this);
            act_t *act = stack_pop (paction, act_t);

            stack_push (baction, act);
            free (paction);
            $my(flags) |= BUF_IS_MODIFIED;
          }
          act_t *act = stack_pop (baction, act_t);
          while (act isnot NULL) {
            stack_push (action, act); act = stack_pop (baction, act_t);
          }

          free (baction);
          vundo_push (this, action);
        }

        VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        goto theend;

      default:
        continue;
    }
  }

theend:
  VISUAL_ADJUST_COL($mycur(cur_col_idx));
  $my(syn)->parse = $my(vis)[0].orig_syn_parser;
  $self(draw) = dbuf;
  $self(draw_cur_row) = drow;
  self(set.mode, prev_mode);
  self(draw);

  return DONE;
}

private int ved_edit_fname (win_t *win, buf_t **thisp, char *fname, int frame,
                                             int reload, int draw, int reopen) {
  buf_t *this = *thisp;
  if (fname is NULL and 0 is reload) return NOTHING_TODO;

  if (fname isnot NULL) {
    ifnot (reopen) {
      int idx;
      buf_t *bn = My(Win).get.buf_by_name (win, fname, &idx);
      ifnot (NULL is bn) {
        *thisp = My(Win).set.current_buf (win, idx, DRAW);
        return DONE;
      }
    }

    if ($my(at_frame) is frame) $my(flags) &= ~BUF_IS_VISIBLE;

    buf_t *that = My(Win).buf.new (win, fname, frame, 0);
    current_list_set (that, 0);

    int cur_idx = win->cur_idx;
    int idx = My(Win).append_buf (win, that);
    current_list_set (win, cur_idx);
    My(Win).set.current_buf (win, idx, (draw ? DRAW : DONOT_DRAW));

    *thisp = that;
    this = that;

    if (draw) self(draw);
    goto theend;
  }

  int cur_idx = this->cur_idx;
  ved_normal_bof (this, DONOT_DRAW);
  ved_delete_line (this, this->num_items, REG_BLACKHOLE);
  self(read.fname);

  ved_normal_bof (this, DONOT_DRAW);
  action_t *action = vundo_pop (this);

  for (;;) {
    act_t *act = AllocType (act);
    vundo_set (act, INSERT_LINE);
    act->idx = this->cur_idx;
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
    stack_push (action, act);
    if (NOTHING_TODO is ved_normal_down (this, 1, DONOT_ADJUST_COL, 0)) break;
  }

  self(cur.set, 0);
  ved_delete_line (this, 1, REG_BLACKHOLE);
  action_t *baction = vundo_pop (this);
  act_t *act = AllocType (act);
  state_cp (act, baction->head);
  buf_free_action (this, baction);

  stack_push (action, act);
  vundo_push (this, action);

  if (this->num_items is 0) buf_on_no_length (this);

  $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;
  $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
  $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
  $my(video_first_row) = this->current;
  $my(video_first_row_idx) = this->cur_idx;

  if (cur_idx >= this->num_items) cur_idx = this->num_items - 1;
  ved_normal_goto_linenr (this, cur_idx + 1, draw);

  if (draw) self(draw);
  MSG("reloaded");
  return DONE;

theend:
  return DONE;
}

private int ved_open_fname_under_cursor (buf_t **thisp, int frame, int force_open, int reopen) {
  buf_t *this = *thisp;
  char fname[PATH_MAX]; int fidx, lidx;
  if (NULL is get_current_word (this, fname, Notfname, Notfname_len, &fidx, &lidx))
    return NOTHING_TODO;

  ifnot (NULL is $my(ftype)->on_open_fname_under_cursor)
    $my(ftype)->on_open_fname_under_cursor (fname, lidx - fidx + 1, PATH_MAX);

  char *line = $mycur(data)->bytes + lidx + 1;
  int lnr = 0;
  if (*line is '|' or *line is ':') {
    while (*++line) {
      if ('0' <= *line and *line <= '9') lnr = (10 * lnr) + (*line - '0');
      else break;
    }
  }

  ifnot (file_exists (fname))
    ifnot (force_open) return NOTHING_TODO;

  ifnot (reopen)
    do {
      int idx;
      buf_t *bn = NULL;
      if (*fname isnot DIR_SEP) {
        char *cwd = buf_get_current_dir (this, SHARED_ALLOCATION);
        if (NULL is cwd) return NOTHING_TODO;
        int len = bytelen (cwd) + bytelen (fname) + 1;
        char nfn[len + 1];
        snprintf (nfn, len + 1, "%s%c%s", cwd, DIR_SEP, fname);
        bn = My(Win).get.buf_by_name ($my(parent), nfn,  &idx);
      } else
        bn = My(Win).get.buf_by_name ($my(parent), fname, &idx);

      if (NULL is bn) break;
      *thisp = My(Win).set.current_buf ($my(parent), idx, DONOT_DRAW);
      this = *thisp;
      if (NOTHING_TODO is ved_normal_goto_linenr (*thisp, lnr, DRAW))
        self(draw);
      return DONE;
    } while (0);

  if (frame is AT_CURRENT_FRAME) frame = $myparents(cur_frame);
  if (NOTHING_TODO is ved_edit_fname ($my(parent), thisp, fname, frame, 0,
  	DONOT_DRAW, 1))
    return NOTHING_TODO;

  this = *thisp;
  if (NOTHING_TODO is ved_normal_goto_linenr (*thisp, lnr, DRAW))
    self(draw);

  return DONE;
}

private int ved_split (buf_t **thisp, char *fname) {
  buf_t *this = *thisp;
  buf_t *that = this;

  if (win_add_frame ($my(parent)) is NOTHING_TODO)
    return NOTHING_TODO;

  ved_edit_fname ($my(parent), thisp, fname, $myparents(num_frames) - 1, 1, 0, 1);

  $myparents(cur_frame) = $myparents(num_frames) - 1;

  $from(that, flags) |= BUF_IS_VISIBLE;

  My(Win).draw ($my(parent));
  return DONE;
}

private int ved_enew_fname (buf_t **thisp, char *fname) {
  buf_t *this = *thisp;
  win_t *w = My(Ed).win.new ($my(root), NULL, 1);
  $my(root)->prev_idx = $my(root)->cur_idx;
  My(Ed).append.win ($my(root), w);
  ved_edit_fname (w, thisp, fname, 0, 1, 1, 1);
  return DONE;
}

private int ed_win_change (ed_t *this, buf_t **bufp, int com, char *name,
                                            int accept_rdonly, int force) {
  if (this->num_items is 1)
    ifnot (force) return NOTHING_TODO;

  int idx = this->cur_idx;
  int cidx = idx;

  ifnot (NULL is name) {
    if (str_eq ($from($from((*bufp), parent), name), name))
      return NOTHING_TODO;
    win_t *w = self(get.win_by_name, name, &idx);
    if (NULL is w) return NOTHING_TODO;
  } else {
change_idx:
    switch (com) {
      case VED_COM_WIN_CHANGE_PREV:
        if (--idx is -1) { idx = this->num_items - 1; } break;
      case VED_COM_WIN_CHANGE_NEXT:
        if (++idx is this->num_items) { idx = 0; } break;
      case VED_COM_WIN_CHANGE_PREV_FOCUSED:
        idx = this->prev_idx;
    }

    if (idx is cidx)
      ifnot (force) return NOTHING_TODO;

    ifnot (accept_rdonly) {
      win_t *w = self(get.win_by_idx, idx);
      if (NULL is w) return NOTHING_TODO;
      if ($from(w, type) is VED_WIN_SPECIAL_TYPE) {
        if (com is VED_COM_WIN_CHANGE_PREV_FOCUSED)
          com = VED_COM_WIN_CHANGE_PREV;

        goto change_idx;
      }
    }
  }

  self(set.current_win, idx);
  win_t *parent = this->current;
  *bufp = parent->current;

  $from((*bufp), parent) = parent;
  *bufp = My(Win).set.current_buf (parent, parent->cur_idx, DONOT_DRAW);
  My(Win).set.video_dividers (parent);
  My(Win).draw (parent);
  return DONE;
}

private int ved_win_delete (ed_t *this, buf_t **thisp, int count_special) {
  if (1 is self(get.num_win, count_special))
    return EXIT;

  win_t *parent = current_list_pop (this, win_t);
  win_free (parent);

  parent = this->current;

  *thisp = My(Win).set.current_buf (parent, parent->cur_idx, DONOT_DRAW);

  My(Win).set.video_dividers (parent);
  My(Win).draw (parent);
  return DONE;
}

private int ved_write_to_fname (buf_t *this, char *fname, int append, int fidx,
                                            int lidx, int force, int verbose) {
  if (NULL is fname) return NOTHING_TODO;
  int retval = NOTHING_TODO;

  string_t *fnstr = string_new_with (fname);
  ifnot (fnstr->num_bytes) goto theend;

  if (fnstr->bytes[fnstr->num_bytes - 1] is '%' and (NULL isnot $my(basename))) {
    My(String).clear_at(fnstr, fnstr->num_bytes - 1);
    My(String).append (fnstr, $my(basename));
  }

  int fexists = file_exists (fnstr->bytes);
  if (fexists and 0 is append and 0 is force) {
    VED_MSG_ERROR(MSG_FILE_EXISTS_AND_NO_FORCE, fnstr->bytes);
    goto theend;
  }

  ifnot (fexists) append = 0;

  FILE *fp = fopen (fnstr->bytes, (append ? "a+" : "w"));
  if (NULL is fp) {
    MSG_ERRNO (errno);
    goto theend;
  }

  int idx = 0;
  row_t *it = this->head;
  while (idx++ < fidx) it = it->next;
  idx--;

  size_t bts = 0;
  while (idx++ <= lidx) {
    bts += fprintf (fp, "%s\n", it->data->bytes);
    it = it->next;
  }

  fclose (fp);
  if (verbose)
    MSG("%s: %zd bytes written%s", fnstr->bytes, bts, (append ? " [appended]" : " "));

  retval = DONE;

theend:
  My(String).free (fnstr);
  return retval;
}

private int buf_write (buf_t *this, int force) {
  if (str_eq ($my(fname), UNAMED)) {
    VED_MSG_ERROR(MSG_CAN_NOT_WRITE_AN_UNAMED_BUFFER);
    return NOTHING_TODO;
  }

  if ($my(flags) & BUF_IS_RDONLY) {
    VED_MSG_ERROR(MSG_BUF_IS_READ_ONLY);
    return NOTHING_TODO;
  }

  ifnot ($my(flags) & BUF_IS_MODIFIED) {
    ifnot (force) {
      VED_MSG_ERROR(MSG_ON_WRITE_BUF_ISNOT_MODIFIED_AND_NO_FORCE);
      return NOTHING_TODO;
    }
  }

  if ($my(flags) & FILE_EXISTS) {
    struct stat st;
    if (NOTOK is stat ($my(fname), &st)) {
      $my(flags) &= ~FILE_EXISTS;
      utf8 chars[] = {'y', 'Y', 'n', 'N'};
      utf8 c = quest (this, str_fmt (
          "[Warning]\n%s: removed from filesystem since last operation\n"
          "continue writing? [yY|nN]", $my(fname)), chars, ARRLEN (chars));
      switch (c) {case 'n': case 'N': return NOTHING_TODO;};
    } else {
      if ($my(st).st_mtim.tv_sec isnot st.st_mtim.tv_sec) {
        utf8 chars[] = {'y', 'Y', 'n', 'N'};
        utf8 c = quest (this, str_fmt (
            "[Warning]%s: has been modified since last operation\n"
            "continue writing? [yY|nN]", $my(fname)), chars, ARRLEN (chars));
        switch (c) {case 'n': case 'N': return NOTHING_TODO;};
      }
    }
  }

  FILE *fp = fopen ($my(fname), "w");
  if (NULL is fp) {
    MSG_ERRNO(errno);
    return NOTHING_TODO;
  }

  row_t *row = this->head;

  size_t bts = 0;
  while (row isnot NULL) {
    bts += fprintf (fp, "%s\n", row->data->bytes);
    row = row->next;
  }

  $my(flags) &= ~BUF_IS_MODIFIED;
  $my(flags) |= (FILE_IS_READABLE|FILE_IS_WRITABLE|FILE_EXISTS);

  fstat (fileno (fp), &$my(st));
  fclose (fp);
  MSG("%s: %zd bytes written", $my(fname), bts);
  return DONE;
}

private int ved_buf_read_from_fp (buf_t *this, fp_t *fp) {
  mark_t t;  state_set (&t);  t.cur_idx = this->cur_idx;
  row_t *row = this->current;
  action_t *action = AllocType (action);

  char *line = NULL;
  size_t len = 0;
  size_t t_len = 0;
  ssize_t nread;
  while (-1 isnot (nread = ed_readline_from_fp (&line, &len, fp->fp))) {
    t_len += nread;
    act_t *act = AllocType (act);
    vundo_set (act, INSERT_LINE);
    buf_current_append_with (this, line);
    act->idx = this->cur_idx;
    stack_push (action, act);
  }

  ifnot (NULL is line) free (line);

  ifnot (t_len)
    free (action);
  else
    vundo_push (this, action);

  $my(flags) |= BUF_IS_MODIFIED;
  state_restore (&t);
  this->current = row; this->cur_idx = t.cur_idx;
  self(draw);
  return DONE;
}

private int ved_buf_read_from_file (buf_t *this, char *fname) {
  if (0 isnot access (fname, R_OK|F_OK)) return NOTHING_TODO;
  ifnot (file_is_reg (fname)) return NOTHING_TODO;

  fp_t fp = (fp_t) {.fp = fopen (fname, "r")};
  if (NULL is fp.fp) {
    MSG_ERRNO(errno);
    return NOTHING_TODO;
  }

  int retval = self(read.from_fp, &fp);
  fclose (fp.fp);
  return retval;
}

private string_t *vsys_which (char *ex, char *path) {
  if (NULL is ex or NULL is path) return NULL;
  size_t
    ex_len = bytelen (ex),
    p_len = bytelen (path);

  ifnot (ex_len and p_len) return NULL;
  char sep[2]; sep[0] = PATH_SEP; sep[1] = '\0';

  char *alpath = str_dup (path, p_len);
  char *sp = strtok (alpath, sep);

  string_t *ex_path = NULL;

  while (sp) {
    size_t toklen = bytelen (sp) + 1;
    char tok[ex_len + toklen + 1];
    snprintf (tok, ex_len + toklen + 1, "%s/%s", sp, ex);
    if (file_is_executable (tok)) {
      ex_path = string_new_with_len (tok, toklen + ex_len);
      break;
    }

    sp = strtok (NULL, sep);
  }

  free (alpath);
  return ex_path;
}

private int ed_sh_popen (ed_t *ed, buf_t *buf, char *com,
  int redir_stdout, int redir_stderr, PopenRead_cb read_cb) {
  (void) ed; (void) buf; (void) com; (void) redir_stdout; (void) redir_stderr;
  (void) read_cb;
  return NOTHING_TODO;
}

private int ved_buf_read_from_shell (buf_t *this, char *com, int rlcom) {
  ifnot ($my(ftype)->read_from_shell) return NOTHING_TODO;
  return My(Ed).sh.popen ($my(root), this, com, rlcom is VED_COM_READ_SHELL, 0, NULL);
}

private int ved_buf_change_bufname (buf_t **thisp, char *bufname) {
  buf_t *this = *thisp;
  if (str_eq ($my(fname), bufname)) return NOTHING_TODO;
  int idx;
  buf_t *buf = My(Win).get.buf_by_name ($my(parent), bufname, &idx);
  if (NULL is buf) return NOTHING_TODO;

  int cur_frame = $myparents(cur_frame);

  /* a little bit before the last bit of the zero cycle  */

  /* This code block reflects perfectly the development philosophy,
   * as if you analyze it, it catches two conditions, with the first
   * to be a quite common scenario, while the second it never happens
   * at least in (my) usual workflow. Now. I know there are quite few
   * more to catch, but unless someone generiously offers the code i'm
   * not going to spend the energy on them and can be safely considered
   * as undefined or unpredictable behavior.
   */

  if (cur_frame isnot $from(buf, at_frame) and
      ($from(buf, flags) & BUF_IS_VISIBLE) is 0) {
    $from(buf, at_frame) = cur_frame;
    int row = $from(buf, cur_video_row) - $from(buf, dim)->first_row;
    int old_rows = $from(buf, dim)->last_row - $from(buf, dim)->first_row;
    $from(buf, dim) = $myparents(frames_dim)[$my(at_frame)];
    $from(buf, statusline_row) = $from(buf, dim)->last_row;
    $from(buf, cur_video_row) = $from(buf, dim)->first_row + row;
    int cur_rows = $from(buf, dim)->last_row - $from(buf, dim)->first_row;
    if (cur_rows < old_rows and
        $from(buf, cur_video_row) isnot $from(buf, dim)->first_row) {
      int diff = old_rows - cur_rows;
      buf_set_video_first_row (buf,  $from(buf, video_first_row_idx) + diff);
      $from(buf, cur_video_row) -= diff;
    }
  }

  *thisp = My(Win).set.current_buf ($my(parent), idx, DONOT_DRAW);
  this = *thisp;
  $my(video)->row_pos = $my(cur_video_row);
  My(Win).draw ($my(parent));
  return DONE;
}

private int ved_buf_change (buf_t **thisp, int com) {
  buf_t *this = *thisp;

  if ($my(is_sticked)) return NOTHING_TODO;
  if (1 is $my(parent)->num_items) return NOTHING_TODO;

  int cur_frame = $myparents(cur_frame);
  int idx = $my(parent)->cur_idx;

  switch (com) {
    case VED_COM_BUF_CHANGE_PREV_FOCUSED:
      idx = $my(parent)->prev_idx;
      if (idx >= $my(parent)->num_items)
        idx = $my(parent)->num_items - 1 -
            ($my(parent)->cur_idx is $my(parent)->num_items - 1);
      goto change;

    case VED_COM_BUF_CHANGE_PREV:
      {
        buf_t *it = $my(parent)->current->prev;
        int index = idx;
        while (it) {
          index--;
          if ($from(it, at_frame) is cur_frame) {
            idx = index;
            goto change;
          }
          it = it->prev;
        }

        index = $my(parent)->num_items;
        it = $my(parent)->tail;
        while (it) {
          index--;
          if (index < idx) {
            idx = index;
            goto change;
          }

          if ($from(it, at_frame) is cur_frame and idx isnot index) {
            idx = index;
            goto change;
          }

          it = it->prev;
        }

        index = $my(parent)->num_items;
        it = $my(parent)->tail;
        while (it) {
          index--;
          if (index isnot idx) {
            idx = index;
            goto change;
          }
          it = it->prev;
        }
      }
      break;

    case VED_COM_BUF_CHANGE_NEXT:
      {
        buf_t *it = $my(parent)->current->next;
        int index = idx;
        while (it) {
          index++;
          if ($from(it, at_frame) is cur_frame) {
            idx = index;
            goto change;
          }
          it = it->next;
        }

        index = -1;
        it = $my(parent)->head;
        while (it) {
          index++;
          if (index > idx) {
            idx = index;
            goto change;
          }

          if ($from(it, at_frame) is cur_frame and idx isnot index) {
            idx = index;
            goto change;
          }

          it = it->next;
        }

        index = -1;
        it = $my(parent)->head;
        while (it) {
          index++;
          if (index isnot idx) {
            idx = index;
            goto change;
          }
          it = it->next;
        }
      }
      break;

    default: return NOTHING_TODO;
  }

change:
  *thisp = My(Win).set.current_buf ($my(parent), idx, DRAW);
  return DONE;
}

private void ved_buf_clear (buf_t *this) {
  self(free.rows);
  vundo_clear (this);
  this->head = this->tail = this->current = NULL;
  this->cur_idx = 0; this->num_items = 0;
  buf_on_no_length (this);

  $my(video_first_row) = this->head;
  $my(video_first_row_idx) = 0;

  self(cur.set, 0);
  $mycur(cur_col_idx) = $mycur(first_col_idx) = 0;
  $my(cur_video_row) = $my(dim)->first_row;
  $my(cur_video_col) = 1;
}

private int ved_buf_delete (buf_t **thisp, int idx, int force) {
  buf_t *this = *thisp;
  win_t *parent = $my(parent);

  int cur_idx = parent->cur_idx;
  int at_frame =$my(at_frame);

  if (cur_idx is idx) {
    if ($my(flags) & BUF_IS_SPECIAL) return NOTHING_TODO;
    if ($my(flags) & BUF_IS_MODIFIED) {
      ifnot (force) {
        VED_MSG_ERROR(MSG_ON_BD_IS_MODIFIED_AND_NO_FORCE);
        return NOTHING_TODO;
      }
    }

    int num = parent->num_items;
    My(Win).pop.current_buf (parent);
    ifnot (num - 1) return WIN_EXIT;
    this = parent->current;
  } else {
    current_list_set (parent, idx);
    this = parent->current;
    if ($my(flags) & BUF_IS_SPECIAL) {
      current_list_set (parent, cur_idx);
      return NOTHING_TODO;
    }

    if ($my(flags) & BUF_IS_MODIFIED) {
      ifnot (force) {
        VED_MSG_ERROR(MSG_ON_BD_IS_MODIFIED_AND_NO_FORCE);
        current_list_set (parent, cur_idx);
        return NOTHING_TODO;
      }
    }

    My(Win).pop.current_buf (parent);
    current_list_set (parent, (idx > cur_idx) ? cur_idx : cur_idx - 1);
  }

  int found = 0;
  int should_draw = 0;

  this = parent->head;
  while (this) {
    if ($my(at_frame) is at_frame) {
      found = 1;
      $my(flags) |= BUF_IS_VISIBLE;
      break;
    }

    this = this->next;
  }

  ifnot (found) win_delete_frame (parent, at_frame);

  this = parent->current;

  *thisp = My(Win).set.current_buf (parent, parent->cur_idx, DONOT_DRAW);

  if (cur_idx is idx) {
    if (found is 1 or $from(parent, num_frames) is 1) {
      if (NOTHING_TODO is ved_buf_change (thisp, VED_COM_BUF_CHANGE_PREV_FOCUSED))
        should_draw = 1;
    } else {
      int frame = WIN_CUR_FRAME(parent) + 1;
      if (frame > WIN_LAST_FRAME(parent)) frame = FIRST_FRAME;
      *thisp = My(Win).frame.change ($my(parent), frame, DONOT_DRAW);
    }
  } else should_draw = 1;

 if (0 is found or should_draw) My(Win).draw (parent);

 return DONE;
}

private int ved_complete_digraph_callback (menu_t *menu) {
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  vstr_t *items = vstr_new ();
  char *digraphs[] = {
    "167 §",  "169 ©",  "171 «",  "174 ®",  "176 °",  "178 ²",  "179 ³", "183 ·",
    "185 ¹",  "187 »",  "188 ¼",  "189 ½",  "190 ¾",  "215 ×",  "247 ÷", "729 ˙",
    "8212 —", "8220 “", "8230 …", "8304 ⁰", "8308 ⁴", "8309 ⁵", "8310 ⁶",
    "8311 ⁷", "8312 ⁸", "8313 ⁹", "8314 ⁺", "8315 ⁻", "8316 ⁼", "8317 ⁽",
    "8318 ⁾", "8319 ⁿ", "8364 €", "8531 ⅓", "8532 ⅔", "8533 ⅕", "8534 ⅖",
    "8535 ⅗", "8536 ⅘", "8537 ⅙", "8538 ⅚", "8539 ⅛", "8540 ⅜", "8541 ⅝",
    "8542 ⅞", "8771 ≃", "9833 ♩", "9834 ♪", "9835 ♫", "9836 ♬", "9837 ♭",
    "9838 ♮", "9839 ♯", "10003 ✓"
  };

  for (int i = 0; i < (int) ARRLEN (digraphs); i++)
    if (menu->patlen) {
      if (str_eq_n (digraphs[i], menu->pat, menu->patlen))
        vstr_append_current_with (items, digraphs[i]);
    } else
      vstr_append_current_with (items, digraphs[i]);

  menu->list = items;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private int ved_complete_digraph (buf_t *this, utf8 *c) {
  int retval = DONE;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
    $my(video)->col_pos, ved_complete_digraph_callback, NULL, 0);
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *item = menu_create ($my(root), menu);
  *c = 0;
  if (item isnot NULL)
    while (*item isnot ' ') *c = (10 * *c) + (*item++ - '0');

theend:
  menu_free (menu);
  return NEWCHAR;
}

private int ved_complete_arg (menu_t *menu) {
  buf_t *this = menu->this;

  int com = $my(shared_int);
  char *line = $my(shared_str)->bytes;

  if ($myroots(commands)[com]->args is NULL and
     (com < VED_COM_BUF_DELETE_FORCE or com > VED_COM_BUF_CHANGE_ALIAS)) {
    menu->state |= MENU_QUIT;
    return NOTHING_TODO;
  }
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  vstr_t *args = vstr_new ();

  int patisopt = (menu->patlen ? str_eq (menu->pat, "--bufname=") : 0);

  if ((com >= VED_COM_BUF_DELETE_FORCE and com <= VED_COM_BUF_CHANGE_ALIAS) or
       patisopt) {
    char *cur_fname = $my(fname);

    buf_t *it = $my(parent)->head;
    while (it) {
      ifnot (str_eq (cur_fname, $from(it, fname))) {
        if ((0 is menu->patlen or 1 is patisopt) or
             str_eq_n ($from(it, fname), menu->pat, menu->patlen)) {
          ifnot (patisopt) {
            size_t len = bytelen ($from(it, fname)) + 10 + 2;
            char bufn[len + 1];
            snprintf (bufn, len + 1, "--bufname=\"%s\"", $from(it, fname));
            vstr_add_sort_and_uniq (args, bufn);
          } else
            vstr_add_sort_and_uniq (args, $from(it, fname));
        }
      }
      it = it->next;
    }

    goto check_list;
  }

  int i = 0;

  ifnot (menu->patlen) {
    while ($myroots(commands)[com]->args[i])
      vstr_add_sort_and_uniq (args, $myroots(commands)[com]->args[i++]);
  } else {
    while ($myroots(commands)[com]->args[i]) {
      if (str_eq_n ($myroots(commands)[com]->args[i], menu->pat, menu->patlen))
        if (NULL is strstr (line, $myroots(commands)[com]->args[i]) or
        	str_eq ($myroots(commands)[com]->args[i], "--fname="))
          vstr_add_sort_and_uniq (args, $myroots(commands)[com]->args[i]);
      i++;
    }
  }

check_list:
  ifnot (args->num_items) {
    menu->state |= MENU_QUIT;
    vstr_free (args);
    return NOTHING_TODO;
  }

  menu->list = args;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  My(String).replace_with (menu->header, menu->pat);

  return DONE;
}

private int ved_complete_command (menu_t *menu) {
  buf_t *this = menu->this;

  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  vstr_t *coms = vstr_new ();
  int i = 0;

  ifnot (menu->patlen) {
    while ($myroots(commands)[i])
      vstr_add_sort_and_uniq (coms, $myroots(commands)[i++]->com);
  } else {
    while ($myroots(commands)[i]) {
      ifnot (strncmp ($myroots(commands)[i]->com, menu->pat, menu->patlen))
        vstr_add_sort_and_uniq (coms, $myroots(commands)[i]->com);
      i++;
    }
  }

  ifnot (coms->num_items) {
    menu->state |= MENU_QUIT;
    vstr_free (coms);
    return NOTHING_TODO;
  }

  menu->list = coms;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  My(String).replace_with (menu->header, menu->pat);

  return DONE;
}

private int ved_complete_filename (menu_t *menu) {
  buf_t *this = menu->this;
  char dir[PATH_MAX];
  int joinpath;

  if (menu->state & MENU_FINALIZE) goto finalize;

  dir[0] = '\0';
  joinpath = 0;

  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  char *sp = (menu->patlen is 0) ? NULL : menu->pat + menu->patlen - 1;
  char *end = sp;

  if (NULL is sp) {
    char *cwd = dir_current ();
    strcpy (dir, cwd);
    free (cwd);
    end = NULL;
    goto getlist;
  }

  if ('~' is menu->pat[0]) {
    if (menu->patlen is 1) {
      end = NULL;
    } else {
      end = menu->pat + 1;
      if (*end is DIR_SEP) { if (*(end + 1)) end++; else end = NULL; }
    }

    memcpy (dir, $myroots(env)->home_dir->bytes, $myroots(env)->home_dir->num_bytes);

    dir[$myroots(env)->home_dir->num_bytes] = '\0';
    joinpath = 1;
    goto getlist;
  }

  if (is_directory (menu->pat) and bytelen (path_basename (menu->pat)) > 1) {
    strcpy (dir, menu->pat);
    end = NULL;
    joinpath = 1;
    goto getlist;
  }

  if (sp is menu->pat) {
   if (*sp is DIR_SEP) {
      dir[0] = *sp; dir[1] = '\0';
      joinpath = 1;
      end = NULL;
    } else {
      char *cwd = dir_current ();
      strcpy (dir, cwd);
      free (cwd);
      end = sp;
    }

    goto getlist;
  }

  if (*sp is DIR_SEP) {
    memcpy (dir, menu->pat, menu->patlen - 1);
    dir[menu->patlen - 1] = '\0';
    end = NULL;
    joinpath = 1;
    goto getlist;
  }

  while (sp > menu->pat and *(sp - 1) isnot DIR_SEP) sp--;
  if (sp is menu->pat) {
    end = sp;
    char *cwd = dir_current ();
    strcpy (dir, cwd);
    free (cwd);
    goto getlist;
  }

  end = sp;
  sp = menu->pat;
  int i = 0;
  while (sp < end) dir[i++] = *sp++;
  dir[i] = '\0';
  joinpath = 1;

getlist:;
  int endlen = (NULL is end) ? 0 : bytelen (end);
  dirlist_t *dlist = dirlist (dir, 0);

  if (NULL is dlist) {
    menu->state |= MENU_QUIT;
    return NOTHING_TODO;
  }

  vstr_t *vs = vstr_new ();
  vstring_t *it = dlist->list->head;

  $my(shared_int) = joinpath;
  My(String).replace_with ($my(shared_str), dir);

  while (it) {
    if (end is NULL or (str_eq_n (it->data->bytes, end, endlen))) {
      vstr_add_sort_and_uniq (vs, it->data->bytes);
    }

    it = it->next;
  }

  dlist->free (dlist);

  menu->list = vs;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  string_replace_with (menu->header, menu->pat);

  return DONE;

finalize:
  menu->state &= ~MENU_FINALIZE;

  if ($my(shared_int)) {
    if ($my(shared_str)->bytes[$my(shared_str)->num_bytes - 1] is DIR_SEP)
      My(String).clear_at ($my(shared_str), $my(shared_str)->num_bytes - 1);

    int len = menu->patlen + $my(shared_str)->num_bytes + 1;
    char tmp[len + 1];
    snprintf (tmp, len + 1, "%s%c%s", $my(shared_str)->bytes, DIR_SEP, menu->pat);
    My(String).replace_with ($my(shared_str), tmp);
  } else
    My(String).replace_with ($my(shared_str), menu->pat);

  if (is_directory ($my(shared_str)->bytes))
    menu->state |= MENU_REDO;
  else
    menu->state |= MENU_DONE;

  return DONE;
}

private int ved_insert_complete_filename (buf_t **thisp) {
  buf_t *this = *thisp;
  int retval = DONE;
  int fidx = 0; int lidx = 0;
  char word[PATH_MAX + 1]; word[0] = '\0';
  if (IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)]) and (
      $mycur(data)->num_bytes > 1 and 0 is IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx) - 1])))
    ved_normal_left (this, 1, DRAW);

  get_current_word (this, word, Notfname, Notfname_len, &fidx, &lidx);
  size_t len = bytelen (word);
  size_t orig_len = len;

redo:;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
      $my(video)->col_pos,  ved_complete_filename, word, len);

  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  for (;;) {
    char *item  = menu_create ($my(root), menu);
    if (NULL is item) { retval = NOTHING_TODO; goto theend; }

    memcpy (menu->pat, item, bytelen (item));
    menu->patlen = bytelen (item);
    menu->pat[menu->patlen] = '\0';
    menu->state |= MENU_FINALIZE;

    ved_complete_filename (menu);

    if (menu->state & MENU_DONE) break;

    memcpy (word, $my(shared_str)->bytes, $my(shared_str)->num_bytes);
    len = $my(shared_str)->num_bytes;
    word[len] = '\0';
    menu_free (menu);
    goto redo;
  }

  My(String).replace_numbytes_at_with ($mycur(data), orig_len, fidx,
      $my(shared_str)->bytes);
  $my(flags) |= BUF_IS_MODIFIED;
  ved_normal_end_word (thisp, 1, 0, DONOT_DRAW);
  self(draw_cur_row);

theend:
  menu_free (menu);
  return retval;
}

private void rline_clear (rline_t *rl) {
  rl->state &= ~RL_CLEAR;
  int row = rl->first_row;
  while (row < rl->prompt_row)
    video_draw_at (rl->cur_video, row++);

  if (rl->prompt_row is $from(rl->ed, prompt_row))
    video_set_row_with (rl->cur_video, rl->prompt_row - 1, " ");
  video_draw_at (rl->cur_video, rl->prompt_row);

  if (rl->state & RL_CLEAR_FREE_LINE) {
    vstr_free (rl->line);
    rl->num_items = 0;
    rl->cur_idx = 0;
    rl->line = vstr_new ();
    vstring_t *vstr = AllocType (vstring);
    vstr->data = string_new_with_len (" ", 1);
    current_list_append (rl->line, vstr);
  }

  rl->state &= ~RL_CLEAR_FREE_LINE;
}

private void rline_free_members (rline_t *rl) {
  vstr_free (rl->line);
  arg_t *it = rl->head;
  while (it isnot NULL) {
    arg_t *tmp = it->next;
    string_free (it->argname);
    string_free (it->argval);
    free (it);
    it = tmp;
  }
  rl->num_items = 0;
  rl->cur_idx = 0;
}

private void rline_free (rline_t *rl) {
  rline_free_members (rl);
  string_free (rl->render);
  free (rl);
}

private void rline_free_api (rline_t *rl) {
  rline_clear (rl);
  rline_free (rl);
}

private int rline_history_at_beg (rline_t **rl) {
  switch ((*rl)->c) {
    case ESCAPE_KEY:
    case '\r':
    case ARROW_UP_KEY:
    case ARROW_DOWN_KEY:
    case '\t':
    (*rl)->state |= RL_POST_PROCESS;
    return RL_POST_PROCESS;
  }

  (*rl)->state |= RL_OK;
  return RL_OK;
}

private rline_t *rline_complete_history (rline_t *rl, int *idx, int dir) {
  ed_t *this = rl->ed;
  ifnot ($my(history)->rline->num_items) return rl;

  if (dir is -1) {
    if (*idx is 0)
      *idx = $my(history)->rline->num_items - 1;
    else
      *idx -= 1;
  } else {
    if (*idx is $my(history)->rline->num_items - 1)
      *idx = 0;
    else
      *idx += 1;
  }

  int lidx = 0;
  h_rlineitem_t *it = $my(history)->rline->head;

  while (lidx < *idx) { it = it->next; lidx++; }

  rline_t *lrl = rline_new (this, $my(term), My(Input).get, $my(prompt_row),
      1, $my(dim)->num_cols, $my(video));

  lrl->prompt_row = rl->first_row - 1;
  lrl->prompt_char = 0;

  RlineAtBeg_cb at_beg = rl->at_beg;
  RlineAtEnd_cb at_end = rl->at_end;
  rl->at_beg = rline_history_at_beg;
  rl->at_end = rline_break;

  int counter = $my(history)->rline->num_items;

  goto thecheck;

theiter:
  ifnot (--counter)
    goto theend;

  if (dir is -1) {
    if (it->prev is NULL) {
      lidx = $my(history)->rline->num_items - 1;
      it = $my(history)->rline->tail;
    } else {
      it = it->prev;
      lidx--;
    }
  } else {
    if (it->next is NULL) {
      lidx = 0;
      it = $my(history)->rline->head;
    } else {
      lidx++;
      it = it->next;
    }
  }

thecheck:;
#define __free_strings__ My(String).free (str); My(String).free (cur)

  string_t *str = vstr_join (it->data->line, "");
  string_t *cur = vstr_join (rl->line, "");
  if (cur->bytes[cur->num_bytes - 1] is ' ')
    My(String).clear_at (cur, cur->num_bytes - 1);
  int match = (str_eq_n (str->bytes, cur->bytes, cur->num_bytes));

  if (0 is cur->num_bytes or $my(history)->rline->num_items is 1 or match) {
    __free_strings__;
    goto theinput;
  }

  __free_strings__;
  goto theiter;

theinput:
  rline_free_members (lrl);
  lrl->line = vstr_dup (it->data->line);
  lrl->first_row = it->data->first_row;
  lrl->row_pos = it->data->row_pos;

  rline_write_and_break (lrl);

  utf8 c = rline_edit (rl)->c;
  switch (c) {
    case ESCAPE_KEY:
      goto theend;

    case ' ':
    case '\r': goto thesuccess;
    case ARROW_DOWN_KEY: dir = -1; goto theiter;
    case ARROW_UP_KEY: dir = 1; goto theiter;
    default: goto theiter;
  }

thesuccess:
  rline_free_members (rl);
  rl->line = vstr_dup (it->data->line);
  rl->first_row = it->data->first_row;
  rl->row_pos = it->data->row_pos;

theend:
  rline_clear (lrl);
  video_draw_at (rl->cur_video, rl->first_row); // is minus one
  rline_free (lrl);
  rl->at_beg = at_beg;
  rl->at_end = at_end;
  return rl;
}

private void ved_rline_history_push (rline_t *rl) {
  ed_t *this = rl->ed;
  if ($my(max_num_hist_entries) < $my(history)->rline->num_items) {
    h_rlineitem_t *tmp = list_pop_tail ($my(history)->rline, h_rlineitem_t);
    rline_free (tmp->data);
    free (tmp);
  }

  h_rlineitem_t *hrl = AllocType (h_rlineitem);
  hrl->data = rl;
  current_list_prepend ($my(history)->rline, hrl);
}

private void ved_rline_history_push_api (rline_t *rl) {
  rline_clear (rl);
  rl->state &= ~RL_CLEAR_FREE_LINE;
  ved_rline_history_push (rl);
}

private vstring_t *ved_rline_parse_command (rline_t *rl) {
  vstring_t *it = rl->line->head;
  char com[MAXCOMLEN]; com[0] = '\0';
  int com_idx = 0;
  while (it isnot NULL and it->data->bytes[0] is ' ') it = it->next;
  if (it isnot NULL and it->data->bytes[0] is '!') {
    com[0] = '!'; com[1] = '\0';
    goto get_command;
  }

  while (it isnot NULL and it->data->bytes[0] isnot ' ') {
    for (size_t zi = 0; zi < it->data->num_bytes; zi++)
      com[com_idx++] = it->data->bytes[zi];
    it = it->next;
  }
  com[com_idx] = '\0';

get_command:
  rl->com = RL_NO_COMMAND;

  int i = 0;
  for (i = 0; i < rl->commands_len; i++) {
    if (str_eq (rl->commands[i]->com, com)) {
      rl->com = i;
      break;
    }
  }

  return it;
}

private void ved_rline_set_line (rline_t *rl, char *bytes, size_t len) {
  BYTES_TO_RLINE (rl, bytes, (int) len);
  rline_write_and_break (rl);
}

private string_t *ved_rline_get_line (rline_t *rl) {
  return vstr_join (rl->line, "");
}

private string_t *ved_rline_get_command (rline_t *rl) {
  string_t *str = string_new (8);
  vstring_t *it = rl->line->head;

  while (it isnot NULL and it->data->bytes[0] isnot ' ') {
    string_append (str, it->data->bytes);
    it = it->next;
  }

  return str;
}

private int ved_rline_tab_completion (rline_t *rl) {
  ifnot (rl->line->num_items) return RL_OK;
  int retval = RL_OK;
  ed_t *this = rl->ed;
  buf_t *curbuf = this->current->current;

  string_t *currline = NULL;  // otherwise segfaults on certain conditions
redo:;
  currline = vstr_join (rl->line, "");
  char *sp = currline->bytes + rl->line->cur_idx;
  char *cur = sp;

  while (sp > currline->bytes and *(sp - 1) isnot ' ') sp--;
  int fidx = sp - currline->bytes;
  char tok[cur - sp + 1];
  int toklen = 0;
  while (sp < cur) tok[toklen++] = *sp++;
  tok[toklen] = '\0';

  int orig_len = toklen;
  int type = 0;

  if (fidx is 0) {
    type |= RL_TOK_COMMAND;
  } else {
    ved_rline_parse_command (rl);

    $from(curbuf, shared_int) = rl->com;
    My(String).replace_with ($from(curbuf, shared_str), currline->bytes);


    if (rl->com isnot RL_NO_COMMAND) {
      if (str_eq_n (tok, "--fname=", 8)) {
        type |= RL_TOK_ARG_FILENAME;
        int len = 8 + (tok[8] is '"');
        char tmp[toklen - len + 1];
        int i;
        for (i = len; i < toklen; i++) tmp[i-len] = tok[i];
        tmp[i-len] = '\0';
        strcpy (tok, tmp);
        toklen = i-len;
      } else if (tok[0] is '-') {
        type |= RL_TOK_ARG;
        ifnot (NULL is strstr (tok, "="))
          type |= RL_TOK_ARG_OPTION;
      } else {
        if (rl->com >= VED_COM_BUF_DELETE_FORCE and
            rl->com <= VED_COM_BUF_CHANGE_ALIAS)
          type |= RL_TOK_ARG;
        else
          type |= RL_TOK_ARG_FILENAME;
      }
    }
  }

  My(String).free (currline);

  ifnot (type) return retval;

  int (*process_list) (menu_t *) = NULL;
  if (type & RL_TOK_ARG_FILENAME)
    process_list = ved_complete_filename;
  else if (type & RL_TOK_COMMAND)
    process_list = ved_complete_command;
  else if (type & RL_TOK_ARG)
    process_list = ved_complete_arg;

  menu_t *menu = menu_new (this, $my(prompt_row) - 2, $my(prompt_row) - 2, 0,
      process_list, tok, toklen);
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  menu->state &= ~RL_IS_VISIBLE;

  if (type & RL_TOK_ARG_FILENAME)
    menu->clear_and_continue_on_backspace = 1;

  menu->return_if_one_item = 1;

  char *item;
  for (;;) {
    item = menu_create (this, menu);

    if (NULL is item) goto theend;
    if (menu->state & MENU_QUIT) break;
    if (type & RL_TOK_COMMAND or type & RL_TOK_ARG) break;

    int len = bytelen (item);
    memcpy (menu->pat, item, len);
    menu->pat[len] = '\0';
    menu->patlen = len;

    if (type & RL_TOK_ARG_FILENAME) menu->state |= MENU_FINALIZE;

    if (menu->process_list (menu) is NOTHING_TODO) goto theend;

    if (menu->state & (MENU_REDO|MENU_DONE)) break;
    if (menu->state & MENU_QUIT) goto theend;
  }

  if (type & RL_TOK_ARG_FILENAME) {
    ifnot (menu->state & MENU_REDO)
      if (rl->com isnot VED_COM_READ_SHELL and rl->com isnot VED_COM_SHELL) {
        string_prepend ($from(curbuf, shared_str), "--fname=\"");
        string_append_byte ($from(curbuf, shared_str), '"');
      }

    item = $from(curbuf, shared_str)->bytes;
  }

  ifnot (type & RL_TOK_ARG_OPTION) {
    current_list_set (rl->line, fidx);
    int lidx = fidx + orig_len;
    while (fidx++ < lidx) {
      vstring_t *tmp = current_list_pop (rl->line, vstring_t);
      string_free (tmp->data);
      free (tmp);
    }
  }

  BYTES_TO_RLINE (rl, item, (int) bytelen (item));

  if (menu->state & MENU_REDO) {
    menu_free (menu);
    goto redo;
  }

theend:
  menu_free (menu);
  return RL_OK;
}

private int rline_call_at_beg (rline_t **rl) {
  (*rl)->state |= RL_OK;
  return RL_OK;
}

private int rline_call_at_end (rline_t **rl) {
  (*rl)->state |= RL_OK;
  return RL_OK;
}

private int rline_break (rline_t **rl) {
  (*rl)->state |= RL_BREAK;
  return RL_BREAK;
}

private int rline_tab_completion (rline_t *rl) {
  (void) rl;
  return NOTHING_TODO;
}

private void ved_deinit_commands (ed_t *this) {
  if (NULL is $my(commands)) return;

  int i = 0;
  while ($my(commands)[i]) {
    free ($my(commands)[i]->com);
    if ($my(commands)[i]->args isnot NULL) {
      int j = 0;
      while ($my(commands)[i]->args[j] isnot NULL)
        free ($my(commands)[i]->args[j++]);
      free ($my(commands)[i]->args);
      }

    free ($my(commands)[i]);
    i++;
  }

  free ($my(commands)); $my(commands) = NULL;
}

private void ved_realloc_command_arg (rlcom_t *rlcom, int num) {
  int orig_num = rlcom->num_args;
  rlcom->num_args = num;
  rlcom->args = Realloc (rlcom->args, sizeof (char *) * (rlcom->num_args + 1));
  for (int i = orig_num; i <= num; i++)
    rlcom->args[i] = NULL;
}

private void ved_add_command_arg (rlcom_t *rlcom, int flags) {
#define ADD_ARG(arg, len, idx) ({                             \
  if (idx is rlcom->num_args)                                 \
    ved_realloc_command_arg (rlcom, idx);                     \
  rlcom->args[idx] = str_dup (arg, len);                      \
  idx++;                                                      \
})

  int i = 0;
  if (flags & RL_ARG_INTERACTIVE) ADD_ARG ("--interactive", 13, i);
  if (flags & RL_ARG_BUFNAME) ADD_ARG ("--bufname=", 10, i);
  if (flags & RL_ARG_RANGE) ADD_ARG ("--range=", 8, i);
  if (flags & RL_ARG_GLOBAL) ADD_ARG ("--global", 8, i);
  if (flags & RL_ARG_APPEND) ADD_ARG ("--append", 8, i);
  if (flags & RL_ARG_FILENAME) ADD_ARG ("--fname=", 8, i);
  if (flags & RL_ARG_SUB) ADD_ARG ("--sub=", 6, i);
  if (flags & RL_ARG_PATTERN) ADD_ARG ("--pat=", 6, i);
  if (flags & RL_ARG_VERBOSE) ADD_ARG ("--verbose", 9, i);
  if (flags & RL_ARG_RECURSIVE) ADD_ARG ("--recursive", 11, i);
}

private void ved_append_command_arg (ed_t *this, char *com, char *argname, size_t len) {
  if (len <= 0) len = bytelen (argname);

  int i = 0;
  while (i < $my(num_commands)) {
    if (str_eq ($my(commands)[i]->com, com)) {
      int idx = 0;
      int found = 0;
      while (idx < $my(commands)[i]->num_args) {
        if (NULL is $my(commands)[i]->args[idx]) {
          $my(commands)[i]->args[idx] = str_dup (argname, len);
          found = 1;
          break;
        }
        idx++;
      }

      ifnot (found) {
        ved_realloc_command_arg ($my(commands)[i], $my(commands)[i]->num_args + 1);
        $my(commands)[i]->args[$my(commands)[i]->num_args - 1] = str_dup (argname, len);
      }

      return;
    }
    i++;
  }
}

private void ved_append_rline_commands (ed_t *this, char **commands,
                     int commands_len, int num_args[], int flags[]) {
  int len = $my(num_commands) + commands_len;

  ifnot ($my(num_commands))
    $my(commands) = Alloc (sizeof (rlcom_t) * (commands_len + 1));
  else
    $my(commands) = Realloc ($my(commands), sizeof (rlcom_t) * (len + 1));

  int j = 0;
  int i = $my(num_commands);
  for (; i < len; i++, j++) {
    $my(commands)[i] = AllocType (rlcom);
    $my(commands)[i]->com = Alloc (bytelen (commands[j]) + 1);
    strcpy ($my(commands)[i]->com, commands[j]);

    ifnot (num_args[j]) {
      $my(commands)[i]->args = NULL;
      continue;
    }

    $my(commands)[i]->args = Alloc (sizeof (char *) * ((int) num_args[j] + 1));
    $my(commands)[i]->num_args = num_args[j];
    for (int k = 0; k <= num_args[j]; k++)
      $my(commands)[i]->args[k] = NULL;

    ved_add_command_arg ($my(commands)[i], flags[j]);
  }

  $my(commands)[len] = NULL;
  $my(num_commands) = len;
}

private void ved_append_rline_command (ed_t *this, char *name, int args, int flags) {
  char *commands[2] = {name, NULL};
  int largs[] = {args, 0};
  int lflags[] = {flags, 0};
  ved_append_rline_commands (this, commands, 1, largs, lflags);
}

private int ved_get_num_rline_commands (ed_t *this) {
  return $my(num_commands);
}

private void ved_init_commands (ed_t *this) {
  ifnot (NULL is $my(commands)) return;

  char *ved_commands[VED_COM_END + 1] = {
    [VED_COM_BUF_CHANGE_NEXT] = "bufnext",
    [VED_COM_BUF_CHANGE_NEXT_ALIAS] = "bn",
    [VED_COM_BUF_CHANGE_PREV_FOCUSED] = "bufprevfocused",
    [VED_COM_BUF_CHANGE_PREV_FOCUSED_ALIAS] = "b`",
    [VED_COM_BUF_CHANGE_PREV] = "bufprev",
    [VED_COM_BUF_CHANGE_PREV_ALIAS] = "bp",
    [VED_COM_BUF_DELETE_FORCE] = "bufdelete!",
    [VED_COM_BUF_DELETE_FORCE_ALIAS] = "bd!",
    [VED_COM_BUF_DELETE] = "bufdelete",
    [VED_COM_BUF_DELETE_ALIAS] = "bd",
    [VED_COM_BUF_CHANGE] = "buffer",
    [VED_COM_BUF_CHANGE_ALIAS] = "b",
    [VED_COM_DIFF_BUF] = "diffbuf",
    [VED_COM_DIFF] = "diff",
    [VED_COM_EDIT_FORCE] = "edit!",
    [VED_COM_EDIT_FORCE_ALIAS] = "e!",
    [VED_COM_EDIT] = "edit",
    [VED_COM_EDIT_ALIAS] = "e",
    [VED_COM_ENEW] = "enew",
    [VED_COM_ETAIL] = "etail",
    [VED_COM_GREP] = "vgrep",
    [VED_COM_MESSAGES] = "messages",
    [VED_COM_QUIT_FORCE] = "quit!",
    [VED_COM_QUIT_FORCE_ALIAS] = "q!",
    [VED_COM_QUIT] = "quit",
    [VED_COM_QUIT_ALIAS] = "q",
    [VED_COM_READ] = "read",
    [VED_COM_READ_ALIAS] = "r",
    [VED_COM_READ_SHELL] = "r!",
    [VED_COM_REDRAW] = "redraw",
    [VED_COM_SCRATCH] = "scratch",
    [VED_COM_SEARCHES] = "searches",
    [VED_COM_SHELL] = "!",
    [VED_COM_SPLIT] = "split",
    [VED_COM_SUBSTITUTE] = "substitute",
    [VED_COM_SUBSTITUTE_WHOLE_FILE_AS_RANGE] = "s%",
    [VED_COM_SUBSTITUTE_ALIAS] = "s",
    [VED_COM_TEST_KEY] = "testkey",
    [VED_COM_WIN_CHANGE_NEXT] = "winnext",
    [VED_COM_WIN_CHANGE_NEXT_ALIAS] = "wn",
    [VED_COM_WIN_CHANGE_PREV_FOCUSED] = "winprevfocused",
    [VED_COM_WIN_CHANGE_PREV_FOCUSED_ALIAS] = "w`",
    [VED_COM_WIN_CHANGE_PREV] = "winprev",
    [VED_COM_WIN_CHANGE_PREV_ALIAS] = "wp",
    [VED_COM_WRITE_FORCE] = "write!",
    [VED_COM_WRITE_FORCE_ALIAS] = "w!",
    [VED_COM_WRITE] = "write",
    [VED_COM_WRITE_ALIAS] = "w",
    [VED_COM_WRITE_QUIT_FORCE] = "wq!",
    [VED_COM_WRITE_QUIT] = "wq",
    [VED_COM_END] = NULL
  };

  int num_args[VED_COM_END + 1] = {
    [VED_COM_BUF_DELETE_FORCE ... VED_COM_BUF_DELETE_ALIAS] = 1,
    [VED_COM_BUF_CHANGE ... VED_COM_BUF_CHANGE_ALIAS] = 1,
    [VED_COM_EDIT ... VED_COM_ENEW] = 1,
    [VED_COM_GREP] = 2,
    [VED_COM_READ ... VED_COM_READ_ALIAS] = 1,
    [VED_COM_SPLIT] = 1,
    [VED_COM_SUBSTITUTE ... VED_COM_SUBSTITUTE_ALIAS] = 5,
    [VED_COM_WRITE_FORCE ... VED_COM_WRITE_ALIAS] = 4,
  };

  int flags[VED_COM_END + 1] = {
    [VED_COM_BUF_DELETE_FORCE ... VED_COM_BUF_DELETE_ALIAS] = RL_ARG_BUFNAME,
    [VED_COM_BUF_CHANGE ... VED_COM_BUF_CHANGE_ALIAS] = RL_ARG_BUFNAME,
    [VED_COM_EDIT ... VED_COM_ENEW] = RL_ARG_FILENAME,
    [VED_COM_GREP] = RL_ARG_FILENAME|RL_ARG_PATTERN,
    [VED_COM_READ ... VED_COM_READ_ALIAS] = RL_ARG_FILENAME,
    [VED_COM_SPLIT] = RL_ARG_FILENAME,
    [VED_COM_SUBSTITUTE ... VED_COM_SUBSTITUTE_ALIAS] =
      RL_ARG_RANGE|RL_ARG_GLOBAL|RL_ARG_PATTERN|RL_ARG_SUB|RL_ARG_INTERACTIVE,
    [VED_COM_WRITE_FORCE ... VED_COM_WRITE_ALIAS] =
      RL_ARG_FILENAME|RL_ARG_RANGE|RL_ARG_BUFNAME|RL_ARG_APPEND,
  };

  $my(commands) = Alloc (sizeof (rlcom_t) * (VED_COM_END + 1));

  int i = 0;
  for (i = 0; i < VED_COM_END; i++) {
    $my(commands)[i] = AllocType (rlcom);
    $my(commands)[i]->com = Alloc (bytelen (ved_commands[i]) + 1);
    strcpy ($my(commands)[i]->com, ved_commands[i]);

    ifnot (num_args[i]) {
      $my(commands)[i]->args = NULL;
      continue;
    }

    $my(commands)[i]->args = Alloc (sizeof (char *) * (num_args[i] + 1));
    $my(commands)[i]->num_args = num_args[i];
    for (int j = 0; j <= num_args[i]; j++)
      $my(commands)[i]->args[j] = NULL;

    ved_add_command_arg ($my(commands)[i], flags[i]);
  }

  $my(commands)[i] = NULL;
  $my(num_commands) = VED_COM_END;
}

private void rline_write_and_break (rline_t *rl){
  rl->state |= (RL_WRITE|RL_BREAK);
  rline_edit (rl);
}

private void rline_insert_char_and_break (rline_t *rl) {
  rl->state |= (RL_INSERT_CHAR|RL_BREAK);
  rline_edit (rl);
}

private string_t *rline_get_string (rline_t *rl) {
  return vstr_join (rl->line, "");
}

private int rline_calc_columns (rline_t *rl, int num_cols) {
  int cols = rl->first_col + num_cols;
  while (cols > $from(rl->ed, dim)->num_cols) cols--;
  return cols;
}

private rline_t *rline_new (ed_t *ed, term_t *this, InputGetch_cb getch,
  int prompt_row, int first_col, int num_cols, video_t *video) {
  rline_t *rl = AllocType (rline);
  rl->ed = ed;
  rl->term = this;
  rl->cur_video = video;
  rl->getch = getch;
  rl->at_beg = rline_call_at_beg;
  rl->at_end = rline_call_at_end;
  rl->tab_completion = rline_tab_completion;

  rl->prompt_char = DEFAULT_PROMPT_CHAR;
  rl->prompt_row = prompt_row;
  rl->first_row = prompt_row;
  rl->first_col = first_col;
  rl->num_cols = rline_calc_columns (rl, num_cols);
  rl->row_pos = rl->prompt_row;
  rl->fd = $my(out_fd);
  rl->render = string_new (num_cols);
  rl->line = vstr_new ();
  vstring_t *s = AllocType (vstring);
  s->data = string_new_with_len (" ", 1);
  current_list_append (rl->line, s);

  rl->state |= (RL_OK|RL_IS_VISIBLE);
  rl->opts |= (RL_OPT_HAS_HISTORY_COMPLETION|RL_OPT_HAS_TAB_COMPLETION);
  return rl;
}

private rline_t *ved_rline_new (ed_t *ed, term_t *this, InputGetch_cb getch,
   int prompt_row, int first_col, int num_cols, video_t *video) {
  rline_t *rl = rline_new (ed, this, getch , prompt_row, first_col, num_cols, video) ;
  if ($from (ed, commands) is NULL) ved_init_commands (ed);
  rl->commands = $from(ed, commands);
  rl->commands_len = $from(ed, num_commands);
  rl->tab_completion = ved_rline_tab_completion;
  return rl;
}

private rline_t *ved_rline_new_api (ed_t *this) {
   return ved_rline_new (this, $my(term), My(Input).get, $my(prompt_row), 1,
     $my(dim)->num_cols, $my(video));
}

private void rline_render (rline_t *rl) {
  int has_prompt_char = (rl->prompt_char isnot 0);
  string_clear (rl->render);

  if (rl->state & RL_SET_POS) goto set_pos;

  vstring_t *chars = rl->line->head;
  rl->row_pos = rl->prompt_row - rl->num_rows + 1;

  string_append_fmt (rl->render, "%s" TERM_GOTO_PTR_POS_FMT
      TERM_SET_COLOR_FMT "%c", TERM_CURSOR_HIDE, rl->first_row,
      rl->first_col, COLOR_PROMPT, rl->prompt_char);

  int cidx = 0;

  for (int i = 0; i < rl->num_rows; i++) {
    if (i)
      string_append_fmt (rl->render, TERM_GOTO_PTR_POS_FMT, rl->first_row + i,
          rl->first_col);

    for (cidx = 0; (cidx + (i * rl->num_cols) + (i == 0 ? has_prompt_char : 0))
       < rl->line->num_items and cidx < (rl->num_cols -
          (i == 0 ? has_prompt_char : 0)); cidx++) {
      string_append (rl->render, chars->data->bytes);
      chars = chars->next;
    }
  }

  while (cidx++ < rl->num_cols) string_append_byte (rl->render, ' ');

  string_append_fmt (rl->render, "%s%s", TERM_COLOR_RESET,
    (rl->state & RL_CURSOR_HIDE) ? "" : TERM_CURSOR_SHOW);

set_pos:
  rl->state &= ~RL_SET_POS;
  int row = rl->first_row;
  int col = rl->first_col;

  row += ((rl->line->cur_idx + has_prompt_char) / rl->num_cols);
  col += ((rl->line->cur_idx + has_prompt_char) < rl->num_cols
    ? has_prompt_char + rl->line->cur_idx
    : ((rl->line->cur_idx + has_prompt_char) % rl->num_cols));

  string_append_fmt (rl->render, TERM_GOTO_PTR_POS_FMT, row, col);
}

private void rline_write (rline_t *rl) {
  int orig_first_row = rl->first_row;

  if (rl->line->num_items + 1 <= rl->num_cols) {
    rl->num_rows = 1;
    rl->first_row = rl->prompt_row;
  } else {
    int mod = rl->line->num_items % rl->num_cols;
    rl->num_rows = (rl->line->num_items / rl->num_cols) + (mod isnot 0);
    if  (rl->num_rows is 0) rl->num_rows = 1;
    rl->first_row = rl->prompt_row - rl->num_rows + 1;
  }

  while (rl->first_row > orig_first_row)
    video_draw_at (rl->cur_video, orig_first_row++);

  rline_render (rl);
  fd_write (rl->fd, rl->render->bytes, rl->render->num_bytes);
  rl->state &= ~RL_WRITE;
}

private void ved_rline_reg (rline_t *rl) {
  ed_t *this = rl->ed;
  int regidx = ed_register_get_idx (this, My(Input).get ($my(term)));
  if (-1 is regidx) return;

  buf_t *buf = self(get.current_buf);
  if (ERROR is ed_register_special_set (this, buf, regidx))
    return;

  rg_t *rg = &$my(regs)[regidx];
  if (rg->type is LINEWISE) return;

  reg_t *reg = rg->head;
  while (reg isnot NULL) {
    BYTES_TO_RLINE (rl, reg->data->bytes, (int) reg->data->num_bytes);
    reg = reg->next;
  }
}

private rline_t *rline_edit (rline_t *rl) {
  ed_t *this = rl->ed; (void) this;
  vstring_t *ch;
  int retval;

  if (rl->state & RL_CLEAR) {
    rline_clear (rl);
    if (rl->state & RL_BREAK) goto theend;
  }

  if (rl->state & RL_WRITE) {
    if (rl->state & RL_IS_VISIBLE)
      rline_write (rl);
    else
      rl->state &= ~RL_WRITE;

    if (rl->state & RL_BREAK) goto theend;
  }

  if (rl->state & RL_PROCESS_CHAR) goto process_char;
  if (rl->state & RL_INSERT_CHAR) goto insert_char;

  for (;;) {
thecontinue:
    rl->state &= ~RL_CONTINUE;
    if (rl->state & RL_IS_VISIBLE) rline_write (rl);

    rl->c = rl->getch (rl->term);

    retval = rl->at_beg (&rl);
    switch (retval) {
      case RL_OK: break;
      case RL_BREAK: goto theend; // CHANGE debug
      case RL_PROCESS_CHAR: goto process_char;
      case RL_CONTINUE: goto thecontinue;
      case RL_POST_PROCESS: goto post_process;
    }

process_char:
    rl->state &= ~RL_PROCESS_CHAR;

    switch (rl->c) {
      case ESCAPE_KEY:
      case '\r':
        goto theend;

      case ARROW_UP_KEY:
      case ARROW_DOWN_KEY:
        ifnot (rl->opts & RL_OPT_HAS_HISTORY_COMPLETION) goto post_process;
        $my(history)->rline->history_idx = (rl->c is ARROW_DOWN_KEY
           ? 0 : $my(history)->rline->num_items - 1);
        rl = rline_complete_history (rl, &$my(history)->rline->history_idx,
           (rl->c is ARROW_DOWN_KEY ? -1 : 1));
        goto post_process;

      case ARROW_LEFT_KEY:
         if (rl->line->cur_idx > 0) {
           rl->line->current = rl->line->current->prev;
           rl->line->cur_idx--;
           rl->state |= RL_SET_POS;
         }
         goto post_process;

      case ARROW_RIGHT_KEY:
         if (rl->line->cur_idx < (rl->line->num_items - 1)) {
           rl->line->current = rl->line->current->next;
           rl->line->cur_idx++;
           rl->state |= RL_SET_POS;
         }
         goto post_process;

      case HOME_KEY:
      case CTRL('a'):
        rl->line->cur_idx = 0;
        rl->line->current = rl->line->head;
        rl->state |= RL_SET_POS;
        goto post_process;

      case END_KEY:
      case CTRL('e'):
        rl->line->cur_idx = (rl->line->num_items - 1);
        rl->line->current =  rl->line->tail;
        rl->state |= RL_SET_POS;
        goto post_process;

      case DELETE_KEY:
        if (rl->line->cur_idx is (rl->line->num_items - 1) or
           (rl->line->cur_idx is 0 and rl->line->current->data->bytes[0] is ' ' and
            rl->line->num_items is 0))
          goto post_process;
        {
          vstring_t *tmp = current_list_pop (rl->line, vstring_t);
          if (NULL isnot tmp) {string_free (tmp->data); free (tmp);}
        }
        goto post_process;

      case BACKSPACE_KEY: {
          if (rl->line->cur_idx is 0) continue;
          rl->line->current = rl->line->current->prev;
          rl->line->cur_idx--;
          vstring_t *tmp = current_list_pop (rl->line, vstring_t);
          if (NULL isnot tmp) {string_free (tmp->data); free (tmp);}
        }
        goto post_process;

      case CTRL('l'):
        rl->state |= RL_CLEAR_FREE_LINE;
        rline_clear (rl);
        goto post_process;

      case CTRL('r'):
        ved_rline_reg (rl);
        goto post_process;

      case '\t':
        ifnot (rl->opts & RL_OPT_HAS_TAB_COMPLETION) goto post_process;
        retval = rl->tab_completion (rl);
        switch (retval) {
          case RL_PROCESS_CHAR: goto process_char;
        }
        goto post_process;

      default:
insert_char:
        rl->state &= ~RL_INSERT_CHAR;

        if (rl->c < ' ' or
            rl->c is INSERT_KEY or
           (rl->c > 0x7f and (rl->c < 0x0a0 or (rl->c >= FN_KEY(1) and rl->c <= FN_KEY(12)) or
           (rl->c >= ARROW_DOWN_KEY and rl->c < HOME_KEY) or
           (rl->c > HOME_KEY and (rl->c is PAGE_DOWN_KEY or rl->c is PAGE_UP_KEY or rl->c is END_KEY))
           ))) {
          if (rl->state & RL_BREAK) goto theend;
          goto post_process;
        }

        if (rl->c < 0x80) {
          ch = AllocType (vstring);
          ch->data = string_new_with_fmt ("%c", rl->c);
        } else {
          ch = AllocType (vstring);
          char buf[5]; int len;
          ch->data = string_new_with (ustring_character (rl->c, buf, &len));
        }

        if (rl->line->cur_idx is rl->line->num_items - 1 and ' ' is rl->line->current->data->bytes[0]) {
          current_list_prepend (rl->line, ch);
          rl->line->current = rl->line->current->next;
          rl->line->cur_idx++;
        } else if (rl->line->cur_idx isnot rl->line->num_items - 1) {
          current_list_prepend (rl->line, ch);
          rl->line->current = rl->line->current->next;
          rl->line->cur_idx++;
        }
        else
          current_list_append (rl->line, ch);

        if (rl->state & RL_BREAK) goto theend;
        goto post_process;
    }

post_process:
    rl->state &= ~RL_POST_PROCESS;
    if (rl->state & RL_BREAK) goto theend;
    retval = rl->at_end (&rl);
    switch (retval) {
      case RL_BREAK: goto theend;
      case RL_PROCESS_CHAR: goto process_char;
      case RL_CONTINUE: goto thecontinue;
    }
  }

theend:;
  rl->state &= ~RL_BREAK;
  return rl;
}

private rline_t *ved_rline_parse (buf_t *this, rline_t *rl) {
  vstring_t *it = ved_rline_parse_command (rl);

  while (it) {
    int type = 0;
    if (str_eq (it->data->bytes, " ")) goto itnext;

    arg_t *arg = AllocType (arg);
    string_t *opt = NULL;

    if (it->data->bytes[0] is '-') {
      if (it->next and it->next->data->bytes[0] is '-') {
        type |= RL_TOK_ARG_LONG;
        it = it->next;
      } else
        type |= RL_TOK_ARG_SHORT;

      it = it->next;
      if (it is NULL) {
        MSG_ERRNO (RL_ARGUMENT_MISSING_ERROR);
        rl->com = RL_ARGUMENT_MISSING_ERROR;
        goto theerror;
      }

      opt = My(String).new (8);
      while (it) {
        if (it->data->bytes[0] is ' ')
          goto arg_type;

        if (it->data->bytes[0] is '=') {
          if (it->next is NULL) {
            MSG_ERRNO (RL_ARG_AWAITING_STRING_OPTION_ERROR);
            rl->com = RL_ARG_AWAITING_STRING_OPTION_ERROR;
            goto theerror;
          }

          it = it->next;

          arg->argval = My(String).new (8);
          int is_quoted = '"' is it->data->bytes[0];
          if (is_quoted) it = it->next;

          while (it) {
            if (' ' is it->data->bytes[0])
              ifnot (is_quoted) {
                type |= RL_TOK_ARG_OPTION;
                goto arg_type;
              }

            if ('"' is it->data->bytes[0])
              if (is_quoted) {
                is_quoted = 0;
                if (arg->argval->bytes[arg->argval->num_bytes - 1] is '\\' and
                    arg->argval->bytes[arg->argval->num_bytes - 2] isnot '\\') {
                  arg->argval->bytes[arg->argval->num_bytes - 1] = '"';
                  is_quoted = 1;
                  it = it->next;
                  continue;
                }
                else { /* accept empty string --opt="" */
                  type |= RL_TOK_ARG_OPTION;
                  goto arg_type;
                }
              }

            string_append (arg->argval, it->data->bytes);
            it = it->next;
          }

          if (is_quoted){
            MSG_ERRNO (RL_UNTERMINATED_QUOTED_STRING_ERROR);
            rl->com = RL_UNTERMINATED_QUOTED_STRING_ERROR;
            goto theerror;
          }

          goto arg_type;
        }

        string_append (opt, it->data->bytes);
        it = it->next;
      }

arg_type:
      if (arg->argname isnot NULL) {
        My(String).replace_with (arg->argname, opt->bytes);
      } else
        arg->argname = My(String).new_with (opt->bytes);

      if (type & RL_TOK_ARG_OPTION) {
        if (str_eq (opt->bytes, "pat"))
          arg->type |= RL_ARG_PATTERN;
        else if (str_eq (opt->bytes, "sub"))
          arg->type |= RL_ARG_SUB;
        else if (str_eq (opt->bytes, "range"))
          arg->type |= RL_ARG_RANGE;
        else if (str_eq (opt->bytes, "bufname"))
          arg->type |= RL_ARG_BUFNAME;
        else if (str_eq (opt->bytes, "fname"))
          arg->type |= RL_ARG_FILENAME;
        else {
          arg->type |= RL_ARG_ANYTYPE;
          int found_arg = 0;
          if (rl->com < rl->commands_len) {
            int idx = 0;
            while (idx < rl->commands[rl->com]->num_args) {
              ifnot (NULL is rl->commands[rl->com]->args[idx]) {
                if (str_eq_n (opt->bytes, rl->commands[rl->com]->args[idx]+2, opt->num_bytes)) {
                  found_arg = 1;
                  break;
                }
              }
              idx++;
            }
          }

          ifnot (found_arg) {
            MSG_ERRNO (RL_UNRECOGNIZED_OPTION);
            rl->com = RL_UNRECOGNIZED_OPTION;
          }
        }

        goto argtype_succeed;
      } else {
        if (str_eq (opt->bytes, "i") or str_eq (opt->bytes, "interactive"))
          arg->type |= RL_ARG_INTERACTIVE;
        else if (str_eq (opt->bytes, "global"))
          arg->type |= RL_ARG_GLOBAL;
        else if (str_eq (opt->bytes, "append"))
          arg->type |= RL_ARG_APPEND;
        else if (str_eq (opt->bytes, "verbose"))
          arg->type |= RL_ARG_VERBOSE;
        else if (str_eq (opt->bytes, "r") or str_eq (opt->bytes, "recursive"))
          arg->type |= RL_ARG_RECURSIVE;
        else
          arg->type |= RL_ARG_ANYTYPE;

        goto argtype_succeed;
      }

theerror:
      My(String).free (opt);
      My(String).free (arg->argname);
      My(String).free (arg->argval);
      free (arg);
      goto theend;

argtype_succeed:
      My(String).free (opt);
      goto append_arg;
    } else {
      arg->argname = My(String).new_with (it->data->bytes);
      it = it->next;
      while (it isnot NULL and 0 is (str_eq (it->data->bytes, " "))) {
        My(String).append (arg->argname, it->data->bytes);
        it = it->next;
      }

      if (rl->com is VED_COM_BUF_CHANGE or rl->com is VED_COM_BUF_CHANGE_ALIAS) {
        opt = My(String).new_with ("bufname");
        arg->argval = My(String).new_with (arg->argname->bytes);
        type |= RL_TOK_ARG_OPTION;
        goto arg_type;
      }

      char *glob = byte_in_str (arg->argname->bytes, '*');
      ifnot (NULL is glob) {
        dirlist_t *dlist = NULL;
        string_t *dir = string_new (16);
        string_t *pre = NULL;
        string_t *post = NULL;

        if (arg->argname->num_bytes is 1) {
          string_append_byte (dir, '.');
          goto getlist;
        }

        char *sp = glob;
        ifnot (sp is arg->argname->bytes) {
          pre = string_new (sp - arg->argname->bytes + 1);
          while (--sp >= arg->argname->bytes and *sp isnot DIR_SEP)
            string_prepend_byte (pre, *sp);

          ifnot (*sp is DIR_SEP) sp++;
        }

        if (sp is arg->argname->bytes)
          string_append_byte (dir, '.');
        else
          while (--sp >= arg->argname->bytes)
            string_prepend_byte (dir, *sp);

        ifnot (bytelen (glob) is 1) {
          post = string_new ((arg->argname->bytes - glob) + 1);
          sp = glob + 1;
          while (*sp) string_append_byte (post, *sp++);
        }
getlist:
        dlist = dirlist (dir->bytes, 0);
        if (NULL is dlist or dlist->list->num_items is 0) goto free_strings;
        vstring_t *fit = dlist->list->head;

        while (fit) {
          char *fname = fit->data->bytes;
           /* matter to change */
          if (fname[fit->data->num_bytes - 1] is DIR_SEP) goto next_fname;

          if (pre isnot NULL)
            ifnot (str_eq_n (fname, pre->bytes, pre->num_bytes)) goto next_fname;

          if (post isnot NULL) {
            int pi; int fi = fit->data->num_bytes - 1;
            for (pi = post->num_bytes - 1; pi >= 0 and fi >= 0; pi--, fi--)
              if (fname[fi] isnot post->bytes[pi]) break;

            if (pi isnot -1) goto next_fname;
          }
          arg_t *larg = AllocType (arg);
          ifnot (str_eq (dir->bytes, "."))
            larg->argval = My(String).new_with_fmt ("%s/%s", dir->bytes, fname);
          else
            larg->argval = My(String).new_with (fname);

          larg->type |= RL_ARG_FILENAME;
          current_list_append (rl, larg);
next_fname:
          fit = fit->next;
        }
free_strings:
        ifnot (NULL is pre) string_free (pre);
        ifnot (NULL is post) string_free (post);
        ifnot (NULL is dlist) dlist->free (dlist);
        string_free (arg->argname);
        string_free (dir);
        free (arg);
        goto itnext;
      } else {
        arg->type |= RL_ARG_FILENAME;
        arg->argval = My(String).new_with (arg->argname->bytes);
      }
    }

append_arg:
    current_list_append (rl, arg);

itnext:
    if (it isnot NULL) it = it->next;
  }

theend:
  return rl;
}

private vstr_t *rline_get_arg_fnames (rline_t *rl, int num) {
  vstr_t *fnames = vstr_new ();
  arg_t *arg = rl->tail;
  if (num < 0) num = 64000;
  while (arg and num) {
    if (arg->type & RL_ARG_FILENAME) {
      vstr_add_sort_and_uniq (fnames, arg->argval->bytes);
      num--;
    }
    arg = arg->prev;
  }

  ifnot (fnames->num_items) {
    free (fnames);
    return NULL;
  }

  return fnames;
}

private arg_t *rline_get_arg (rline_t *rl, int type) {
  arg_t *arg = rl->tail;
  while (arg) {
    if (arg->type & type) return arg;
    arg = arg->prev;
  }

  return NULL;
}

private string_t *rline_get_anytype_arg (rline_t *rl, char *argname) {
  arg_t *arg = rl->tail;
  while (arg) {
    if (arg->type & RL_ARG_ANYTYPE) {
      if (str_eq (arg->argname->bytes, argname))
        return arg->argval;
    }
    arg = arg->prev;
  }

  return NULL;
}

private int rline_arg_exists (rline_t *rl, char *argname) {
  arg_t *arg = rl->head;
  while (arg) {
    if (str_eq (arg->argname->bytes, argname)) return 1;
    arg = arg->next;
  }

  return 0;
}

private int ved_rline_parse_range (buf_t *this, rline_t *rl, arg_t *arg) {
  if (arg is NULL) {
    rl->range[0] = rl->range[1] = this->cur_idx;
    return OK;
  }

  if (arg->argval->num_bytes is 1) {
    if (arg->argval->bytes[0] is '%') {
      rl->range[0] = 0; rl->range[1] = this->num_items - 1;
      return OK;
    }

    if (arg->argval->bytes[0] is '.') {
      rl->range[0] = rl->range[1] = this->cur_idx;
      return OK;
    }

    if ('0' < arg->argval->bytes[0] and arg->argval->bytes[0] <= '9') {
      rl->range[0] = rl->range[1] = (arg->argval->bytes[0] - '0') - 1;
      if (rl->range[0] >= this->num_items) return NOTOK;
      return OK;
    }

    return NOTOK;
  }

  char *sp = strstr (arg->argval->bytes, ",");

  if (NULL is sp) {
    sp = arg->argval->bytes;

    int num = 0;
    int idx = 0;
    while ('0' <= *sp and *sp <= '9' and idx++ <= MAX_COUNT_DIGITS)
      num = (10 * num) + (*sp++ - '0');

    if (*sp isnot 0) return NOTOK;
    rl->range[0] = rl->range[1] = num - 1;
    if (rl->range[0] >= this->num_items or rl->range[0] < 0) return NOTOK;
    return OK;
  }

  int diff = sp - arg->argval->bytes;
  sp++;
  do {
    if (*sp is '.') {
      if (*(sp + 1) isnot 0) return NOTOK;
      rl->range[1] = this->cur_idx;
      break;
    }

    if (*sp is '$') {
      if (*(sp + 1) isnot 0) return NOTOK;
      rl->range[1] = this->num_items - 1;
      break;
    }

    if (*sp > '9' or *sp < '0') return NOTOK;
    int num = 0;
    int idx = 0;
    while ('0' <= *sp and *sp <= '9' and idx++ <= MAX_COUNT_DIGITS)
      num = (10 * num) + (*sp++ - '0');

    if (*sp isnot 0) return NOTOK;
    rl->range[1] = num - 1;
  } while (0);

  My(String).clear_at (arg->argval, diff);
  ifnot (arg->argval->num_bytes) return NOTOK;
  sp = arg->argval->bytes;

  loop (1) {
    if (*sp is '.') {
      if (*(sp + 1) isnot 0) return NOTOK;
      rl->range[0] = this->cur_idx;
      break;
    }

    if (*sp > '9' or *sp < '0') return NOTOK;
    int num = 0;
    int idx = 0;
    while ('0' <= *sp and *sp <= '9' and idx++ <= MAX_COUNT_DIGITS)
      num = (10 * num) + (*sp++ - '0');

    if (*sp isnot 0) return NOTOK;
    rl->range[0] = num - 1;
  } while (0);

  if (rl->range[0] < 0) return NOTOK;
  if (rl->range[0] > rl->range[1]) return NOTOK;
  if (rl->range[1] >= this->num_items) return NOTOK;
  return OK;
}

private int rline_get_range (rline_t  *rl, buf_t *this, int *range) {
  arg_t *arg = rline_get_arg (rl, RL_ARG_RANGE);
  if (NULL is arg or (NOTOK is ved_rline_parse_range (this, rl, arg))) {
    range[0] = -1; range[1] = -1;
    return NOTOK;
  }

  range[0] = rl->range[0];
  range[1] = rl->range[1];

  return OK;
}

private int ved_test_key (buf_t *this) {
  utf8 c;
  MSG("press any key to test, press escape to end the test");
  char str[128]; char bin[32]; char chr[8]; int len;
  for (;;) {
    My(Cursor).hide ($my(term_ptr));
    c = My(Input).get ($my(term_ptr));
    snprintf (str, 128, "dec: %d hex: 0x%x octal: 0%o bin: %s char: %s",
        c, c, c, itoa (c, bin, 2), ustring_character (c, chr, &len));
    MSG(str);
    if (c is ESCAPE_KEY) break;
  }

  My(Cursor).show ($my(term_ptr));
  return DONE;
}

public rline_T __init_rline__ (void) {
  return ClassInit (rline,
    .self = SelfInit (rline,
      .get = SubSelfInit (rline, get,
        .line = ved_rline_get_line,
        .command = ved_rline_get_command,
        .arg_fnames = rline_get_arg_fnames,
        .anytype_arg = rline_get_anytype_arg,
        .arg = rline_get_arg,
        .range = rline_get_range
      ),
      .set = SubSelfInit (rline, set,
        .line = ved_rline_set_line
      ),
      .arg = SubSelfInit (rline, arg,
       .exists = rline_arg_exists
      ),
      .history = SubSelfInit (rline, history,
        .push = ved_rline_history_push
      ),
      .new = ved_rline_new_api,
      .free = rline_free_api,
      .exec = ved_rline,
      .parse = ved_rline_parse
    )
  );
}

public void __deinit_rline__ (rline_T *this) {
  (void) this;
}

private void ved_set_rline_cb (ed_t *this, Rline_cb cb) {
  $my(num_rline_cbs)++;
  ifnot ($my(num_rline_cbs) - 1)
    $my(rline_cbs) = Alloc (sizeof (Rline_cb));
  else
    $my(rline_cbs) = Realloc ($my(rline_cbs), sizeof (Rline_cb) * $my(num_rline_cbs));

  $my(rline_cbs)[$my(num_rline_cbs) - 1] = cb;
}

private void ved_free_rline_cbs (ed_t *this) {
  ifnot ($my(num_rline_cbs)) return;
  free ($my(rline_cbs));
}

private void ved_set_normal_on_g_cb (ed_t *this, BufNormalOng_cb cb) {
  $my(num_on_normal_g_cbs)++;
  ifnot ($my(num_on_normal_g_cbs) - 1)
    $my(on_normal_g_cbs) = Alloc (sizeof (BufNormalOng_cb));
  else
    $my(on_normal_g_cbs) = Realloc ($my(on_normal_g_cbs), sizeof (BufNormalOng_cb) * $my(num_on_normal_g_cbs));

  $my(on_normal_g_cbs)[$my(num_on_normal_g_cbs) - 1] = cb;
}

private void ved_free_on_normal_g_cbs (ed_t *this) {
  ifnot ($my(num_on_normal_g_cbs)) return;
  free ($my(on_normal_g_cbs));
}

private int ved_rline (buf_t **thisp, rline_t *rl) {
  int retval = NOTHING_TODO;

  buf_t *this = *thisp;
  int is_special_win = $myparents(type) is VED_WIN_SPECIAL_TYPE;

  rl = rline_edit (rl);

  if (rl->c isnot '\r') goto theend;
  if (rl->line->head is NULL or rl->line->head->data->bytes[0] is ' ')
    goto theend;

  ved_rline_parse  (this, rl);

  switch (rl->com) {
    case VED_COM_WRITE_FORCE_ALIAS:
      rl->com = VED_COM_WRITE_FORCE; //__fallthrough__;
    case VED_COM_WRITE_FORCE:
    case VED_COM_WRITE:
    case VED_COM_WRITE_ALIAS:
#ifdef ENABLE_WRITING
      {
        arg_t *fname = rline_get_arg (rl, RL_ARG_FILENAME);
        arg_t *range = rline_get_arg (rl, RL_ARG_RANGE);
        arg_t *append = rline_get_arg (rl, RL_ARG_APPEND);
        if (NULL is fname) {
          if (is_special_win) goto theend;
          if (NULL isnot range or NULL isnot append) goto theend;
          retval = self(write, VED_COM_WRITE_FORCE is rl->com);
        } else {
          if (NULL is range) {
            rl->range[0] = 0;
            rl->range[1] = this->num_items - 1;
          } else
            if (NOTOK is ved_rline_parse_range (this, rl, range))
              goto theend;
          retval = ved_write_to_fname (this, fname->argval->bytes, NULL isnot append,
            rl->range[0], rl->range[1], VED_COM_WRITE_FORCE is rl->com, VERBOSE_ON);
        }
      }
#else
  My(Msg).error ($my(root), "writing is disabled, to enable use ENABLE_WRITING=1 during compilation");
#endif
      goto theend;

    case VED_COM_EDIT_FORCE_ALIAS:
      rl->com = VED_COM_EDIT_FORCE; //__fallthrough__;
    case VED_COM_EDIT_FORCE:
    case VED_COM_EDIT:
    case VED_COM_EDIT_ALIAS:
      if (is_special_win) goto theend;
      if ($my(is_sticked)) goto theend;
      {
        arg_t *fname = rline_get_arg (rl, RL_ARG_FILENAME);
        retval = ved_edit_fname ($my(parent), thisp, (NULL is fname ? NULL: fname->argval->bytes),
           $myparents(cur_frame), VED_COM_EDIT_FORCE is rl->com, 1, 1);
      }
      goto theend;

    case VED_COM_ETAIL:
      retval = ved_edit_fname ($my(parent), thisp, NULL, $myparents(cur_frame), 1, 0, 1);
      ved_normal_eof (*thisp, DONOT_DRAW);
      goto theend;

    case VED_COM_QUIT_FORCE_ALIAS:
      rl->com = VED_COM_QUIT_FORCE; //__fallthrough__;
    case VED_COM_QUIT_FORCE:
    case VED_COM_QUIT:
    case VED_COM_QUIT_ALIAS:
      retval = ved_quit ($my(root), VED_COM_QUIT_FORCE is rl->com);
      goto theend;

    case VED_COM_WRITE_QUIT:
    case VED_COM_WRITE_QUIT_FORCE:
      self(write, NO_FORCE);
      retval = ved_quit ($my(root), VED_COM_WRITE_QUIT_FORCE is rl->com);
      goto theend;

    case VED_COM_ENEW:
       {
         arg_t *fname = rline_get_arg (rl, RL_ARG_FILENAME);
         if (NULL isnot fname)
           retval = ved_enew_fname (thisp, fname->argval->bytes);
         else
           retval = ved_enew_fname (thisp, UNAMED);
        }
        goto theend;

    case VED_COM_REDRAW:
       My(Win).draw ($my(root)->current);
       retval = DONE;
       goto theend;

    case VED_COM_MESSAGES:
      retval = ved_messages ($my(root), thisp, AT_EOF);
      goto theend;

    case VED_COM_SCRATCH:
      retval = ved_scratch ($my(root), thisp, AT_EOF);
      goto theend;

    case VED_COM_SEARCHES:
      retval = ed_change_buf ($my(root), thisp, VED_SEARCH_WIN, VED_SEARCH_BUF);
      goto theend;

    case VED_COM_DIFF_BUF:
      retval = ed_change_buf ($my(root), thisp, VED_DIFF_WIN, VED_DIFF_BUF);
      goto theend;

    case VED_COM_DIFF:
      retval = ved_diff (thisp, 0);
      goto theend;

    case VED_COM_GREP:
      {
        arg_t *pat = rline_get_arg (rl, RL_ARG_PATTERN);
        if (NULL is pat) break;

        vstr_t *fnames = rline_get_arg_fnames (rl, -1);
        dirlist_t *dlist = NULL;

        if (NULL is fnames) {
          dlist = dirlist (".", 0);
          if (NULL is dlist) break;
          fnames = dlist->list;
        }

        retval = ved_grep (thisp, pat->argval->bytes, fnames);
        ifnot (NULL is dlist)
          dlist->free (dlist);
        else
          vstr_free (fnames);
      }
      goto theend;

    case VED_COM_SPLIT:
      {
        if (is_special_win) goto theend;
        arg_t *fname = rline_get_arg (rl, RL_ARG_FILENAME);
        if (NULL is fname)
          retval = ved_split (thisp, UNAMED);
        else
          retval = ved_split (thisp, fname->argval->bytes);
      }
      goto theend;


    case VED_COM_READ:
    case VED_COM_READ_ALIAS:
      {
        arg_t *fname = rline_get_arg (rl, RL_ARG_FILENAME);
        if (NULL is fname) goto theend;
        retval = ved_buf_read_from_file (this, fname->argval->bytes);
        goto theend;
      }

    case VED_COM_SHELL:
    case VED_COM_READ_SHELL:
      {
        string_t *com = vstr_join (rl->line, "");
        string_delete_numbytes_at (com, (rl->com is VED_COM_SHELL ? 1 : 3), 0);
        retval = ved_buf_read_from_shell (this, com->bytes, rl->com);
        string_free (com);
        goto theend;
      }

    case VED_COM_SUBSTITUTE:
    case VED_COM_SUBSTITUTE_WHOLE_FILE_AS_RANGE:
    case VED_COM_SUBSTITUTE_ALIAS:
      {
        arg_t *pat = rline_get_arg (rl, RL_ARG_PATTERN);
        arg_t *sub = rline_get_arg (rl, RL_ARG_SUB);
        arg_t *global = rline_get_arg (rl, RL_ARG_GLOBAL);
        arg_t *interactive = rline_get_arg (rl, RL_ARG_INTERACTIVE);
        arg_t *range = rline_get_arg (rl, RL_ARG_RANGE);

        if (pat is NULL or sub is NULL) goto theend;

        if (NULL is range and rl->com is VED_COM_SUBSTITUTE_WHOLE_FILE_AS_RANGE) {
          rl->range[0] = 0; rl->range[1] = this->num_items - 1;
        } else
          if (NOTOK is ved_rline_parse_range (this, rl, range))
            goto theend;

        retval = buf_substitute (this, pat->argval->bytes, sub->argval->bytes,
           global isnot NULL, interactive isnot NULL, rl->range[0], rl->range[1]);
       }

       goto theend;

    case VED_COM_BUF_CHANGE_PREV_ALIAS:
      rl->com = VED_COM_BUF_CHANGE_PREV; //__fallthrough__;
    case VED_COM_BUF_CHANGE_PREV:
      if ($my(flags) & BUF_IS_SPECIAL) goto theend;
      if ($my(is_sticked)) goto theend;
      retval = ved_buf_change (thisp, rl->com);
      goto theend;

    case VED_COM_BUF_CHANGE_NEXT_ALIAS:
      rl->com = VED_COM_BUF_CHANGE_NEXT; //__fallthrough__;
    case VED_COM_BUF_CHANGE_NEXT:
      if ($my(flags) & BUF_IS_SPECIAL) goto theend;
      if ($my(is_sticked)) goto theend;
      retval = ved_buf_change (thisp, rl->com);
      goto theend;

    case VED_COM_BUF_CHANGE_PREV_FOCUSED_ALIAS:
      rl->com = VED_COM_BUF_CHANGE_PREV_FOCUSED; //__fallthrough__;
    case VED_COM_BUF_CHANGE_PREV_FOCUSED:
      if (is_special_win) goto theend;
      if ($my(is_sticked)) goto theend;
      retval = ved_buf_change (thisp, rl->com);
      goto theend;

    case VED_COM_BUF_CHANGE_ALIAS:
      rl->com = VED_COM_BUF_CHANGE; //__fallthrough__;
    case VED_COM_BUF_CHANGE:
      if ($my(flags) & BUF_IS_SPECIAL) goto theend;
      if ($my(is_sticked)) goto theend;
      {
        arg_t *bufname = rline_get_arg (rl, RL_ARG_BUFNAME);
        if (NULL is bufname) goto theend;
        retval = ved_buf_change_bufname (thisp, bufname->argval->bytes);
      }
      goto theend;

    case VED_COM_WIN_CHANGE_PREV_ALIAS:
      rl->com = VED_COM_WIN_CHANGE_PREV; //__fallthrough__;
    case VED_COM_WIN_CHANGE_PREV:
      retval = ed_win_change ($my(root), thisp, rl->com, NULL, 0, NO_FORCE);
      goto theend;

    case VED_COM_WIN_CHANGE_NEXT_ALIAS:
      rl->com = VED_COM_WIN_CHANGE_NEXT; //__fallthrough__;
    case VED_COM_WIN_CHANGE_NEXT:
      retval = ed_win_change ($my(root), thisp, rl->com, NULL, 0, NO_FORCE);
      goto theend;

    case VED_COM_WIN_CHANGE_PREV_FOCUSED_ALIAS:
      rl->com = VED_COM_WIN_CHANGE_PREV_FOCUSED; //__fallthrough__;
    case VED_COM_WIN_CHANGE_PREV_FOCUSED:
      retval = ed_win_change ($my(root), thisp, rl->com, NULL, 0, NO_FORCE);
      goto theend;

    case VED_COM_BUF_DELETE_FORCE_ALIAS:
      rl->com = VED_COM_BUF_DELETE_FORCE; //__fallthrough__;
    case VED_COM_BUF_DELETE_FORCE:
    case VED_COM_BUF_DELETE_ALIAS:
    case VED_COM_BUF_DELETE:
      {
        int idx;
        arg_t *bufname = rline_get_arg (rl, RL_ARG_BUFNAME);
        if (NULL is bufname)
          idx = $my(parent)->cur_idx;
        else
          if (NULL is My(Win).get.buf_by_name ($my(parent), bufname->argval->bytes, &idx))
            idx = $my(parent)->cur_idx;

        retval = ved_buf_delete (thisp, idx, rl->com is VED_COM_BUF_DELETE_FORCE);
        goto theend;
      }

     case VED_COM_TEST_KEY:
       ved_test_key (this);
       retval = DONE;
       goto theend;

    default:
      for (int i = 0; i < $myroots(num_rline_cbs); i++) {
        retval = $myroots(rline_cbs)[i] (thisp, rl, rl->com);
        if (retval isnot RLINE_NO_COMMAND) break;
      }
      goto theend;
  }

theend:
  rline_clear (rl);
  ifnot (DONE is retval)
    rline_free (rl);
  else {
    rl->state &= ~RL_CLEAR_FREE_LINE;
    ved_rline_history_push (rl);
  }

  return retval;
}

private buf_t *ved_insert_char_rout (buf_t *this, utf8 c, string_t *cur_insert) {
  int orig_col = $mycur(cur_col_idx)++;
  int width = 1;
  char buf[5];

  if ('~' >= c and c >= ' ') {
     buf[0] = c;
     buf[1] = '\0';
  } else if (c is '\t') {
    if ($my(ftype)->tab_indents is 0 or ($my(state) & ACCEPT_TAB_WHEN_INSERT)) {
      $my(state) &= ~ACCEPT_TAB_WHEN_INSERT;
      buf[0] = c;
      buf[1] = '\0';
      width = $my(ftype)->tabwidth;
    } else {
      width = $my(ftype)->shiftwidth;
      $mycur(cur_col_idx) += $my(ftype)->shiftwidth - 1;
      int i = 0;
      for (; i < $my(ftype)->shiftwidth; i++) buf[i] = ' ';
      buf[i] = '\0';
    }
  } else {
    $my(state) &= ~ACCEPT_TAB_WHEN_INSERT;
    $mycur(cur_col_idx)++;
    if (c < 0x800) {
      buf[0] = (c >> 6) | 0xC0;
      buf[1] = (c & 0x3F) | 0x80;
      buf[2] = '\0';
      width = char_utf8_width (buf, $my(ftype)->tabwidth);
    } else {
      $mycur(cur_col_idx)++;
      if (c < 0x10000) {
        buf[0] = (c >> 12) | 0xE0;
        buf[1] = ((c >> 6) & 0x3F) | 0x80;
        buf[2] = (c & 0x3F) | 0x80;
        buf[3] = '\0';
        width = char_utf8_width (buf, $my(ftype)->tabwidth);
      } else if (c < 0x110000) {
        $mycur(cur_col_idx)++;
        buf[0] = (c >> 18) | 0xF0;
        buf[1] = ((c >> 12) & 0x3F) | 0x80;
        buf[2] = ((c >> 6) & 0x3F) | 0x80;
        buf[3] = (c & 0x3F) | 0x80;
        buf[4] = '\0';
        width = char_utf8_width (buf, $my(ftype)->tabwidth);
      }
    }
  }

  My(String).insert_at ($mycur(data), buf, orig_col);
  My(String).append (cur_insert, buf);
  if ($my(cur_video_col) is $my(dim)->num_cols or
      $my(cur_video_col) + width > $my(dim)->num_cols) {
    $mycur(first_col_idx) = $mycur(cur_col_idx);
    $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
  } else
    $my(video)->col_pos = $my(cur_video_col) = $my(cur_video_col) + width;

  return this;
}

private int ved_insert_reg (buf_t **thisp, string_t *cur_insert) {
  buf_t *this = *thisp;
  MSG ("insert register (charwise mode):");
  int regidx = ed_register_get_idx ($my(root), My(Input).get ($my(term_ptr)));
  if (-1 is regidx) return NOTHING_TODO;

  if (ERROR is ed_register_special_set ($my(root), this, regidx))
    return NOTHING_TODO;

  rg_t *rg = &$my(regs)[regidx];
  if (rg->type is LINEWISE) return NOTHING_TODO;

  reg_t *reg = rg->head;
  if (NULL is reg) return NOTHING_TODO;

  utf8 c = 0;
  while (reg isnot NULL) {
    char *sp = reg->data->bytes;
    while (*sp) {
      int clen = ustring_charlen ((uchar) *sp);
      c = utf8_code (sp);
      sp += clen;
      this = ved_insert_char_rout (this, c, cur_insert);
    }
    reg = reg->next;
  }

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_insert (buf_t **thisp, utf8 com) {
  buf_t *this = *thisp;
  utf8 c = 0;
  if (com is '\n') {
    com = 'i';
    c = '\n';
  }

  string_t *cur_insert = string_new ($my(dim)->num_cols);

  char prev_mode[bytelen ($my(mode)) + 1];
  memcpy (prev_mode, $my(mode), sizeof (prev_mode));

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->num_bytes = $mycur(data)->num_bytes;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);

  action = stack_push (action, act);

  if (com is 'A' or ((com is 'a' or com is 'C') and $mycur(cur_col_idx) is (int)
      $mycur(data)->num_bytes - ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]))) {
    ADD_TRAILING_NEW_LINE;

    ved_normal_eol (this);
  } else if (com is 'a')
    ved_normal_right (this, 1, DRAW);

  if (com is 'i' or com is 'a') {
    int width = char_utf8_width ($mycur(data)->bytes + $mycur(cur_col_idx),
        $my(ftype)->tabwidth);

    if (width > 1) {
      if ($my(cur_video_col) - width + 1 < 1) {
        ustring_encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
            CLEAR, $my(ftype)->tabwidth, $mycur(first_col_idx));
        vchar_t *it = $my(line)->current;
        while ($mycur(first_col_idx) > 0 and $my(cur_video_col) < 1) {
          $mycur(first_col_idx) -= it->len;
          $my(video)->col_pos = $my(cur_video_col) =
            $my(cur_video_col) - it->width + 1;
          it = it->prev;
        }
      } else
        $my(video)->col_pos = $my(cur_video_col) =
          $my(cur_video_col) - width + 1;
      self(draw_cur_row);
    }
  }

  strcpy ($my(mode), INSERT_MODE);
  self(set.mode, INSERT_MODE);

  if ($my(show_statusline) is UNSET) buf_set_draw_statusline (this);
  buf_set_draw_topline (this);

  if (c isnot 0) goto handle_char;

theloop:
  for (;;) {

get_char:
    ed_check_msg_status (this);
    c = My(Input).get ($my(term_ptr));

handle_char:
    if (c > 0x7f)
      if (c < 0x0a0 or (c >= FN_KEY(1) and c <= FN_KEY(12)) or c is INSERT_KEY)
        continue;

    if ((c is '\t') or (c >= ' ' and c <= '~') or
       (c > 0x7f and ((c < ARROW_DOWN_KEY) or (c > HOME_KEY and
       (c isnot DELETE_KEY) and (c isnot PAGE_DOWN_KEY) and (c isnot PAGE_UP_KEY)
        and (c isnot END_KEY)))))
      goto new_char;

    switch (c) {
      case ESCAPE_KEY:
        goto theend;

      case CTRL('n'):
        ved_complete_word (thisp);
        goto get_char;

      case CTRL('x'):
        ved_handle_ctrl_x (thisp);
        goto get_char;

      case CTRL('a'):
        ved_insert_last_insert (this);
        goto get_char;

      case CTRL('v'):
        if (NEWCHAR is ved_insert_character (this, &c))
          goto new_char;
        goto get_char;

      case CTRL('r'):
        ved_insert_reg (thisp, cur_insert);
        goto get_char;

      case CTRL('k'):
        if (NEWCHAR is ved_complete_digraph (this, &c))
          goto new_char;
        goto get_char;

      case CTRL('y'):
      case CTRL('e'):
        if (NEWCHAR is ved_insert_handle_ud_line_completion (this, &c)) {
          $my(state) |= ACCEPT_TAB_WHEN_INSERT;
          goto new_char;
        }

        goto get_char;

      case BACKSPACE_KEY:
        ifnot ($mycur(cur_col_idx)) goto get_char;

        ved_normal_left (this, 1, DONOT_DRAW);
        if (NOTHING_TODO is ved_normal_delete (this, 1, -1))
          self(draw_cur_row);
        goto get_char;

      case DELETE_KEY:
        if ($mycur(data)->num_bytes is 0 or
          // HAS_THIS_LINE_A_TRAILING_NEW_LINE or
            $mycur(data)->bytes[$mycur(cur_col_idx)] is 0 or
            $mycur(data)->bytes[$mycur(cur_col_idx)] is '\n') {
          if (HAS_THIS_LINE_A_TRAILING_NEW_LINE) {
            if ($mycur(cur_col_idx) + ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)])
                is (int) $mycur(data)->num_bytes)
              ved_normal_left (this, 1, DRAW);
            RM_TRAILING_NEW_LINE;
          }

          if (DONE is ved_join (this)) {
            act_t *lact = stack_pop (action, act_t);
            if (lact isnot NULL) {
              free (lact->bytes);
              free (lact);
            }
          }

          if ($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes -
              ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]))
            ADD_TRAILING_NEW_LINE;

          if ($mycur(cur_col_idx) isnot 0)
            ved_normal_right (this, 1, DRAW);

          goto get_char;
        }

        ved_normal_delete (this, 1, -1);
        goto get_char;

      case HOME_KEY:
        ved_normal_bol (this);
        goto get_char;

      case END_KEY:
        ADD_TRAILING_NEW_LINE;
        ved_normal_eol (this);
        goto get_char;

      case ARROW_RIGHT_KEY:
        if($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes -
            ustring_charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]))
          ADD_TRAILING_NEW_LINE;
        ved_normal_right (this, 1, DRAW);
        goto get_char;

      case ARROW_LEFT_KEY:
        ved_normal_left (this, 1, DRAW);
        goto get_char;

      case ARROW_UP_KEY:
      case ARROW_DOWN_KEY:
      case PAGE_UP_KEY:
      case PAGE_DOWN_KEY:
      case  '\r':
      case  '\n':
        insert_change_line (this, c, &action);

        if ('\r' is c and cur_insert->num_bytes) {
          string_replace_with ($my(last_insert), cur_insert->bytes);
          string_clear (cur_insert);
        }

        if ('\n' is c) goto theend;

        goto theloop;

      default:
        goto get_char;
    }

new_char:
    this = ved_insert_char_rout (this, c, cur_insert);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_cur_row);
    goto get_char;
  }

theend:
  if ($mycur(data)->num_bytes)
    RM_TRAILING_NEW_LINE;

  ved_normal_left (this, 1, DRAW);
  self(set.mode, prev_mode);

  if ($mycur(data)->num_bytes) {
    $my(video)->col_pos = $my(cur_video_col) = $my(cur_video_col) +
       (char_utf8_width ($mycur(data)->bytes + $mycur(cur_col_idx),
       $my(ftype)->tabwidth) - 1);
  }

  if ($my(cur_video_col) > $my(dim)->num_cols) {
    $mycur(first_col_idx) = $mycur(cur_col_idx);
      $my(video)->col_pos = $my(cur_video_col) =
        char_utf8_width ($mycur(data)->bytes + $mycur(cur_col_idx),
           $my(ftype)->tabwidth - 1);
  }

/*
  if ($mycur(data)->bytes[$mycur(cur_col_idx)] is '\t') {
    $my(video)->col_pos = $my(cur_video_col) =
        $my(cur_video_col) + ($my(ftype)->tabwidth - 1);
 */
    self(draw_cur_row);
 // }

  buf_set_draw_topline (this);

  if (cur_insert->num_bytes)
    string_replace_with ($my(last_insert), cur_insert->bytes);

  string_free (cur_insert);

  if (NULL isnot action->head)
    vundo_push (this, action);
  else
    free (action);
  return DONE;
}

private int ved_buf_exec_cmd_handler (buf_t **thisp, utf8 com, int *range, int regidx) {
  int count = 1;

  buf_t *this = *thisp;

  if (range[0] <= 0) {
    if (range[0] is 0)
      return INDEX_ERROR;
  } else {
    if (range[0] > this->num_items)
      if (range[1] >= 0)
        return INDEX_ERROR;
    count = range[0];
  }

  int retval = NOTHING_TODO;
  switch (this->on_normal_beg (thisp, com, range, regidx)) {
    case -1: goto theend;
    case  1: goto atend;
    case EXIT: return EXIT;
    default: break;
  }

  switch (com) {
    case 'q':
      if ($my(flags) & BUF_IS_PAGER) return BUF_QUIT;
      break;

    case ':':
      {
      rline_t *rl = ved_rline_new ($my(root), $my(term_ptr), My(Input).get,
          *$my(prompt_row_ptr), 1, $my(dim)->num_cols, $my(video));
      retval = ved_rline (thisp, rl);
      }

      goto theend;

    case '/':
    case '?':
    case '*':
    case '#':
    case 'n':
    case 'N':
      retval = ved_search (this, com); break;

    case 'm':
      retval = mark_set (this, -1); break;

    case '`':
      retval = mark_goto (this); break;

    case CTRL('o'):
      retval = buf_jump (this, RIGHT_DIRECTION); break;

    case CTRL('i'):
      retval = buf_jump (this, LEFT_DIRECTION); break;

    case '~':
      retval = ved_normal_change_case (this); break;

    case CTRL('a'):
    case CTRL('x'):
      retval = ved_word_math (this, count, com); break;

    case '^':
      retval = ved_normal_noblnk (this); break;

    case '>':
    case '<':
      retval = ved_indent (this, count, com); break;

    case 'y':
      retval = ved_normal_yank (this, count, regidx); break;

    case 'Y':
      retval = ved_normal_Yank (this, count, regidx); break;

    case ' ':
      ifnot ($my(ftype)->space_on_normal_is_like_insert_mode) break;

      (&$my(regs)[REG_RDONLY])->type = CHARWISE;
      (&$my(regs)[REG_RDONLY])->head->data->bytes[0] = ' ';
      (&$my(regs)[REG_RDONLY])->head->data->bytes[1] = '\0';
      (&$my(regs)[REG_RDONLY])->head->data->num_bytes = 1;
      (&$my(regs)[REG_RDONLY])->head->next = NULL;
      (&$my(regs)[REG_RDONLY])->head->prev = NULL;
      retval = ved_normal_put (this, REG_RDONLY, 'P'); break;

    case 'p':
    case 'P':
      retval = ved_normal_put (this, regidx, com); break;

    case 'd':
      retval = ved_normal_handle_d (this, count, regidx); break;

    case 'x':
    case DELETE_KEY:
      retval = ved_normal_delete (this, count, regidx); break;

    case BACKSPACE_KEY:
      ifnot ($mycur(cur_col_idx)) {
        if ($my(ftype)->backspace_on_first_idx_remove_trailing_spaces) {
          size_t len = $mycur(data)->num_bytes;
          for (int i = $mycur(data)->num_bytes - 1; i > 0; i--)
            if ($mycur(data)->bytes[i] is ' ')
              My(String).clear_at ($mycur(data), i);
            else
              break;

          if (len isnot $mycur(data)->num_bytes) {
            $my(flags) |= BUF_IS_MODIFIED;
            self(draw_cur_row);
          }
          retval = DONE;
        }

        break;
      }

      ifnot ($my(ftype)->backspace_on_normal_is_like_insert_mode) break;

      //__fallthrough__;

    case 'X':
       if (DONE is ved_normal_left (this, 1, DONOT_DRAW))
         if (NOTHING_TODO is (retval = ved_normal_delete (this, count, regidx)))
          self(draw_cur_row);
       break;

    case 'J':
      retval = ved_join (this); break;

    case '$':
      retval = ved_normal_eol (this); break;

    case CTRL('w'):
      retval = ved_normal_handle_ctrl_w (thisp); break;

    case 'g':
      retval = ved_normal_handle_g (thisp, count); break;

    case 'G':
      retval = ved_normal_handle_G (this, count); break;

    case ',':
      retval = ved_normal_handle_comma (thisp); break;

    case '0':
      retval = ved_normal_bol (this); break;

    case 'E':
    case 'e':
      retval = ved_normal_end_word (thisp, count,
        ($my(ftype)->small_e_on_normal_goes_insert_mode is 1 and 'e' is com), DRAW);
      break;

    case ARROW_RIGHT_KEY:
    case 'l':
      retval = ved_normal_right (this, count, DRAW); break;

    case ARROW_LEFT_KEY:
    case 'h':
      retval = ved_normal_left (this, count, DRAW); break;

    case ARROW_UP_KEY:
    case 'k':
      retval = ved_normal_up (this, count, ADJUST_COL, DRAW); break;

    case ARROW_DOWN_KEY:
    case 'j':
      retval = ved_normal_down (this, count, ADJUST_COL, DRAW); break;

    case PAGE_DOWN_KEY:
    case CTRL('f'):
      retval = ved_normal_page_down (this, count); break;

    case PAGE_UP_KEY:
    case CTRL('b'):
      retval = ved_normal_page_up (this, count); break;

    case HOME_KEY:
      retval = ved_normal_bof (this, DRAW); break;

    case END_KEY:
      retval = ved_normal_eof (this, DONOT_DRAW); break;

    case CTRL('v'):
      retval = ved_normal_visual_bw (this); break;

    case 'V':
      retval = ved_normal_visual_lw (thisp); break;

    case 'v':
      retval = ved_normal_visual_cw (thisp); break;

    case 'D':
      retval = ved_normal_delete_eol (this, regidx); break;

    case 'r':
      retval = ved_normal_replace_char (this); break;

    case 'c':
      retval = ved_normal_handle_c (this, count, regidx); break;

    case 'C':
       ved_normal_delete_eol (this, regidx);
       retval = ved_insert (thisp, com); break;

    case 'o':
    case 'O':
      retval = ved_insert_new_line (thisp, com); break;

    case '\r':
      ifnot ($my(ftype)->cr_on_normal_is_like_insert_mode) break;

      com = '\n';
//      __fallthrough__;
    case 'i':
    case 'a':
    case 'A':
      retval = ved_insert (thisp, com); break;

    case CTRL('r'):
    case 'u':
      retval = vundo (this, com); break;

    case CTRL('l'):
      My(Win).draw ($my(root)->current);
      retval = DONE; break;

    case CTRL('j'):
      My(Ed).suspend ($my(root));
      return EXIT;

    case 'W':
      retval = ved_normal_handle_W (thisp);
      break;

    default:
      break;
  }

atend:
  this->on_normal_end (thisp, com, range, regidx);

theend:
  return retval;
}

#define ADD_RANGE(n) range[IS_FIRST_RANGE_OK] = n
#define IS_FIRST_RANGE_OK range[0] != -1
#define IS_SECOND_RANGE_OK range[1] != -1
#define ARE_BOTH_RANGES_OK (IS_FIRST_RANGE_OK && IS_SECOND_RANGE_OK)

#define NORMAL_GET_NUMBER                 \
  c = ({                                  \
    int i = 0;                            \
    char intbuf[8];                       \
    intbuf[i++] = c;                      \
    utf8 cc = BUF_GET_NUMBER (intbuf, i); \
                                          \
    if (i is MAX_COUNT_DIGITS)            \
      goto new_state;                     \
                                          \
    intbuf[i] = '\0';                     \
    ADD_RANGE (atoi (intbuf));            \
    cc;                                   \
    })

private int ved_loop (ed_t *ed, buf_t *this) {
  int retval = NOTOK;
  int range[2];
  utf8 c;
  int cmd_retv;
  int regidx = -1;

  for (;;) {
 new_state:
    regidx = -1;
    range[0] = range[1] = -1;

get_char:
    ed_check_msg_status (this);
    c = My(Input).get ($my(term_ptr));

handle_char:
    switch (c) {
      case NOTOK: goto theend;

      case '"':
        if (-1 isnot regidx) goto exec_block;
        regidx = ed_register_get_idx (ed, My(Input).get ($my(term_ptr)));
        goto get_char;

     case '0':
        if (0 is (IS_FIRST_RANGE_OK)) goto exec_block;
        //__fallthrough__;

      case '1'...'9':
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        NORMAL_GET_NUMBER;
        goto handle_char;

      case '.':
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        ADD_RANGE (this->cur_idx + 1);
        goto get_char;

      case '$':
        if (0 is (IS_FIRST_RANGE_OK)) goto exec_block;
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        ADD_RANGE (this->num_items);
        goto get_char;

      case ',':
        if (0 is (IS_FIRST_RANGE_OK)) goto exec_block;
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        ifnot (IS_FIRST_RANGE_OK) range[0] = this->cur_idx + 1;
          goto get_char;

      case '%':
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        if (IS_FIRST_RANGE_OK) goto exec_block;
        range[0] = 1; range[1] = this->num_items;
        goto get_char;

      default:
exec_block:
        if (regidx is -1) regidx = REG_UNAMED;

        cmd_retv = My(Ed).exec.cmd (&this, c, range, regidx);

        if (cmd_retv is DONE || cmd_retv is NOTHING_TODO)
          goto new_state;

        if (cmd_retv is BUF_QUIT) {
          retval = ved_buf_change (&this, VED_COM_BUF_CHANGE_PREV_FOCUSED);
          if (retval is NOTHING_TODO) {
            retval = ed_win_change ($my(root), &this, VED_COM_WIN_CHANGE_PREV_FOCUSED, NULL, 0, NO_FORCE);
            if (retval is NOTHING_TODO)
              cmd_retv = EXIT;
            else
              goto new_state;
          } else
            goto new_state;
        }

        if (cmd_retv is EXIT) {retval = OK; goto theend;}

        if (cmd_retv is WIN_EXIT) {
                       /* at this point this (probably) is a null reference */
          retval = ved_win_delete (ed, &this, NO_COUNT_SPECIAL);
          if (retval is DONE) goto new_state;
          if (retval is EXIT) {retval = OK; goto theend;}
          goto theend;
        }
    }
  }

theend:
  return retval;
}

private int ved_main (ed_t *this, buf_t *buf) {
  if ($my(state) & ED_SUSPENDED) {
    self(resume);
  } else
    My(Win).draw (this->current);

 // My(Msg).send(this, COLOR_NORMAL, " ");
/*
  My(Msg).send (this, COLOR_CYAN,
      "Υγειά σου Κόσμε και καλό ταξίδι στο ραντεβού με την αιωνοιότητα");
 */

  return ved_loop (this, buf);
}

private buf_t *ed_get_current_buf (ed_t *this) {
  return this->current->current;
}

private win_t *ed_get_current_win (ed_t *this) {
  return this->current;
}

private int ed_get_current_win_idx (ed_t *this) {
  return this->cur_idx;
}

private win_t *ed_get_win_by_name (ed_t *this, char *name, int *idx) {
  if (NULL is name) return NULL;
  win_t *it = this->head;
  *idx = 0;
  while (it) {
    if (str_eq ($from(it, name), name)) return it;
    (*idx)++;
    it = it->next;
  }

  return NULL;
}

private win_t *ed_get_win_by_idx (ed_t *this, int idx) {
  if (idx >= this->num_items) return NULL;
  int i = 0;
  win_t *it = this->head;
  while (it) {
    if (i++ is idx) return it;
    it = it->next;
  }

  return NULL;
}

private term_t *ed_get_term (ed_t *this) {
  return $my(term);
}

private int ed_get_num_win (ed_t *this, int count_special) {
  if (count_special) return this->num_items;
  int num = 0;
  win_t *it = this->head;
  while (it) {
    ifnot ($from(it, type) is VED_WIN_SPECIAL_TYPE) num++;
    it = it->next;
  }
  return num;
}

private int ed_get_state (ed_t *this) {
  return $my(state);
}

private win_t *ed_get_win_head (ed_t *this) {
  return this->head;
}

private win_t *ed_get_win_next (ed_t *this, win_t *w) {
  (void) this;
  return w->next;
}

private ed_t *ed_get_next (ed_t *this) {
  ifnot (NULL is this->next)
    return this->next;
  return this->prev;
}

private ed_t *ed_get_prev (ed_t *this) {
  ifnot (NULL is this->prev)
    return this->prev;
  return this->next;
}

private buf_t *ed_get_bufname (ed_t *this, char *fname) {
  buf_t *buf = NULL;
  win_t *w = this->head;
  int idx;
  while (w) {
    buf = My(Win).get.buf_by_name (w, fname, &idx);
    if (buf) return buf;
    w = w->next;
  }

  return buf;
}

private win_t *ed_set_current_win (ed_t *this, int idx) {
  int cur_idx = this->cur_idx;
  if (INDEX_ERROR isnot current_list_set (this, idx))
    this->prev_idx = cur_idx;
  return this->current;
}

private void ed_set_screen_size (ed_t *this) {
  My(Term).reset ($my(term));
  My(Term).init_size ($my(term), $from(&$my(term), lines), $from(&$my(term), columns));
  My(Term).set ($my(term));
  ifnot (NULL is $my(dim)) {
    free ($my(dim));
    $my(dim) = NULL;
  }

  $my(dim) = ed_dim_new (this, 1, $from($my(term), lines), 1, $from($my(term), columns));
  $my(msg_row) = $from($my(term), lines);
  $my(prompt_row) = $my(msg_row) - 1;
  ifnot (NULL is $my(video)->rows) free ($my(video)->rows);
  $my(video)->rows = Alloc (sizeof (int) * $my(video)->num_rows);
}

private dim_t *ed_set_dim (ed_t *this, dim_t *dim, int f_row, int l_row,
                                                   int f_col, int l_col) {
  (void) this;
  return dim_set (dim, f_row, l_row, f_col, l_col);
}

private int ved_append_win (ed_t *this, win_t *w) {
  current_list_append (this, w);
  return this->cur_idx;
}

private void ved_history_add (ed_t *this, vstr_t *hist, int what) {
  if (what is RLINE_HISTORY) {
    vstring_t *it = hist->head;
    while (it) {
      rline_t *rl = ved_rline_new (this, $my(term), My(Input).get,
           $my(prompt_row), 1, $my(dim)->num_cols, $my(video));
      char *sp = str_trim_end (it->data->bytes, '\n');
      sp = str_trim_end (sp, ' ');
      BYTES_TO_RLINE (rl, it->data->bytes, (int) bytelen (sp));
      ved_rline_history_push (rl);
      it = it->next;
    }
    return;
  }

  if (what is SEARCH_HISTORY) {
    vstring_t *it = hist->head;
    while (it) {
      ved_search_history_push (this, it->data->bytes, it->data->num_bytes);
      it = it->next;
    }
    return;
  }
}

private void ved_history_write (ed_t *this, char *dir) {
   if (-1 is access (dir, F_OK|R_OK)) return;
   ifnot (is_directory (dir)) return;
   size_t dirlen = bytelen (dir);
   char fname[dirlen + 16];
   snprintf (fname, dirlen + 16, "%s/.ved_h_search", dir);
   FILE *fp = fopen (fname, "w");
   if (NULL is fp) return;
   histitem_t *it = $my(history)->search->tail;
   while (it) {
     fprintf (fp, "%s\n", it->data->bytes);
     it = it->prev;
   }
   fclose (fp);

   snprintf (fname, dirlen + 16, "%s/.ved_h_rline", dir);
   fp = fopen (fname, "w");
   if (NULL is fp) return;

   h_rlineitem_t *hrl = $my(history)->rline->tail;
   while (hrl) {
     string_t *line = vstr_join (hrl->data->line, "");
     fprintf (fp, "%s\n", line->bytes);
     string_free (line);
     hrl = hrl->prev;
   }
   fclose (fp);
}

private void ved_history_read (ed_t *this, char *dir) {
   if (-1 is access (dir, F_OK|R_OK)) return;
   ifnot (is_directory (dir)) return;
   size_t dirlen = bytelen (dir);
   char fname[dirlen + 16];
   snprintf (fname, dirlen + 16, "%s/.ved_h_search", dir);
   vstr_t *lines = file_readlines (fname, NULL, NULL, NULL);
   self(history.add, lines, SEARCH_HISTORY);
   snprintf (fname, dirlen + 16, "%s/.ved_h_rline", dir);
   vstr_free (lines);
   lines = file_readlines (fname, NULL, NULL, NULL);
   self(history.add, lines, RLINE_HISTORY);
   vstr_free (lines);
}

private void ed_suspend (ed_t *this) {
  if ($my(state) & ED_SUSPENDED) return;
  $my(state) |= ED_SUSPENDED;
  My(Screen).clear ($my(term));
  My(Term).reset ($my(term));
  // My(Term).restore ($my(term));
}

private void ed_resume (ed_t *this) {
  ifnot ($my(state) & ED_SUSPENDED) return;
  $my(state) &= ~ED_SUSPENDED;
  My(Term).set ($my(term));
  // My(Term).raw ($my(term));
  My(Win).set.current_buf (this->current, this->current->cur_idx, DONOT_DRAW);
  My(Win).draw (this->current);
}

private int ed_word_actions_cb (buf_t **thisp, int fidx, int lidx,
                                bufiter_t *it, char *word, utf8 c, char *action) {
  (void) thisp; (void) word; (void) c; (void) fidx; (void) lidx; (void) it;
  (void) action;
  return 1;
}

private void ed_set_word_actions (ed_t *this, utf8 *chars, int len,
                                  char *actions, WordActions_cb cb) {
  if (len <= 0) return;
  int tlen = $my(word_actions_chars_len) + len;

  ifnot ($my(word_actions_chars_len)) {
    $my(word_actions_chars) = Alloc (sizeof (int *) * len);
    $my(word_actions_cb) = Alloc (sizeof (WordActions_cb) * len);
  }
  else {
    $my(word_actions_chars) = Realloc ($my(word_actions_chars), sizeof (int *) * tlen);
    $my(word_actions_cb) = Realloc ($my(word_actions_cb), sizeof (WordActions_cb) * tlen);
  }

  if (NULL is cb)
    cb = ed_word_actions_cb;

  for (int i = $my(word_actions_chars_len), j = 0; i < tlen; i++, j++) {
    $my(word_actions_chars)[i] = chars[j];
    $my(word_actions_cb)[i] = cb;
  }

  $my(word_actions_chars_len) = tlen;
  $my(word_actions) = str_chop (actions, '\n', $my(word_actions), NULL, NULL);
}

private void ed_set_word_actions_default (ed_t *this) {
  $my(word_actions) = vstr_new ();
  $my(word_actions_chars) = NULL;
  $my(word_actions_chars_len) = 0;
  utf8 chars[] = {'+', '*', ESCAPE_KEY};
  char actions[] =
    "+send selected area to XA_CLIPBOARD\n"
    "*send selected area to XA_PRIMARY";

  self(set.word_actions, chars, ARRLEN(chars), actions, ed_selection_to_X_word_actions_cb);
}

private void ed_set_cw_mode_actions (ed_t *this, utf8 *chars, int len,
                 char *actions, VisualCwMode_cb cb) {
  ifnot (len) return;
  int tlen = $my(cw_mode_chars_len) + len;

  ifnot ($my(cw_mode_chars_len)) {
    $my(cw_mode_chars) = Alloc (sizeof (int *) * len);
    $my(cw_mode_actions) = str_dup (actions, bytelen (actions));
  } else {
    $my(cw_mode_chars) = Realloc ($my(cw_mode_chars), sizeof (int *) * tlen);
    size_t alen = bytelen (actions);
    size_t plen = bytelen ($my(cw_mode_actions));
    $my(cw_mode_actions) = Realloc ($my(cw_mode_actions), alen + plen + 2);
    $my(cw_mode_actions)[plen] = '\n';
    for (size_t i = plen + 1, j = 0; i < alen + plen + 2; i++, j++) {
      $my(cw_mode_actions)[i] = actions[j];
    }

    $my(cw_mode_actions)[alen + plen + 1] = '\0';
  }

  for (int i = $my(cw_mode_chars_len), j = 0; i < tlen; i++, j++)
    $my(cw_mode_chars)[i] = chars[j];
  $my(cw_mode_chars_len) = tlen;
  ifnot (NULL is cb) $my(cw_mode_cb) = cb;
}

private void ed_set_cw_mode_actions_default (ed_t *this) {
  utf8 chars[] = {'e', 'd', 'y', 'Y', '+', '*', 033};
  char actions[] =
    "edit selected area as filename\n"
    "delete selected area\n"
    "yank selected area\n"
    "Yank selected and also send selected area to XA_PRIMARY\n"
    "+send selected area to XA_CLIPBOARD\n"
    "*send selected area to XA_PRIMARY";

 self(set.cw_mode_actions, chars, ARRLEN(chars), actions, ed_cw_mode_cb);
}

private void ed_set_lw_mode_actions (ed_t *this, utf8 *chars, int len,
                                   char *actions, VisualLwMode_cb cb) {
  ifnot (len) return;
  int tlen = $my(lw_mode_chars_len) + len;

  ifnot ($my(lw_mode_chars_len)) {
    $my(lw_mode_chars) = Alloc (sizeof (int *) * len);
    $my(lw_mode_actions) = str_dup (actions, bytelen (actions));
  } else {
    $my(lw_mode_chars) = Realloc ($my(lw_mode_chars), sizeof (int *) * tlen);
    size_t alen = bytelen (actions);
    size_t plen = bytelen ($my(lw_mode_actions));
    $my(lw_mode_actions) = Realloc ($my(lw_mode_actions), alen + plen + 2);
    $my(lw_mode_actions)[plen] = '\n';
    for (size_t i = plen + 1, j = 0; i < alen + plen + 2; i++, j++) {
      $my(lw_mode_actions)[i] = actions[j];
    }

    $my(lw_mode_actions)[alen + plen + 1] = '\0';
  }

  for (int i = $my(lw_mode_chars_len), j = 0; i < tlen; i++, j++)
    $my(lw_mode_chars)[i] = chars[j];
  $my(lw_mode_chars_len) = tlen;
  ifnot (NULL is cb) $my(lw_mode_cb) = cb;
}

private void ed_set_lw_mode_actions_default (ed_t *this) {
  utf8 chars[] = {'s', 'w', 'd', 'y', '>', '<', '+', '*', 033};
  char actions[] =
    "substitute command for the selected lines\n"
    "write selected lines to file\n"
    "delete selected lines\n"
    "yank selected lines\n"
    "Yank selected and also send selected lines to XA_PRIMARY\n"
    ">indent in\n"
    "<indent out\n"
    "+send selected lines to XA_CLIPBOARD\n"
    "*send selected lines to XA_PRIMARY";

  self(set.lw_mode_actions, chars, ARRLEN(chars), actions, ed_lw_mode_cb);
}

private void ed_syn_append (ed_t *this, syn_t syn) {
  $my(syntaxes)[$my(num_syntaxes)] = syn;
  ifnot (NULL is syn.keywords) {
    char chars[] = COLOR_CHARS;
    int arlen = 16;
    int keyword_colors[16] = {
        HL_IDENTIFIER, HL_KEYWORD, HL_COMMENT, HL_OPERATOR, HL_NUMBER,
        HL_STRING, HL_STRING_DELIM, HL_FUNCTION, HL_VARIABLE, HL_TYPE,
        HL_DEFINITION, HL_ERROR, HL_QUOTE, HL_QUOTE_1, HL_QUOTE_2, HL_NORMAL};

#define whereis_c(c_) ({                \
	int idx_ = arlen - 1;               \
    char *sp = byte_in_str (chars, c_); \
    if (sp isnot NULL) idx_ = sp-chars; \
    idx_;                               \
})

    int num = 0;
    while (syn.keywords[num] isnot NULL) num++;
    $my(syntaxes)[$my(num_syntaxes)].keywords_len = Alloc (sizeof (size_t) * num);
    $my(syntaxes)[$my(num_syntaxes)].keywords_colors = Alloc (sizeof (size_t) * num);
    for (int i = 0; i < num; i++) {
      size_t len = bytelen (syn.keywords[i]);
      $my(syntaxes)[$my(num_syntaxes)].keywords_len[i] = len - 2;
      char c = syn.keywords[i][len-1];
      $my(syntaxes)[$my(num_syntaxes)].keywords_colors[i] =
         keyword_colors[whereis_c(c)];
    }
  }
  $my(num_syntaxes)++;
#undef whereis_c
}

private void ed_init_syntaxes (ed_t *this) {
  ed_syn_append (this, HL_DB[0]);
  ed_syn_append (this, HL_DB[1]);
}

private void ed_free (ed_t *this) {
  if (this is NULL) return;

  win_t *w = this->head;
  win_t *next;

  while (w isnot NULL) {
    next = w->next;
    win_free (w);
    w = next;
  }

  if ($myprop isnot NULL) {
    free ($my(dim));
    free ($my(saved_cwd));

    My(String).free ($my(topline));
    My(String).free ($my(msgline));
    My(String).free ($my(last_insert));
    My(String).free ($my(ed_str));
    My(Ustring).free ($my(uline));

    My(Video).free ($my(video));

    history_free (&$my(history));
    env_free (&$my(env));

    for (int i = 0; i < NUM_REGISTERS; i++)
      self(free_reg, &$my(regs)[i]);

    for (int i = 0; i < $my(num_syntaxes); i++) {
      free ($my(syntaxes)[i].keywords_len);
      free ($my(syntaxes)[i].keywords_colors);
    }

    free ($my(cw_mode_chars)); free ($my(cw_mode_actions));
    free ($my(lw_mode_chars)); free ($my(lw_mode_actions));
    free ($my(word_actions_chars));
    free ($my(word_actions_cb));

    ved_free_rline_cbs (this);

    ved_free_on_normal_g_cbs (this);

    vstr_free ($my(word_actions));

    ved_deinit_commands (this);

    free ($myprop);
  }

  free (this);
}

private void ed_init_special_win (ed_t *this) {
  ved_scratch_buf (this);
  ved_msg_buf (this);
  ved_diff_buf (this);
  ved_search_buf (this);
}

private ed_t *ed_init (ed_T *E) {
  ed_t *this = AllocType (ed);
  this->prop = AllocProp (ed);

  $my(dim) = ed_dim_new (this, 1, $from($from(E, term), lines), 1, $from($from(E, term), columns));
  $my(has_topline) = $my(has_msgline) = $my(has_promptline) = 1;

  $my(Me) = E;
  $my(Cstring) = &E->Cstring;
  $my(Vstring) = &E->Vstring;
  $my(Ustring) = &E->Ustring;
  $my(String) = &E->String;
  $my(Re) = &E->Re;
  $my(Term) = &E->Term;
  $my(Input) = &E->Input;
  $my(Screen) = &E->Screen;
  $my(Cursor) = &E->Cursor;
  $my(Video) = &E->Video;
  $my(Win) = &E->Win;
  $my(Buf) = &E->Buf;
  $my(Msg) = &E->Msg;
  $my(Error) = &E->Error;
  $my(File) = &E->File;
  $my(Path) = &E->Path;
  $my(Dir) =  &E->Dir;
  $my(Rline) = &E->Rline;
  $my(Vsys) = &E->Vsys;
  $my(Venv) = &E->Venv;

  $my(env) = env_new ();

  $my(term) = $from(E, term);
  $my(video) = My(Video).new (OUTPUT_FD, $from($my(term), lines),
      $from($my(term), columns), 1, 1);
  My(Term).set ($my(term));

  $my(topline) = My(String).new ($from($my(term), columns));
  $my(msgline) = My(String).new ($from($my(term), columns));
  $my(ed_str) =  My(String).new  ($from($my(term), columns));
  $my(last_insert) = My(String).new ($from($my(term), columns));
  $my(uline) = My(Ustring).new ();

  $my(msg_tabwidth) = 2;
  $my(msg_row) = $from($my(term), lines);
  $my(prompt_row) = $my(msg_row) - 1;
  $my(saved_cwd) = dir_current ();

  $my(history) = AllocType (hist);
  $my(history)->search = AllocType (h_search);
  $my(history)->rline = AllocType (h_rline);
  $my(history)->rline->history_idx = 0;
  $my(max_num_hist_entries) = RLINE_HISTORY_NUM_ENTRIES;
  $my(max_num_undo_entries) = UNDO_NUM_ENTRIES;

  this->name_gen = 26;

  ed_register_init_all (this);

  ed_init_syntaxes (this);

  ved_init_commands (this);

  ed_init_special_win (this);

  ed_set_cw_mode_actions_default (this);
  ed_set_lw_mode_actions_default (this);
  ed_set_word_actions_default (this);

  $my(num_rline_cbs) = $my(num_on_normal_g_cbs) = 0;

  current_list_append (E, this);

  return this;
}

private ed_t *ed_new (ed_T *E, int num_wins) {
  ed_t *this = ed_init (E);

  if (num_wins <= 0) num_wins = 1;

  int num_frames = 1;

  win_t *w;
  loop (num_wins) {
    w = self(win.new, NULL, num_frames);
    self(append.win, w);
  }

  return this;
}

private ed_T *__allocate_prop__ (ed_T *this) {
  $myprop = AllocProp (ed);
  $my(Cstring) = &this->Cstring;
  $my(String) = &this->String;
  $my(Vstring) = &this->Vstring;
  $my(Re) = &this->Re;
  $my(Term) = &this->Term;
  $my(Input) = &this->Input;
  $my(Screen) = &this->Screen;
  $my(Cursor) = &this->Cursor;
  $my(Video) = &this->Video;
  $my(Win) = &this->Win;
  $my(Buf) = &this->Buf;
  $my(Msg) = &this->Msg;
  $my(Error) = &this->Error;
  $my(File) = &this->File;
  $my(Path) = &this->Path;
  $my(Dir) = &this->Dir;
  $my(Rline) = &this->Rline;
  $my(Vsys) = &this->Vsys;
  $my(Venv) = &this->Venv;

  $my(Me) = this;
  return this;
}

private void __deallocate_prop__ (ed_T *this) {
  if ($myprop isnot NULL) {
    My(Video).free ($my(video));
    My(Term).free ($my(term));
    free ($myprop); $myprop = NULL;
  }
}

private int __init__ (ed_T *this) {
  __allocate_prop__ (this);

  $my(term) = My(Term).new ();
  $from($my(term), Me) = &this->Term;
  My(Term).set ($my(term));
  My(Term).reset ($my(term));
  setvbuf (stdin, 0, _IONBF, 0);
  return OK;
}

private void __deinit__ (ed_T *this) {
  My(Term).reset ($my(term));
  __deallocate_prop__ (this);
}

public cstring_T __init_cstring__ (void) {
  return ClassInit (cstring,
    .self = SelfInit (cstring,
      .cmp_n = str_cmp_n,
      .dup = str_dup,
      .eq = str_eq,
      .eq_n = str_eq_n,
      .chop = str_chop,
      .itoa = itoa,
      .extract_word_at = str_extract_word_at,
      .substr = str_substr,
      .byte_in_str = byte_in_str,
      .trim = SubSelfInit (cstring, trim,
        .end = str_trim_end,
      ),
    )
  );
}

public ustring_T __init_ustring__ (void) {
  return ClassInit (ustring,
    .self = SelfInit (ustring,
      .get = SubSelfInit (ustring, get,
        .code_at = ustring_get_code_at
      ),
      .new = ustring_new,
      .free = ustring_free,
      .encode = ustring_encode,
      .to_lower = ustring_to_lower,
      .to_upper = ustring_to_upper,
      .is_lower = ustring_is_lower,
      .is_upper = ustring_is_upper,
      .character = ustring_character,
      .change_case = ustring_change_case,
      .charlen = ustring_charlen
    )
  );
}

public string_T __init_string__ (void) {
  return ClassInit (string,
    .self = SelfInit (string,
      .free = string_free,
      .new = string_new,
      .reallocate = string_reallocate,
      .new_with = string_new_with,
      .new_with_len = string_new_with_len,
      .new_with_fmt = string_new_with_fmt,
      .insert_at = string_insert_at,
      .insert_at_with_len  = string_insert_at_with_len,
      .append = string_append,
      .append_fmt = string_append_fmt,
      .append_with_len = string_append_with_len,
      .append_byte = string_append_byte,
      .prepend = string_prepend,
      .prepend_fmt = string_prepend_fmt,
      .prepend_byte = string_prepend_byte,
      .delete_numbytes_at = string_delete_numbytes_at,
      .replace_numbytes_at_with = string_replace_numbytes_at_with,
      .replace_with = string_replace_with,
      .replace_with_len = string_replace_with_len,
      .replace_with_fmt = string_replace_with_fmt,
      .clear = string_clear,
      .clear_at = string_clear_at,
    )
  );
}

public void __deinit_string__ (string_T *this) {
  (void) this;
}

public term_T __init_term__ (void) {
  return ClassInit (term,
    .self = SelfInit (term,
      .set_mode = term_set_mode,
      .set = term_set,
      .reset = term_reset,
      .new = term_new,
      .free  = term_free,
      .set_name = term_set_name,
      .init_size = term_init_size,
      .get = SubSelfInit (term, get,
        .dim = term_get_dim
      ),
      .Cursor = SubSelfInit (term, cursor,
        .get_pos = term_cursor_get_ptr_pos,
        .set_pos = term_cursor_set_ptr_pos,
        .hide = term_cursor_hide,
        .show = term_cursor_show,
        .restore = term_cursor_restore
      ),
      .Screen = SubSelfInit (term, screen,
        .restore = term_screen_restore,
        .save = term_screen_save,
        .clear = term_screen_clear,
        .clear_eol = term_screen_clear_eol
      ),
      .Input = SubSelfInit (term, input,
        .get = term_get_input
      )
    )
  );
}

public void __deinit_term__ (term_T *this) {
  (void) this;
}

public video_T __init_video__ (void) {
  return ClassInit (video,
    .self = SelfInit (video,
      .free = video_free,
      .flush = video_flush,
      .new =  video_new,
      .set_with = video_set_row_with,
      .Draw = SubSelfInit (video, draw,
        .row_at = video_draw_at,
        .all = video_draw_all
      )
    )
  );
}

public void __deinit_video__ (video_T *this) {
  (void) this;
}

private ed_T *editor_new (char *name) {
  ed_T *this = Alloc (sizeof (ed_T));
  *this = ClassInit (ed,
    .self = SelfInit (ed,
      .init = ed_init,
      .new = ed_new,
      .free = ed_free,
      .quit = ved_quit,
      .free_reg = ed_register_free,
      .loop = ved_loop,
      .main = ved_main,
      .scratch = ved_scratch,
      .messages = ved_messages,
      .resume = ed_resume,
      .suspend = ed_suspend,
      .question = ved_question,
      .dim_calc = ed_dim_calc,
      .dims_init = ed_dims_init,
      .set = SubSelfInit (ed, set,
        .dim = ed_set_dim,
        .screen_size = ed_set_screen_size,
        .current_win = ed_set_current_win,
        .topline = buf_set_topline,
        .rline_cb = ved_set_rline_cb,
        .lw_mode_actions = ed_set_lw_mode_actions,
        .cw_mode_actions = ed_set_cw_mode_actions,
        .word_actions = ed_set_word_actions,
        .on_normal_g_cb = ved_set_normal_on_g_cb
      ),
      .get = SubSelfInit (ed, get,
        .bufname = ed_get_bufname,
        .current_buf = ed_get_current_buf,
        .scratch_buf = ved_scratch_buf,
        .current_win = ed_get_current_win,
        .current_win_idx = ed_get_current_win_idx,
        .state = ed_get_state,
        .win_head = ed_get_win_head,
        .win_next = ed_get_win_next,
        .win_by_idx = ed_get_win_by_idx,
        .win_by_name = ed_get_win_by_name,
        .num_win = ed_get_num_win,
        .term = ed_get_term,
        .next = ed_get_next,
        .prev = ed_get_prev,
        .num_rline_commands = ved_get_num_rline_commands
      ),
      .syn = SubSelfInit (ed, syn,
        .append = ed_syn_append,
        .get_ftype_idx = ed_syn_get_ftype_idx
      ),
      .append = SubSelfInit (ed, append,
        .win = ved_append_win,
        .message = ved_append_message,
        .message_fmt = ved_append_message_fmt,
        .toscratch = ved_append_toscratch,
        .toscratch_fmt = ved_append_toscratch_fmt,
        .command_arg = ved_append_command_arg,
        .rline_commands = ved_append_rline_commands,
        .rline_command = ved_append_rline_command
      ),
      .readjust = SubSelfInit (ed, readjust,
        .win_size =ed_win_readjust_size
      ),
      .exec = SubSelfInit (ed, exec,
        .cmd = ved_buf_exec_cmd_handler
      ),
      .buf = SubSelfInit (ed, buf,
        .change = ed_change_buf,
        .get = ed_get_buf
      ),
      .win = SubSelfInit (ed, win,
        .init = ed_win_init,
        .new = ed_win_new,
        .new_special = ed_win_new_special,
        .change = ed_win_change
      ),
      .menu = SubSelfInit (ed, menu,
        .new = ved_menu_new,
        .free = ved_menu_free,
        .create = menu_create
      ),
      .sh = SubSelfInit (ed, sh,
        .popen = ed_sh_popen
      ),
      .history = SubSelfInit (ed, history,
        .add = ved_history_add,
        .read = ved_history_read,
        .write = ved_history_write
      ),
      .draw = SubSelfInit (ed, draw,
        .current_win = ved_draw_current_win
      ),
    ),
    .Win = ClassInit (win,
      .self = SelfInit (win,
        .set = SubSelfInit (win, set,
          .current_buf = win_set_current_buf,
          .has_dividers = win_set_has_dividers,
          .video_dividers = win_set_video_dividers,
          .min_rows = win_set_min_rows,
          .num_frames = win_set_num_frames
        ),
        .buf = SubSelfInit (win, buf,
          .init = win_buf_init,
          .new = win_buf_new
        ),
        .frame = SubSelfInit (win, frame,
          .change = win_frame_change
        ),
        .get = SubSelfInit (win, get,
          .current_buf = win_get_current_buf,
          .current_buf_idx = win_get_current_buf_idx,
          .buf_by_idx = win_get_buf_by_idx,
          .buf_by_name = win_get_buf_by_name,
          .num_buf = win_get_num_buf,
          .num_cols = win_get_num_cols
        ),
        .pop = SubSelfInit (win, pop,
          .current_buf = win_pop_current_buf
        ),
        .adjust = SubSelfInit (win, adjust,
          .buf_dim = win_adjust_buf_dim,
        ),
        .draw = win_draw,
        .append_buf = win_append_buf,
        .dim_calc = win_dim_calc
      ),
    ),
    .Buf = ClassInit (buf,
      .self = SelfInit (buf,
        .free = SubSelfInit (buf, free,
          .row = buf_free_row,
          .rows =buf_free_rows
        ),
        .get = SubSelfInit (buf, get,
          .fname = buf_get_fname,
          .num_lines = buf_get_num_lines,
          .current_video_row = buf_get_current_video_row,
          .current_video_col = buf_get_current_video_col,
          .prop = SubSelfInit (bufget, prop,
            .tabwidth = buf_get_prop_tabwidth
          ),
          .row = SubSelfInit (bufget, row,
            .at = buf_get_row_at,
            .current = buf_get_row_current,
            .current_bytes = buf_get_row_current_bytes,
            .current_col_idx = buf_get_row_current_col_idx,
            .col_idx = buf_get_row_col_idx,
          ),
        ),
        .set = SubSelfInit (buf, set,
          .fname = buf_set_fname,
          .video_first_row = buf_set_video_first_row,
          .ftype = buf_set_ftype,
          .mode = buf_set_mode,
          .show_statusline = buf_set_show_statusline,
          .modified = buf_set_modified,
          .as = SubSelfInit (bufset, as,
            .unamed = buf_set_as_unamed,
            .non_existant = buf_set_as_non_existant
          ),
          .normal = SubSelfInit (bufset, normal,
            .at_beg_cb = buf_set_normal_cb
          ),
          .row = SubSelfInit (bufset, row,
            .idx = buf_set_row_idx
          ),
        ),
        .syn = SubSelfInit (buf, syn,
          .init = buf_syn_init,
          .parser = buf_syn_parser
        ),
        .ftype = SubSelfInit (buf, ftype,
          .init = buf_ftype_init,
          .autoindent = buf_ftype_autoindent
        ),
        .to = SubSelfInit (buf, to,
          .video = buf_to_video
        ),
        .cur = SubSelfInit (buf, cur,
          .set = buf_current_set,
          .prepend = buf_current_prepend,
          .append = buf_current_append,
          .append_with = buf_current_append_with,
          .append_with_len = buf_current_append_with_len,
          .prepend_with = buf_current_prepend_with,
          .replace_with = buf_current_replace_with,
          .delete = buf_current_delete,
          .pop = buf_current_pop,
         ),
        .row =  SubSelfInit (buf, row,
          .new_with = buf_row_new_with,
          .new_with_len = buf_row_new_with_len,
        ),
        .read = SubSelfInit (buf, read,
          .fname = buf_read_fname,
          .from_fp = ved_buf_read_from_fp
        ),
        .iter = SubSelfInit (buf, iter,
          .free = buf_iter_free,
          .new = buf_iter_new,
          .next = buf_iter_next
        ),
        .action = SubSelfInit (buf, action,
          .new = buf_action_new,
          .free = buf_free_action,
          .set_with = buf_action_set_with,
          .set_current = buf_action_set_current,
          .push = buf_action_push
        ),
        .normal = SubSelfInit (buf, normal,
          .bof = ved_normal_bof,
          .eof = ved_normal_eof,
          .down = ved_normal_down,
          .up = ved_normal_up,
          .page_down = ved_normal_page_down
        ),
        .draw = buf_draw,
        .flush = buf_flush,
        .draw_cur_row = buf_draw_cur_row,
        .clear = ved_buf_clear,
        .append_with = buf_append_with,
        .write = buf_write,
        .substitute = buf_substitute,
        .input_box = buf_input_box,
        .init_fname = buf_init_fname
      ),
    ),
    .Msg = ClassInit (msg,
      .self = SelfInit (msg,
        .set = ed_msg_set,
        .set_fmt = ed_msg_set_fmt,
        .send = ed_msg_send,
        .send_fmt = ed_msg_send_fmt,
        .error = ed_msg_error,
        //.error_fmt = ed_msg_error_fmt,
        .fmt = ed_msg_fmt,
        .write = ed_msg_write
      ),
    ),
    .Error = ClassInit (error,
      .self = SelfInit (error,
        .string = ed_error_string
      ),
    ),
    .Video = __init_video__ (),
    .Term = __init_term__ (),
    .String = __init_string__ (),
    .Cstring = __init_cstring__ (),
    .Vstring = __init_vstring__ (),
    .Ustring = __init_ustring__ (),
    .Re = __init_re__ (),
    .File = __init_file__ (),
    .Path = __init_path__ (),
    .Dir = __init_dir__ (),
    .Rline = __init_rline__ (),
    .Vsys = __init_vsys__ (),
    .Venv = __init_venv__ ()
  );

  this->Cursor.self = this->Term.self.Cursor;
  this->Screen.self = this->Term.self.Screen;
  this->Input.self = this->Term.self.Input;

  strcpy (this->name, name);
  return this;
}

public ed_T *__init_ed__ (void) {
  ed_T *this = editor_new ("veda");

  if (NOTOK is __init__ (this)) {
    this->error_state |= ED_INIT_ERROR;
    return NULL;
  }

  return this;
}

public void __deinit_ed__ (ed_T *Ed) {
  __deinit__ (Ed);

  ed_t *this = Ed->head;
  while (this) {
    ed_t *tmp = this->next;
    self(free);
    this = tmp;
  }

  free (Ed);
}

private int fp_tok_cb (vstr_t *unused, char *bytes, void *fp_type) {
  (void) unused;
  fp_t *fpt = (fp_t *) fp_type;
  ssize_t num = fprintf (fpt->fp, "%s\n", bytes);
  if   (0 > num) fpt->error = errno;
  else fpt->num_bytes += num;
  return STRCHOP_OK;
}

public mutable size_t tostderr (char *bytes) {
  vstr_t unused;
  fp_t fpt = {.fp = stderr};
  str_chop (bytes, '\n', &unused, fp_tok_cb, &fpt);
  return fpt.num_bytes;
}

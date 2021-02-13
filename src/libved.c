#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#ifndef __MACH__
#define _DEFAULT_SOURCE

#else
#define _DARWIN_C_SOURCE

// https://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x
#include <sys/time.h>

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#endif /* __MACH__ */

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
#include <sys/param.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>

#include "libved.h"
#include "__libved.h"

private int cstring_eq (const char *sa, const char *sb) {
  const uchar *spa = (const uchar *) sa;
  const uchar *spb = (const uchar *) sb;
  for (; *spa == *spb; spa++, spb++)
    if (*spa == 0) return 1;

  return 0;
}

private int cstring_cmp_n (const char *sa, const char *sb, size_t n) {
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
private int cstring_eq_n  (const char *sa, const char *sb, size_t n) {
  return (0 == cstring_cmp_n (sa, sb, n));
}

private char *cstring_byte_in_str (const char *s, int c) {
  const char *sp = s;
  while (*sp != c) {
    if (*sp == 0) return NULL;
    sp++;
  }
  return (char *) sp;
}

private char *cstring_byte_null_in_str (const char *s) {
  return cstring_byte_in_str (s, 0);
}

private char *cstring_byte_in_str_r (const char *s, int c) {
  const char *sp = cstring_byte_null_in_str (s);
  if (sp == s) return NULL;
  while (*--sp != c)
    if (sp == s) return NULL;

  return (char *) sp;
}

/* the same function under a different name is 25-30 times slower */
private char *strstr (const char *str, const char *substr) {
  while (*str != '\0') {
    if (*str == *substr) {
      const char *spa = str + 1;
      const char *spb = substr + 1;

      while (*spa && *spb){
        if (*spa != *spb)
          break;
        spa++; spb++;
      }

      if (*spb == '\0')
        return (char *) str;
    }

    str++;
  }

  return NULL;
}

private size_t byte_cp (char *dest, const char *src, size_t nelem) {
  const char *sp = src;
  size_t len = 0;

  while (len < nelem and *sp) { // this differs in memcpy()
    dest[len] = *sp++;
    len++;
  }

  return len;
}

private size_t byte_cp_all (char *dest, const char *src, size_t nelem) {
  const char *sp = src;
  size_t len = 0;

  while (len < nelem) {
    dest[len] = *sp++; // like memcpy (needed in one case in the code)
    len++;             // i'm not sure if it is wise
  }

  return len;
}

private size_t cstring_cat (char *dest, size_t dest_sz, const char *src) {
  char *dp = cstring_byte_null_in_str (dest);
  size_t len = dp - dest;
  size_t i = 0;

  // do not change it for *src - it is confirmed that doesn't provide the expected
  while (src[i] and i + len < dest_sz - 1) *dp++ = src[i++];
  *dp = '\0';
  return len + i;
}

private size_t cstring_byte_mv (char *str, size_t len, size_t to_idx,
                                   size_t from_idx, size_t nelem) {
  if (from_idx is to_idx) return 0;
  while (to_idx + nelem > len) nelem--;

  size_t n = nelem;

  if (to_idx > from_idx) {
    char *sp = str + from_idx + nelem;
    char *dsp = str + to_idx + nelem;

    while (nelem--) *--dsp = *--sp;
    return (n - nelem) - 1;
  }

  while (from_idx + nelem > len) nelem--;
  n = nelem;

  char *sp = str + from_idx;
  char *dsp = str + to_idx;

  while (nelem) {
    ifnot (*sp) {  // stop at the first null byte
      *dsp = '\0'; // this differs in memmove()
      break;
    }

    *dsp++ = *sp++;
    nelem--;
  }

  return n - nelem;
}

private size_t cstring_cp (char *dest, size_t dest_len, const char *src, size_t nelem) {
  size_t num = (nelem > (dest_len - 1) ? dest_len - 1 : nelem);
  size_t len = (NULL is src ? 0 : byte_cp (dest, src, num));
  dest[len] = '\0';
  return len;
}

private size_t cstring_cp_fmt (char *dest, size_t dest_len, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  return cstring_cp (dest, dest_len, bytes, len);
}

/* the signature changed as in this namespace, size has been already computed */
private char  *cstring_dup (const char *src, size_t len) {
  /* avoid recomputation */
  // size_t len = bytelen (src);
  char *dest = Alloc (len + 1);
  cstring_cp (dest, len + 1, src, len);
  return dest;
}

private char  *cstring_trim_end (char *bytes, char c) {
  char *sp = cstring_byte_null_in_str (bytes);
  sp--;

  while (sp >= bytes) {
    if (*sp isnot c) break;
    *sp = '\0';
    if (sp is bytes) break;
  }
  return bytes;
}

private char  *cstring_substr (char *dest, size_t len, char *src, size_t src_len, size_t idx) {
  if (src_len < idx + len) {
    dest[0] = '\0';
    return NULL;
  }

  for (size_t i = 0; i < len; i++) dest[i] = src[i+idx];
  dest[len] = '\0';
  return dest;
}

/* This is itoa version 0.4, written by Lukás Chmela and released under GPLv3,
 * Original Source:  http://www.strudel.org.uk/itoa/
 */

/* this is for integers (i wonder maybe is better to use *printf() - but i hate to
   do it, because of the short code and its focus to this specific functionality) */
private char *cstring_itoa (int value, char *result, int base) {
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

private char  *cstring_extract_word_at (
char *bytes,  size_t bsize, char *word, size_t wsize, char *Nwtype, size_t Nwsize,
                                               int cur_idx, int *fidx, int *lidx) {
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

private cstring_T __init_cstring__ (void) {
  return ClassInit (cstring,
    .self = SelfInit (cstring,
      .cp = cstring_cp,
      .eq = cstring_eq,
      .itoa = cstring_itoa,
      .cat = cstring_cat,
      .dup = cstring_dup,
      .eq_n = cstring_eq_n,
      .chop = cstring_chop,
      .cmp_n = cstring_cmp_n,
      .substr = cstring_substr,
      .cp_fmt = cstring_cp_fmt,
      .bytes_in_str = strstr,
      .extract_word_at = cstring_extract_word_at,
      .trim = SubSelfInit (cstring, trim,
        .end = cstring_trim_end,
      ),
      .byte = SubSelfInit (cstring, byte,
        .mv = cstring_byte_mv,
        .in_str = cstring_byte_in_str,
        .in_str_r = cstring_byte_in_str_r,
        .null_in_str = cstring_byte_null_in_str
      ),
    )
  );
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

private int cwidth (utf8 c) {
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

private int ustring_width (char *s, int tabwidth) {
  if (s[0] >= ' ' and s[0] <= '~') return 1;
  if (s[0] is '\t') return tabwidth;
  return cwidth (utf8_code (s));
}

/* The following function is from the is_utf8 project at:
   https://github.com/JulienPalard/is_utf8
   specifically the is_utf8.c unit. 
   Many Thanks
 */

/* is_utf8 is distributed under the following terms: */

/*
Copyright (c) 2013 Palard Julien. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must rei_produce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*/

/*
  Check if the given unsigned char * is a valid utf-8 sequence.

  Return value :
  If the string is valid utf-8, 0 is returned.
  Else the position, starting from 1, is returned.

  Source:
   http://www.unicode.org/versions/Unicode7.0.0/UnicodeStandard-7.0.pdf
   page 124, 3.9 "Unicode Encoding Forms", "UTF-8"

  Table 3-7. Well-Formed UTF-8 Byte Sequences
  -----------------------------------------------------------------------------
  |  Code Points        | First Byte | Second Byte | Third Byte | Fourth Byte |
  |  U+0000..U+007F     |     00..7F |             |            |             |
  |  U+0080..U+07FF     |     C2..DF |      80..BF |            |             |
  |  U+0800..U+0FFF     |         E0 |      A0..BF |     80..BF |             |
  |  U+1000..U+CFFF     |     E1..EC |      80..BF |     80..BF |             |
  |  U+D000..U+D7FF     |         ED |      80..9F |     80..BF |             |
  |  U+E000..U+FFFF     |     EE..EF |      80..BF |     80..BF |             |
  |  U+10000..U+3FFFF   |         F0 |      90..BF |     80..BF |      80..BF |
  |  U+40000..U+FFFFF   |     F1..F3 |      80..BF |     80..BF |      80..BF |
  |  U+100000..U+10FFFF |         F4 |      80..8F |     80..BF |      80..BF |
  -----------------------------------------------------------------------------
*/

private size_t ustring_validate (unsigned char *str, size_t len,
                              char **message, int *faulty_bytes) {
  size_t i = 0;
  *message = NULL;
  *faulty_bytes = 0;

  while (i < len) {
    if (str[i] <= 0x7F) { /* 00..7F */
      i += 1;
    } else if (str[i] >= 0xC2 && str[i] <= 0xDF) { /* C2..DF 80..BF */
      if (i + 1 < len) { /* Expect a 2nd byte */
        if (str[i + 1] < 0x80 || str[i + 1] > 0xBF) {
          *message = "After a first byte between C2 and DF, expecting a 2nd byte between 80 and BF";
          *faulty_bytes = 2;
          return i;
        }
      } else {
        *message = "After a first byte between C2 and DF, expecting a 2nd byte.";
        *faulty_bytes = 1;
        return i;
      }
      i += 2;

    } else if (str[i] == 0xE0) { /* E0 A0..BF 80..BF */
      if (i + 2 < len) { /* Expect a 2nd and 3rd byte */
        if (str[i + 1] < 0xA0 || str[i + 1] > 0xBF) {
          *message = "After a first byte of E0, expecting a 2nd byte between A0 and BF.";
          *faulty_bytes = 2;
          return i;
        }

        if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
          *message = "After a first byte of E0, expecting a 3nd byte between 80 and BF.";
          *faulty_bytes = 3;
          return i;
        }
      } else {
        *message = "After a first byte of E0, expecting two following bytes.";
        *faulty_bytes = 1;
        return i;
      }
      i += 3;

    } else if (str[i] >= 0xE1 && str[i] <= 0xEC) { /* E1..EC 80..BF 80..BF */
      if (i + 2 < len) { /* Expect a 2nd and 3rd byte */
        if (str[i + 1] < 0x80 || str[i + 1] > 0xBF) {
          *message = "After a first byte between E1 and EC, expecting the 2nd byte between 80 and BF.";
          *faulty_bytes = 2;
          return i;
        }

        if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
          *message = "After a first byte between E1 and EC, expecting the 3rd byte between 80 and BF.";
          *faulty_bytes = 3;
          return i;
        }
      } else {
        *message = "After a first byte between E1 and EC, expecting two following bytes.";
        *faulty_bytes = 1;
        return i;
      }
      i += 3;

    } else if (str[i] == 0xED) { /* ED 80..9F 80..BF */
      if (i + 2 < len) { /* Expect a 2nd and 3rd byte */
        if (str[i + 1] < 0x80 || str[i + 1] > 0x9F) {
          *message = "After a first byte of ED, expecting 2nd byte between 80 and 9F.";
          *faulty_bytes = 2;
          return i;
        }

        if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
          *message = "After a first byte of ED, expecting 3rd byte between 80 and BF.";
          *faulty_bytes = 3;
          return i;
        }
      } else {
        *message = "After a first byte of ED, expecting two following bytes.";
        *faulty_bytes = 1;
        return i;
      }
      i += 3;

    } else if (str[i] >= 0xEE && str[i] <= 0xEF) { /* EE..EF 80..BF 80..BF */
      if (i + 2 < len) { /* Expect a 2nd and 3rd byte */
        if (str[i + 1] < 0x80 || str[i + 1] > 0xBF) {
          *message = "After a first byte between EE and EF, expecting 2nd byte between 80 and BF.";
          *faulty_bytes = 2;
          return i;
        }

        if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
          *message = "After a first byte between EE and EF, expecting 3rd byte between 80 and BF.";
          *faulty_bytes = 3;
          return i;
        }
      } else {
        *message = "After a first byte between EE and EF, two following bytes.";
        *faulty_bytes = 1;
        return i;
      }
      i += 3;

    } else if (str[i] == 0xF0) { /* F0 90..BF 80..BF 80..BF */
      if (i + 3 < len) { /* Expect a 2nd, 3rd 3th byte */
        if (str[i + 1] < 0x90 || str[i + 1] > 0xBF) {
          *message = "After a first byte of F0, expecting 2nd byte between 90 and BF.";
          *faulty_bytes = 2;
          return i;
        }

        if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
          *message = "After a first byte of F0, expecting 3rd byte between 80 and BF.";
          *faulty_bytes = 3;
          return i;
        }

        if (str[i + 3] < 0x80 || str[i + 3] > 0xBF) {
          *message = "After a first byte of F0, expecting 4th byte between 80 and BF.";
          *faulty_bytes = 4;
          return i;
        }
      } else {
        *message = "After a first byte of F0, expecting three following bytes.";
        *faulty_bytes = 1;
        return i;
      }
      i += 4;

    } else if (str[i] >= 0xF1 && str[i] <= 0xF3) { /* F1..F3 80..BF 80..BF 80..BF */
      if (i + 3 < len) { /* Expect a 2nd, 3rd 3th byte */
        if (str[i + 1] < 0x80 || str[i + 1] > 0xBF) {
          *message = "After a first byte of F1, F2, or F3, expecting a 2nd byte between 80 and BF.";
          *faulty_bytes = 2;
          return i;
        }

        if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
          *message = "After a first byte of F1, F2, or F3, expecting a 3rd byte between 80 and BF.";
          *faulty_bytes = 3;
          return i;
        }

        if (str[i + 3] < 0x80 || str[i + 3] > 0xBF) {
          *message = "After a first byte of F1, F2, or F3, expecting a 4th byte between 80 and BF.";
          *faulty_bytes = 4;
          return i;
        }
      } else {
        *message = "After a first byte of F1, F2, or F3, expecting three following bytes.";
        *faulty_bytes = 1;
        return i;
      }
      i += 4;

    } else if (str[i] == 0xF4) { /* F4 80..8F 80..BF 80..BF */
      if (i + 3 < len) { /* Expect a 2nd, 3rd 3th byte */
        if (str[i + 1] < 0x80 || str[i + 1] > 0x8F) {
          *message = "After a first byte of F4, expecting 2nd byte between 80 and 8F.";
          *faulty_bytes = 2;
          return i;
        }

        if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
          *message = "After a first byte of F4, expecting 3rd byte between 80 and BF.";
          *faulty_bytes = 3;
          return i;
        }

        if (str[i + 3] < 0x80 || str[i + 3] > 0xBF) {
          *message = "After a first byte of F4, expecting 4th byte between 80 and BF.";
          *faulty_bytes = 4;
          return i;
        }
      } else {
        *message = "After a first byte of F4, expecting three following bytes.";
        *faulty_bytes = 1;
        return i;
      }
      i += 4;

    } else {
      *message = "Expecting bytes in the following ranges: 00..7F C2..F4.";
      *faulty_bytes = 1;
      return i;
    }
  }

  message = NULL;
  return 0;
}

private void ustring_free_members (Ustring_t *u) {
  ifnot (u->num_items) return;
  ustring_t *it = u->head;
  while (it) {
    ustring_t *tmp = it->next;
    free (it);
    it = tmp;
  }
  u->num_items = 0;
  u->cur_idx = -1;
  u->head = u->tail = u->current = NULL;
}

private void ustring_free (Ustring_t *u) {
  if (NULL is u) return;
  ustring_free_members (u);
  free (u);
}

private Ustring_t *ustring_new (void) {
  return Alloc (sizeof (Ustring_t));
}

private ustring_t *ustring_encode (Ustring_t *u, char *bytes,
            size_t len, int clear_line, int tabwidth, int curidx) {
  if (clear_line) ustring_free_members (u);

  ifnot (len) {
    u->num_items = 0;
    return NULL;
  }

  int curpos = 0;

  char *sp = bytes;
  u->len = 0;

  while (*sp) {
    uchar c = (uchar) *sp;
    ustring_t *chr = AllocType (ustring);
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
    current_list_append (u, chr);
    if (curidx is u->len or (u->len + (chr->len - 1) is curidx))
      curpos = u->cur_idx;

    u->len += chr->len;
    sp++;
  }

  current_list_set (u, curpos);
  return u->current;
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

/* use the above code (many thanks) and adjust it for this environment */
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

private int ustring_swap_case (char *dest, char *src, size_t src_len) {
  size_t idx = 0;
  while (idx < src_len) {
    int len = 0;
    utf8 c = ustring_get_code_at (src, src_len, idx, &len);
    int is_upper = ustring_is_upper (c);
    char buf[len];  // this might called dispatch in programming (not sure though)
    ustring_character ((is_upper ? ustring_to_lower : ustring_to_upper) (c),
        buf, &len);
    for (int i = 0; i < len; i++)
      dest[idx] = buf[i];

    idx += len;
  }

  dest[idx] = '\0';
  return OK;
}

private ustring_T __init_ustring__ (void) {
  return ClassInit (ustring,
    .self = SelfInit (ustring,
      .get = SubSelfInit (ustring, get,
        .code_at = ustring_get_code_at
      ),
      .new = ustring_new,
      .free = ustring_free,
      .width = ustring_width,
      .encode = ustring_encode,
      .validate = ustring_validate,
      .to_lower = ustring_to_lower,
      .to_upper = ustring_to_upper,
      .is_lower = ustring_is_lower,
      .is_upper = ustring_is_upper,
      .character = ustring_character,
      .swap_case = ustring_swap_case,
      .change_case = ustring_change_case,
      .charlen = ustring_charlen
    )
  );
}

/* and here it starts */
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

/* this is not like realloc(), as len here is the extra size */
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
  len = cstring_cp (buf, sz, bytes, len);
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
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len+1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  return string_new_with_len (bytes, len);
}

private string_t *string_insert_at_with_len (string_t *this,
                     const char *bytes, int idx, size_t len) {
  size_t bts = this->num_bytes + len;
  if (bts >= this->mem_size) string_reallocate (this, bts - this->mem_size + 1);

  if (idx is (int) this->num_bytes) {
    byte_cp (this->bytes + this->num_bytes, bytes, len);
  } else {
    cstring_byte_mv (this->bytes, this->mem_size - 1, idx + len, idx, this->num_bytes - idx);
    byte_cp (this->bytes + idx, bytes, len);
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
    tostderr (STR_FMT ("this->bytes:\n%s\nlen: %zd\n(argument) index: %d\n",
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

  cstring_byte_mv (this->bytes, this->num_bytes + 1, 1, 0, this->num_bytes);

  this->bytes[0] = c;
  this->bytes[++this->num_bytes] = '\0';
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
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  return string_insert_at_with_len (this, bytes, this->num_bytes, len);
}

private string_t *string_prepend_fmt (string_t *this, const char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  return string_insert_at_with_len (this, bytes, 0, len);
}

private int string_delete_numbytes_at (string_t *this, int num, int idx) {
  if (num < 0) return NOTOK;
  ifnot (num) return OK;

  if (idx < 0 or idx >= (int) this->num_bytes or
      idx + num > (int) this->num_bytes)
    return INDEX_ERROR;

  if (idx + num isnot (int) this->num_bytes)
    byte_cp (this->bytes + idx, this->bytes + idx + num,
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
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  return string_replace_with_len (this, bytes, len);
}

private string_t *string_trim_end (string_t *this, char c) {
  char *sp = cstring_byte_null_in_str (this->bytes);
  sp--;

  while (sp >= this->bytes) {
    if (*sp isnot c) break;
    string_clear_at (this, -1);
    if (sp is this->bytes) break;
    sp--;
  }

  return this;
}

private string_T __init_string__ (void) {
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
      .trim_end = string_trim_end,
      .clear = string_clear,
      .clear_at = string_clear_at,
    )
  );
}

private void __deinit_string__ (string_T *this) {
  (void) this;
}

/* array semantics type */

private void vstring_clear (Vstring_t *this) {
  if (NULL is this) return;

  vstring_t *it = this->head;
  while (it) {
    vstring_t *tmp = it->next;
    string_free (it->data);
    free (it);
    it = tmp;
  }

  this->head = this->tail = this->current = NULL;
  this->num_items = 0; this->cur_idx = -1;
}

private void vstring_free (Vstring_t *this) {
  if (NULL is this) return;
  vstring_clear (this);
  free (this);
}

private Vstring_t *vstring_new (void) {
  return Alloc (sizeof (Vstring_t));
}

private size_t vstring_get_size (Vstring_t *this) {
  size_t len = 0;
  vstring_t *it = this->head;

  while (it) {
    len += it->data->num_bytes;
    it = it->next;
  }

  return len;
}

private char *vstring_to_cstring (Vstring_t *this, int addnl) {
  size_t len = vstring_get_size (this) + (addnl ? this->num_items : 0);
  ifnot (len) return NULL;

  char *buf = Alloc (len + 1);

  vstring_t *it = this->head;

  size_t offset = 0;

  while (it) {
    byte_cp (buf + offset, it->data->bytes, it->data->num_bytes);
    offset += it->data->num_bytes;
    if (addnl) buf[offset++] = '\n';
    it = it->next;
  }

  buf[len] = '\0';
  return buf;
}

/* maybe also a vstring_join_u() for characters as separators */
private string_t *vstring_join (Vstring_t *this, char *sep) {
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

private void vstring_append (Vstring_t *this, vstring_t *new) {
  int cur_idx = this->cur_idx;
  current_list_set (this, -1);
  current_list_append (this, new);
  current_list_set(this, cur_idx);
}

private void vstring_current_append_with (Vstring_t *this, char *bytes) {
  vstring_t *vstr = AllocType (vstring);
  vstr->data = string_new_with (bytes);
  current_list_append (this, vstr);
}

private void vstring_current_append_with_len (Vstring_t *this, char *bytes, size_t len) {
  vstring_t *vstr = AllocType (vstring);
  vstr->data = string_new_with_len (bytes, len);
  current_list_append (this, vstr);
}

private void vstring_append_with_len (Vstring_t *this, char *bytes, size_t len) {
  int cur_idx = this->cur_idx;
  if (cur_idx isnot this->num_items - 1)
    current_list_set (this, -1);

  vstring_current_append_with_len (this, bytes, len);
  current_list_set(this, cur_idx);
}

private void vstring_append_with (Vstring_t *this, char *bytes) {
  int cur_idx = this->cur_idx;
  if (cur_idx isnot this->num_items - 1)
    current_list_set (this, -1);

  vstring_current_append_with (this, bytes);
  current_list_set(this, cur_idx);
}

private void vstring_append_with_fmt (Vstring_t *this, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  vstring_append_with_len (this, bytes, len);
}

private void vstring_prepend_current_with (Vstring_t *this, char *bytes) {
  vstring_t *vstr = AllocType (vstring);
  vstr->data = string_new_with (bytes);
  current_list_prepend (this, vstr);
}

/* like cstring_dup(), as a new copy */
private Vstring_t *vstring_dup (Vstring_t *this) {
  Vstring_t *vs = vstring_new ();
  vstring_t *it = this->head;
  while (it) {
    vstring_current_append_with (vs, it->data->bytes);
    it = it->next;
  }

  current_list_set (vs, this->cur_idx);
  return vs;
}

/* so far, we always want sorted and unique members */
private Vstring_t *vstring_add_sort_and_uniq (Vstring_t *this, char *bytes) {
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

private void vstring_append_uniq (Vstring_t *this, char *bytes) {
  vstring_t *it = this->head;
  while (it) {
    if (cstring_eq (it->data->bytes, bytes)) return;
    it = it->next;
  }

  vstring_t *vs = AllocType (vstring);
  vs->data = string_new_with (bytes);

  list_append (this, vs);
}

private char **vstring_shallow_copy (Vstring_t *vstr, char **array) {
  vstring_t *it = vstr->head;
  int idx = 0;
  while (it) {
    array[idx++] = it->data->bytes;
    it = it->next;
  }

  return array;
}

private vstring_t *vstring_pop_at (Vstring_t *vstr, int idx) {
  vstring_t *t = list_pop_at (vstr, vstring_t, idx);
  return t;
}

private vstring_T __init_vstring__ (void) {
  return ClassInit (vstring,
    .self = SelfInit (vstring,
      .free = vstring_free,
      .clear = vstring_clear,
      .new = vstring_new,
      .dup = vstring_dup,
      .join = vstring_join,
      .append = vstring_append,
      .append_with = vstring_append_with,
      .append_uniq = vstring_append_uniq,
      .append_with_fmt = vstring_append_with_fmt,
      .append_with_len = vstring_append_with_len,
      .shallow_copy = vstring_shallow_copy,
      .pop_at = vstring_pop_at,
      .current = SubSelfInit (vstring, current,
        .append_with = vstring_current_append_with,
        .append_with_len = vstring_current_append_with_len
      ),
      .add = SubSelfInit (vstring, add,
        .sort_and_uniq = vstring_add_sort_and_uniq
      ),
      .to = SubSelfInit (vstring, to,
        .cstring = vstring_to_cstring
      ),
      .get = SubSelfInit (vstring, get,
        .size = vstring_get_size
      )
    )
  );
}

private void __deinit_vstring__ (vstring_T *this) {
  (void) this;
}

/* this replaced cstring_tok() below */
private Vstring_t *cstring_chop (char *buf, char tok, Vstring_t *tokstr,
                                     StrChop_cb cb, void *obj) {
  Vstring_t *ts = tokstr;
  int ts_isnull = (NULL is ts);
  if (ts_isnull) ts = vstring_new ();

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
      } when commented, this broke once the code */

      char s[len + 1];
      cstring_cp (s, len + 1, p, len);

      ifnot (NULL is cb) {
        int retval;
        if (STRCHOP_NOTOK is (retval = cb (ts, s, obj))) {
          if (ts_isnull) vstring_free (ts); /* this might bring issues (not */
          return NULL;                   /* with current code though) */
        }

        if (retval is STRCHOP_RETURN)
          return ts;
      }
      else
        vstring_current_append_with (ts, s);

      sp++;
      p = sp;
      continue;
    }

    sp++;
  }

  return ts;
}

/* unused (based on strtok(), but i do not want functions with static state) */
/* private Vstring_t  *cstring_tok (char *buf, char *tok, Vstring_t *tokstr,
 *     void (*cb) (Vstring_t *, char *, void *), void *obj) {
 *   Vstring_t *ts = tokstr;
 *   if (NULL is ts) ts = vstring_new ();
 *
 *   char *src = cstring_dup (buf, bytelen (buf));
 *   char *sp = strtok (src, tok);
 *   while (sp) {
 *     ifnot (NULL is cb)
 *       cb (ts, sp, obj);
 *     else
 *       vstring_current_append_with (ts, sp);
 *     sp = strtok (NULL, tok);
 *   }
 *
 *   free (src);
 *   return ts;
 * }
 */

#define MAP_DEFAULT_LENGTH 32
#define MAP_HASH_KEY(__map__, __key__) ({           \
  ssize_t hs = 5381; int i = 0;                     \
  while (key[i]) hs = ((hs << 5) + hs) + key[i++];  \
  hs % __map__->num_slots;                          \
})

#define MAP_FREE_SLOT(_it, _tp, _fun) \
({                                    \
  while (_it) {                       \
    _tp *_tmp = _it->next;            \
    free (_it->key);                  \
    _fun (_it->val);                  \
    free (_it);                       \
    _it = _tmp;                       \
  }                                   \
})

private void imap_free_slot (imap_t *item) {
  MAP_FREE_SLOT(item, imap_t, (void));
}

private void smap_free_slot (smap_t *item) {
  MAP_FREE_SLOT(item, smap_t, string_free);
}

#define MAP_CLEAR(_map, _fun)                 \
({                                            \
  for (size_t i = 0; i < map->num_slots; i++) \
    _fun (map->slots[i]);                     \
  map->num_keys = 0;                          \
})

private void imap_clear (Imap_t *map) {
  MAP_CLEAR(map, imap_free_slot);
}

private void smap_clear (Smap_t *map) {
  MAP_CLEAR(map, smap_free_slot);
}

#define MAP_FREE(_map, _fun) \
do {                         \
  if (_map is NULL) return;  \
  _fun (_map);               \
  free (_map->slots);        \
  free (_map);               \
} while (0)

private void imap_free (Imap_t *map) {
  MAP_FREE(map, imap_clear);
}

private void smap_free (Smap_t *map) {
  MAP_FREE(map, smap_clear);
}

#define MAP_NEW(_TP, _tp, _num)                            \
({                                                         \
  _TP *_map = Alloc (sizeof (_TP));                        \
  int _num_slots = (_num < 1 ? MAP_DEFAULT_LENGTH : _num); \
  _map->slots = Alloc (sizeof (_tp *) * num_slots);        \
  _map->num_slots = _num_slots;                            \
  _map->num_keys = 0;                                      \
  for (;--_num_slots >= 0;) _map->slots[_num_slots] = 0;   \
  _map;                                                    \
})

private Imap_t *imap_new (int num_slots) {
  return MAP_NEW(Imap_t, imap_t, num_slots);
}

private Smap_t *smap_new (int num_slots) {
  return MAP_NEW(Smap_t, smap_t, num_slots);
}

private imap_t *__imap_get__ (Imap_t *imap, char *key, uint idx) {
  imap_t *slot = imap->slots[idx];
  while (slot) {
    if (cstring_eq (slot->key, key)) return slot;
    slot = slot->next;
  }
  return NULL;
}

#define MAP_GET(_tp, _map, _key, _idx)        \
({                                            \
  _idx = MAP_HASH_KEY (_map, _key);           \
  _tp *_slot = _map->slots[_idx];             \
  while (_slot) {                             \
    if (cstring_eq (_slot->key, _key)) break; \
    _slot = _slot->next;                      \
  }                                           \
  _slot;                                      \
})

private int imap_get (Imap_t *imap, char *key) {
  uint idx = 0;
  imap_t *item = MAP_GET(imap_t, imap, key, idx);
  ifnot (NULL is item) return item->val;
  return 0;
}

private string_t *smap_get (Smap_t *smap, char *key) {
  uint idx = 0;
  smap_t *item = MAP_GET(smap_t, smap, key, idx);
  ifnot (NULL is item) return item->val;
  return NULL;
}

#define MAP_SET(_tp_, _map_, _key_, _val)             \
({                                                    \
  uint _idx_ = 0;                                     \
  _tp_ *_it_ = MAP_GET(_tp_, _map_, _key_, _idx_);    \
  ifnot (NULL is _it_) {                              \
    _it_->val = _val;                                 \
  } else {                                            \
    _it_ = Alloc (sizeof (_tp_));                     \
    _it_->key = cstring_dup (_key_, bytelen (_key_)); \
    _it_->val = _val;                                 \
    _it_->next = _map_->slots[_idx_];                 \
    _map_->slots[_idx_] = _it_;                       \
    _map_->num_keys++;                                \
  }                                                   \
  _idx_;                                              \
})

private uint imap_set (Imap_t *imap, char *key, int val) {
  return MAP_SET(imap_t, imap, key, val);
}

private uint smap_set (Smap_t *smap, char *key, string_t *val) {
  return MAP_SET(smap_t, smap, key, val);
}

private uint imap_set_with_keylen (Imap_t *imap, char *key) {
  return MAP_SET(imap_t, imap, key, bytelen (key));
}

private int imap_key_exists (Imap_t *imap, char *key) {
  uint idx = 0;
  imap_t *item = MAP_GET(imap_t, imap, key, idx);
  return (NULL isnot item);
}

private int smap_key_exists (Smap_t *smap, char *key) {
  uint idx = 0;
  smap_t *item = MAP_GET(smap_t, smap, key, idx);
  return (NULL isnot item);
}

private Class (imap) __init_imap__ (void) {
  return ClassInit (imap,
    .self = SelfInit (imap,
      .new = imap_new,
      .free = imap_free,
      .clear = imap_clear,
      .get = imap_get,
      .set = imap_set,
      .set_with_keylen = imap_set_with_keylen,
      .key_exists = imap_key_exists
    )
  );
}

private Class (smap) __init_smap__ (void) {
  return ClassInit (smap,
    .self = SelfInit (smap,
      .new = smap_new,
      .free = smap_free,
      .clear = smap_clear,
      .get = smap_get,
      .set = smap_set,
      .key_exists = smap_key_exists
    )
  );
}

/* used by slre and ts */
private int is_digit (int c) {
  return (c >= '0' and c <= '9');
}

private int is_hexchar (int c) {
  return (c >= '0' and c <= '9') or cstring_byte_in_str ("abcdefABCDEF", c);
}

private int is_lower (int c) {
  return (c >= 'a' and c <= 'z');
}

private int is_space (int c) {
  return (c is ' ') or (c is '\t');
}

private int is_upper (int c) {
  return (c >= 'A' and c <= 'Z');
}

private int is_alpha (int c) {
  return is_lower (c) or is_upper (c);
}

private int is_idpunct (int c) {
  return NULL isnot cstring_byte_in_str (".:_", c);
}

private int is_identifier (int c) {
  return is_alpha (c) or is_idpunct (c);
}

private int is_notquote (int chr) {
  uchar c = (unsigned char) chr;
  return NULL is cstring_byte_in_str ("\"\n", c);
}

private int is_metacharacter (const unsigned char *s) {
  static const char *metacharacters = "^$().[]*+?|\\Ssdbfnrtv";
  return cstring_byte_in_str (metacharacters, *s) != NULL;
}

private int is_quantifier (const char *re) {
  return re[0] == '*' || re[0] == '+' || re[0] == '?';
}

private void re_reset_captures (regexp_t *re) {
  re->match_len = re->match_idx = 0;
  re->match_ptr = NULL;

  ifnot (re->flags & RE_PATTERN_IS_STRING_LITERAL) {
    string_free (re->match);
    re->match = NULL;
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
  if (re is NULL) return;
  re_free_pat (re);
  re_free_captures (re);
  free (re);
}

private regexp_t *re_new (char *pat, int flags, int num_caps, ReCompile_cb compile) {
  regexp_t *re = AllocType (regexp);
  re->flags |= flags;
  re->pat = string_new_with (pat);
  compile (re);
  re_allocate_captures (re, num_caps);
  re->match = NULL;
  return re;
}

/* slre:
   https://github.com/cesanta/slre
 */

/*
 * Copyright (c) 2004-2013 Sergey Lyubka <valenok@gmail.com>
 * Copyright (c) 2013 Cesanta Software Limited
 * All rights reserved
 *
 * This library is dual-licensed: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. For the terms of this
 * license, see <http://www.gnu.org/licenses/>.
 *
 * You are free to use this library under the terms of the GNU General
 * Public License, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * Alternatively, you can license this library under a commercial
 * license, as set out in <http://cesanta.com/i_products.html>.
 */

#define FAIL_IF(condition, error_code) if (condition) return (error_code)

#define ARRAY_SIZE ARRLEN

#ifdef RE_DEBUG
#define DBG(x) printf x
#else
#define DBG(x)
#endif

private int re_op_len (const char *re) {
  return re[0] == '\\' && re[1] == 'x' ? 4 : re[0] == '\\' ? 2 : 1;
}

private int re_set_len (const char *re, int re_len) {
  int len = 0;

  while (len < re_len && re[len] != ']') {
    len += re_op_len (re + len);
  }

  return len <= re_len ? len + 1 : -1;
}

private int re_get_op_len (const char *re, int re_len) {
  return re[0] == '[' ? re_set_len (re + 1, re_len - 1) + 1 : re_op_len (re);
}

private int re_toi (int x) {
  return is_digit (x) ? x - '0' : x - 'W';
}

private int re_hextoi (const unsigned char *s) {
  return (re_toi (ustring_to_lower (s[0])) << 4) | re_toi (ustring_to_lower (s[1]));
}

private int re_match_op (const unsigned char *re, const unsigned char *s,
                    struct regex_info *info) {
  int result = 0;
  switch (*re) {
    case '\\':
      /* Metacharacters */
      switch (re[1]) {
        case 'S': FAIL_IF(is_space (*s), RE_NO_MATCH); result++; break;
        case 's': FAIL_IF(!is_space (*s), RE_NO_MATCH); result++; break;
        case 'd': FAIL_IF(!is_digit (*s), RE_NO_MATCH); result++; break;
        case 'b': FAIL_IF(*s != '\b', RE_NO_MATCH); result++; break;
        case 'f': FAIL_IF(*s != '\f', RE_NO_MATCH); result++; break;
        case 'n': FAIL_IF(*s != '\n', RE_NO_MATCH); result++; break;
        case 'r': FAIL_IF(*s != '\r', RE_NO_MATCH); result++; break;
        case 't': FAIL_IF(*s != '\t', RE_NO_MATCH); result++; break;
        case 'v': FAIL_IF(*s != '\v', RE_NO_MATCH); result++; break;

        case 'x':
          /* Match byte, \xHH where HH is hexadecimal byte representaion */
          FAIL_IF(re_hextoi (re + 2) != *s, RE_NO_MATCH);
          result++;
          break;

        default:
          /* Valid metacharacter check is done in bar() */
          FAIL_IF(re[1] != s[0], RE_NO_MATCH);
          result++;
          break;
      }
      break;

    case '|': FAIL_IF(1, RE_INTERNAL_ERROR); break;
    case '$': FAIL_IF(1, RE_NO_MATCH); break;
    case '.': result++; break;

    default:
      if (info->flags & RE_IGNORE_CASE) {
        FAIL_IF(ustring_to_lower (*re) != ustring_to_lower (*s), RE_NO_MATCH);
      } else {
        FAIL_IF(*re != *s, RE_NO_MATCH);
      }
      result++;
      break;
  }

  return result;
}

private int re_match_set (const char *re, int re_len, const char *s,
                     struct regex_info *info) {
  int len = 0, result = -1, invert = re[0] == '^';

  if (invert) re++, re_len--;

  while (len <= re_len && re[len] != ']' && result <= 0) {
    /* Support character range */
    if (re[len] != '-' && re[len + 1] == '-' && re[len + 2] != ']' &&
        re[len + 2] != '\0') {
      result = info->flags &  RE_IGNORE_CASE ?
        ustring_to_lower (*s) >= ustring_to_lower (re[len]) && ustring_to_lower (*s) <= ustring_to_lower (re[len + 2]) :
        *s >= re[len] && *s <= re[len + 2];
      len += 3;
    } else {
      result = re_match_op ((const unsigned char *) re + len, (const unsigned char *) s, info);
      len += re_op_len (re + len);
    }
  }
  return (!invert && result > 0) || (invert && result <= 0) ? 1 : -1;
}

private int re_doh (const char *s, int s_len, struct regex_info *info, int bi);

private int re_bar (const char *re, int re_len, const char *s, int s_len,
               struct regex_info *info, int bi) {
  /* i is offset in re, j is offset in s, bi is brackets index */
  int i, j, n, step;

  for (i = j = 0; i < re_len && j <= s_len; i += step) {

    /* Handle quantifiers. Get the length of the chunk. */
    step = re[i] == '(' ? info->brackets[bi + 1].len + 2 :
      re_get_op_len (re + i, re_len - i);

    DBG(("%s [%.*s] [%.*s] re_len=%d step=%d i=%d j=%d\n", __func__,
         re_len - i, re + i, s_len - j, s + j, re_len, step, i, j));

    FAIL_IF(is_quantifier (&re[i]), RE_UNEXPECTED_QUANTIFIER_ERROR);
    FAIL_IF(step <= 0, RE_INVALID_CHARACTER_SET_ERROR);

    if (i + step < re_len && is_quantifier (re + i + step)) {
      DBG(("QUANTIFIER: [%.*s]%c [%.*s]\n", step, re + i,
           re[i + step], s_len - j, s + j));
      if (re[i + step] == '?') {
        int result = re_bar (re + i, step, s + j, s_len - j, info, bi);
        j += result > 0 ? result : 0;
        i++;
      } else if (re[i + step] == '+' || re[i + step] == '*') {
        int j2 = j, nj = j, n1, n2 = -1, ni, non_greedy = 0;

        /* Points to the regexp code after the quantifier */
        ni = i + step + 1;
        if (ni < re_len && re[ni] == '?') {
          non_greedy = 1;
          ni++;
        }

        do {
          if ((n1 = re_bar (re + i, step, s + j2, s_len - j2, info, bi)) > 0) {
            j2 += n1;
          }
          if (re[i + step] == '+' && n1 < 0) break;

          if (ni >= re_len) {
            /* After quantifier, there is nothing */
            nj = j2;
          } else if ((n2 = re_bar (re + ni, re_len - ni, s + j2,
                               s_len - j2, info, bi)) >= 0) {
            /* Regex after quantifier matched */
            nj = j2 + n2;
          }
          if (nj > j && non_greedy) break;
        } while (n1 > 0);

        /*
         * Even if we found one or more pattern, this branch will be executed,
         * changing the next captures.
         */
        if (n1 < 0 && n2 < 0 && re[i + step] == '*' &&
            (n2 = re_bar (re + ni, re_len - ni, s + j, s_len - j, info, bi)) > 0) {
          nj = j + n2;
        }

        DBG(("STAR/PLUS END: %d %d %d %d %d\n", j, nj, re_len - ni, n1, n2));
        FAIL_IF(re[i + step] == '+' && nj == j, RE_NO_MATCH);

        /* If while loop body above was not executed for the * quantifier,  */
        /* make sure the rest of the regex matches                          */
        FAIL_IF(nj == j && ni < re_len && n2 < 0, RE_NO_MATCH);

        /* Returning here cause we've matched the rest of RE already */
        return nj;
      }
      continue;
    }

    if (re[i] == '[') {
      n = re_match_set (re + i + 1, re_len - (i + 2), s + j, info);
      DBG(("SET %.*s [%.*s] -> %d\n", step, re + i, s_len - j, s + j, n));
      FAIL_IF(n <= 0, RE_NO_MATCH);
      j += n;
    } else if (re[i] == '(') {
      n = RE_NO_MATCH;
      bi++;
      FAIL_IF(bi >= info->num_brackets, RE_INTERNAL_ERROR);
      DBG(("CAPTURING [%.*s] [%.*s] [%s]\n",
           step, re + i, s_len - j, s + j, re + i + step));

      if (re_len - (i + step) <= 0) {
        /* Nothing follows brackets */
        n = re_doh (s + j, s_len - j, info, bi);
      } else {
        int j2;
        for (j2 = 0; j2 <= s_len - j; j2++) {
          if ((n = re_doh (s + j, s_len - (j + j2), info, bi)) >= 0 &&
              re_bar (re + i + step, re_len - (i + step),
                  s + j + n, s_len - (j + n), info, bi) >= 0) break;
        }
      }

      DBG(("CAPTURED [%.*s] [%.*s]:%d\n", step, re + i, s_len - j, s + j, n));
      FAIL_IF(n < 0, n);

      if (info->caps != NULL && n > 0) {
        info->caps[bi - 1].ptr = s + j;
        info->caps[bi - 1].len = n;
//        info->total_caps++;
        info->total_caps = bi;
      }
      j += n;
    } else if (re[i] == '^') {
      FAIL_IF(j != 0, RE_NO_MATCH);
    } else if (re[i] == '$') {
      FAIL_IF(j != s_len, RE_NO_MATCH);
    } else {
      FAIL_IF(j >= s_len, RE_NO_MATCH);
      n = re_match_op ((const unsigned char *) (re + i), (const unsigned char *) (s + j), info);
      FAIL_IF(n <= 0, n);
      j += n;
    }
  }

  return j;
}

/* Process branch points */
private int re_doh (const char *s, int s_len, struct regex_info *info, int bi) {
  const struct bracket_pair *b = &info->brackets[bi];
  int i = 0, len, result;
  const char *p;

  do {
    p = i == 0 ? b->ptr : info->branches[b->branches + i - 1].schlong + 1;
    len = b->num_branches == 0 ? b->len :
      i == b->num_branches ? (int) (b->ptr + b->len - p) :
      (int) (info->branches[b->branches + i].schlong - p);
    DBG(("%s %d %d [%.*s] [%.*s]\n", __func__, bi, i, len, p, s_len, s));
    result = re_bar (p, len, s, s_len, info, bi);
    DBG(("%s <- %d\n", __func__, result));
  } while (result <= 0 && i++ < b->num_branches);  /* At least 1 iteration */

  return result;
}

private int re_baz (const char *s, int s_len, struct regex_info *info) {
  int i, result = -1, is_anchored = info->brackets[0].ptr[0] == '^';

  for (i = 0; i <= s_len; i++) {
    result = re_doh (s + i, s_len - i, info, 0);

    if (result >= 0) {
      /* EXTENSION */
      info->match_idx = i;
      info->match_len = result;
      /**/
      result += i;
      break;
    }
    if (is_anchored) break;
  }

  return result;
}

private void re_setup_branch_points (struct regex_info *info) {
  int i, j;
  struct branch tmp;

  /* First, sort branches. Must be stable, no qsort. Use bubble algo. */
  for (i = 0; i < info->num_branches; i++) {
    for (j = i + 1; j < info->num_branches; j++) {
      if (info->branches[i].bracket_index > info->branches[j].bracket_index) {
        tmp = info->branches[i];
        info->branches[i] = info->branches[j];
        info->branches[j] = tmp;
      }
    }
  }

  /*
   * For each bracket, set their branch points. This way, for every bracket
   * (i.e. every chunk of regex) we know all branch points before matching.
   */
  for (i = j = 0; i < info->num_brackets; i++) {
    info->brackets[i].num_branches = 0;
    info->brackets[i].branches = j;
    while (j < info->num_branches && info->branches[j].bracket_index == i) {
      info->brackets[i].num_branches++;
      j++;
    }
  }
}

private int re_foo (const char *re, int re_len, const char *s, int s_len,
               struct regex_info *info) {
  int i, step, depth = 0;

  /* First bracket captures everything */
  info->brackets[0].ptr = re;
  info->brackets[0].len = re_len;
  info->num_brackets = 1;

  /* Make a single pass over regex string, memorize brackets and branches */
  for (i = 0; i < re_len; i += step) {
    step = re_get_op_len (re + i, re_len - i);

    if (re[i] == '|') {
      FAIL_IF(info->num_branches >= (int) ARRAY_SIZE(info->branches),
              RE_TOO_MANY_BRANCHES_ERROR);
      info->branches[info->num_branches].bracket_index =
        info->brackets[info->num_brackets - 1].len == -1 ?
        info->num_brackets - 1 : depth;
      info->branches[info->num_branches].schlong = &re[i];
      info->num_branches++;
    } else if (re[i] == '\\') {
      FAIL_IF(i >= re_len - 1, RE_INVALID_METACHARACTER_ERROR);
      if (re[i + 1] == 'x') {
        /* Hex digit specification must follow */
        FAIL_IF(re[i + 1] == 'x' && i >= re_len - 3, RE_INVALID_METACHARACTER_ERROR);
        FAIL_IF(re[i + 1] ==  'x' && !(is_hexchar (re[i + 2]) &&
                is_hexchar (re[i + 3])), RE_INVALID_METACHARACTER_ERROR);
      } else {
        FAIL_IF(!is_metacharacter ((const unsigned char *) re + i + 1),
                RE_INVALID_METACHARACTER_ERROR);
      }
    } else if (re[i] == '(') {
      FAIL_IF(info->num_brackets >= (int) ARRAY_SIZE(info->brackets),
              RE_TOO_MANY_BRACKETS_ERROR);
      depth++;  /* Order is important here. Depth increments first. */
      info->brackets[info->num_brackets].ptr = re + i + 1;
      info->brackets[info->num_brackets].len = -1;
      info->num_brackets++;
      FAIL_IF(info->num_caps > 0 && info->num_brackets - 1 > info->num_caps,
              RE_CAPS_ARRAY_TOO_SMALL_ERROR);
    } else if (re[i] == ')') {
      int ind = info->brackets[info->num_brackets - 1].len == -1 ?
        info->num_brackets - 1 : depth;
      info->brackets[ind].len = (int) (&re[i] - info->brackets[ind].ptr);
      DBG(("SETTING BRACKET %d [%.*s]\n",
           ind, info->brackets[ind].len, info->brackets[ind].ptr));
      depth--;
      FAIL_IF(depth < 0, RE_UNBALANCED_BRACKETS_ERROR);
      FAIL_IF(i > 0 && re[i - 1] == '(', RE_NO_MATCH);
    }
  }

  FAIL_IF(depth != 0, RE_UNBALANCED_BRACKETS_ERROR);
  re_setup_branch_points(info);

  return re_baz (s, s_len, info);
}

/* this is like slre_match(), with an aditional argument and three extra fields
 * in the slre regex_info structure */
private int re_match (regexp_t *re, const char *regexp, const char *s,
             int s_len, struct re_cap *caps, int num_caps, int flags) {
  struct regex_info info;

  info.flags = flags;
  info.num_brackets = info.num_branches = 0;
  info.num_caps = num_caps;
  info.caps = caps;

  info.match_idx = info.match_len = -1;
  info.total_caps = 0;

  int retval = re_foo (regexp, (int) bytelen(regexp), s, s_len, &info);

  if (0 <= retval) {
    re->match_idx = info.match_idx;
    re->match_len = info.match_len;
    re->total_caps = info.total_caps;
    re->match_ptr = (char *) s + info.match_idx;
  }

  return retval;
}

private int re_compile (regexp_t *re) {
  ifnot (cstring_cmp_n (re->pat->bytes, "(?i)", 4)) {
    re->flags |= RE_IGNORE_CASE;
    string_delete_numbytes_at (re->pat, 4, 0);
  }

  return OK;
}

private int re_exec (regexp_t *re, char *buf, size_t buf_len) {
  re->retval = RE_NO_MATCH;
  if (re->pat->num_bytes is 1 and
     (re->pat->bytes[0] is '^' or
      re->pat->bytes[0] is '$' or
      re->pat->bytes[0] is '|'))
    return re->retval;

  do {
    struct re_cap cap[re->num_caps];
    for (int i = 0; i < re->num_caps; i++) cap[i].len = 0;
    re->retval = re_match (re, re->pat->bytes, buf, buf_len,
        cap, re->num_caps, re->flags);

    if (re->retval is RE_CAPS_ARRAY_TOO_SMALL_ERROR) {
      re_free_captures (re);
      re_allocate_captures (re, re->num_caps + (re->num_caps / 2));

      continue;
    }

    if (0 > re->retval) goto theend;
    re->match = string_new_with (re->match_ptr);
    string_clear_at (re->match, re->match_len);

    for (int i = 0; i < re->total_caps; i++) {
      re->cap[i] = AllocType (capture);
      re->cap[i]->ptr = cap[i].ptr;
      re->cap[i]->len = cap[i].len;
    }
  } while (0);

theend:
  return re->retval;
}

private string_t *re_parse_substitute (regexp_t *re, char *sub, char *replace_buf) {
  string_t *substr = string_new (64);

  char *sub_p = sub;
  while (*sub_p) {
    switch (*sub_p) {
      case '\\':
        if (*(sub_p + 1) is 0) {
          cstring_cp (re->errmsg, RE_MAXLEN_ERR_MSG, "awaiting escaped char, found (null byte) 0", RE_MAXLEN_ERR_MSG - 1);
          goto theerror;
        }

        switch (*++sub_p) {
          case '&':
            string_append_byte (substr, '&');
            sub_p++;
            continue;

          case 's':
            string_append_byte (substr, ' ');
            sub_p++;
            continue;

          case '\\':
            string_append_byte (substr, '\\');
            sub_p++;
            continue;

          case '1'...'9':
            {
              int idx = 0;
              while (*sub_p and ('0' <= *sub_p and *sub_p <= '9')) {
                idx = (10 * idx) + (*sub_p - '0');
                sub_p++;
              }
              idx--;

              if (0 > idx or idx + 1 > re->total_caps) goto theerror;

              char buf[re->cap[idx]->len + 1];
              cstring_cp (buf, re->cap[idx]->len + 1, re->cap[idx]->ptr, re->cap[idx]->len);
              string_append (substr, buf);
            }

            continue;

          default:
            snprintf (re->errmsg, 256, "awaiting \\,&,s[0..9,...], got %d [%c]",
                *sub_p, *sub_p);
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

private re_T __init_re__ (void) {
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

private int file_is_sock (const char *fname) {
  struct stat st;
  if (NOTOK is stat (fname, &st)) return 0;
  return S_ISSOCK (st.st_mode);
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

private int file_is_writable (const char *fname) {
  return (0 is access (fname, R_OK));
}

private int file_is_rwx (const char *fname) {
  return (0 is access (fname, R_OK|W_OK|X_OK));
}

private int file_is_elf (const char *file) {
  int fd = open (file, O_RDONLY);
  if (-1 is fd) return 0;
  int retval = 0;
  char buf[8];
  ssize_t bts = fd_read (fd, buf, 7);
  buf[bts] = '\0';
  retval = buf[0] is 0x7f and cstring_eq_n (buf + 1, "ELF", 3);
  ifnot (retval) // static .a files magic number !<arch> (for archive)
    retval = cstring_eq_n (buf, "!<arch>", 7);

  close (fd);
  return retval;
}

private Vstring_t *file_readlines (char *file, Vstring_t *lines,
                                 FileReadLines_cb cb, void *obj) {
  Vstring_t *llines = lines;
  if (NULL is llines) llines = vstring_new ();
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
  } else {  /* by default an array of lines */
    while (-1 isnot (nread = getline (&buf, &len, fp))) {
      buf[nread - 1] = '\0';
      vstring_current_append_with (llines, buf);
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

private void file_tmpfname_free (tmpfname_t *this) {
  ifnot (this) return;
  ifnot (NULL is this->fname) {
    unlink (this->fname->bytes);
    string_free (this->fname);
    this->fname = NULL;
  }

  if (-1 isnot this->fd)
    close (this->fd);

  free (this);
}

private tmpfname_t *file_tmpfname_new (char *dname, char *prefix) {
  static unsigned int seed = 12252;
  if (NULL is dname) return NULL;
  ifnot (is_directory (dname)) return NULL;

  tmpfname_t *this = NULL;

  char bpid[6];
  pid_t pid = getpid ();
  cstring_itoa ((int) pid, bpid, 10);

  int len = bytelen (dname) + bytelen (bpid) + bytelen (prefix) + 10;

  char name[len];
  snprintf (name, len, "%s/%s-%s.xxxxxx", dname, prefix, bpid);

  srand ((unsigned int) time (NULL) + (unsigned int) pid + seed++);

  dirlist_t *dlist = dir_list (dname, 0);
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
      if (cstring_eq (name, it->data->bytes)) goto again;
      it = it->next;
    }

    found = 1;
    break;
  }

  ifnot (found) goto theend;

  this = AllocType (tmpfname);
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
    file_tmpfname_free (this);
    this = NULL;
  }

theend:
  dlist->free (dlist);
  return this;
}

private file_T __init_file__ (void) {
  return ClassInit (file,
    .self = SelfInit (file,
      .exists = file_exists,
      .is_readable = file_is_readable,
      .is_writable = file_is_writable,
      .is_executable = file_is_executable,
      .is_rwx = file_is_rwx,
      .is_elf = file_is_elf,
      .is_reg = file_is_reg,
      .is_sock = file_is_sock,
      .readlines = file_readlines,
      .write = file_write,
      .append = file_append,
      .tmpfname = SubSelfInit (file, tmpfname,
        .new = file_tmpfname_new,
        .free = file_tmpfname_free
      )
    )
  );
}

/* i like the idea for an __is_directory (char *dname, struct stat st); */
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

private void dir_list_free (dirlist_t *dlist) {
  vstring_free (dlist->list);
  free (dlist);
}

/* this needs a simplification and enhancement */
private dirlist_t *dir_list (char *dir, int flags) {
  if (NULL is dir) return NULL;
  ifnot (flags & DIRLIST_DONOT_CHECK_DIRECTORY)
    ifnot (is_directory (dir)) return NULL;

  DIR *dh = NULL;
  if (NULL is (dh = opendir (dir))) return NULL;
  struct dirent *dp;

  size_t len;

  dirlist_t *dlist = AllocType (dirlist);
  dlist->free = dir_list_free;
  dlist->list = vstring_new ();
  cstring_cp (dlist->dir, PATH_MAX, dir, PATH_MAX - 1);

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

private void dir_walk_free (dirwalk_t **thisp) {
  if (NULL is thisp) return;
  dirwalk_t *this = *thisp;
  vstring_free (this->files);
  string_free (this->dir);
  free (this);
  *thisp = NULL;
}

private int dir_walk_process_dir_def (dirwalk_t *this, char *dir, struct stat *st) {
  (void) st;
  vstring_add_sort_and_uniq (this->files, dir);
  return 1;
}

private int dir_walk_process_file_def (dirwalk_t *this, char *file, struct stat *st) {
  (void) st;
  vstring_add_sort_and_uniq (this->files, file);
  return 1;
}

private dirwalk_t *dir_walk_new (DirProcessDir_cb process_dir, DirProcessFile_cb process_file) {
  dirwalk_t *this = AllocType (dirwalk);
  this->orig_depth = this->depth = 0;
  this->dir = string_new (PATH_MAX);
  this->files = NULL;
  this->process_dir = (NULL is process_dir ? dir_walk_process_dir_def : process_dir);
  this->process_file = (NULL is process_file ? dir_walk_process_file_def : process_file);
  this->stat_file = stat;
  this->object = NULL;

  return this;
}

private int __dir_walk_run__ (dirwalk_t *this, char *dir) {
  if (NOTOK is this->status) return this->status;

  int depth = 0;
  char *sp = dir;
  while (*sp) {
    if (*sp is DIR_SEP) depth++;
    sp++;
  }

  depth -= this->orig_depth;

  struct stat st;
  ifnot (OK is (this->status = this->stat_file (dir, &st)))
    return this->status;

  ifnot (S_ISDIR (st.st_mode)) {
    this->status = this->process_file (this, dir, &st);
    return NOTOK;
  }

  if (depth >= this->depth) {
    this->status = this->process_dir (this, dir, &st);
    return OK;
  }

  DIR *dh = NULL;
  if (NULL is (dh = opendir (dir))) return OK;
  struct dirent *dp;

  string_t *new = string_new (PATH_MAX);

  while (1) {
    errno = 0;
    if (NULL is (dp = readdir (dh)))
      break;

    size_t len = bytelen (dp->d_name);

    if (len < 3 and dp->d_name[0] is '.')
      if (len is 1 or dp->d_name[1] is '.')
        continue;

    string_replace_with_fmt (new, "%s/%s", dir, dp->d_name);

    switch (dp->d_type) {
      case DT_DIR:
      case DT_UNKNOWN:
        this->status = this->process_dir (this, new->bytes, &st);
        if (1 is this->status) {
          __dir_walk_run__ (this, new->bytes);
        } else if (NOTOK is this->status)
          goto theend;
        break;

      default:
        this->status = this->process_file (this, new->bytes, &st);
        if (NOTOK is this->status)
          goto theend;
    }
  }

theend:
  closedir (dh);
  string_free (new);
  return this->status;
}

private int dir_walk_run (dirwalk_t *this, char *dir) {
  if (NULL is this->files)
    this->files = vstring_new ();

  string_replace_with (this->dir, dir);
  char *sp = dir;
  size_t len = 0;
  while (*sp) {
    len++;
    if (*sp is DIR_SEP) this->orig_depth++;
    sp++;
  }

  if (dir[len-1] is DIR_SEP)
    dir[len-1] = '\0';
  else
    this->orig_depth++;

  __dir_walk_run__ (this, dir);
  return OK;
}

private dir_T __init_dir__ (void) {
  return ClassInit (dir,
    .self = SelfInit (dir,
      .list = dir_list,
      .current = dir_current,
      .is_directory = is_directory,
      .walk = SubSelfInit (dir, walk,
        .free = dir_walk_free,
        .new = dir_walk_new,
        .run = dir_walk_run
      )
    )
  );
}

private char *path_basename (char *name) {
  ifnot (name) return name;
  char *p = cstring_byte_null_in_str (name);
  if (p is NULL) p = name + bytelen (name) + 1;
  if (p - 1 is name and IS_DIR_SEP (*(p - 1)))
    return p - 1;

  while (p > name and IS_DIR_SEP (*(p - 1))) p--;
  while (p > name and IsNotDirSep (*(p - 1))) --p;
  if (p is name and IS_DIR_SEP (*p))
    return DIR_SEP_STR;
  return p;
}

/* ala SLang */
private char *path_basename_sans_extname (char *name) {
  char *bsnm = path_basename (name);
  ifnot (bsnm) return name;

  char *sp = bsnm;

  while (*(sp + 1) and *(sp + 1) isnot '.') sp++;

  size_t len = sp - bsnm + 1;
  char *buf = Alloc (len + 1);
  cstring_cp (buf, len + 1, bsnm, len);
  return buf;
}

private char *path_extname (char *name) {
  ifnot (name) return name;
  char *p = cstring_byte_null_in_str (name);
  if (p is NULL) p = name + bytelen (name) + 1;
  while (p > name and (*(p - 1) isnot '.')) --p;
  if (p is name) return "";
  p--;
  return p;
}

/* as a new c string (null terninated), note: that all new c strings here
 * should be null byte terminated */
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
  dname = Alloc (len + 1);
  cstring_cp (dname, len + 1, name, len);
  return dname;
}

private int path_is_absolute (char *path) {
  return IS_DIR_ABS (path);
}

/* Adapt realpath() from OpenBSD to this environment.
 *
 * While the algorithm is from the above implementation, we use our functions to do
 * the string processing work. */

/* $OpenBSD: realpath.c,v 1.13 2005/08/08 08:05:37 espie Exp $
 * Copyright (c) 2003 Constantin S. Svintsoff <kostik@iclub.nsu.ru>
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must rei_produce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 * i_products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

private char *path_real (const char *path, char resolved[PATH_MAX]) {
  struct stat sb;
  char *p, *q, *s;
  size_t left_len, resolved_len;
  unsigned symlinks;
  int serrno, slen;
  char left[PATH_MAX], next_token[PATH_MAX], symlink[PATH_MAX];

  serrno = errno;
  symlinks = 0;

  if (path[0] is '/') {
    resolved[0] = '/';
    resolved[1] = '\0';

    if (path[1] == '\0')
      return (resolved);

    resolved_len = 1;
    left_len = cstring_cp (left, PATH_MAX, path + 1, PATH_MAX - 1);
  } else {
    if (getcwd (resolved, PATH_MAX) is NULL) {
      cstring_cp (resolved, PATH_MAX,  ".", 1);
      return NULL;
    }

    resolved_len = bytelen (resolved);
    left_len = cstring_cp (left, PATH_MAX, path, PATH_MAX -1);
  }

  if (left_len >= PATH_MAX or resolved_len >= PATH_MAX) {
    errno = ENAMETOOLONG;
    return NULL;
  }

  /* Iterate over path components in `left' */
  while (left_len isnot 0) {
    /* Extract the next path component and adjust `left' and its length */
    p = cstring_byte_in_str (left, '/');
    s = p ? p : left + left_len;

    if (s - left >= PATH_MAX) {
      errno = ENAMETOOLONG;
      return NULL;
    }

    /* in the case of ../../tmp/../home/../usr/lib/../../home/../tmp/a  and in the
     * last iteration (s - left) gives 16 bytes to copy, when it is just one [a].
     * memcpy() does what it told to do, so copies more than one byte, but since the
     * second byte in "a" is the null byte, the function works, but the statement:
     * next_token[s - left] = '\0'; does it wrong.
     * So i had to introduce byte_cp_all, that behaves as memcpy behaves.
     * The next line is the original implementation
     * memcpy (next_token, left, s - left); 
     */
    byte_cp_all (next_token, left, s - left);
    next_token[s - left] = '\0';
    left_len -= s - left;

    if (p isnot NULL)
      cstring_byte_mv (left, PATH_MAX, 0, s + 1 - left, left_len + 1);

    if (resolved[resolved_len - 1] isnot '/') {
      if (resolved_len + 1 >= PATH_MAX)  {
        errno = ENAMETOOLONG;
        return NULL;
      }

      resolved[resolved_len++] = '/';
      resolved[resolved_len] = '\0';
    }

    if (next_token[0] is '\0')
      continue;
    else if (cstring_eq (next_token, "."))
      continue;
    else if (cstring_eq (next_token, "..")) {
      /* Strip the last path component except when we have single "/" */
      if (resolved_len > 1) {
        resolved[resolved_len - 1] = '\0';
        q = cstring_byte_in_str_r (resolved, '/') + 1;
        *q = '\0';
        resolved_len = q - resolved;
      }
      continue;
    }

    /* Append the next path component and lstat() it. If lstat() fails we still
     * can return successfully if there are no more path components left */
    resolved_len = cstring_cat (resolved, PATH_MAX, next_token);
    if (resolved_len >= PATH_MAX) {
      errno = ENAMETOOLONG;
      return NULL;
    }

    if (lstat(resolved, &sb) isnot 0) {
      if (errno is ENOENT and p is NULL) {
        errno = serrno;
        return resolved;
      }
      return NULL ;
    }

    if (S_ISLNK(sb.st_mode)) {
      if (symlinks++ > MAXSYMLINKS) {
        errno = ELOOP;
        return NULL;
      }

      slen = readlink (resolved, symlink, PATH_MAX - 1);
      if (slen < 0)
        return NULL;

      symlink[slen] = '\0';
      if (symlink[0] is '/') {
        resolved[1] = 0;
        resolved_len =  1;
      } else if (resolved_len > 1) {
        /* Strip the last path component. */
        resolved[resolved_len - 1] = '\0';
        q = cstring_byte_in_str_r (resolved, '/') + 1;
        *q = '\0';
        resolved_len = q - resolved;
      }

      /* If there are any path components left,then append them to symlink.
       * The result is placed in `left'. */
      if (p isnot NULL) {
        if (symlink[slen - 1] isnot '/') {
          if (slen + 1 >= PATH_MAX) {
            errno = ENAMETOOLONG;
            return NULL;
          }

          symlink[slen] = '/';
          symlink[slen + 1] = 0;
        }
        left_len = cstring_cat (symlink, PATH_MAX, left);
        if (left_len >= PATH_MAX) {
          errno = ENAMETOOLONG;
          return NULL;
        }
      }
     left_len = cstring_cp (left, PATH_MAX, symlink, PATH_MAX - 1);
    }
  }

  /* Remove trailing slash except when the resolved pathname is a single "/" */
  if (resolved_len > 1 and resolved[resolved_len - 1] is '/')
    resolved[resolved_len - 1] = '\0';

  return resolved;
}

private path_T __init_path__ (void) {
  return ClassInit (path,
    .self = SelfInit (path,
      .real = path_real,
      .basename = path_basename,
      .basename_sans_extname = path_basename_sans_extname,
      .extname = path_extname,
      .dirname = path_dirname,
      .is_absolute = path_is_absolute
    )
  );
}

#define CONTINUE_ON_EXPECTED_ERRNO(fd__)  \
  if (errno == EINTR) {                   \
    errno = 0;                            \
    continue;                             \
  }                                       \
  if (errno == EAGAIN) {                  \
    struct timeval tv;                    \
    fd_set read_fd;                       \
    FD_ZERO(&read_fd);                    \
    FD_SET(fd, &read_fd);                 \
    tv.tv_sec = 0;                        \
    tv.tv_usec = 100000;                  \
    select (fd__ + 1, &read_fd, NULL, NULL, &tv); \
    errno = 0;                            \
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

private int fd_write (int fd, char *buf, size_t len) {
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

private Class (fd) __init_fd__ () {
  return ClassInit (fd,
    .self = SelfInit (fd,
      .read = fd_read,
      .write = fd_write
    )
  );
}

private void term_screen_bell (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_BELL);
}

private void term_screen_set_color (term_t *this, int color) {
  fd_write ($my(out_fd), TERM_MAKE_COLOR(color), 5);
}

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

private void term_cursor_save (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_CURSOR_SAVE);
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

private void term_free (term_t **thisp) {
  if (NULL is *thisp) return;

  term_t *this = *thisp;

  ifnot (NULL is $myprop) {
    ifnot (NULL is $my(name)) {
      free ($my(name));
      $my(name) = NULL;
    }
    free ($myprop);
    $myprop = NULL;
  }
  free (this); *thisp = NULL;
}

private term_t *term_new (void) {
  term_t *this = AllocType (term);
  $myprop = AllocProp (term);
  $my(num_rows) = 24;
  $my(num_cols) = 78;
  $my(out_fd) = STDOUT_FILENO;
  $my(in_fd) = STDIN_FILENO;
  $my(mode) = 'o';
  $my(state) = 0;
  return this;
}

private void term_init_size (term_t *this, int *rows, int *cols) {
  struct winsize wsiz;

  do {
    if (OK is ioctl ($my(out_fd), TIOCGWINSZ, &wsiz)) {
      $my(num_rows) = (int) wsiz.ws_row;
      $my(num_cols) = (int) wsiz.ws_col;
      return;
    }
  } while (errno is EINTR);

  int orig_row, orig_col;
  term_cursor_get_ptr_pos (this, &orig_row, &orig_col);

  TERM_SEND_ESC_SEQ (TERM_LAST_RIGHT_CORNER);
  term_cursor_get_ptr_pos (this, rows, cols);
  term_cursor_set_ptr_pos (this, orig_row, orig_col);

  $my(num_rows) = *rows;
  $my(num_cols) = *cols;
}

/* three modes: 's' for sane, 'r' for raw and 'o' for original */
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
  mode.c_lflag &= ~(ECHONL|NOFLSH|TOSTOP|ECHOPRT);
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
  dim[0] = $my(num_rows);
  dim[1] = $my(num_cols);
  return dim;
}

private void term_set_state_bit (term_t *this, int bit) {
  $my(state) |= (bit);
}

private void term_unset_state_bit (term_t *this, int bit) {
  $my(state) &= ~(bit);
}

private int term_set_mode (term_t *this, char mode) {
  switch (mode) {
    case 'o': return term_orig_mode (this);
    case 's': return term_sane_mode (this);
    case 'r': return term_raw_mode (this);
  }
  return NOTOK;
}

/* for now */
private void term_set_name (term_t *this) {
  $my(name) = NULL;
}

private int term_set (term_t *this) {
  if (NOTOK is term_set_mode (this, 'r')) return NOTOK;
  term_cursor_get_ptr_pos (this, &$my(orig_curs_row_pos), &$my(orig_curs_col_pos));
  term_init_size (this, &$my(num_rows), &$my(num_cols));

  ifnot ($my(state) & TERM_DONOT_SAVE_SCREEN)
    term_screen_save (this);

  ifnot ($my(state) & TERM_DONOT_CLEAR_SCREEN)
    term_screen_clear (this);

  this->is_initialized = 1;
  return OK;
}

private int term_reset (term_t *this) {
  ifnot (this->is_initialized) return OK;
  term_set_mode (this, 's');
  term_cursor_set_ptr_pos (this, $my(orig_curs_row_pos), $my(orig_curs_col_pos));

  ifnot ($my(state) & TERM_DONOT_RESTORE_SCREEN)
    term_screen_restore (this);

  this->is_initialized = 0;
  return OK;
}

/* This is an extended version of the same function of the kilo editor at:
 * https://github.com/antirez/kilo.git
 *
 * It should work the same, under xterm, rxvt-unicode, st and linux terminals.
 * It also handles UTF8 byte sequences and it should return the integer represantation
 * of such sequence */
private utf8 term_input_get (term_t *this) {
  char c;
  int n;
  char buf[5];

  while (0 is (n = fd_read ($my(in_fd), buf, 1)));

  if (n is NOTOK) return NOTOK;

  c = buf[0];

  switch (c) {
    case ESCAPE_KEY:
      ifnot (fd_read ($my(in_fd), buf, 1))
        return ESCAPE_KEY;

      /* recent (revailed through CTRL-[other than CTRL sequence]) and unused */
      if ('z' >= buf[0] and buf[0] >= 'a')
        return 0;

      if (buf[0] is ESCAPE_KEY /* probably alt->arrow-key */)
        ifnot (fd_read ($my(in_fd), buf, 1))
          return 0;

      if (buf[0] isnot '[' and buf[0] isnot 'O')
        return 0;

      ifnot (fd_read ($my(in_fd), buf + 1, 1))
        return ESCAPE_KEY;

      if (buf[0] is '[') {
        if ('0' <= buf[1] and buf[1] <= '9') {
          ifnot (fd_read ($my(in_fd), buf + 2, 1))
            return ESCAPE_KEY;

          if (buf[2] is '~') {
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
          } else if (buf[1] is '1') {
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
          } else if (buf[1] is '2') {
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
            if (buf[2] is 'h')
              return INSERT_KEY; /* sample/test (logically return 0) */
            else
              return 0;
          }
        } else if (buf[1] is '[') {
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
      } else if (buf[0] is 'O') {
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

    if (127 is c) return BACKSPACE_KEY;

    return c;
  }

  return NOTOK;
}

private void video_alloc_list (video_t *this) {
  this->tmp_list = vstring_new ();
  for (int i = 0; i < this->num_rows; i++) {
    vstring_t *vstr = AllocType (vstring);
    vstr->data = string_new (this->num_cols);
    current_list_append (this->tmp_list, vstr);
  }
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
  video_alloc_list (this);
  return this;
}

private void video_free (video_t *this) {
  if (this is NULL) return;

  vstring_free (this->tmp_list);
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
  this = NULL;
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
        this->current->data->bytes, TERM_NEXT_BOL);
  }

  string_append (this->render, TERM_CURSOR_SHOW);
}

private void video_draw_row_at (video_t *this, int at) {
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
        row->data->bytes, TERM_NEXT_BOL);
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
  while (0 isnot (row = this->rows[i++])) video_draw_row_at (this, row);
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

  Vstring_t *Vstr = this->tmp_list;
  vstring_t *vstr = Vstr->head;

  int num_items = 0;
  while (l_p) {
    string_clear (vstr->data);

    l_p = cstring_byte_in_str (l_p, '\n');

    if (NULL is l_p) {
      string_append (vstr->data, f_p);
    } else {
      char line[l_p - f_p];
      int i = 0;
      while (f_p < l_p) line[i++] = *f_p++;
      f_p++; l_p++;
      line[i] = '\0';
      string_append (vstr->data, line);
    }

    if (++num_items is this->num_rows) break;
    vstr = vstr->next;
  }

  int num_rows = num_items;

  int i = 0; while (i < num_rows - 1 and first_row > 2) {i++; first_row--;};
  num_rows = i + 1;
  int num_chars = last_col - first_col + 1;

  string_clear (this->tmp_render);
  string_append_fmt (this->tmp_render, "%s" TERM_SET_COLOR_FMT, TERM_CURSOR_HIDE, COLOR_BOX);

  vstring_t *it = Vstr->head;
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
  return this;
}

private term_T __init_term__ (void) {
  return ClassInit (term,
    .self = SelfInit (term,
      .set = term_set,
      .new = term_new,
      .free  = term_free,
      .reset = term_reset,
      .set_mode = term_set_mode,
      .set_name = term_set_name,
      .init_size = term_init_size,
      .set_state_bit = term_set_state_bit,
      .unset_state_bit = term_unset_state_bit,
      .get = SubSelfInit (term, get,
        .dim = term_get_dim
      ),
      .__Cursor__ = SubSelfInit (term, cursor,
        .get_pos = term_cursor_get_ptr_pos,
        .set_pos = term_cursor_set_ptr_pos,
        .show = term_cursor_show,
        .hide = term_cursor_hide,
        .save = term_cursor_save,
        .restore = term_cursor_restore
      ),
      .__Screen__ = SubSelfInit (term, screen,
        .restore = term_screen_restore,
        .save = term_screen_save,
        .clear = term_screen_clear,
        .clear_eol = term_screen_clear_eol,
        .bell = term_screen_bell,
        .set_color = term_screen_set_color
      ),
      .__Input__ = SubSelfInit (term, input,
        .get = term_input_get
      )
    )
  );
}

private void __deinit_term__ (term_T *this) {
  (void) this;
}

private video_T __init_video__ (void) {
  return ClassInit (video,
    .self = SelfInit (video,
      .free = video_free,
      .flush = video_flush,
      .new =  video_new,
      .set = SubSelfInit (video, set,
        .row_with = video_set_row_with
      ),
      .draw = SubSelfInit (video, draw,
        .row_at = video_draw_row_at,
        .all = video_draw_all
      )
    )
  );
}

private void __deinit_video__ (video_T *this) {
  (void) this;
}

private void menu_free_list (menu_t *this) {
  if (this->state & MENU_LIST_IS_ALLOCATED) {
    vstring_free (this->list);
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
  menu->next_key = 0;
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
    menu->patlen = patlen;
    cstring_cp (menu->pat, MAXLEN_PAT, pat, patlen);
  }

  menu->retval = menu->process_list (menu);
  return menu;
}

private void menu_clear (menu_t *menu) {
  buf_t *this = menu->this;
  if (menu->header->num_bytes)
    Video.draw.row_at (menu->cur_video, menu->first_row - 1);

  for (int i = 0;i < menu->num_rows; i++)
    Video.draw.row_at (menu->cur_video, menu->first_row + i);
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
  rline_t *rl = rline_new (this, $my(term), Input.get, $my(prompt_row),
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

  while ((first_col + 1) and first_col + maxlen > menu->num_cols)
     first_col--;

  int avail_cols = menu->num_cols - first_col;

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

  char *fmt = STR_FMT ("\033[%%dm%%-%ds%%s", maxlen);

  vrow_pos = first_row;

  for (;;) {
    it = menu->list->head;
    for (int i = 0; i < frow_idx; i++)
      for (int j = 0; j < num; j++)
        it = it->next;

    string_t *render = String.new_with (TERM_CURSOR_HIDE);
    if (menu->header->num_bytes) {
      String.append_fmt (render, TERM_GOTO_PTR_POS_FMT TERM_SET_COLOR_FMT,
         first_row - 1, first_col, COLOR_MENU_HEADER);

      if ((int) menu->header->num_bytes > num_cols) {
        String.clear_at (menu->header, num_cols - 1);
      } else
      while ((int) menu->header->num_bytes < num_cols)
        String.append_byte (menu->header, ' ');

      String.append_fmt (render, "%s%s", menu->header->bytes, TERM_COLOR_RESET);
    }

    int ridx, iidx = 0; int start_row = 0;
    for (ridx = 0; ridx < rend_rows; ridx++) {
      start_row = first_row + ridx;
      String.append_fmt (render, TERM_GOTO_PTR_POS_FMT, start_row, first_col);

      for (iidx = 0; iidx < num and iidx + (ridx  * num) + (frow_idx * num) < menu->list->num_items; iidx++) {
        int len = ((int) it->data->num_bytes > maxlen ? maxlen : (int) it->data->num_bytes);
        char item[len + 1];
        Cstring.cp (item, len + 1, it->data->bytes, len);

        int color = (iidx + (ridx * num) + (frow_idx * num) is cur_idx)
           ? COLOR_MENU_SEL : COLOR_MENU_BG;
        String.append_fmt (render, fmt, color, item, TERM_COLOR_RESET);
        it = it->next;
      }

      if (mod)
        for (int i = mod + 1; i < num; i++)
          for (int j = 0; j < maxlen; j++)
            String.append_byte (render, ' ');
   }

//    string_append (render, TERM_CURSOR_SHOW);

    fd_write (menu->fd, render->bytes, render->num_bytes);

    String.free (render);

    menu->c = Rline.edit (rl)->c;
    if (menu->state & MENU_QUIT) goto theend;

handle_char:

    if (menu->c is menu->next_key)
      menu->c = ARROW_RIGHT_KEY;

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
          string_t *p = Vstring.join (rl->line, "");
          if (rl->line->tail->data->bytes[0] is ' ')
            String.clear_at (p, p->num_bytes - 1);

          ifnot (Cstring.eq (menu->pat, p->bytes)) {
            menu->patlen = p->num_bytes;
            Cstring.cp (menu->pat, MAXLEN_PAT, p->bytes, p->num_bytes);
          }

          String.free (p);
        }

        menu->process_list (menu);

        if (menu->state & MENU_QUIT) goto theend;

        cur_idx = 0;  // reset, as cur_idx can be out of bounds

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
  Rline.free (rl);
  return match;
}

private void ed_menu_free (ed_t *ed, menu_t *this) {
  (void) ed;
  menu_free (this);
}

private menu_t *ed_menu_new (ed_t *this, buf_t *buf, MenuProcessList_cb cb) {
  menu_t *menu = menu_new (this, $my(video)->row_pos, $my(prompt_row) - 2,
    $my(video)->col_pos, cb, NULL, 0);
  menu->this = buf;
  menu->return_if_one_item = 1;
  return menu;
}

private string_t *buf_input_box (buf_t *this, int row, int col,
                            int abort_on_escape, char *buf) {
  string_t *str = NULL;
  rline_t *rl = rline_new ($my(root), $my(term_ptr), Input.get, row,
      col, $my(dim)->num_cols - col + 1, $my(video));
  rl->opts &= ~RL_OPT_HAS_HISTORY_COMPLETION;
  rl->opts &= ~RL_OPT_HAS_TAB_COMPLETION;
  rl->prompt_char = 0;

  ifnot (NULL is buf)
    BYTES_TO_RLINE (rl, buf, (int) bytelen (buf));

  utf8 c;
  for (;;) {
     c = Rline.edit (rl)->c;
     switch (c) {
       case ESCAPE_KEY:
          if (abort_on_escape) {
            str = String.new (1);
            goto theend;
          }
       case '\r': str = Rline.get.line (rl); goto theend;
     }
  }

theend:
  String.clear_at (str, -1);
  Rline.free (rl);
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

  if (num_rows < rows) {
    Term.reset ($my(term));
    fprintf (stderr, "Available LINES are less than the required to be functional\n");
    exit (1);
  }

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

/* bad code, this is the case where the only thing you can do is to
 * pray to got the things right, very fragile code */
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

  self(current.set, mark->cur_idx);
  state_restore (mark);

  if ($mycur(first_col_idx) or $mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes) {
    $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;
    $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
  }

  return DONE;
}

private ustring_t *buf_get_line_nth (Ustring_t *line, int idx) {
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

private ustring_t *buf_get_line_idx (Ustring_t *line, int idx) {
  int cidx = current_list_set (line, idx);
  if (cidx is INDEX_ERROR) return NULL;
  return line->current;
}

private char *buf_get_line_data (buf_t *this, Ustring_t *line) {
  ifnot (line->num_items) return "";
  String.clear ($my(shared_str));
  ustring_t *it = line->head;
  while (it) {
    String.append ($my(shared_str), it->buf);
    it = it->next;
  }
  return $my(shared_str)->bytes;
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

private int buf_adjust_col (buf_t *this, int nth, int isatend) {
  if (this->current is NULL) return 1;

  Ustring.encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
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

  ustring_t *it;
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

#define undo_set(act, type__)                            \
  (act)->type = (type__);                                \
  state_set(act)

#define undo_restore(act)                                \
  state_restore(act)

private void buf_undo_init (buf_t *this) {
  if (NULL is $my(undo)) $my(undo) = AllocType (undo);
  if (NULL is $my(redo)) $my(redo) = AllocType (undo);
}

private Action_t *buf_undo_pop (buf_t *this) {
  return current_list_pop ($my(undo), Action_t);
}

private Action_t *buf_redo_pop (buf_t *this) {
  if ($my(redo)->head is NULL) return NULL;
  return current_list_pop ($my(redo), Action_t);
}

private void __buf_redo_clear__ (buf_t *this) {
  if ($my(redo)->head is NULL) return;

  Action_t *action = self(redo.pop);
  while (action) {
    self(Action.free, action);
    action = self(redo.pop);
  }

  $my(redo)->num_items = 0; $my(redo)->cur_idx = 0;
  $my(redo)->head = $my(redo)->tail = $my(redo)->current = NULL;
}

private void __buf_undo_clear__ (buf_t *this) {
  if ($my(undo)->head is NULL) return;

  Action_t *action = self(undo.pop);
  while (action isnot NULL) {
    self(Action.free, action);
    action = self(undo.pop);
  }
  $my(undo)->num_items = 0; $my(undo)->cur_idx = 0;
  $my(undo)->head = $my(undo)->tail = $my(undo)->current = NULL;
}

private void buf_undo_clear (buf_t *this) {
  __buf_undo_clear__ (this);
  __buf_redo_clear__ (this);
}

private void buf_undo_push (buf_t *this, Action_t *action) {
  if ($my(undo)->num_items > $myroots(max_num_undo_entries)) {
    Action_t *tmp = list_pop_tail ($my(undo), Action_t);
    self(Action.free, tmp);
  }

  ifnot ($my(undo)->state & VUNDO_RESET)
    __buf_redo_clear__ (this);
  else
    $my(undo)->state &= ~VUNDO_RESET;

  current_list_prepend ($my(undo), action);
}

private void buf_redo_push (buf_t *this, Action_t *action) {
  if ($my(redo)->num_items > $myroots(max_num_undo_entries)) {
    Action_t *tmp = list_pop_tail ($my(redo), Action_t);
    self(Action.free, tmp);
  }

  current_list_prepend ($my(redo), action);
}

private int buf_undo_insert (buf_t *this, Action_t *redoact, action_t *act) {
  ifnot (this->num_items) return DONE;

  action_t *ract = self(action.new);
  self(current.set, act->idx);
  self(adjust.view);
  ract->type = DELETE_LINE;
  undo_set (ract, DELETE_LINE);
  ract->idx = this->cur_idx;
  ract->bytes = Cstring.dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (redoact, ract);

  self(current.delete);

  if (this->num_items) {
    self(current.set, act->cur_idx);
    self(adjust.marks, DELETE_LINE, act->idx, act->idx);
    undo_restore (act);
  }

  return DONE;
}

private int buf_undo_delete_line (buf_t *this, Action_t *redoact, action_t *act) {
  action_t *ract = self(action.new);
  row_t *row = self(row.new_with, act->bytes);
  if (this->num_items) {
    if (act->idx >= this->num_items) {
      self(current.set, this->num_items - 1);
      self(adjust.view);
      undo_set (ract, INSERT_LINE);
      self(current.append, row);
      ract->idx = this->cur_idx;
    } else {
      self(current.set, act->idx);
      self(adjust.view);
      undo_set (ract, INSERT_LINE);
      self(current.prepend, row);
      ract->idx = this->cur_idx;
    }
    stack_push (redoact, ract);
    self(adjust.marks, INSERT_LINE, act->idx, act->idx + 1);
    undo_restore (act);
  } else {
    this->head = row;
    this->tail = row;
    this->cur_idx = 0;
    this->current = this->head;
    this->num_items = 1;
    self(adjust.view);
    undo_set (ract, INSERT_LINE);
    stack_push (redoact, ract);
    //  $my(video_first_row_idx) = this->cur_idx;
    //  self(current.append, row);
  }

  if ($my(video_first_row_idx) is this->cur_idx)
    $my(video_first_row) = this->current;

  return DONE;
}

private int buf_undo_replace_line (buf_t *this, Action_t *redoact, action_t *act) {
  self(current.set, act->idx);

  action_t *ract = self(action.new);
  self(set.row.idx, act->idx, act->row_pos - $my(dim)->first_row, act->col_pos);
  undo_set (ract, REPLACE_LINE);
  ract->idx = this->cur_idx;
  ract->bytes = Cstring.dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (redoact, ract);

  String.replace_with ($mycur(data), act->bytes);

  undo_restore (act);
  return DONE;
}

/* ATTENTION */
/* generally speaking the undo/redo basic functionality seems to be
 * working. what is not working always perfect, is the state of the
 * screen, i thing on redoing'it, so this has a very serious bug */
private int buf_undo_exec (buf_t *this, utf8 com) {
  Action_t *Action = NULL;
  if (com is 'u')
    Action = self(undo.pop);
  else
    Action = self(redo.pop);

  if (NULL is Action) return NOTHING_TODO;

  action_t *action = stack_pop (Action, action_t);

  Action_t *Redoact = self(Action.new);

  while (action) {
    if (action->type is DELETE_LINE)
      self(undo.delete_line, Redoact, action);
    else
      if (action->type is REPLACE_LINE)
        self(undo.replace_line, Redoact, action);
      else
        self(undo.insert, Redoact, action);

    self(action.free, action);
    action = stack_pop (Action, action_t);
  }

  if (com is 'u')
    self(redo.push, Redoact);
  else {
    $my(undo)->state |= VUNDO_RESET;
    self(undo.push, Redoact);
  }

  free (Action);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw);
  return DONE;
}

private Action_t *buf_Action_new (buf_t *this) {
  (void) this;
  return AllocType (Action);
}

private void buf_Action_free (buf_t *this, Action_t *Action) {
  (void) this;
  action_t *action = stack_pop (Action, action_t);
  while (action) {
    self(action.free, action);
    action = stack_pop (Action, action_t);
  }

  free (Action);
}

private void buf_Action_set_with_current (buf_t *this, Action_t *Action, int type) {
  action_t *action = self(action.new_with, type, this->cur_idx,
      $mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (Action, action);
}

private void buf_Action_set_with (buf_t *this, Action_t *Action,
                     int type, int idx, char *bytes, size_t len) {
  action_t *action = self(action.new);
  undo_set (action, type);
  action->idx = ((idx < 0 or idx >= this->num_items) ? this->cur_idx : idx);
  if (NULL is bytes)
    action->bytes = Cstring.dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  else
    action->bytes = Cstring.dup (bytes, len);

  stack_push (Action, action);
}

action_t *buf_action_new (buf_t *this) {
  (void) this;
  return AllocType(action);
}

action_t *buf_action_new_with (buf_t *this, int type, int idx, char *bytes, size_t len) {
  action_t *action = self(action.new);
  undo_set (action, type);
  action->idx = idx;
  action->num_bytes = len;
  action->bytes = Cstring.dup (bytes, len);
  return action;
}

void buf_action_free (buf_t *this, action_t *action) {
  (void) this;
  if (NULL is action) return;
  ifnot (NULL is action->bytes)
    free (action->bytes);
  free (action);
}

/* a highlight theme derived from tte editor, fork of kilo editor,
 * adjusted and enhanced for the environment
 * written by itself (the very first lines) (iirc somehow after the middle days
 * of February of 2019) 
 */

private char *buf_syn_parser (buf_t *, char *, int, int, row_t *);
private ftype_t *buf_syn_init (buf_t *);
private ftype_t *buf_syn_init_c (buf_t *);
private ftype_t *buf_syn_init_i (buf_t *);

char *default_extensions[] = {".txt", NULL};

char *c_extensions[] = {".c", ".h", ".cpp", ".hpp", ".cc", NULL};
char *c_keywords[] = {
    "is I", "isnot I", "or I", "and I", "if I", "for I", "return I", "else I",
    "ifnot I", "self I", "this V", "NULL K",
    "OK K", "NOTOK K", "static I", "char T",
    "#include M", "struct T", "typedef I",
    "switch I", "while I", "break I", "continue I", "do I", "default I", "goto I",
    "case I", "free F", "$my V",  "uint T", "int T", "size_t T",
    "idx_t T",
    "utf8 T", "uchar T", "sizeof T", "void T", "thisp V",
    "union T", "const T",
    "$myprop V", "theend I", "theerror E", "UNSET N", "#define M",
    "#endif M", "#error M", "#ifdef M", "#ifndef M", "#undef M", "#if M", "#else I", "#elif I",
    "STDIN_FILENO K", "STDOUT_FILENO K", "STDERR_FILENO K",
    "inline I",
    "Alloc T", "Realloc T", "AllocType T", "$from V", "forever I",
    "AllocClass T", "AllocProp T", "AllocSelf T", "$myroots V", "$myparents V",
    "$OurRoot V", "$OurRoots V", "public I", "mutable I", "loop I",
     "enum T", "bool T", "long T", "ulong T", "double T", "float T", "unsigned T",
    "ifnull I", "private I", "selfp I","My V", "$mycur V",
    "extern I", "signed T", "volatile T", "register T",  "auto T",
    NULL
};

char c_singleline_comment[] = "//";
char c_multiline_comment_start[] = "/*";
char c_multiline_comment_end[] = "*/";
char c_multiline_comment_continuation[] = " * ";
char c_operators[] = "+:?-%*^><=|&~.()[]{}!";
char c_balanced_pairs[] = "[](){}";

char *i_extensions[] = {".i", NULL};
char *i_shebangs[] = {"#!/bin/env i", NULL};
char i_operators[] = "+:-*^><=|&~.()[]{}/";
char *i_keywords[] = {
  "while I", "if I", "var V", "ifnot I", "else I", "return I", "func I", "is I",
  "isnot I", "print F", "println F", "true V", "false V", "OK V", "NOTOK V", NULL
};

char i_singleline_comment[] = "#";

char *NULL_ARRAY[] = {NULL};

syn_t HL_DB[] = {
   {
     "txt", NULL_ARRAY, default_extensions, NULL_ARRAY,
     NULL, NULL,
     NULL, NULL, NULL, NULL,
     HL_STRINGS_NO, HL_NUMBERS_NO, buf_syn_parser, buf_syn_init,
     0, 0, NULL, NULL, c_balanced_pairs,
  },
  {
    "c", NULL_ARRAY, c_extensions, NULL_ARRAY,
    c_keywords, c_operators,
    c_singleline_comment, c_multiline_comment_start, c_multiline_comment_end,
    c_multiline_comment_continuation,
    HL_STRINGS, HL_NUMBERS,
    buf_syn_parser, buf_syn_init_c, 0, 0, NULL, NULL, c_balanced_pairs,
  },
  {
    "i", NULL_ARRAY, i_extensions, i_shebangs, i_keywords, i_operators,
    i_singleline_comment, NULL, NULL, NULL, HL_STRINGS, HL_NUMBERS,
    buf_syn_parser, buf_syn_init_i, 0, 0, NULL, NULL, c_balanced_pairs
  }
};

#define IsSeparator(c)                          \
  ((c) is ' ' or (c) is '\t' or (c) is '\0' or  \
   cstring_byte_in_str (",.()+-/=*~%<>[]:;}@", (c)) isnot NULL)

#define IGNORE(c) ((c) > '~' || (c) <= ' ')

#define ADD_COLORED_CHAR(_c, _clr) String.append_fmt ($my(shared_str), \
  TERM_SET_COLOR_FMT "%c" TERM_COLOR_RESET,(_clr), (_c))

#define ADD_INVERTED_CHAR(_c, _clr) String.append_fmt ($my(shared_str), \
  TERM_INVERTED "%s%c" TERM_COLOR_RESET, TERM_MAKE_COLOR ((_clr)), (_c))

#define SYN_HAS_OPEN_COMMENT       (1 << 0)
#define SYN_HAS_SINGLELINE_COMMENT (1 << 1)
#define SYN_HAS_MULTILINE_COMMENT  (1 << 2)

private int buf_syn_has_mlcmnt (buf_t *this, row_t *row) {
  int found = 0;
  row_t *it = row->prev;

  for (int i = 0; i < MAX_BACKTRACK_LINES_FOR_ML_COMMENTS and it; i++) {
    ifnot (NULL is strstr (it->data->bytes, $my(syn)->multiline_comment_end))
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
      if (cstring_eq_n (it->data->bytes, $my(syn)->multiline_comment_continuation,
      	$my(syn)->multiline_comment_continuation_len)) {
        found = 1;
        break;
      }

    it = it->prev;
  }

  return found;
}

/* Sorry but the highlight system is ridiculously simple (word by word), but is fast and works for me in C */
private char *buf_syn_parser (buf_t *this, char *line, int len, int index, row_t *row) {
  (void) index;

  ifnot (len) return line;

  /* make sure to reset, in the case the last character on previous line
   * is in e.g., a string. Do it here instead with an "if" later on */
  String.replace_with_len ($my(shared_str), TERM_COLOR_RESET, TERM_COLOR_RESET_LEN);

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
      String.append_fmt ($my(shared_str), "%s%s", TERM_MAKE_COLOR (HL_COMMENT), TERM_ITALIC);

      if ($my(syn)->state & SYN_HAS_MULTILINE_COMMENT) {
        $my(syn)->state &= ~SYN_HAS_MULTILINE_COMMENT;
        char *sp = strstr (line + idx, $my(syn)->multiline_comment_end);
        if (sp is NULL) {
          while (idx < len)
            String.append_byte ($my(shared_str), line[idx++]);

          String.append ($my(shared_str), TERM_COLOR_RESET);
          goto theend;
        }

        diff = idx + (sp - (line + idx)) + (int) bytelen ($my(syn)->multiline_comment_end);

      } else
        $my(syn)->state &= ~SYN_HAS_SINGLELINE_COMMENT;

      while (idx < diff) String.append_byte ($my(shared_str), line[idx++]);
      String.append ($my(shared_str), TERM_COLOR_RESET);
      if (idx is len) goto theend;
      c = line[idx];
    }

parse_char:
    while (IGNORE (c)) {
      if (c is ' ') {
        if (idx + 1 is (int) row->data->num_bytes) {
          ADD_INVERTED_CHAR (' ', HL_TRAILING_WS);
        } else
          String.append_byte ($my(shared_str), c);
      } else
        if (c is '\t') {
          for (int i = 0; i < $my(ftype)->tabwidth; i++)
            String.append_byte ($my(shared_str), ' ');
        } else
          if ((c < ' ' and (c isnot 0 and c isnot 0x0a)) or c is 0x7f) {
            ADD_INVERTED_CHAR ('?', HL_ERROR);
          }
          else {
            String.append_byte ($my(shared_str),  c);
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
      while (NULL isnot Cstring.byte.in_str ($my(syn)->operators, c)) {
        ADD_COLORED_CHAR (c, HL_OPERATOR);
        if (++idx is len) goto theend;
        c = line[idx];
      }

      if (idx isnot lidx) goto parse_char;
    }

    if ($my(syn)->hl_strings)
      if (c is '"' or c is '\'') {
        String.append_fmt ($my(shared_str),
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

          String.append_byte ($my(shared_str),  c);
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

        if (cstring_eq_n (&line[idx], $my(syn)->keywords[j], kw_len) and
           IsSeparator (line[idx + kw_len])) {
         // (0 is is_glob ? IsSeparator (line[idx + kw_len]) : 1)) {
          int color = $my(syn)->keywords_colors[j];
          String.append ($my(shared_str), TERM_MAKE_COLOR (color));

          for (int i = 0; i < kw_len; i++)
            String.append_byte ($my(shared_str), line[idx+i]);

          idx += (kw_len - 1);

          // ifnot (is_glob) {
          String.append ($my(shared_str), TERM_COLOR_RESET);
          goto next_char;
          // }
          /*
          idx++;
          while (idx < len) String.append_byte ($my(shared_str), line[idx++]);
          String.append ($my(shared_str), TERM_COLOR_RESET);
          goto theend;
          */
        }
      }

    String.append_byte ($my(shared_str),  c);

next_char:;
  }

theend:
  Cstring.cp (line, MAXLEN_LINE, $my(shared_str)->bytes, $my(shared_str)->num_bytes);
  return line;
}

private void balanced_push (balanced_t *this, char obj, int idx) {
  this->bytes[++this->last_idx] = obj;
  this->linenr[this->last_idx] = idx;
}

private char balanced_pop (balanced_t *this) {
  char c = this->bytes[this->last_idx];
  this->bytes[this->last_idx--] = '\0';
  return c;
}

private int balanced_check_obj (char *pair, char ca, char cb) {
  if (ca is *pair) return (cb is *(pair + 1) ? OK : NOTOK);
  return NOTOK;
}

private int buf_balanced_obj (buf_t **thisp, int first_idx, int last_idx) {
  int retval = NOTOK;
  buf_t *this = *thisp;

  if ($my(syn)->balanced_pairs is NULL) return NOTOK;

  balanced_t balanced = (balanced_t) {
    .last_idx = -1,
    .has_opening_string = 0
  };

  int idx = first_idx;

theloop:
  while (idx <= last_idx) {
    string_t *data = self(get.row.bytes_at, idx++);
    if (data is NULL) break;

    for (size_t i = 0; i < data->num_bytes; i++) {
      char c = data->bytes[i];

      ifnot (balanced.has_opening_string) {
        ifnot (NULL is $my(syn)->singleline_comment) {
          if (c is $my(syn)->singleline_comment[0]) {
            if ((bytelen ($my(syn)->singleline_comment) is 1) or
                ((data->num_bytes - 1 > i and data->bytes[i+1] is
                 $my(syn)->singleline_comment[1]))) {
              goto theloop;
            }
          }
        }
      }

      if (c is '"') {
        if ((i isnot 0 and data->bytes[i-1] is '\\' and balanced.has_opening_string)
          or ((i isnot 0 and data->bytes[i-1] is '\'') and
           ((data->num_bytes - 1 > i and data->bytes[i+1] is '\''))))
         continue;

          if (balanced.has_opening_string)
            balanced.has_opening_string = 0;
          else
            balanced.has_opening_string = 1;

        continue;
      }

      if (balanced.has_opening_string) continue;

      char *sp = Cstring.byte.in_str ($my(syn)->balanced_pairs, c);
      ifnot (NULL is sp) {
        if (i isnot 0 and data->bytes[i-1] is '\'' and
          (data->num_bytes - 1 > i and data->bytes[i+1] is '\'')) continue;

        ifnot ((sp - $my(syn)->balanced_pairs) % 2) {
          balanced_push (&balanced, c, idx);
          continue;
        }

        if (balanced.last_idx is -1) {
          Msg.write_fmt ($my(root), "no opening objects to match close object |%c| at: "
              "%d line number", c, idx);
          retval = NOTOK;
          goto theend;
        }

        char last_p = balanced_pop (&balanced);

        if (NOTOK is balanced_check_obj (sp - 1, last_p, c)) {
          Msg.write_fmt ($my(root), "%c opened at %d but closed object is %c at %d",
             last_p, balanced.linenr[balanced.last_idx + 1], c, idx + 1);
          goto theend;
        }
      }

      continue;
    }
  }

  if (balanced.last_idx isnot -1) {
     char last_p = balanced_pop (&balanced);

     Msg.write_fmt ($my(root), "%c opened at %d and didn't found it's closed pair", last_p,
         balanced.linenr[balanced.last_idx + 1]);
     retval = NOTOK;
  } else
    retval = OK;

theend:
  if (NOTOK is retval)
    Ed.messages ($my(root), thisp, NOT_AT_EOF);
  else
    Msg.line ($my(root), COLOR_NORMAL, "pair objects looks symmetrical");

  return retval;
}

private int buf_balanced_lw_mode_cb (buf_t **thisp, int fidx, int lidx, Vstring_t *vstr, utf8 c, char *action) {
  (void) vstr; (void) c; (void) action;
  if (c is 'b')
    return buf_balanced_obj (thisp, fidx, lidx);
  return NO_CALLBACK_FUNCTION;
}

private int buf_interpret (buf_t **thisp, char *malloced) {
  buf_t *this = *thisp;
  char *str = malloced;

  i_t *in = I.get.current ($my(__I__));
  ifnot (in)
    in = I.init_instance ($my(__I__), IOpts());

  Term.reset ($my(term_ptr));

  int retval = I.eval_string (in, str, 1, 1);

  free (malloced);

  Term.set_mode ($my(term_ptr), 'r');
  Input.get ($my(term_ptr));
  Term.set ($my(term_ptr));

  if (retval is I_ERR_SYNTAX)
    Ed.messages ($my(root), thisp, AT_EOF);

  this = *thisp;

  Win.draw ($my(parent));

  if (retval isnot OK or retval isnot NOTOK)
    retval = NOTOK;
  return retval;
}

private int buf_evaluate_lw_mode_cb (buf_t **thisp, int fidx, int lidx, Vstring_t *vstr, utf8 c, char *action) {
  (void) fidx; (void) lidx; (void) action;

  if (c isnot '@') return NO_CALLBACK_FUNCTION;

  buf_t *this = *thisp;
  ifnot ($from($my(root), env)->uid)
    return NO_CALLBACK_FUNCTION;

  char *str = Vstring.to.cstring (vstr, ADD_NL);
  return buf_interpret (thisp, str);
}

private string_t *buf_ftype_autoindent (buf_t *this, row_t *row) {
  (void) row;
  $my(shared_int) = 0; // needed by the caller
  char s[$my(shared_int) + 1];
  for (int i = 0; i < $my(shared_int); i++) s[i] = ' ';
  s[$my(shared_int)] = '\0';
  String.replace_with ($my(shared_str), s);
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
  String.replace_with ($my(shared_str), s);
  return $my(shared_str);
}

private ftype_t *__ftype_new__ (syn_t *syn) {
  ftype_t *this = AllocType (ftype);
  cstring_cp (this->name, MAXLEN_FTYPE_NAME, syn->filetype, MAXLEN_FTYPE_NAME - 1);
  return this;
}

private void buf_ftype_free (buf_t *this) {
  if (this is NULL or $myprop is NULL or $my(ftype) is NULL) return;
  String.free ($my(ftype)->on_emptyline);
  free ($my(ftype));
  $my(ftype) = NULL;
}

private int ed_syn_get_ftype_idx (ed_t *this, char *name) {
  if (NULL is name) return FTYPE_DEFAULT;

  for (int i = 0; i < $my(num_syntaxes); i++) {
    if (Cstring.eq ($my(syntaxes)[i].filetype, name))
      return i;
  }

  return FTYPE_DEFAULT;
}

private ftype_t *__ftype_set__ (ftype_t *this, ftype_t q) {
  this->autochdir = q.autochdir;
  this->shiftwidth = q.shiftwidth;
  this->tabwidth = q.tabwidth;
  this->clear_blanklines = q.clear_blanklines;
  this->tab_indents = q.tab_indents;
  this->cr_on_normal_is_like_insert_mode = q.cr_on_normal_is_like_insert_mode;
  this->backspace_on_normal_is_like_insert_mode = q.backspace_on_normal_is_like_insert_mode;
  this->backspace_on_normal_goes_up = q.backspace_on_normal_goes_up;
  this->backspace_on_insert_goes_up_and_join = q.backspace_on_insert_goes_up_and_join;
  this->backspace_on_first_idx_remove_trailing_spaces = q.backspace_on_first_idx_remove_trailing_spaces;
  this->small_e_on_normal_goes_insert_mode = q.small_e_on_normal_goes_insert_mode;
  this->space_on_normal_is_like_insert_mode = q.space_on_normal_is_like_insert_mode;
  this->read_from_shell = q.read_from_shell;

  this->autoindent = (NULL is q.autoindent ? buf_ftype_autoindent : q.autoindent);
  this->on_open_fname_under_cursor = q.on_open_fname_under_cursor;
  this->balanced = q.balanced;

  if (NULL is q.on_emptyline) {
    if (NULL is this->on_emptyline)
      this->on_emptyline = string_new_with (DEFAULT_ON_EMPTY_LINE_STRING);
  } else
    this->on_emptyline = q.on_emptyline;

  return this;
}

private ftype_t *buf_ftype_init (buf_t *this, int ftype, FtypeAutoIndent_cb indent_cb) {
  if (ftype >= $myroots(num_syntaxes) or ftype < 0) ftype = 0;
  $my(syn) = &$myroots(syntaxes)[ftype];
  return __ftype_set__ (__ftype_new__ ($my(syn)),
    FtypeOpts (.autoindent = indent_cb));
}

private ftype_t *buf_ftype_set (buf_t *this, int ftype, ftype_t q) {
  if (ftype >= $myroots(num_syntaxes) or ftype < 0) ftype = 0;
  $my(syn) = &$myroots(syntaxes)[ftype];
  if (NULL is $my(ftype))
    $my(ftype) = __ftype_new__ ($my(syn));

  return __ftype_set__ ($my(ftype), q);
}

/* this retval pointer provides information, since the callee resets retval at the
 * beginning of its function. The early return here is actually a goto theend there */
private int buf_com_substitute (buf_t *this, rline_t *rl, int *retval) {
  arg_t *pat = rline_get_arg (rl, RL_ARG_PATTERN);
  arg_t *sub = rline_get_arg (rl, RL_ARG_SUB);
  arg_t *global = rline_get_arg (rl, RL_ARG_GLOBAL);
  arg_t *interactive = rline_get_arg (rl, RL_ARG_INTERACTIVE);
  arg_t *range = rline_get_arg (rl, RL_ARG_RANGE);

  if (NULL is range and rl->com is VED_COM_SUBSTITUTE_WHOLE_FILE_AS_RANGE) {
    rl->range[0] = 0; rl->range[1] = this->num_items - 1;
  } else
    if (NOTOK is rline_parse_arg_buf_range (rl, range, this))
      return *retval;

  if (rline_arg_exists (rl, "remove-tabs")) {
    int shiftwidth = $my(ftype)->shiftwidth;
    string_t *sw = rline_get_anytype_arg (rl, "shiftwidth");
    ifnot (NULL is sw) shiftwidth = atoi (sw->bytes);
    char subst[shiftwidth];
    for (int i = 0; i < shiftwidth; i++) subst[i] = ' ';
      subst[shiftwidth] = '\0';
    return buf_substitute (this, "\t", subst, GLOBAL, interactive isnot NULL, rl->range[0], rl->range[1]);
  }

  if (rline_arg_exists (rl, "remove-doseol")) {
    return buf_substitute (this, "\x0d", "", GLOBAL, interactive isnot NULL, rl->range[0], rl->range[1]);
  }

  if (pat is NULL or sub is NULL) return *retval;

  *retval = buf_substitute (this, pat->argval->bytes, sub->argval->bytes,
     global isnot NULL, interactive isnot NULL, rl->range[0], rl->range[1]);

  return *retval;
}

private int buf_com_set (buf_t *this, rline_t *rl) {
  int draw = 0;

  string_t *arg = rline_get_anytype_arg (rl, "ftype");
  ifnot (NULL is arg) {
    int idx = Ed.syn.get_ftype_idx ($my(root), arg->bytes);
    syn_t syn = $myroots(syntaxes)[idx];
    ifnot (Cstring.eq (syn.filetype, $my(ftype)->name)) {
      self(ftype.free);
      $my(ftype) = syn.init (this);
      Cstring.cp ($my(ftype)->name, MAXLEN_FTYPE_NAME, $my(syn)->filetype, MAXLEN_FTYPE_NAME - 1);
      draw = 1;
    }
  }

  arg = rline_get_anytype_arg (rl, "tabwidth");
  ifnot (NULL is arg) {
    $my(ftype)->tabwidth = atoi (arg->bytes);
    self(normal.bol, DONOT_DRAW);
    draw = 1;
  }

  arg = rline_get_anytype_arg (rl, "shiftwidth");
  ifnot (NULL is arg) {
    $my(ftype)->shiftwidth = atoi (arg->bytes);
    self(normal.bol, DONOT_DRAW);
    draw = 1;
  }

  arg = rline_get_anytype_arg (rl, "autosave");
  ifnot (NULL is arg) {
    long minutes = atol (arg->bytes);
    if (minutes)
      self(set.autosave, minutes);
  }

  arg = rline_get_anytype_arg (rl, "save-image");
  ifnot (NULL is arg)
    Root.set.save_image ($OurRoot, atoi (arg->bytes));

  arg = rline_get_anytype_arg (rl, "image-file");
  ifnot (NULL is arg)
    Root.set.image_file ($OurRoot, arg->bytes);

  arg = rline_get_anytype_arg (rl, "image-name");
  ifnot (NULL is arg)
    Root.set.image_name ($OurRoot, arg->bytes);

  arg = rline_get_anytype_arg (rl, "persistent-layout");
  ifnot (NULL is arg)
    Root.set.persistent_layout ($OurRoot, atoi (arg->bytes));

  if (rline_arg_exists (rl, "backupfile")) {
    arg = rline_get_anytype_arg (rl, "backup-suffix");
    self(set.backup, 1, (NULL is arg ? BACKUP_SUFFIX : arg->bytes));
  }

  if (rline_arg_exists (rl, "no-backupfile"))
    ifnot (NULL is $my(backupfile)) {
      free ($my(backupfile));
      $my(backupfile) = NULL;
    }

  if (rline_arg_exists (rl, "enable-writing"))
    $my(enable_writing) = 1;

  if (draw) self(draw);

  return OK;
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

private ftype_t *buf_syn_init_c (buf_t *this) {
  int idx = Ed.syn.get_ftype_idx ($my(root), "c");
  return self(ftype.set, idx, FtypeOpts (
    .autoindent = buf_autoindent_c,
    .shiftwidth = C_DEFAULT_SHIFTWIDTH,
    .tab_indents = C_TAB_ON_INSERT_MODE_INDENTS,
    .on_open_fname_under_cursor = ftype_on_open_fname_under_cursor_c,
    .balanced = buf_balanced_obj
    ));
}

private ftype_t *buf_syn_init_i (buf_t *this) {
  int idx = Ed.syn.get_ftype_idx ($my(root), "i");
  return self(ftype.set, idx, FtypeOpts (
      .autoindent = buf_autoindent_c,
      .tabwidth = 2,
      .tab_indents = 1,
      .balanced = buf_balanced_obj
  ));
}

private ftype_t *buf_syn_init (buf_t *this) {
  return self(ftype.init, FTYPE_DEFAULT, buf_ftype_autoindent);
}

private void buf_set_ftype (buf_t *this, int ftype) {
  if (FTYPE_DEFAULT < ftype and ftype < $myroots(num_syntaxes)) {
    $my(ftype) = $myroots(syntaxes)[ftype].init (this);
    return;
  }

  for (int i = 0; i < $myroots(num_syntaxes); i++) {
    int j = 0;
    while ($myroots(syntaxes)[i].filenames[j])
      if (Cstring.eq ($myroots(syntaxes)[i].filenames[j++], $my(basename))) {
        $my(ftype) = $myroots(syntaxes)[i].init (this);
        return;
      }

    if (NULL is $my(extname)) continue;

    j = 0;
    while ($myroots(syntaxes)[i].extensions[j])
      if (Cstring.eq ($myroots(syntaxes)[i].extensions[j++], $my(extname))) {
        $my(ftype) = $myroots(syntaxes)[i].init (this);
        return;
      }

    if (NULL is this->head or this->head->data->num_bytes < 2) continue;

    j = 0;
    while ($myroots(syntaxes)[i].shebangs[j]) {
      if (Cstring.eq_n ($myroots(syntaxes)[i].shebangs[j], this->head->data->bytes,
          bytelen ($myroots(syntaxes)[i].shebangs[j]))) {
        $my(ftype) = $myroots(syntaxes)[i].init (this);
        return;
      }

      j++; /* gcc complains (and probably for a right) if j++ at the end of the
            * conditional expression (even if it is right) */
     }
  }

  $my(ftype) = self(syn.init);
}

private void buf_set_modified (buf_t *this) {
  $my(flags) |= BUF_IS_MODIFIED;
}

private row_t *buf_row_new_with (buf_t *this, const char *bytes) {
  row_t *row = AllocType (row);
  string_t *data = String.new_with (bytes);
  row->data = data;
  return row;
}

private row_t *buf_row_new_with_len (buf_t *this, const char *bytes, size_t len) {
  row_t *row = AllocType (row);
  string_t *data = String.new_with_len (bytes, len);
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

private int buf_get_current_row_idx (buf_t *this) {
  return this->cur_idx;
}

private int buf_get_current_col_idx (buf_t *this) {
  return $mycur(cur_col_idx);
}

private string_t *buf_get_row_current_bytes (buf_t *this) {
  return $mycur(data);
}

private string_t *buf_get_row_bytes_at (buf_t *this, int idx) {
  row_t *row = self(get.row.at, idx);
  if (NULL is row) return NULL;
  return row->data;
}

private void buf_free_row (buf_t *this, row_t *row) {
  if (row is NULL) return;
  String.free (row->data);
  free (row);
}

private void buf_free_line (buf_t *this) {
  if (this is NULL or $myprop is NULL or $my(line) is NULL) return;
  ustring_free_members ($my(line));
  free ($my(line));
}

private void buf_jumps_free (buf_t *this) {
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
    $my(jumps) = AllocType (Jump);
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
  char *m = cstring_byte_in_str (marks, c);
  if (NULL is m) return -1;
  return m - marks;
}

private int buf_mark_set (buf_t *this, int mark) {
  if (mark < 0) {
    mark = mark_get_idx (Input.get ($my(term_ptr)));
    if (-1 is mark) return NOTHING_TODO;
  }

  state_set (&$my(marks)[mark]);
  $my(marks)[mark].cur_idx = this->cur_idx;

  if (mark isnot MARK_UNNAMED)
    MSG("set [%c] mark", MARKS[mark]);

  self(jump.push, &$my(marks)[mark]);
  return DONE;
}

private int buf_mark_jump (buf_t *this) {
  int c = mark_get_idx (Input.get ($my(term_ptr)));
  if (-1 is c) return NOTHING_TODO;

  mark_t *mark = &$my(marks)[c];
  mark_t t;  state_set (&t);  t.cur_idx = this->cur_idx;

  if (NOTHING_TODO is self(mark.restore, mark))
    return NOTHING_TODO;

  $my(marks)[MARK_UNNAMED] = t;

  self(draw);
  return DONE;
}

private int buf_jump_to (buf_t *this, int dir) {
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
    self(jump.push, &m);
  }

  $my(jumps)->old_idx = $my(jumps)->cur_idx;
  if ($my(jumps)->cur_idx + 1 isnot $my(jumps)->num_items)
    $my(jumps)->cur_idx++;

theend:
  state_set (&$my(marks)[MARK_UNNAMED]);
  $my(marks)[MARK_UNNAMED].cur_idx = this->cur_idx;

  self(mark.restore, jump->mark);
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

private void buf_undo_free (buf_t *this) {
  self(undo.clear);
  free ($my(undo));
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

private long vsys_get_clock_sec (clockid_t clock_id) {
  struct timespec cspec;
  clock_gettime (clock_id, &cspec);
  return cspec.tv_sec;
}

private vsys_T __init_vsys__ (void) {
  return ClassInit (vsys,
    .self = SelfInit (vsys,
      .which = vsys_which,
      .get = SubSelfInit (vsys, get,
        .clock_sec = vsys_get_clock_sec
      ),
      .stat = SubSelfInit (vsys, stat,
        .mode_to_string = vsys_stat_mode_to_string
      )
    )
  );
}

private void __deinit_vsys__ (vsys_T *this) {
  (void) this;
}

private int __env_check_directory__ (char *dir, char *dir_descr,
               int exit_on_error, int exit_on_warning, int warn) {
  int retval = OK;

  if (NULL is dir) {
    fprintf (stderr, "Fatal Error: NULL (%s) directory argument\n", dir_descr);
    retval = 1;
    goto theend;
  }

  int fexists = file_exists (dir);

  ifnot (fexists)
    if (-1 is mkdir (dir, S_IRWXU)) {
      fprintf (stderr, "Fatal Error: Cannot create %s directory\n", dir);
      retval = errno;
      goto theend;
    }

  if (-1 is access (dir, R_OK)) {
    fprintf (stderr, "Fatal Error: %s, (%s) directory, Is Not Readable\n", dir_descr, dir);
    retval = errno;
    goto theend;
  }

  if (-1 is access (dir, W_OK)) {
    fprintf (stderr, "Fatal Error: %s, (%s) directory, Is Not Writable\n", dir_descr, dir);
    retval = errno;
    goto theend;
  }

  if (-1 is access (dir, X_OK)) {
    fprintf (stderr, "Fatal Error: %s, (%s) directory, Has Not Execution Bits\n", dir_descr, dir);
    retval = errno;
    goto theend;
  }

  struct stat st;
  if (-1 is stat (dir, &st)) {
    fprintf (stderr, "Fatal Error: %s, (%s) directory, Can not stat()\n", dir_descr, dir);
    retval = errno;
    goto theend;
  }

  ifnot (S_ISDIR (st.st_mode)) {
    fprintf (stderr, "Fatal Error: %s, (%s) directory, Is Not A Directory\n", dir_descr, dir);
    retval = errno;
    goto theend;
  }

  if (warn) {
    char mode_string[12];
    vsys_stat_mode_to_string (mode_string, st.st_mode);
    ifnot (cstring_eq (mode_string, "drwx------")) {
      fprintf (stderr, "Warning: (%s) directory |%s| permissions is not 0700 or drwx------\n",
         dir_descr, dir);

      if (exit_on_warning) {
        retval = 1;
        goto theend;
      }
    }
  }

theend:
  if (retval isnot OK)
    if (exit_on_error) exit (retval);

  return retval;
}

private venv_t *venv_new (void) {
  venv_t *env = AllocType (venv);
  env->pid = getpid ();
  env->uid = getuid ();
  env->gid = getgid ();

  errno = 0;
  struct passwd *pswd = getpwuid (env->uid);
  if (NULL is pswd) {
    fprintf (stderr, "Can not read password record %s\n", strerror (errno));
    exit (1);
  }

  env->user_name = string_new_with (pswd->pw_name);

  struct group *gr = getgrgid (env->gid);
  if (NULL is gr) {
    fprintf (stderr, "Can not read group record %s\n", strerror (errno));
    exit (1);
  }

  env->group_name = string_new_with (gr->gr_name);

  char *term_name = getenv ("TERM");
  if (NULL is term_name) {
    fprintf (stderr, "TERM environment variable isn't set\n");
    env->term_name = string_new (1);
  } else
    env->term_name = string_new_with (term_name);

  char *hdir = getenv ("HOME");
  ifnot (NULL is hdir)
    env->home_dir = string_new_with (hdir);
  else
    env->home_dir = string_new_with ((NULL is pswd ? "NONE" : pswd->pw_name));

  if (hdir[env->home_dir->num_bytes - 1] is DIR_SEP)
    string_clear_at (env->home_dir, env->home_dir->num_bytes - 1);

#ifndef LIBVED_DIR
  env->my_dir = string_new_with_fmt ("%s/.libved", env->home_dir->bytes);
#else
  env->my_dir = string_new_with (LIBVED_DIR);
#endif
  if (env->uid)
    __env_check_directory__ (env->my_dir->bytes, "libved directory", 1, 0, 0);

#ifndef LIBVED_TMPDIR
  env->tmp_dir = string_new_with_fmt ("%s/tmp", env->my_dir->bytes);
#else
  env->tmp_dir = string_new_with (LIBVED_TMPDIR);
#endif
  if (env->uid)
    __env_check_directory__ (env->tmp_dir->bytes, "temp directory", 1, 1, 0);

#ifndef LIBVED_DATADIR
  env->data_dir = string_new_with_fmt ("%s/data", env->my_dir->bytes);
#else
  env->data_dir = string_new_with (LIBVED_DATADIR);
#endif
  if (env->uid)
    __env_check_directory__ (env->data_dir->bytes, "data directory", 1, 1, 0);

  env->i_dir = string_new_with_fmt ("%s/i", env->data_dir->bytes);
  if (env->uid)
    __env_check_directory__ (env->i_dir->bytes, "integrated interpreter directory", 1, 1, 0);
  if (env->uid)
    __env_check_directory__ (STR_FMT("%s/scripts", env->i_dir->bytes), "integrated interpreter scripts directory", 1, 1, 0);

  char *path = getenv ("PATH");
  env->path = (path is NULL) ? NULL : string_new_with (path);

  env->diff_exec = vsys_which ("diff", env->path->bytes);
  env->xclip_exec = vsys_which ("xclip", env->path->bytes);

  env->env_str = string_new (8);

  return env;
}

private void venv_free (venv_t **env) {
  if (NULL is env) return;

  string_free ((*env)->group_name);
  string_free ((*env)->user_name);
  string_free ((*env)->term_name);
  string_free ((*env)->home_dir);
  string_free ((*env)->env_str);
  string_free ((*env)->path);
  string_free ((*env)->i_dir);
  string_free ((*env)->my_dir);
  string_free ((*env)->tmp_dir);
  string_free ((*env)->data_dir);
  string_free ((*env)->diff_exec);
  string_free ((*env)->xclip_exec);

  free (*env); *env = NULL;
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
    rline_release (it->data);
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
  self(current.set, this->num_items - 1);
  current_list_append (this, row);
  self(current.set, cur_idx);
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
  String.replace_with (this->current->data, bytes);
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
  int islastidx = this->cur_idx + 1 == this->num_items;
  row_t *row;
  __buf_current_delete (this, &row);

  ifnot (islastidx) {
    this->current = this->current->prev;
    this->cur_idx--;
  }

  return row;
}

private char *buf_get_contents (buf_t *this, int addnl) {
  size_t len = self(get.size) - (addnl ? 0 : this->num_items);
  char *buf = Alloc (len + 1);

  row_t *it = this->head;
  size_t offset = 0;

  while (it) {
    byte_cp (buf + offset, it->data->bytes, it->data->num_bytes);
    offset += it->data->num_bytes;
    if (addnl)
      buf[offset++] = '\n';
    it = it->next;
  }

  buf[len] = '\0';

  return buf;
}

private win_t *buf_get_my_parent (buf_t *this) {
  return $my(parent);
}

private ed_t *buf_get_my_root (buf_t *this) {
  return $my(root);
}

private char *buf_get_fname (buf_t *this) {
  return $my(fname);
}

private char *buf_get_ftype_name (buf_t *this) {
  return $my(ftype)->name;
}

private char *buf_get_basename (buf_t *this) {
  return $my(basename);
}

private string_t *buf_get_shared_str (buf_t *this) {
  return $my(shared_str);
}

private int buf_get_flags (buf_t *this) {
  return $my(flags);
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

private int buf_isit_special_type (buf_t *this) {
  return ($my(flags) & BUF_IS_SPECIAL);
}

private int buf_set_row_idx (buf_t *this, int idx, int ofs, int col) {
  if (idx < 0) idx = 0;

  do {
    idx = current_list_set (this, idx);
    if (idx is INDEX_ERROR) {
      idx--;
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

  if (col > (int) $mycur(data)->num_bytes or col < 1)
    col = 1;
  $my(video)->col_pos = $my(cur_video_col) = col;
  return this->cur_idx;
}

private int buf_current_set (buf_t *this, int idx) {
  return current_list_set (this, idx);
}

private void buf_set_mode (buf_t *this, char *mode) {
  Cstring.cp ($my(mode), MAXLEN_MODE, mode, MAXLEN_MODE - 1);
}

private void buf_set_autosave (buf_t *this, long minutes) {
  if (minutes <= 0) {
    $my(autosave) = $my(saved_sec) = 0;
    return;
  }

  if (minutes > (60 * 24)) minutes = (60 * 24);
  $my(autosave) = minutes * 60;
  ifnot ($my(saved_sec))
    $my(saved_sec) = Vsys.get.clock_sec (DEFAULT_CLOCK);
}

private void buf_set_on_emptyline (buf_t *this, char *str) {
  if (NULL is str) return;
  String.replace_with ($my(ftype)->on_emptyline, str);
}

private void buf_set_autochdir (buf_t *this, int val) {
  $my(ftype)->autochdir = (val isnot 0);
}

private void buf_set_backup (buf_t *this, int backup, char *suffix) {
  if (backup <= 0
      or NULL isnot $my(backupfile)
      or NULL is suffix
      or ($my(flags) & BUF_IS_SPECIAL)
      or Cstring.eq ($my(fname), UNNAMED))
    return;

  size_t len = bytelen (suffix);
  ifnot (len) return;
  if (len > 7) len = 7;
  for (size_t i = 0; i < len; i++) $my(backup_suffix)[i] = suffix[i];
  $my(backup_suffix)[len] = '\0';

  char *dname = Path.dirname ($my(fname));
  size_t dname_len = bytelen (dname);

  size_t baselen = bytelen ($my(basename));
  size_t suffixlen = bytelen ($my(backup_suffix));
  size_t backuplen = 2 + dname_len + baselen + suffixlen;

  $my(backupfile) = Alloc (backuplen + 1);

  size_t i = 0;
  for (; i < dname_len; i++) $my(backupfile)[i] = dname[i];
  free (dname);

  $my(backupfile)[i++] = '/'; $my(backupfile)[i++] = '.';

  for (size_t j = 0; j < baselen; j++, i++) $my(backupfile)[i] = $my(basename)[j];
  for (size_t j = 0; j < suffixlen; i++, j++) $my(backupfile)[i] = $my(backup_suffix)[j];
  $my(backupfile)[backuplen] = '\0';
}

private void buf_set_normal_beg_cb (buf_t *this, BufNormalBeg_cb cb) {
  this->on_normal_beg = cb;
}

private void buf_set_normal_end_cb (buf_t *this, BufNormalEnd_cb cb) {
  this->on_normal_end = cb;
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
  $my(cwd) = Dir.current ();
  $my(flags) &= ~FILE_EXISTS;
}

private void buf_set_as_unnamed (buf_t *this) {
  size_t len = bytelen (UNNAMED);
  /* static size_t len = bytelen (UNNAMED); fails on tcc with:
   * error: initializer element is not constant
   */
  $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
  Cstring.cp ($my(fname), len + 1, UNNAMED, len);
  self(set.as.non_existant);
}

private void buf_set_as_pager (buf_t *this) {
  $my(flags) &= BUF_IS_PAGER;
}

private int buf_set_fname (buf_t *this, char *filename) {
  int is_null = (NULL is filename);
  int is_unnamed = (is_null ? 0 : Cstring.eq (filename, UNNAMED));
  size_t len = ((is_null or is_unnamed) ? 0 : bytelen (filename));

  if (is_null or 0 is len or is_unnamed) {
    buf_set_as_unnamed (this);
    return OK;
  }

  string_t *fname = $my(shared_str);
  String.replace_with_len (fname, filename, len);

  /* normalize */
  for (size_t i = 0; i < len; i++) {
    if (fname->bytes[i] isnot DIR_SEP) continue;
    i++;
    while (i < len and fname->bytes[i] is DIR_SEP) {
      String.delete_numbytes_at (fname, 1, i);
      len--;
    }
  }
  if (len > 1 and fname->bytes[len-1] is DIR_SEP) {
    String.clear_at (fname, len-1);
    len--;
  }

  if ($my(flags) & BUF_IS_SPECIAL) {
    $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
    Cstring.cp ($my(fname), len + 1, fname->bytes, len);
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

  int fname_exists = File.exists (fname->bytes);
  int is_abs = IS_DIR_ABS (fname->bytes);

  if (fname_exists) {
    ifnot (File.is_reg (fname->bytes)) {
      VED_MSG_ERROR(MSG_FILE_EXISTS_BUT_IS_NOT_A_REGULAR_FILE, fname->bytes);
      buf_set_as_unnamed (this);
      return NOTOK;
    }

    if (File.is_elf (fname->bytes)) {
      VED_MSG_ERROR(MSG_FILE_EXISTS_BUT_IS_AN_OBJECT_FILE, fname->bytes);
      buf_set_as_unnamed (this);
      return NOTOK;
    }

    $my(flags) |= FILE_EXISTS;

    ifnot (is_abs)
      goto concat_with_cwd;
    else {
      $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
      cstring_cp ($my(fname), len + 1, fname->bytes, len);
    }
  } else {
    $my(flags) &= ~FILE_EXISTS;
    if (is_abs) {
      $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
      cstring_cp ($my(fname), len + 1, fname->bytes, len);
    } else {
concat_with_cwd:;
      char *cwd = Dir.current ();
      len += bytelen (cwd) + 1;
      char tmp[len + 1]; snprintf (tmp, len + 1, "%s/%s", cwd, fname->bytes);
      $my(fname) = mem_should_realloc ($my(fname), PATH_MAX + 1, len + 1);
      /* $my(fname) = realpath (tmp, NULL); aborts with invalid argument on tcc */
      Path.real (tmp, $my(fname));
      free (cwd);
    }
  }

  buf_t *buf = Ed.get.bufname ($my(root), $my(fname));
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
  String.clear_at (this->current->data, 0);
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

private int buf_com_backupfile (buf_t *this) {
  // RECENT WORDS
  if (NULL is $my(backupfile)) return NOTHING_TODO;

  if (file_exists ($my(backupfile))) {
    utf8 chars[] = {'y', 'Y', 'n', 'N'};
    utf8 c =  buf_quest (this, STR_FMT ("backup file: %s exists\noverride? [yYnN]",
        $my(backupfile)), chars, ARRLEN(chars));

      switch (c) {
        case 'n': case 'N': return NOTHING_TODO;
      }
  }

  return buf_write_to_fname (this, $my(backupfile), DONOT_APPEND, 0,
      this->num_items - 1, FORCE, VERBOSE_OFF);
}

private int buf_com_edit_image (buf_t **thisp, rline_t *rl) {
  (void) rl;
  buf_t *this = *thisp;
  if (NULL is $OurRoots(image_file))
    return NOTHING_TODO;

  return Win.edit_fname ($my(parent), thisp, $OurRoots(image_file),
     $myparents(cur_frame), NO_FORCE, DRAW, REOPEN);
}

private int buf_com_save_image (buf_t *this, rline_t *rl) {
  char *fn = NULL;
  string_t *fn_arg = Rline.get.anytype_arg (rl, "as");

  ifnot (NULL is fn_arg)
    fn = fn_arg->bytes;
  else ifnot (NULL is $OurRoots(image_name))
    fn = $OurRoots(image_name);
  else
    fn = Path.basename (self(get.basename));

  return Root.save_image ($OurRoot, fn);
}

private ssize_t buf_read_fname (buf_t *this) {
  if ($my(fname) is NULL or cstring_eq ($my(fname), UNNAMED)) return NOTOK;

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
    $my(flags) |= FILE_IS_WRITABLE;
  else
    $my(flags) &= ~FILE_IS_WRITABLE;

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

  /* actually this might be called only on :e! (and in that case we do not want it)
   * self(backupfile);
   */

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
    $my(video) = $myroots(video);
    $my(dim) = $from(w, frames_dim)[$my(at_frame)];
    $my(statusline_row) = $my(dim)->last_row;
    $my(video_first_row_idx) = this->cur_idx;
    $my(video_first_row) = this->current;
    $my(cur_video_row) = $my(dim)->first_row;
    for (int i = 0; i < 4 and $my(video_first_row_idx) and
        i < $my(dim)->num_rows - 1; i++) {
      $my(video_first_row_idx)--;
      $my(video_first_row) = $my(video_first_row)->prev;
      $my(cur_video_row)++;
    }

    this = this->next;
  }
}

private int win_isit_special_type (win_t *this) {
  return ($my(type) is VED_WIN_SPECIAL_TYPE);
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
     Video.set.row_with ($my(video), $my(frames_dim)[i]->last_row, line);
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
      Win.set.current_buf ($my(parent), idx, draw);
      WIN_CUR_FRAME(w) = frame;
      return w->current;
    }

    idx++;
    this = this->next;
  }

  return NULL;
}

private int buf_on_normal_beg (buf_t **thisp, utf8 com, int count, int regidx) {
  (void) thisp; (void) com; (void) count; (void) regidx;
  return 0;
}

private int buf_on_normal_end (buf_t **thisp, utf8 com, int count, int regidx) {
  (void) thisp; (void) com; (void) count; (void) regidx;
  return 0;
}

private void buf_free_rows (buf_t *this) {
  row_t *row = this->head;
  while (row) {
    row_t *next = row->next;
    self(free.row, row);
    row = next;
  }
}

private void buf_free (buf_t *this) {
  if (this is NULL) return;

  self(free.rows);

  if ($myprop is NULL) return;

  if ($my(fname) isnot NULL) free ($my(fname));

  free ($my(cwd));

  ifnot (NULL is $my(backupfile))
    free ($my(backupfile));

  String.free ($my(statusline));
  String.free ($my(shared_str));
  String.free ($my(cur_insert));

  self(free.line);
  self(ftype.free);
  self(undo.free);
  self(jumps.free);

  free ($myprop);
  free (this);
}

private size_t buf_get_size (buf_t *this) {
  size_t size = 0;
  row_t *it = this->head;
  while (it) {
    size += it->data->num_bytes + 1;
    it = it->next;
  }

  return size;
}

private void buf_free_info (buf_t *this, bufinfo_t **info) {
  (void) this;
  if (NULL is *info) return;
  free ((*info)->fname);
  free ((*info)->cwd);
  free ((*info)->parents_name);
  free (*info);
  *info = NULL;
}

private bufinfo_t *buf_get_info_as_type (buf_t *this) {
  bufinfo_t *info = AllocType (bufinfo);
  info->fname = Cstring.dup ($my(fname), bytelen ($my(fname)));
  info->cwd = Cstring.dup ($my(cwd), bytelen ($my(cwd)));
  info->parents_name = Cstring.dup ($myparents(name), bytelen ($myparents(name)));
  info->at_frame = $my(at_frame);
  info->cur_idx = this->cur_idx;
  info->is_writable = (($my(flags) & FILE_IS_WRITABLE) ? 1 : 0);
  info->num_bytes = self(get.size);
  info->num_lines = self(get.num_lines);
  info->autosave = $my(autosave);
  return info;
}

private void win_free_info (win_t *this, wininfo_t **info) {
  (void) this;
  if (NULL is *info) return;
  free ((*info)->name);
  free ((*info)->parents_name);
  free ((*info)->cur_buf);

  for (size_t i = 0; i < (*info)->num_items; i++)
    free ((*info)->buf_names[i]);
  free ((*info)->buf_names);

  free (*info);
  *info = NULL;
}

private wininfo_t *win_get_info_as_type (win_t *this) {
  wininfo_t *info = AllocType (wininfo);
  info->name = Cstring.dup ($my(name), bytelen ($my(name)));
  info->parents_name = Cstring.dup ($myparents(name), bytelen ($myparents(name)));
  info->num_frames = $my(num_frames);
  info->cur_idx = this->cur_idx;
  info->num_items = this->num_items;
  info->buf_names = Alloc (sizeof (char *) * (info->num_items));
  buf_t *it = this->head;
  int idx = 0;
  while (it) {
    size_t len = bytelen ($from(it, fname));
    if (it is this->current)
      info->cur_buf = Cstring.dup ($from(it, fname), len);

    info->buf_names[idx++] = Cstring.dup ($from(it, fname), len);

    it = it->next;
  }

  return info;
}

private void ed_free_info (ed_t *this, edinfo_t **info) {
  (void) this;
  if (NULL is *info) return;
  free ((*info)->name);
  free ((*info)->cur_win);
  for (size_t i = 0; i < (*info)->num_items; i++)
    free ((*info)->win_names[i]);
  free ((*info)->win_names);

  for (size_t i = 0; i < (*info)->num_special_win; i++)
    free ((*info)->special_win_names[i]);
  free ((*info)->special_win_names);

  free (*info);
  *info = NULL;
}

private edinfo_t *ed_get_info_as_type (ed_t *this) {
  edinfo_t *info = AllocType (edinfo);
  info->num_special_win = self(get.num_special_win);
  info->name = Cstring.dup ($my(name), bytelen ($my(name)));
  info->num_items = this->num_items - info->num_special_win;
  info->win_names = Alloc (sizeof (char *) * (info->num_items));
  info->special_win_names = Alloc (sizeof (char *) * (info->num_special_win));
  win_t *it = this->head;
  int idx = 0;
  int sp_idx = 0;
  while (it) {
    size_t len = bytelen ($from(it, name));
    if (it is this->current)
      info->cur_win = Cstring.dup ($from(it, name), len);

    if ($from(it, type) is VED_WIN_NORMAL_TYPE)
      info->win_names[idx++] = Cstring.dup ($from(it, name), len);
    else
      info->special_win_names[sp_idx++] = Cstring.dup ($from(it, name), len);

    it = it->next;
  }

  info->cur_idx = this->cur_idx;
  return info;
}

private buf_t *win_buf_init (win_t *w, int at_frame, int flags) {
  buf_t *this = AllocType (buf);
  $myprop = AllocProp (buf);

  $my(parent)  = w;
  $my(Me)          = $myparents(__Buf__);
  $my(root)        = $myparents(parent);
  $my(__Ed__)      = $myparents(__Ed__);
  $my(__Win__)     = $myparents(Me);
  $my(__I__)       = $myparents(__I__);
  $my(__E__)       = $myparents(__E__);
  $my(__Re__)      = $myparents(__Re__);
  $my(__Msg__)     = $myparents(__Msg__);
  $my(__Dir__)     = $myparents(__Dir__);
  $my(__File__)    = $myparents(__File__);
  $my(__Term__)    = $myparents(__Term__);
  $my(__Path__)    = $myparents(__Path__);
  $my(__Vsys__)    = $myparents(__Vsys__);
  $my(__Smap__)    = $myparents(__Smap__);
  $my(__Imap__)    = $myparents(__Imap__);
  $my(__Input__)   = $myparents(__Input__);
  $my(__Error__)   = $myparents(__Error__);
  $my(__Video__)   = $myparents(__Video__);
  $my(__Rline__)   = $myparents(__Rline__);
  $my(__Cursor__)  = $myparents(__Cursor__);
  $my(__Screen__)  = $myparents(__Screen__);
  $my(__String__)  = $myparents(__String__);
  $my(__Cstring__) = $myparents(__Cstring__);
  $my(__Vstring__) = $myparents(__Vstring__);
  $my(__Ustring__) = $myparents(__Ustring__);

  $my(term_ptr) = $myroots(term);
  $my(msg_row_ptr) = &$myroots(msg_row);
  $my(prompt_row_ptr) = &$myroots(prompt_row);
  $my(history) = $myroots(history);
  $my(last_insert) = $myroots(last_insert);
  $my(regs) = &$myroots(regs)[0];
  $my(video) = $myroots(video);

  self(undo.init);
  self(jumps.init);

  $my(shared_str) = String.new (128);
  $my(statusline) = String.new (64);
  $my(cur_insert) = String.new (128);

  $my(line) = Ustring.new ();

  $my(flags) = flags;
  $my(flags) &= ~BUF_IS_MODIFIED;
  $my(flags) &= ~BUF_IS_VISIBLE;

  $my(enable_writing) = 1;

  self(set.mode, NORMAL_MODE);

  $my(fname) = Alloc (PATH_MAX + 1);

  for (int i = 0; i < NUM_MARKS; i++)
    $my(marks)[i] = (mark_t) {.mark = MARKS[i], .video_first_row = NULL};

  $my(at_frame) = at_frame;
  $my(dim) = $myparents(frames_dim)[$my(at_frame)];
  $my(statusline_row) = $my(dim)->last_row;
  $my(show_statusline) = 1;

  $my(autosave) = 0;
  $my(backupfile) = NULL;
  $my(backup_suffix)[0] = '~'; $my(backup_suffix)[1] = '\0';

  $my(lw_vis_prev)[0].fidx = -1;
  $my(lw_vis_prev)[0].lidx = -1;

  this->on_normal_beg = buf_on_normal_beg;
  this->on_normal_end = buf_on_normal_end;

  return this;
}

private buf_t *win_buf_new (win_t *win, buf_opts opts) {
  buf_t *this = win_buf_init (win, opts.at_frame, opts.flags);

  self(init_fname, opts.fname);
  self(set.ftype, opts.ftype);
  self(set.row.idx, 0, NO_OFFSET, 1);
  self(set.autosave, opts.autosave);
  self(set.backup, opts.backupfile,
    (NULL is opts.backup_suffix ? BACKUP_SUFFIX : opts.backup_suffix));
  self(backupfile);

  buf_normal_goto_linenr (this, opts.at_linenr, DONOT_DRAW);
  buf_normal_right (this, opts.at_column - 1, DONOT_DRAW);

  return this;
}

private void win_set_min_rows (win_t *this, int rows) {
  $my(min_rows) = rows;
}

private void win_set_num_frames (win_t *this, int num_frames) {
  if (num_frames > $my(max_frames)) return;
  $my(num_frames) = num_frames;
}

private void win_set_previous_idx (win_t *this, int idx) {
  if (idx < 0 or idx > this->num_items - 1) return;
  this->prev_idx = idx;
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
      VED_MSG_ERROR(MSG_FILE_REMOVED_FROM_FILESYSTEM, $my(fname));
    } else {
#if defined(__MACH__)
      if ($my(st).st_mtimespec.tv_sec isnot st.st_mtimespec.tv_sec)
#else
      if ($my(st).st_mtim.tv_sec isnot st.st_mtim.tv_sec)
#endif
        VED_MSG_ERROR(MSG_FILE_HAS_BEEN_MODIFIED, $my(fname));
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

private buf_t *win_get_buf_head (win_t *this) {
  return this->head;
}

private buf_t *win_get_buf_next (win_t *w, buf_t *this) {
  (void) w;
  return this->next;
}

private buf_t *win_get_buf_by_name (win_t *w, const char *fname, int *idx) {
  if (w is NULL or fname is NULL) return NULL;
  *idx = 0;
  buf_t *this = w->head;
  while (this) {
    if ($my(fname) isnot NULL)
      if (cstring_eq ($my(fname), fname)) return this;

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
  buf_t *this = w->head;

  char line[$from(w, dim->num_cols) + 1];
  for (int i = 0; i < $from(w, dim->num_cols); i++) line[i] = ' ';
  line[$from(w, dim->num_cols)] = '\0';

//  if (w->num_items is 0) return;

  for (int i = $from(w, dim->first_row) - 1; i < $from(w, dim->last_row); i++)
    Video.set.row_with ($from(w, video), i, line);

  Win.set.video_dividers ($my(parent));

  while (this) {
    if ($my(flags) & BUF_IS_VISIBLE) {
      if (this is w->current)
        Ed.set.topline ($my(root), this);

      self(to.video);
    }
    this = this->next;
  }

  this = w->head;

  Video.draw.all ($my(video));
}

private void ed_draw_current_win (ed_t *this) {
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

private char *ed_name_gen (int *name_gen, char *prefix, size_t prelen) {
  size_t num = (*name_gen / 26) + prelen;
  char *name = Alloc (num * sizeof (char *) + 1);
  uidx_t i = 0;
  for (; i < prelen; i++) name[i] = prefix[i];
  for (; i < num; i++) name[i] = 'a' + ((*name_gen)++ % 26);
  name[num] = '\0';
  return name;
}

private win_t *ed_win_init (ed_t *ed, char *name, WinDimCalc_cb dim_calc_cb) {
  win_t *this = AllocType (win);
  $myprop = AllocProp (win);

  if (NULL is name) {
    $my(name) = ed_name_gen (&ed->name_gen, "win:", 4);
  } else
    $my(name) = cstring_dup (name, bytelen (name));

  $my(parent) = ed;

  $my(Me)          = &$myparents(Me)->__Win__;
  $my(__Ed__)      = $myparents(Me);
  $my(__Buf__)     = &$myparents(Me)->__Buf__;
  $my(__I__)       = $myparents(__I__);
  $my(__E__)       = $myparents(__E__);
  $my(__Re__)      = $myparents(__Re__);
  $my(__Msg__)     = $myparents(__Msg__);
  $my(__Dir__)     = $myparents(__Dir__);
  $my(__File__)    = $myparents(__File__);
  $my(__Term__)    = $myparents(__Term__);
  $my(__Path__)    = $myparents(__Path__);
  $my(__Vsys__)    = $myparents(__Vsys__);
  $my(__Smap__)    = $myparents(__Smap__);
  $my(__Imap__)    = $myparents(__Imap__);
  $my(__Input__)   = $myparents(__Input__);
  $my(__Error__)   = $myparents(__Error__);
  $my(__Video__)   = $myparents(__Video__);
  $my(__Rline__)   = $myparents(__Rline__);
  $my(__String__)  = $myparents(__String__);
  $my(__Cursor__)  = $myparents(__Cursor__);
  $my(__Screen__)  = $myparents(__Screen__);
  $my(__Cstring__) = $myparents(__Cstring__);
  $my(__Vstring__) = $myparents(__Vstring__);
  $my(__Ustring__) = $myparents(__Ustring__);

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

  $my(video) = $myparents(video);

  self(dim_calc);
  self(adjust.buf_dim);
  self(set.video_dividers);

  $my(video)->row_pos = $from(this->current, cur_video_row);
  $my(video)->col_pos = $from(this->current, cur_video_col);
  if (this is $my(parent)->current) {
    Video.set.row_with ($my(video), $myparents(prompt_row), " ");
    Video.set.row_with ($my(video), $myparents(msg_row), " ");
    self(draw);
  }
}

private void ed_check_msg_status (ed_t *this) {
  if ($my(msg_send) is 1)
    $my(msg_send)++;
  else if (2 is $my(msg_send)) {
    Video.set.row_with ($my(video), $my(msg_row) - 1, " ");
    Video.draw.row_at ($my(video), $my(msg_row));
    $my(msg_send) = 0;
  }
}

private buf_t *ed_buf_get (ed_t *this, char *wname, char *bname) {
  int idx;
  win_t *w = self(get.win_by_name, wname, &idx);
  ifnot (w) return NULL;
  return Win.get.buf_by_name (w, bname, &idx);
}

private int ed_buf_change (ed_t *this, buf_t **thisp, char *wname, char *bname) {
  self(win.change, thisp, NO_COMMAND, wname, NO_OPTION, NO_FORCE);
  return buf_change_bufname (thisp, bname);
}

private buf_t *ed_special_buf (ed_t *this, char *wname, char *bname,
    int num_frames, int at_frame) {
  int idx;
  win_t *w = self(get.win_by_name, wname, &idx);
  if (NULL is w) {
    w = self(win.new_special, wname, num_frames);
    self(append.win, w);
  }

  buf_t *buf = Win.get.buf_by_name (w, bname, &idx);
  if (NULL is buf) {
      buf = Win.buf.new (w, BufOpts (
          .fname = bname,
          .at_frame = at_frame,
          .flags = BUF_IS_PAGER|BUF_IS_RDONLY|BUF_IS_SPECIAL));

    Win.append_buf (w, buf);
  }

  return buf;
}

private buf_t *ed_msg_buf (ed_t *this) {
  return ed_special_buf (this, VED_MSG_WIN, VED_MSG_BUF, 1, 0);
}

private buf_t *ed_diff_buf (ed_t *this) {
  return ed_special_buf (this, VED_DIFF_WIN, VED_DIFF_BUF, 1, 0);
}

private buf_t *ed_search_buf (ed_t *ed) {
  buf_t *this = ed_special_buf (ed, VED_SEARCH_WIN, VED_SEARCH_BUF, 2, 1);
  this->on_normal_beg = buf_grep_on_normal;
  $my(is_sticked) = 1;
  $myparents(cur_frame) = 1;
  Win.set.current_buf ($my(parent), 0, DONOT_DRAW);
  return this;
}

private buf_t *ed_get_scratch_buf (ed_t *this) {
  return ed_special_buf (this, VED_SCRATCH_WIN, VED_SCRATCH_BUF, 1, 0);
}

private void ed_append_toscratch (ed_t *this, int clear_first, char *bytes) {
  buf_t *buf = self(buf.get, VED_SCRATCH_WIN, VED_SCRATCH_BUF);
  if (NULL is buf)
    buf = self(get.scratch_buf);

  if (clear_first) Buf.clear (buf);

  Vstring_t *lines = Cstring.chop (bytes, '\n', NULL, NO_CB_FN, NULL);
  vstring_t *it = lines->head;

  int ifclear = 0;
  while (it) {
    if (clear_first and 0 is ifclear++)
      Buf.current.replace_with (buf, it->data->bytes);
    else
      Buf.append_with (buf, it->data->bytes);
    it = it->next;
  }

  Vstring.free (lines);
}

private void ed_append_toscratch_fmt (ed_t *this, int clear_first, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  ed_append_toscratch (this, clear_first, bytes);
}

private int ed_scratch (ed_t *this, buf_t **bufp, int at_eof) {
  ifnot (Cstring.eq ($from((*bufp), fname), VED_SCRATCH_BUF)) {
    self(buf.change, bufp, VED_SCRATCH_WIN, VED_SCRATCH_BUF);
    ifnot (Cstring.eq ($from((*bufp), fname), VED_SCRATCH_BUF)) {
      self(get.scratch_buf);
      self(buf.change, bufp, VED_SCRATCH_WIN, VED_SCRATCH_BUF);
    }
  }

  if (at_eof) Buf.normal.eof (*bufp, DRAW);
  else Buf.draw (*bufp);

  return DONE;
}

private int ed_messages (ed_t *this, buf_t **bufp, int at_eof) {
  self(buf.change, bufp, VED_MSG_WIN, VED_MSG_BUF);
  ifnot (Cstring.eq ($from((*bufp), fname), VED_MSG_BUF))
    return NOTHING_TODO;

  if (at_eof) Buf.normal.eof (*bufp, DRAW);
  else Buf.draw (*bufp);

  return DONE;
}

private int ed_append_message_cb (Vstring_t *str, char *tok, void *obj) {
  (void) str;
  buf_t *this = (buf_t *) obj;
  self(append_with, tok);
  return OK;
}

private void ed_append_message (ed_t *this, char *msg) {
  buf_t *buf = self(buf.get, VED_MSG_WIN, VED_MSG_BUF);
  ifnot (buf) return;
  Vstring_t unused;
  Cstring.chop (msg, '\n', &unused, ed_append_message_cb, (void *) buf);
}

private void ed_append_message_fmt (ed_t *this, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  ed_append_message (this, bytes);
}

private char *ed_msg_fmt (ed_t *this, int msgid, ...) {
  char efmt[MAXLEN_ERR_MSG]; efmt[0] = '%'; efmt[1] = 's'; efmt[2] = '\0';
  char pat[16]; snprintf (pat, 16, "%d:", msgid);
  char *sp = strstr (ED_MSGS_FMT, pat);
  if (sp isnot NULL) {
    int i;
    for (i = 0; i < (int) bytelen (pat); i++) sp++;
    for (i = 0; *sp and *sp isnot '.'; sp++) efmt[i++] = *sp;
    efmt[i] = '\0';
  }

  size_t len = ({
    int size = 0;
    va_list ap; va_start(ap, msgid);
    size = vsnprintf (NULL, size, efmt, ap);
    va_end(ap);
    size;
  });

  char bytes[len + 1];
  va_list ap;
  va_start(ap, msgid);
  vsnprintf (bytes, sizeof (bytes), efmt, ap);
  va_end(ap);

  String.replace_with_len ($my(ed_str), bytes, len);
  return $my(ed_str)->bytes;
}

private int ed_fmt_string_with_numchars (ed_t *this, string_t *dest,
   int clear_dest, char *src, size_t src_len, int tabwidth, int num_cols) {

  int numchars = 0;
  if (clear_dest) String.clear (dest);

  if (src_len <= 0) src_len = bytelen (src);

  ifnot (src_len) return numchars;

  ustring_t *it = Ustring.encode ($my(uline), src, src_len, CLEAR, tabwidth, 0);

  ustring_t *tmp = it;

  while (it and numchars < num_cols) {
    if (Cstring.eq (it->buf, "\t")) {
      for (int i = 0; i < it->width; i++)
        String.append_byte (dest, ' ');
    } else
      String.append (dest, it->buf);

    numchars += it->width;
    tmp = it;
    it = it->next;
  }

  it = tmp;
  while (it and numchars > num_cols) {
    if (Cstring.eq (it->buf, "\t")) {
      for (int i = 0; i < it->width; i++) {
        if (numchars-- is num_cols)
          return numchars;
        String.clear_at (dest, dest->num_bytes - 1);
      }

      goto thenext_it;
    }

    String.clear_at (dest, (dest->num_bytes - it->len));
    numchars -= it->width;

thenext_it:
    it = it->prev;
  }

  return numchars;
}

private void ed_msg_write (ed_t *this, char *msg) {
  self(append.message, msg);
}

private void ed_msg_write_fmt (ed_t *this, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  ed_msg_write (this, bytes);
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
      String.append_byte ($my(msgline), ' ');

  int set_color = (msg_flags & MSG_SET_COLOR);
  if (set_color) {
    String.prepend ($my(msgline), TERM_MAKE_COLOR(color));
    String.append($my(msgline), TERM_COLOR_RESET);
  }

  Video.set.row_with ($my(video), $my(msg_row) - 1, $my(msgline)->bytes);

  int draw = (msg_flags & MSG_SET_DRAW);
  if (draw)
    Video.draw.row_at ($my(video), $my(msg_row));

  $my(msg_send) = 1;
}

private void ed_msg_set_fmt (ed_t *this, int color, int msg_flags, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  ed_msg_set (this, color, msg_flags, bytes, len);
}

private void ed_msg_line (ed_t *this, int color, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  int flags = MSG_SET_DRAW|MSG_SET_COLOR|MSG_SET_TO_MSG_LINE;
  ed_msg_set (this, color, flags, bytes, len);
}

private void ed_msg_error (ed_t *this, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  ed_msg_set (this, COLOR_ERROR, MSG_SET_DRAW|MSG_SET_COLOR, bytes, len);
}

private void ed_msg_send (ed_t *this, int color, char *msg) {
  ed_msg_set (this, color, MSG_SET_DRAW|MSG_SET_COLOR, msg, -1);
}

private void ed_msg_send_fmt (ed_t *this, int color, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  ed_msg_set (this, color, MSG_SET_DRAW|MSG_SET_COLOR, bytes, len);
}

private char *ed_error_string (ed_t *this, int err) {
  char ebuf[MAXLEN_ERR_MSG];
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

  String.replace_with_len ($my(ed_str), ebuf, i);
  return $my(ed_str)->bytes;
}

private void ed_set_topline (ed_t *ed, buf_t *this) {
  time_t tim = time (NULL);
  struct tm *tm = localtime (&tim);

  String.replace_with_fmt ($myroots(topline), TERM_SET_COLOR_FMT,
      $myroots(env)->uid ? COLOR_TOPLINE : COLOR_SU);
  String.append_fmt ($myroots(topline), "[%s] [pid %d]",
    $my(mode), $from(ed, env)->pid);

  char tmnow[32];
  size_t len = strftime (tmnow, sizeof (tmnow), "[%a %d %H:%M:%S]", tm);
  int pad = $my(dim->num_cols) - ($myroots(topline)->num_bytes - 5) - len;
  if (pad > 0)
    loop (pad) String.append_byte ($myroots(topline), ' ');
  String.append ($myroots(topline), tmnow);
  String.clear_at ($myroots(topline), $my(dim)->num_cols + 5);
  String.append_fmt ($myroots(topline), TERM_COLOR_RESET);
  Video.set.row_with ($my(video), 0, $myroots(topline)->bytes);
}

private void buf_set_draw_topline (buf_t *this) {
  Ed.set.topline ($my(root), this);
  Video.draw.row_at ($my(video), 1);
}

private void buf_set_statusline (buf_t *this) {
  if ($my(dim->num_rows) is 1 or (
      $my(show_statusline) is 0 and 0 is IS_MODE (INSERT_MODE))) {
    String.replace_with ($my(statusline), " ");
    Video.set.row_with ($my(video), $my(statusline_row) - 1, $my(statusline)->bytes);
    return;
  }

  int cur_code = 0;
  if ($mycur(cur_col_idx) < (int) $mycur(data)->num_bytes) {
    cur_code = CUR_UTF8_CODE;
  }

  String.replace_with_fmt ($my(statusline),
    TERM_SET_COLOR_FMT "%s [%s] (line: %d/%d idx: %d len: %d chr: %d) %s",
    COLOR_STATUSLINE, $my(basename), $my(ftype)->name, this->cur_idx + 1,
    this->num_items, $mycur(cur_col_idx), $mycur(data)->num_bytes, cur_code,
    ($my(flags) & FILE_IS_WRITABLE) ? "" : "[RDONLY]");

  String.clear_at ($my(statusline), $my(dim)->num_cols + TERM_SET_COLOR_FMT_LEN);
  String.append_fmt ($my(statusline), "%s", TERM_COLOR_RESET);
  Video.set.row_with ($my(video), $my(statusline_row) - 1, $my(statusline)->bytes);
}

private void buf_set_draw_statusline (buf_t *this) {
  buf_set_statusline (this);
  Video.draw.row_at ($my(video), $my(statusline_row));
}

private string_t *get_current_number (buf_t *this, int *fidx) {
  if ($mycur(data)->num_bytes is 0) return NULL;

  string_t *nb = String.new (8);
  int type = 'd';
  int issign = 0;

  int orig_idx = $mycur(cur_col_idx);
  int idx = orig_idx;
  uchar c;

  while (idx >= 0) {
    c = $mycur(data)->bytes[idx--];

    if (type is 'x') {
      if ('0' isnot c) goto theerror;
      String.prepend_byte (nb, c);
      *fidx = idx + 1;
      break;
    }

    if (IS_DIGIT (c) or IsAlsoAHex (c)) {
      String.prepend_byte (nb, c);
      continue;
    }

    if (c is 'x') {
      if (type is 'x') goto theerror;
      type = 'x';
      String.prepend_byte (nb, c);
      continue;
    }

    if (c is '-') {
      issign = 1; idx--;
    }

    *fidx = idx + 2;
    break;
  }

  int cur_idx = nb->num_bytes - 1;

  idx = orig_idx + 1;
  while (idx < (int) $mycur(data)->num_bytes) {
    c = $mycur(data)->bytes[idx++];

    if (IS_DIGIT (c) or IsAlsoAHex (c)) {
      String.append_byte (nb, c);
      continue;
    }

    if (c is 'x') {
      if (type is 'x') goto theerror;
      if (nb->num_bytes isnot 1 and nb->bytes[0] isnot '0') goto theerror;
      String.append_byte (nb, c);
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
  if (idx < (int) nb->num_bytes) String.clear_at (nb, idx);

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

  if (num) String.delete_numbytes_at (nb, num, 0);
  ifnot (nb->num_bytes) goto theerror;

  idx = 0;
  if (nb->bytes[idx++] is '0') {
    while (idx <= (int) nb->num_bytes - 1) {
      if (nb->bytes[idx] > '8') {
        String.clear_at (nb, idx);
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
  String.free (nb);
  return NULL;

theend:
  if (issign) String.prepend_byte (nb, '-');
  String.prepend_byte (nb, type);
  return nb;
}

private char *buf_get_current_word (buf_t *this,
          char *word, char *NotWord, int NotWordlen, int *fidx, int *lidx) {
  return cstring_extract_word_at ($mycur(data)->bytes, $mycur(data)->num_bytes,
    word, MAXLEN_WORD, NotWord, NotWordlen, $mycur (cur_col_idx), fidx, lidx);
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
  String.free (sch->pat);                     \
  stack_free (sch, search_t);                     \
  ifnot (NULL is sch->prefix) free (sch->prefix); \
  ifnot (NULL is sch->match) free (sch->match);   \
  free (sch);                                     \
})

#define SEARCH_PUSH(idx_, row_)                   \
  search_t *s_ = AllocType (search);              \
  s_->idx = (idx_);                               \
  s_->row  = (row_);                              \
  stack_push (sch, s_)

private int __buf_search__ (buf_t *this, Search_t *sch) {
  int retval = NOTOK;
  int idx = sch->cur_idx;
  int flags = 0;
  regexp_t *re = Re.new (sch->pat->bytes, flags, RE_MAX_NUM_CAPTURES, Re.compile);

  sch->found = 0;
  SEARCH_PUSH (sch->cur_idx, sch->row);

  do {
    int ret = Re.exec (re, sch->row->data->bytes, sch->row->data->num_bytes);
    if (ret is RE_UNBALANCED_BRACKETS_ERROR) {
      MSG_ERRNO (RE_UNBALANCED_BRACKETS_ERROR);
      break;
    }

    if (0 <= ret) {
      sch->idx = idx;
      sch->found = 1;

      sch->match = Alloc ((size_t) (re->match_len) + 1);
      Cstring.cp (sch->match, re->match_len + 1, re->match_ptr, re->match_len);

      sch->col = re->match_idx;
      sch->prefix = Alloc ((size_t) sch->col + 1);
      Cstring.cp (sch->prefix, sch->col + 1, sch->row->data->bytes, sch->col);

      sch->end = sch->row->data->bytes + re->retval;
      retval = OK;
      goto theend;
    }

    idx = SEARCH_UPDATE_ROW (idx);

  } while (idx isnot sch->cur_idx);

theend:
  Re.free (re);
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

private void ed_search_history_push (ed_t *this, char *bytes, size_t len) {
  if ($my(max_num_hist_entries) < $my(history)->search->num_items) {
    histitem_t *tmp = list_pop_tail ($my(history)->search, histitem_t);
    String.free (tmp->data);
    free (tmp);
  }

  histitem_t *h = AllocType (histitem);
  h->data = String.new_with_len (bytes, len);
  list_push ($my(history)->search, h);
}

private int buf_search (buf_t *this, char com, char *str, utf8 cc) {
  if (this->num_items is 0) return NOTHING_TODO;

  int toggle = 0;
  int hist_called = 0; /* if this variable declared a little bit before the for loop
  * gcc will complain for "maybe used uninitialized" because the code can jump to
  * the search label which is inside the for loop, clang do not warn for this, the
  * code works but valgrind reports uninitialized value */

  Search_t *sch = AllocType (Search);
  sch->found = 0;
  sch->prefix = sch->match = NULL;
  sch->row = this->current;

  if (com is '/' or com is '*' or com is 'n') sch->dir = 1;
  else sch->dir = -1;

  sch->cur_idx = SEARCH_UPDATE_ROW (this->cur_idx);

  histitem_t *his = $my(history)->search->head;

  MSG(" ");

  rline_t *rl = rline_new ($my(root), $my(term_ptr), Input.get,
      *$my(prompt_row_ptr), 1, $my(dim)->num_cols, $my(video));
  rl->at_beg = rline_search_at_beg;
  rl->at_end = rline_break;

  rl->prompt_char = (com is '*' or com is '/') ? '/' : '?';

  if (com is '*' or com is '#') {
    com = '*' is com ? '/' : '?';

    char word[MAXLEN_WORD]; word[0] = '\0';
    int fidx, lidx;
    self(get.current_word, word, Notword, Notword_len, &fidx, &lidx);
    sch->pat = String.new_with (word);
    if (sch->pat->num_bytes) {
      BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);
      Rline.write_and_break (rl);
      goto search;
    }
  } else {
    if (str isnot NULL) {
      sch->pat = String.new_with (his->data->bytes);
      BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);

      com = 'n' is com ? '/' : '?';
      rl->prompt_char = com;
      Rline.write_and_break (rl);
      if (cc is -1)
        goto search;
      else
        goto theloop;

    } else if (com is 'n' or com is 'N') {
      if ($my(history)->search->num_items is 0) return NOTHING_TODO;
      sch->pat = String.new_with (his->data->bytes);
      BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);

      com = 'n' is com ? '/' : '?';
      rl->prompt_char = com;
      Rline.write_and_break (rl);
      goto search;
    } else
      sch->pat = String.new (1);
  }

theloop:
  for (;;) {
    utf8 c;
    if (cc isnot -1)
      c = cc;
    else
      c = Rline.edit (rl)->c;

    string_t *p = Vstring.join (rl->line, "");

    if (rl->line->tail->data->bytes[0] is ' ')
      String.clear_at (p, p->num_bytes - 1);

    if (Cstring.eq (sch->pat->bytes, p->bytes)) {
      String.free (p);
    } else {
      String.replace_with (sch->pat, p->bytes);
      String.free (p);

search:
      ifnot (NULL is sch->prefix) { free (sch->prefix); sch->prefix = NULL; }
      ifnot (NULL is sch->match) { free (sch->match); sch->match = NULL; }
      __buf_search__ (this, sch);
      if (toggle) {
        sch->dir = (sch->dir is 1) ? -1 : 1;
        toggle = 0;
      }

      if (sch->found) {
        Msg.set_fmt ($my(root), COLOR_NORMAL, MSG_SET_OPEN, "|%d %d|%s",
            sch->idx + 1, sch->col, sch->prefix);

        size_t hl_idx = $myroots(msgline)->num_bytes;

        Msg.set_fmt ($my(root), COLOR_NORMAL, MSG_SET_APPEND|MSG_SET_CLOSE, "%s%s",
            sch->match, sch->end);

        video_row_hl_at ($my(video), *$my(msg_row_ptr) - 1, COLOR_RED,
            hl_idx, hl_idx + bytelen(sch->match));

        sch->cur_idx = sch->idx;
      } else {
        search_t *s = stack_pop (sch, search_t);
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

        String.replace_with (sch->pat, his->data->bytes);

        rl->state |= RL_CLEAR_FREE_LINE;
        Rline.clear (rl);
        BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);
        goto search;

      default:
        continue;
    }
  }

theend:
  if (sch->found) {
    ed_search_history_push ($my(root), sch->pat->bytes, sch->pat->num_bytes);
    self(normal.goto_linenr, sch->idx + 1, DRAW);
  }

  MSG(" ");

  Rline.clear (rl);
  Rline.free (rl);

  SEARCH_FREE;
  return DONE;
}

private int buf_grep_on_normal (buf_t **thisp, utf8 com, int count, int regidx) {
  (void) count; (void) regidx;
  buf_t *this = *thisp;

  if (com isnot '\r' and com isnot 'q') return 0;
  if (com is 'q') {
    if (NOTHING_TODO is Ed.win.change ($my(root), thisp, VED_COM_WIN_CHANGE_PREV_FOCUSED,
       NULL, NO_OPTION, NO_FORCE))
      return EXIT_THIS;
    return -1;
  }

  this = *thisp;
  self(normal.bol, DONOT_DRAW);
  int retval = buf_open_fname_under_cursor (thisp, 0, OPEN_FILE_IFNOT_EXISTS,
      DONOT_REOPEN_FILE_IF_LOADED, DRAW);
  if (NOTHING_TODO is retval)
    self(draw);
  return retval + 1;
}

private int buf_search_file (buf_t *this, char *fname, regexp_t *re) {
  FILE *fp = fopen (fname, "r");
  if (fp is NULL) return NOTOK;

  size_t fnlen = bytelen (fname);
  char *line = NULL;
  size_t len = 0;
  size_t nread = 0;
  int idx = 0;
  while (-1 isnot (int) (nread = ed_readline_from_fp (&line, &len, fp))) {
    idx++;
    int ret = Re.exec (re, line, nread);
    if (ret is RE_UNBALANCED_BRACKETS_ERROR) {
      MSG_ERRNO (RE_UNBALANCED_BRACKETS_ERROR);
      break;
    }
    if (ret is RE_NO_MATCH) continue;

    size_t blen = (fnlen) + 32 + nread;
    char bytes[blen + 1];
    snprintf (bytes, blen, "%s|%d col %d| %s", fname, idx, re->match_idx, line);
    buf_current_append_with (this, bytes);
    Re.reset_captures (re);
  }

  fclose (fp);
  if (line isnot NULL) free (line);
  return 0;
}

private int buf_grep (buf_t **thisp, char *pat, Vstring_t *fnames) {
  buf_t *this = *thisp;
  int idx = 0;
  win_t *w = Ed.get.win_by_name ($my(root), VED_SEARCH_WIN, &idx);
  this = Win.get.buf_by_name (w, VED_SEARCH_BUF, &idx);
  if (this is NULL) return NOTHING_TODO;
  self(clear);
  String.replace_with_fmt ($mycur(data), "searching for %s", pat);
  int flags = 0;
  regexp_t *re = Re.new (pat, flags, RE_MAX_NUM_CAPTURES, Re.compile);

  vstring_t *it = fnames->head;
  char *dname = it is NULL ? NULL : it->data->bytes;

  while (it) {
    char *fname = it->data->bytes;
    if (File.exists (fname))
      ifnot (Dir.is_directory (fname))
        if (File.is_readable (fname))
          ifnot (File.is_elf (fname))
            buf_search_file (this, fname, re);
    it = it->next;
  }

  ifnot (NULL is dname) {
    free ($my(cwd));
    if (*dname is '.' or *dname isnot DIR_SEP)
      $my(cwd) = Dir.current ();
    else
      $my(cwd) = Path.dirname (dname);
  }

  Re.free (re);

  if (this->num_items is 1) return NOTHING_TODO;

  self(set.video_first_row, 0);
  self(current.set, 0);
  $my(video)->row_pos = $my(cur_video_row);
  $my(video)->col_pos = $my(cur_video_col);
  self(normal.down, 1, DONOT_ADJUST_COL, DONOT_DRAW);
  ifnot (Cstring.eq ($from((*thisp), fname), VED_SEARCH_BUF))
    ed_buf_change ($my(root), thisp, VED_SEARCH_WIN, VED_SEARCH_BUF);
  else
    self(draw);

  return DONE;
}

private int buf_substitute (buf_t *this, char *pat, char *sub, int global,
                                     int interactive, int fidx, int lidx) {
  ed_record ($my(root), "buf_substitute (buf, \"%s\", \"%s\", %d, %d, %d, %d)",
    pat, sub, global, interactive, fidx, lidx);

  int retval = NOTHING_TODO;
  ifnot (this->num_items) return retval;

  string_t *substr = NULL;
  int flags = 0;
  regexp_t *re = Re.new (pat, flags, RE_MAX_NUM_CAPTURES, Re.compile);

  Action_t *Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);

  row_t *it = this->head;
  int idx = 0;
  while (idx < fidx) {
    idx++;
    if (NULL is it->next) break;
    it = it->next;
  }

  if (lidx >= this->num_items) lidx = this->num_items - 1;

  while (idx++ <= lidx) {
    int bidx = 0;

searchandsub:;
    int done_substitution = 0;
    Re.reset_captures (re);

    if (0 > Re.exec (re, it->data->bytes + bidx,
        it->data->num_bytes - bidx)) goto thenext;

    String.free (substr);

    if (NULL is (substr = Re.parse_substitute (re, sub, re->match->bytes))) {
      MSG_ERROR("Error: %s", re->errmsg);
      goto theend;
    }

    if (interactive) {
      size_t matchlen = bidx + re->match_idx;
      char prefix[matchlen + 1];
      Cstring.cp (prefix, matchlen + 1, it->data->bytes, matchlen);

      utf8 chars[] = {'y', 'Y', 'n', 'N', 'q', 'Q', 'a', 'A', 'c', 'C'};
      char qu[MAXLEN_LINE]; /* using STR_FMT (a statement expression) causes  */
                        /* messages for uninitialized value[s] (on clang) */
      snprintf (qu, MAXLEN_LINE,
        "|match at line %d byte idx %d|\n"
        "%s%s%s%s%s\n"
        "|substitution string|\n"
        "%s%s%s\n"
        "replace? yY[es]|nN[o] replace all?aA[ll], continue next line? cC[ontinue], quit? qQ[uit]\n",
         idx, re->match_idx + bidx, prefix, TERM_MAKE_COLOR(COLOR_MENU_SEL), re->match->bytes,
         TERM_MAKE_COLOR(COLOR_MENU_BG), re->match_ptr + re->match_len,
         TERM_MAKE_COLOR(COLOR_MENU_SEL), substr->bytes, TERM_MAKE_COLOR(COLOR_MENU_BG));

      utf8 c =  buf_quest (this, qu, chars, ARRLEN(chars));

      switch (c) {
        case 'n': case 'N': goto if_global;
        case 'q': case 'Q': goto theend;
        case 'c': case 'C': goto thenext;
        case 'a': case 'A': interactive = 0;
      }
    }

    action_t *baction = self(action.new_with, REPLACE_LINE, idx - 1,
        it->data->bytes, it->data->num_bytes);
    stack_push (Action, baction);

    String.replace_numbytes_at_with (it->data, re->match_len, re->match_idx + bidx,
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
    self(undo.push, Action);

    if ($mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes)
      self(normal.eol, DONOT_DRAW);
    self(draw);
  } else
    self(Action.free, Action);

  Re.free (re);
  String.free (substr);

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

private int buf_selection_to_X_word_actions_cb (buf_t **thisp, int fidx, int lidx,
                                bufiter_t *it, char *word, utf8 c, char *action) {
  (void) it; (void) action;
  buf_t *this = *thisp;
  ed_selection_to_X ($my(root), word, lidx - fidx + 1,
  	('*' is c ? X_PRIMARY : X_CLIPBOARD));
  return DONE;
}

private void reg_free (Reg_t *rg) {
  reg_t *reg = rg->head;
  while (reg) {
    reg_t *tmp = reg->next;
    string_free (reg->data);
    free (reg);
    reg = tmp;
  }

  rg->head = NULL;
}

private Reg_t *ed_reg_get (ed_t *this, int regidx) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];

  Reg_t *rg = NULL;
  if (regidx isnot REG_SHARED)
    rg = &$my(regs)[regidx];
  else
    rg = &$from($my(root), shared_reg)[0];

  return rg;
}

private Reg_t *ed_reg_new (ed_t *this, int regidx) {
  Reg_t *rg = ed_reg_get (this, regidx);
  reg_free (rg);
  return rg;
}

private void ed_reg_init_all (ed_t *this) {
  for (int i = 0; i < NUM_REGISTERS; i++)
    $my(regs)[i] = (Reg_t) {.reg = REGISTERS[i]};

  $my(regs)[REG_RDONLY].head = AllocType (reg);
  $my(regs)[REG_RDONLY].head->data = String.new_with ("     ");
  $my(regs)[REG_RDONLY].head->next = NULL;
  $my(regs)[REG_RDONLY].head->prev = NULL;
}

private int ed_reg_get_idx (ed_t *this, int c) {
  (void) this;
  if (c is 0x17) c = REG_CURWORD_CHR;
  char regs[] = REGISTERS; /* this is for tcc */
  char *r = Cstring.byte.in_str (regs, c);
  return (NULL isnot r) ? (r - regs) : NOTOK;
}

private Reg_t *ed_reg_append (ed_t *this, int regidx, int type, reg_t *reg) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];
  Reg_t *rg = NULL;
  if (regidx isnot REG_SHARED)
    rg = &$my(regs)[regidx];
  else
    rg = &$from($my(root), shared_reg)[0];

  rg->reg = regidx;
  rg->type = type;

  stack_append (rg, reg_t, reg);
  return rg;
}

private Reg_t *ed_reg_push (ed_t *this, int regidx, int type, reg_t *reg) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];
  Reg_t *rg = NULL;
  if (regidx isnot REG_SHARED)
    rg = &$my(regs)[regidx];
  else
    rg = &$from($my(root), shared_reg)[0];

  rg->reg = regidx;
  rg->type = type;
  stack_push (rg, reg);
  return rg;
}

private Reg_t *ed_reg_push_r (ed_t *this, int regidx, int type, reg_t *reg) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];
  Reg_t *rg = NULL;
  if (regidx isnot REG_SHARED)
    rg = &$my(regs)[regidx];
  else
    rg = &$from($my(root), shared_reg)[0];

  reg_t *it = rg->head;
  if (it is NULL)
    return ed_reg_push (this, regidx, type, reg);

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

private Reg_t *ed_reg_push_with (ed_t *this, int regidx, int type, char *bytes, int dir) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];
  reg_t *reg = AllocType (reg);
  reg->data = String.new_with (bytes);
  if (dir is REVERSE_ORDER)
    return ed_reg_push_r (this, regidx, type, reg);
  return ed_reg_push (this, regidx, type, reg);
}

private Reg_t *ed_reg_set_ (ed_t *this, int regidx, int type, reg_t *reg) {
  ed_reg_new (this, regidx);
  return ed_reg_push (this, regidx, type, reg);
}

private int ed_reg_is_special (ed_t *this, int regidx) {
  (void) this; // maybe in future
  if (REG_SEARCH > regidx or (regidx > REG_EXPR and regidx < REG_CURWORD))
    return NOTOK;
  return OK;
}

private Reg_t *ed_reg_set (ed_t *this, int c, int type, char *bytes, int dir) {
  int regidx = ed_reg_get_idx (this, c);
  if (NOTOK is regidx) return NULL;
  ed_reg_new (this, regidx);
  return ed_reg_push_with (this, regidx, type, bytes, dir);
}

private Reg_t *ed_reg_setidx (ed_t *this, int regidx, int type, char *bytes, int dir) {
  ed_reg_new (this, regidx);
  return ed_reg_push_with (this, regidx, type, bytes, dir);
}

private int ed_reg_expression (ed_t *this, buf_t *buf, int regidx) {
  for (int i = 0; i < $my(num_expr_reg_cbs); i++) {
    int retval = $my(expr_reg_cbs)[i] (this, buf, regidx);
    if (retval isnot NO_CALLBACK_FUNCTION) return retval;
  }

  return NOTOK;
}

private int ed_reg_special_set (ed_t *this, buf_t *buf, int regidx) {
  if (NOTOK is ed_reg_is_special (this, regidx))
    return NOTHING_TODO;

  switch (regidx) {
    case REG_SEARCH:
      {
        histitem_t *his = $my(history)->search->head;
        if (NULL is his) return NOTOK;
        ed_reg_push_with (this, regidx, CHARWISE, his->data->bytes, DEFAULT_ORDER);
        return DONE;
      }

    case REG_PROMPT:
      if ($my(history)->rline->num_items is 0) return NOTOK;
      {
        h_rlineitem_t *it = $my(history)->rline->head;
        if (NULL is it) return ERROR;
        string_t *str = Vstring.join (it->data->line, "");
        ed_reg_push_with (this, regidx, CHARWISE, str->bytes, DEFAULT_ORDER);

        String.free (str);
        return DONE;
      }

    case REG_FNAME:
      ed_reg_new (this, regidx);
      ed_reg_push_with (this, regidx, CHARWISE, $from(buf, fname), DEFAULT_ORDER);
      return DONE;

    case REG_PLUS:
    case REG_STAR:
      {
        FILE *fp = ed_file_pointer_from_X (this, (REG_STAR is regidx) ? X_PRIMARY : X_CLIPBOARD);
        if (NULL is fp) return ERROR;
        ed_reg_new (this, regidx);
        size_t len = 0;
        char *line = NULL;

        // while (-1 isnot ed_readline_from_fp (&line, &len, fp)) {
        // do it by hand to look for new lines and proper set the type
        ssize_t nread;
        int type = CHARWISE;
        while (-1 isnot (nread = getline (&line, &len, fp))) {
          if (nread) {
            if (line[nread - 1] is '\n' or line[nread - 1] is '\r') {
              line[nread - 1] = '\0';
              type = LINEWISE;
            }

            ed_reg_push_with (this, regidx, type, line, REVERSE_ORDER);
            //ed_reg_push_with (this, regidx, type, line, DEFAULT_ORDER);
          }
        }

        if (line isnot NULL) free (line);
        pclose (fp);
        return DONE;
      }

    case REG_CURWORD:
      {
        char word[MAXLEN_WORD]; int fidx, lidx;
        ifnot (NULL is buf_get_current_word (buf, word, Notword, Notword_len,
            &fidx, &lidx)) {
          ed_reg_new (this, regidx);
          ed_reg_push_with (this, regidx, CHARWISE, word, DEFAULT_ORDER);
        } else
          return ERROR;
      }
      return DONE;

    case REG_EXPR:
      return ed_reg_expression (this, buf, regidx);

    case REG_SHARED:
      $my(regs)[REG_SHARED] = $from($my(root), shared_reg)[0];
      return DONE;

    case REG_BLACKHOLE:
      return NOTOK;
  }

  return NOTHING_TODO;
}

private Reg_t *ed_reg_set_with (ed_t *this, int regidx, int type, char *bytes, int dir) {
  if (regidx is REG_BLACKHOLE) return &$my(regs)[REG_BLACKHOLE];

  ed_reg_new (this, regidx);
  reg_t *reg = AllocType (reg);
  reg->data = String.new_with (bytes);
  if (-1 is dir)
    return ed_reg_push_r (this, regidx, type, reg);
  return ed_reg_push (this, regidx, type, reg);
}

private utf8 buf_quest (buf_t *this, char *qu, utf8 *chs, int len) {
  video_paint_rows_with ($my(video), -1, -1, -1, qu);
  SEND_ESC_SEQ ($my(video)->fd, TERM_CURSOR_HIDE);
  utf8 c;
  for (;;) {
    c = Input.get ($my(term_ptr));
    for (int i = 0; i < len; i++)
      if (chs[i] is c) goto theend;
  }

theend:
  video_resume_painted_rows ($my(video));
  SEND_ESC_SEQ ($my(video)->fd, TERM_CURSOR_SHOW);
  return c;
}

private utf8 ed_question (ed_t *this, char *qu, utf8 *chs, int len) {
  return buf_quest (this->current->current, qu, chs, len);
}

private char *buf_parse_line (buf_t *this, row_t *row, char *line, int idx) {
  Ustring.encode ($my(line), row->data->bytes, row->data->num_bytes,
      CLEAR, $my(ftype)->tabwidth, row->first_col_idx);

  int numchars = 0;
  int j = 0;

  ustring_t *it = $my(line)->current;
  ustring_t *tmp = it;
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

private void buf_draw_current_row (buf_t *this) {
  char line[MAXLEN_LINE];
  buf_parse_line (this, this->current, line, this->cur_idx);
  Video.set.row_with ($my(video), $my(video)->row_pos - 1, line);
  Video.draw.row_at ($my(video), $my(video)->row_pos);
  buf_set_draw_statusline (this);
  Cursor.set_pos ($my(term_ptr), $my(video)->row_pos, $my(video)->col_pos);
}

private void buf_to_video (buf_t *this) {
  row_t *row = $my(video_first_row);
  int idx = $my(video_first_row_idx);
  char line[MAXLEN_LINE];

  int i;
  for (i = $my(dim)->first_row - 1; i < $my(statusline_row) - 1; i++) {
    if (row is NULL) break;
    buf_parse_line (this, row, line, idx++);
    Video.set.row_with ($my(video), i, line);
    row = row->next;
  }

  while (i < $my(statusline_row) - 1)
    Video.set.row_with ($my(video), i++, $my(ftype)->on_emptyline->bytes);

  buf_set_statusline (this);
}

private void buf_flush (buf_t *this) {
  video_render_set_from_to ($my(video), $my(dim)->first_row, $my(statusline_row));
  String.append_fmt ($my(video)->render, TERM_GOTO_PTR_POS_FMT,
      $my(video)->row_pos, $my(video)->col_pos);
  video_flush ($my(video), $my(video)->render);
}

private void buf_draw (buf_t *this) {
  String.clear ($my(video)->render);
  Ed.set.topline ($my(root), this);
  video_render_set_from_to ($my(video), $myroots(topline_row), $myroots(topline_row));
  self(to.video);
  self(flush);
}

private int buf_com_diff (buf_t **thisp, rline_t *rl, int to_stdout) {
  buf_t *this = *thisp;
  if (NULL is $myroots(env)->diff_exec) {
    Msg.error ($my(root), "diff executable can not be found in $PATH");
    return NOTOK;
  }

  char file[PATH_MAX]; file[0] = '\0';

  ifnot (NULL is rl) {
    int origin = Rline.arg.exists (rl, "origin");
    ifnot (origin) goto thenext_condition;

    if (NULL is $my(backupfile)) {
      Msg.send ($my(root), COLOR_WARNING, "backupfile hasn't been set");
      return NOTHING_TODO;
    }

    Cstring.cp (file, PATH_MAX, $my(backupfile), bytelen ($my(backupfile)));
    goto thediff;
  }

thenext_condition:
  ifnot (File.exists ($my(fname))) return NOTOK;

  Cstring.cp (file, PATH_MAX, $my(fname), bytelen ($my(fname)));

thediff:;

  int retval = NOTHING_TODO;

  tmpfname_t *tmpn = File.tmpfname.new ($myroots(env)->tmp_dir->bytes, $my(basename));
  if (NULL is tmpn or -1 is tmpn->fd) return NOTOK;

  char com[MAXLEN_COM];
  Cstring.cp_fmt (com, MAXLEN_COM, "%s -u %s %s", $myroots(env)->diff_exec->bytes,
      tmpn->fname->bytes, file);

  buf_write_to_fname (this, tmpn->fname->bytes, DONOT_APPEND, 0, this->num_items - 1, FORCE, VERBOSE_OFF);

  if (to_stdout)
    retval = Ed.sh.popen ($my(root), this, com, 0, 0, NULL);
  else {
    this = Ed.buf.get ($my(root), VED_DIFF_WIN, VED_DIFF_BUF);
    if (this) {
      self(clear);
      retval = Ed.sh.popen ($my(root), this, com, 1, 0, NULL);
      retval = (retval == 1 ? OK : (retval == 0 ? 1 : retval));
      if (OK is retval) {     // diff returns 1 when files differ
        Ed.buf.change ($my(root), thisp, VED_DIFF_WIN, VED_DIFF_BUF);
        retval = OK;
      } else
        Msg.send_fmt ($my(root), COLOR_MSG, "No differences have been found");
    }
  }

  File.tmpfname.free (tmpn);

  return retval;
}

private int ed_quit (ed_t *ed, int force, int global) {
  int retval = (global ? (force ? EXIT_ALL_FORCE : EXIT_ALL) : EXIT_THIS);
  if (force) return retval;

  win_t *w = ed->head;

 while (w) {
    if (win_isit_special_type (w)) goto winnext;

    buf_t *this = w->head;
    while (this isnot NULL) {
      if ($my(flags) & BUF_IS_SPECIAL
          or 0 is ($my(flags) & BUF_IS_MODIFIED)
          or ($my(flags) & BUF_IS_PAGER)
          or Cstring.eq ($my(fname), UNNAMED))
        goto bufnext;

      utf8 chars[] = {'y', 'Y', 'n', 'N', 'c', 'C','d'};
thequest:;
      utf8 c = buf_quest (this, STR_FMT (
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
          buf_com_diff (&this, NULL, 1);
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

private void buf_on_blankline (buf_t *this) {
  ifnot ($my(ftype)->clear_blanklines) return;

  int lineisblank = 1;
  for (int i = 0; i < (int) $mycur(data)->num_bytes; i++) {
    if ($mycur(data)->bytes[i] isnot ' ') {
      lineisblank = 0;
      break;
    }
  }

  if (lineisblank) {
    String.replace_with ($mycur(data), "");
    $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
    $mycur(cur_col_idx) = $mycur(first_col_idx) = 0;
  }
}

private int buf_normal_right (buf_t *this, int count, int draw) {
  int is_ins_mode = IS_MODE (INSERT_MODE);

  if ($mycur(cur_col_idx) is ((int) $mycur(data)->num_bytes -
      Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]) +
      is_ins_mode) or 0 is $mycur(data)->num_bytes or
      $mycur(data)->bytes[$mycur(cur_col_idx)] is 0 or
      $mycur(data)->bytes[$mycur(cur_col_idx)] is '\n')
    return NOTHING_TODO;

  Ustring.encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
      CLEAR, $my(ftype)->tabwidth, $mycur(cur_col_idx));

  ustring_t *it = buf_get_line_nth ($my(line), $mycur(cur_col_idx));

  int orig_count = count;
  while (count-- and it) {
    if (it->code is '\n' or it is $my(line)->tail) {
      if (count is orig_count - 1)
        return NOTHING_TODO;
      else
        break;
    }

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

  if (draw) self(draw_current_row);
  return DONE;
}

private int buf_normal_noblnk (buf_t *this) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  if ($mycur(cur_col_idx) is 0 and 0 is IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)]))
    return NOTHING_TODO;

  $mycur(cur_col_idx) = 0;
  $my(video)->col_pos = $my(cur_video_col) =
      Ustring.width ($mycur(data)->bytes, $my(ftype)->tabwidth);

  int i = 0;
  for (; i < (int) $mycur(data)->num_bytes; i++) {
    if (IS_SPACE ($mycur(data)->bytes[i])) {
      self(normal.right, 1, DONOT_DRAW);
      continue;
    }

    break;
  }

  if (i) self(draw);

  return DONE;
}

private int buf_normal_eol (buf_t *this, int draw) {
  int retval_bol = DONE;
  if ($mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes)
    retval_bol = self(normal.bol, DONOT_DRAW);
  int retval = self(normal.right, $mycur(data)->num_bytes * 4, draw);
  if (NOTHING_TODO is retval and retval_bol is DONE) {
    if (draw) self(draw);
    return DONE;
  }

  return retval;
}

private int buf_normal_left (buf_t *this, int count, int draw) {
  int is_ins_mode = IS_MODE (INSERT_MODE);
  ifnot ($mycur(cur_col_idx)) {
    if ($my(cur_video_col) isnot 1 and $mycur(data)->bytes[0] is 0)
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
    return NOTHING_TODO;
  }

  Ustring.encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
      CLEAR, $my(ftype)->tabwidth, $mycur(cur_col_idx));

  ustring_t *it = buf_get_line_nth ($my(line), $mycur(cur_col_idx));

  if (it is NULL)
    it = $my(line)->tail;
  else
    if ($my(line)->num_items isnot 1)
      if ((0 is is_ins_mode) or (is_ins_mode and (it->next isnot NULL or
        it->code is '\n')))
      it = it->prev;

  int curcol = $mycur(first_col_idx);

  while (it and count-- and $mycur(cur_col_idx)) {
    int len = it->len;
    $mycur(cur_col_idx) -= len;
    ustring_t *fcol = buf_get_line_nth ($my(line), $mycur(first_col_idx));

    if ($my(cur_video_col) is 1 or $my(cur_video_col) - fcol->width is 0) {
      ifnot ($mycur(first_col_idx)) return NOTHING_TODO;

      int num = 0;
      ustring_t *tmp = NULL; // can not be NULL
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
        if (it->next)
          width -= (it->next->width) - 1;

      ifnot (width) {
        width = 1;
        //$mycur(first_col_idx) -= it->len;
      }

      $my(video)->col_pos = $my(cur_video_col) = width;
    }

    it = it->prev;
  }

  ifnot (draw) return DONE;

  if ($mycur(first_col_idx) isnot curcol) {
    self(draw_current_row);
  } else {
    buf_set_draw_statusline (this);
    Cursor.set_pos ($my(term_ptr), $my(video)->row_pos, $my(video)->col_pos);
  }

  return DONE;
}

private int buf_normal_bol (buf_t *this, int draw) {
  ifnot ($mycur(cur_col_idx)) return NOTHING_TODO;
  $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;
  $my(video)->col_pos = $my(cur_video_col) =
      Ustring.width ($mycur(data)->bytes, $my(ftype)->tabwidth);

  if (draw) self(draw_current_row);
  return DONE;
}

private int buf_normal_end_word (buf_t *this, int count, int run_insert_mode, int draw) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  int cur_idx = $mycur(cur_col_idx);
  int retval = count <= 0 ? NOTHING_TODO : DONE;
  for (int i = 0; i < count; i++) {
    while (($mycur(cur_col_idx) isnot (int) $mycur(data)->num_bytes - 1) and
          (0 is (IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)])))) {
      retval = self(normal.right, 1, DONOT_DRAW);
      if (NOTHING_TODO is retval) break;
   }

    if (NOTHING_TODO is retval) break;
    if (NOTHING_TODO is (retval = self(normal.right, 1, DONOT_DRAW))) break;
  }

  if (cur_idx is $mycur(cur_col_idx)) {
    if (retval is DONE) {
      if (draw) self(draw);
      return NOTHING_TODO;
    }
  }

  if (retval is DONE) self(normal.left, 1, DONOT_DRAW);
  if (run_insert_mode) {
    if (draw) self(draw);
    buf_t **thisp = &this;
    return selfp(insert.mode, 'i', NULL);
  }

  if (IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)]))
    self(normal.left, 1, DONOT_DRAW);

  if (draw) self(draw);
  return DONE;
}

#define THIS_LINE_PTR_IS_AT_NTH_POS                                          \
({                                                                           \
  Ustring.encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,   \
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

private int buf_normal_up (buf_t *this, int count, int adjust_col, int draw) {
  int currow_idx = this->cur_idx;

  if (0 is currow_idx or 0 is count) return NOTHING_TODO;
  if (count > currow_idx) count = currow_idx;

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  buf_on_blankline (this);

  currow_idx -= count;
  self(current.set, currow_idx);

  int col_pos = adjust_col ? self(adjust.col, nth, isatend) : $my(video)->first_col;
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
    if (draw) self(draw_current_row);
    return DONE;
  }

  if (draw) self(draw);
  return DONE;
}

private int buf_normal_down (buf_t *this, int count, int adjust_col, int draw) {
  int currow_idx = this->cur_idx;
  if (this->num_items - 1 is currow_idx)
    return NOTHING_TODO;

  if (count + currow_idx >= this->num_items)
    count = this->num_items - currow_idx - 1;

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  buf_on_blankline (this);

  currow_idx += count;

  self(current.set, currow_idx);

  int col_pos = adjust_col ? self(adjust.col, nth, isatend) : $my(video)->first_col;

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
    if (draw) self(draw_current_row);
    return DONE;
  }

  if (draw) self(draw);
  return DONE;
}

private int buf_normal_page_down (buf_t *this, int count, int draw) {
  if (this->num_items < ONE_PAGE
      or this->num_items - $my(video_first_row_idx) < ONE_PAGE + 1)
    return NOTHING_TODO;

  self(mark.set, MARK_UNNAMED);

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

  buf_on_blankline (this);

  self(current.set, currow_idx);
  self(set.video_first_row, frow);
  $my(video)->row_pos = $my(cur_video_row) = row;
  $my(video)->col_pos = $my(cur_video_col) = self(adjust.col, nth, isatend);
  if (draw) self(draw);
  return DONE;
}

private int buf_normal_page_up (buf_t *this, int count, int draw) {
  //ed_record ($my(root), "buf_normal_page_up (buf, %d)", count);

  if ($my(video_first_row_idx) is 0 or this->num_items < ONE_PAGE)
    return NOTHING_TODO;

  self(mark.set, MARK_UNNAMED);

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

  buf_on_blankline (this);

  self(current.set, curlnr);
  self(set.video_first_row, frow);
  $my(video)->row_pos = $my(cur_video_row) = row;
  $my(video)->col_pos = $my(cur_video_col) = self(adjust.col, nth, isatend);

  if (draw) self(draw);
  return DONE;
}

private int buf_normal_bof (buf_t *this, int draw) {
  if (this->cur_idx is 0) return NOTHING_TODO;

  self(mark.set, MARK_UNNAMED);

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  buf_on_blankline (this);

  self(set.video_first_row, 0);
  self(current.set, 0);

  $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
  $my(video)->col_pos = $my(cur_video_col) = self(adjust.col, nth, isatend);

  if (draw) self(draw);
  return DONE;
}

private int buf_normal_eof (buf_t *this, int draw) {
  if ($my(video_first_row_idx) is this->num_items - 1) {
    ifnot (draw)
      return NOTHING_TODO;
    else
      goto do_draw;
  }

  self(mark.set, MARK_UNNAMED);

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  buf_on_blankline (this);

  self(current.set, this->num_items - 1);

  $my(video)->col_pos = $my(cur_video_col) = self(adjust.col, nth, isatend);

  if (this->num_items < ONE_PAGE) {
    $my(video)->row_pos = $my(cur_video_row) =
        ($my(dim)->first_row + this->num_items) - 1;

    if (draw) goto do_draw;

    buf_set_draw_statusline (this);
    Cursor.set_pos ($my(term_ptr), $my(video)->row_pos, $my(video)->col_pos);
    return DONE;
  } else {
    self(set.video_first_row, this->num_items - (ONE_PAGE));
    $my(video)->row_pos = $my(cur_video_row) = $my(statusline_row) - 1;
  }

do_draw:
  if (draw) self(draw);
  return DONE;
}

private int buf_normal_goto_linenr (buf_t *this, int lnr, int draw) {
  int currow_idx = this->cur_idx;

  if (lnr <= 0 or lnr is currow_idx + 1)
    return NOTHING_TODO;

  if (lnr > this->num_items)
    lnr = this->num_items;

  self(mark.set, MARK_UNNAMED);

  if (lnr < currow_idx + 1)
    return self(normal.up, currow_idx - lnr + 1, ADJUST_COL, draw);

  return self(normal.down, lnr - currow_idx - 1, ADJUST_COL, draw);
}

private int buf_normal_replace_character_with (buf_t *this, utf8 c) {
  ed_record ($my(root), "buf_normal_replace_character_with (buf, %d)", c);

  char buf[5]; int len;
  Ustring.character (c, buf, &len);
  int clen = Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]);
  String.replace_numbytes_at_with ($mycur(data), clen,
    $mycur(cur_col_idx), buf);

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_current_row);
  return DONE;
}

private int buf_normal_replace_character (buf_t *this) {
  if ($mycur(data)->num_bytes is 0) return NOTHING_TODO;

  Action_t *Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);
  self(undo.push, Action);

  utf8 c = Input.get ($my(term_ptr));

  return self(normal.replace_character_with, c);
}

private int buf_normal_delete_eol (buf_t *this, int regidx, int draw) {
  int clen = Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]);
  if ($mycur(data)->num_bytes is 0)
    // or $mycur(cur_col_idx) is (int) $mycur(data)->num_bytes - clen)
    return NOTHING_TODO;

  Action_t *Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);
  self(undo.push, Action);

  int len = $mycur(data)->num_bytes - $mycur(cur_col_idx);
  char buf[len + 1];
  Cstring.cp (buf, len + 1, $mycur(data)->bytes + $mycur(cur_col_idx), len);

  ed_reg_set_with ($my(root), regidx, CHARWISE, buf, 0);

  String.delete_numbytes_at ($mycur(data),
     $mycur(data)->num_bytes - $mycur(cur_col_idx), $mycur(cur_col_idx));

  if ($mycur(cur_col_idx) isnot 0)
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
  if (draw) self(draw_current_row);
  return DONE;
}

private int buf_insert_new_line (buf_t **thisp, utf8 com) {
  buf_t *this = *thisp;
  Action_t *Action = self(Action.new);
  action_t *action = self(action.new);
  undo_set (action, INSERT_LINE);

  int new_idx = this->cur_idx + ('o' is com ? 1 : -1);
  int currow_idx = this->cur_idx;

  if ('o' is com) {
    self(current.append_with, " ");
    String.clear ($mycur(data));
    action->idx = this->cur_idx;
    this->current = this->current->prev;
    this->cur_idx--;
    self(normal.down, 1, DONOT_ADJUST_COL, DONOT_DRAW);
  } else {
    self(current.prepend_with, " ");
    String.clear ($mycur(data));
    action->idx = this->cur_idx;

    if ($my(video_first_row_idx) is this->cur_idx and 0 is this->cur_idx) {
      $my(video_first_row_idx)--;
      $my(video_first_row) = $my(video_first_row)->prev;
    }

    this->current = this->current->next;
    this->cur_idx++;
    if ($my(video)->row_pos isnot $my(dim)->first_row)
      $my(video)->row_pos = $my(cur_video_row) = $my(video)->row_pos + 1;

    self(normal.up, 1, ADJUST_COL, DONOT_DRAW);
  }

  stack_push (Action, action);
  self(undo.push, Action);

  if (currow_idx > new_idx) {int t = new_idx; new_idx = currow_idx; currow_idx = t;}
  self(adjust.marks, INSERT_LINE, currow_idx, new_idx);

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw);
  return selfp(insert.mode, com, NULL);
}

private int buf_normal_join (buf_t *this, int draw) {
  if (this->num_items is 0 or this->num_items - 1 is this->cur_idx)
    return NOTHING_TODO;

  Action_t *Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);

  row_t *row = buf_current_pop_next (this);

  self(Action.set_with, Action, DELETE_LINE, this->cur_idx + 1,
      row->data->bytes, row->data->num_bytes);
  self(undo.push, Action);

  if (row->data->num_bytes isnot 0) {
    RM_TRAILING_NEW_LINE;
    char *sp = row->data->bytes;
    if (*sp is ' ') { /* MAYBE string_trim_beg() */
      while (*++sp and *sp is ' '); /* at least a space */
      String.delete_numbytes_at (row->data, (sp - row->data->bytes) - 1, 1);
    }
    String.append ($mycur(data), row->data->bytes);
  }

  self(adjust.marks, DELETE_LINE, this->cur_idx, this->cur_idx + 1);
  $my(flags) |= BUF_IS_MODIFIED;
  self(free.row, row);
  if (draw) self(draw);
  return DONE;
}

private int buf_normal_delete (buf_t *this, int count, int regidx, int draw) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  Action_t *Action = NULL;
  action_t *action = NULL;

  int is_norm_mode = IS_MODE (NORMAL_MODE);
  int perfom_undo = is_norm_mode or IS_MODE (VISUAL_MODE_CW) or IS_MODE (VISUAL_MODE_BW);

  if (perfom_undo) {
    action = self(action.new_with, REPLACE_LINE, this->cur_idx,
      $mycur(data)->bytes, $mycur(data)->num_bytes);
  }

  int clen = Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]);
  int len = 0;

  int calc_width = perfom_undo;
  int width = 0;

  while (count--) {
    len += clen;
    if ($mycur(cur_col_idx) + clen is (int) $mycur(data)->num_bytes)
      break;

    if (calc_width) {
      int lwidth = Ustring.width ($mycur(data)->bytes + $mycur(cur_col_idx),
        $my(ftype)->tabwidth);
      if (lwidth > 1) width += lwidth - 1;
    }

    clen = Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx) + len]);
  }

  char buf[len + 1];
  Cstring.cp (buf, len + 1, $mycur(data)->bytes + $mycur(cur_col_idx), len);
  String.delete_numbytes_at ($mycur(data), len, $mycur(cur_col_idx));

  if (calc_width and 0 isnot width)
    $my(video)->col_pos = $my(cur_video_col) =
      $my(cur_video_col) - width;

  if ($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes + (0 is is_norm_mode))
    self(normal.left, 1, DONOT_DRAW);
  else {
    int lwidth = Ustring.width ($mycur(data)->bytes + $mycur(cur_col_idx),
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
    action->cur_col_idx = $mycur(cur_col_idx);
    action->first_col_idx = $mycur(first_col_idx);

    Action = self(Action.new);
    stack_push (Action, action);
    self(undo.push, Action);
    ed_reg_set_with ($my(root), regidx, CHARWISE, buf, 0);
  }

  $my(flags) |= BUF_IS_MODIFIED;
  if (draw) self(draw_current_row);
  return DONE;
}

private int buf_inc_dec_char (buf_t *this, int count, utf8 com) {
  utf8 c = CUR_UTF8_CODE;
  if (CTRL('a') is com)
    c += count;
  else
    c -= count;

  /* this needs accuracy, so dont give that illusion and be strict to what you know */
  if (c < ' ' or (c > 126 and c < 161) or (c > 254 and c < 902) or c > 974)
    return NOTHING_TODO;

  Action_t *Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);
  self(undo.push, Action);

  char ch[5]; int len; ustring_character (c, ch, &len);
  int clen = Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]);
  String.replace_numbytes_at_with ($mycur(data), clen, $mycur(cur_col_idx), ch);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_current_row);
  return DONE;
}

private int buf_word_math (buf_t *this, int count, utf8 com) {
  int fidx = 0;

  string_t *word = get_current_number (this, &fidx);
  if (NULL is word) return buf_inc_dec_char (this, count, com);

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
    Cstring.itoa (nr, new, 10);
  else {
    char s[16];
    if (type is 'o')
       Cstring.itoa (nr, s, 8);
    else
       Cstring.itoa (nr, s, 16);
    snprintf (new, 32, "0%s%s", ('x' is type ? "x" : ""), s);
  }

  Action_t *Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);
  self(undo.push, Action);

  String.replace_numbytes_at_with ($mycur(data), word->num_bytes - 1, fidx, new);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_current_row);

  String.free (word);
  return DONE;
}

private int ed_complete_word_callback (menu_t *menu) {
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

  Vstring_t *words = Vstring.new ();

  row_t *row = this->head;
  idx = -1;

  while (row isnot NULL) {
    if (row->data->bytes and ++idx isnot this->cur_idx) {
      char *p = strstr (row->data->bytes, menu->pat);
      if (NULL isnot p) {
        char word[MAXLEN_WORD];
        int lidx = 0;
        while (*p and (
            IS_SPACE (*p) is 0 and
            IS_CNTRL (*p) is 0 and
            NULL is memchr (Notword, *p, Notword_len))) {
          word[lidx++] = *p++;
        }

        word[lidx] = '\0';
        Vstring.add.sort_and_uniq (words, word);
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

private int buf_complete_word (buf_t **thisp) {
  buf_t *this = *thisp;
  int retval = DONE;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2, $my(video)->col_pos,
  ed_complete_word_callback, NULL, 0);
  menu->next_key = CTRL('n');
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;
  //menu->space_selects = 0;

  int orig_patlen = menu->patlen;

  char *word = menu_create ($my(root), menu);
  if (word isnot NULL) {
    Action_t *Action = self(Action.new);
    self(Action.set_with_current, Action, REPLACE_LINE);
    self(undo.push, Action);

    String.delete_numbytes_at ($mycur(data), orig_patlen, $my(shared_int));
    String.insert_at ($mycur(data), word, $my(shared_int));
    self(normal.end_word, 1, 0, DONOT_DRAW);

    this = *thisp;
    self(normal.right, 1, DONOT_DRAW);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_current_row);
  }

theend:
  menu_free (menu);
  return retval;
}

private int ed_complete_line_callback (menu_t *menu) {
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

  Vstring_t *lines = Vstring.new ();

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
           Vstring.add.sort_and_uniq (lines, row->data->bytes);
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

private int buf_complete_line (buf_t *this) {
  int retval = DONE;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2, $my(video)->col_pos,
      ed_complete_line_callback, NULL, 0);
  //menu->space_selects = 0;
  menu->next_key = CTRL('l');
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *line = menu_create ($my(root), menu);

  if (line isnot NULL) {
    Action_t *Action = self(Action.new);
    self(Action.set_with_current, Action, REPLACE_LINE);
    self(undo.push, Action);

    String.clear ($mycur(data));
    String.append ($mycur(data), line);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_current_row);
  }

theend:
  menu_free (menu);
  return retval;
}

private int buf_win_only (buf_t *this) {
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

  Win.draw ($my(parent));
  return DONE;
}

private int ed_complete_word_actions_cb (menu_t *menu) {
  buf_t *this = menu->this;
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  ifnot (menu->patlen)
    menu->list = Vstring.dup ($myroots(word_actions));
  else {
    menu->list = Vstring.new ();
    vstring_t *it = $myroots(word_actions)->head;
    while (it) {
      if (Cstring.eq_n (it->data->bytes, menu->pat, menu->patlen))
        Vstring.add.sort_and_uniq (menu->list, it->data->bytes);
      it = it->next;
    }
  }

  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private utf8 buf_complete_word_actions (buf_t *this, char *action) {
  int retval = DONE;
  utf8 c = ESCAPE_KEY;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
    $my(video)->col_pos, ed_complete_word_actions_cb, NULL, 0);
  menu->next_key = 'W';
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;
  menu->this = this;
  menu->return_if_one_item = 1;
  char *item = menu_create ($my(root), menu);
  if (item isnot NULL) {
    c = *item;
    char *tmp = item;
    int i = 0;
    for (; i < MAXLEN_WORD_ACTION - 1 and *tmp; i++)
      action[i] = *tmp++;
    action[i] = '\0';
  }

theend:                                     /* avoid list (de|re)allocation */
  //menu->state &= ~MENU_LIST_IS_ALLOCATED; /* list will be free'd at ed_free() */
  menu_free (menu);
  return c;
}

private int buf_normal_handle_W (buf_t **thisp) {
  buf_t *this = *thisp;
  char action[MAXLEN_WORD_ACTION];
  utf8 c = buf_complete_word_actions (this, action);
  if (c is ESCAPE_KEY) return NOTHING_TODO;

  char word[MAXLEN_WORD]; int fidx, lidx;
  if (NULL is buf_get_current_word (this, word, Notword, Notword_len, &fidx, &lidx))
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

private int ed_actions_token_cb (Vstring_t *str, char *tok, void *menu_o) {
  menu_t *menu = (menu_t *) menu_o;
  buf_t *this = menu->this;
  if (menu->patlen)
    ifnot (Cstring.eq_n (tok, menu->pat, menu->patlen)) return OK;

  Vstring.current.append_with (str, tok);
  return OK;
}

private int ed_complete_line_mode_actions_cb (menu_t *menu) {
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  buf_t *this = menu->this;

  Vstring_t *items;
  items = Cstring.chop ($myroots(line_mode_actions), '\n', NULL, ed_actions_token_cb, menu);
  menu->list = items;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private utf8 buf_complete_line_mode_actions (buf_t *this, char *action) {
  int retval = DONE;
  utf8 c = ESCAPE_KEY;

  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
    $my(video)->col_pos, ed_complete_line_mode_actions_cb, NULL, 0);
  menu->this = this;
  menu->return_if_one_item = ($myroots(line_mode_chars_len) isnot 1);

  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *item = menu_create ($my(root), menu);

  if (item isnot NULL) {
    c = *item;
    char *tmp = item;
    int i = 0;
    for (; i < MAXLEN_WORD_ACTION - 1 and *tmp; i++)
      action[i] = *tmp++;
    action[i] = '\0';
  }

theend:
  menu_free (menu);
  return c;
}

private int buf_normal_handle_L (buf_t **thisp) {
  buf_t *this = *thisp;

  ifnot ($myroots(line_mode_chars_len)) return NOTHING_TODO;
  char action[MAXLEN_WORD_ACTION];
  utf8 c = buf_complete_line_mode_actions (this, action);

  if (c is ESCAPE_KEY) return NOTHING_TODO;

  int retval = NOTHING_TODO;

  for (int i = 0; i < $myroots(num_line_mode_cbs); i++) {
    if (NULL is $myroots(line_mode_cbs)[i]) continue;
    retval = $myroots(line_mode_cbs)[i] (thisp, c, action,
      $mycur(data)->bytes, $mycur(data)->num_bytes);
    if (retval isnot NO_CALLBACK_FUNCTION) break;
  }

  retval = (retval is NO_CALLBACK_FUNCTION ? NOTHING_TODO : retval);
  return retval;
}

private int ed_complete_file_actions_cb (menu_t *menu) {
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  buf_t *this = menu->this;

  Vstring_t *items;
  items = Cstring.chop ($myroots(file_mode_actions), '\n', NULL, ed_actions_token_cb, menu);
  menu->list = items;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private utf8 buf_complete_file_actions (buf_t *this, char *action) {
  int retval = DONE;
  utf8 c = ESCAPE_KEY;

  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
    $my(video)->col_pos, ed_complete_file_actions_cb, NULL, 0);
  menu->this = this;
  menu->return_if_one_item = ($myroots(file_mode_chars_len) isnot 1);

  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *item = menu_create ($my(root), menu);

  if (item isnot NULL) {
    c = *item;
    char *tmp = item;
    int i = 0;
    for (; i < MAXLEN_WORD_ACTION - 1 and *tmp; i++)
      action[i] = *tmp++;
    action[i] = '\0';
  }

theend:
  menu_free (menu);
  return c;
}

private int buf_normal_handle_F (buf_t **thisp) {
  buf_t *this = *thisp;

  ifnot ($myroots(file_mode_chars_len)) return NOTHING_TODO;
  char action[MAXLEN_WORD_ACTION];
  utf8 c = buf_complete_file_actions (this, action);

  if (c is ESCAPE_KEY) return NOTHING_TODO;

  int retval = NOTHING_TODO;

  for (int i = 0; i < $myroots(num_file_mode_cbs); i++) {
    if (NULL is $myroots(file_mode_cbs)[i]) continue;
    retval = $myroots(file_mode_cbs)[i] (thisp, c, action);
    if (retval isnot NO_CALLBACK_FUNCTION) break;
  }

  retval = (retval is NO_CALLBACK_FUNCTION ? NOTHING_TODO : retval);
  return retval;
}

private int buf_normal_handle_ctrl_w (buf_t **thisp) {
  buf_t *this = *thisp;

  utf8 c = Input.get ($my(term_ptr));
  switch (c) {
    case CTRL('w'):
    case ARROW_DOWN_KEY:
    case 'j':
    case 'w':
       {
         int frame = WIN_CUR_FRAME($my(parent)) + 1;
         if (frame > WIN_LAST_FRAME($my(parent))) frame = FIRST_FRAME;
         this = Win.frame.change ($my(parent), frame, DRAW);
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
         this = Win.frame.change ($my(parent), frame, DRAW);
         if (NULL isnot this) {
           *thisp = this;
           return DONE;
         }
       }
       break;

    case 'o':
      return buf_win_only (this);

    case 'n':
      return buf_enew_fname (thisp, UNNAMED);

    case 's':
      return buf_split (thisp, UNNAMED);

    case 'l':
    case ARROW_RIGHT_KEY:
      return Ed.win.change ($my(root), thisp, VED_COM_WIN_CHANGE_NEXT,
          NULL, NO_OPTION, NO_FORCE);

    case 'h':
    case ARROW_LEFT_KEY:
      return Ed.win.change ($my(root), thisp, VED_COM_WIN_CHANGE_PREV,
          NULL, NO_OPTION, NO_FORCE);

    case '`':
      return Ed.win.change ($my(root), thisp, VED_COM_WIN_CHANGE_PREV_FOCUSED,
          NULL, NO_OPTION, NO_FORCE);

    default:
      break;
  }

  return NOTHING_TODO;
}


private int buf_normal_handle_g (buf_t **thisp, int count) {
  buf_t *this = *thisp;

  if (1 isnot count) return self(normal.goto_linenr, count, DRAW);

  utf8 c = Input.get ($my(term_ptr));
  switch (c) {
    case 'g':
      return self(normal.bof, DRAW);

    case 'f':
      return buf_open_fname_under_cursor (thisp, cstring_eq ($my(fname), VED_MSG_BUF)
         ? 0 : AT_CURRENT_FRAME, OPEN_FILE_IFNOT_EXISTS, DONOT_REOPEN_FILE_IF_LOADED,
         DRAW);

    case 'v':
      $my(state) |= BUF_LW_RESELECT;
      return selfp(normal.visual.lw);

    default:
      for (int i = 0; i < $myroots(num_on_normal_g_cbs); i++) {
        int retval = $myroots(on_normal_g_cbs)[i] (thisp, c);
        if (retval isnot NO_CALLBACK_FUNCTION) return retval;
      }

      return NOTHING_TODO;
  }

  return self(normal.goto_linenr, count, DRAW);
}

private int buf_normal_handle_G (buf_t *this, int count) {
  if (1 isnot count)
    return self(normal.goto_linenr, count, DRAW);

  return self(normal.eof, DRAW);
}

private int buf_normal_handle_comma (buf_t **thisp) {
  buf_t *this = *thisp;
  utf8 c = Input.get ($my(term_ptr));
  switch (c) {
    case 'n':
      return buf_change (thisp, VED_COM_BUF_CHANGE_NEXT);

    case 'm':
      return buf_change (thisp, VED_COM_BUF_CHANGE_PREV);

    case ',':
      return buf_change (thisp, VED_COM_BUF_CHANGE_PREV_FOCUSED);

    case '.':
      return Ed.win.change ($my(root), thisp, VED_COM_WIN_CHANGE_PREV_FOCUSED,
          NULL, NO_OPTION, NO_FORCE);

    case '/':
      return Ed.win.change ($my(root), thisp, VED_COM_WIN_CHANGE_NEXT,
          NULL, NO_OPTION, NO_FORCE);

    case ';':
      $myroots(state) |= ED_NEXT;
      return EXIT_THIS;

    case '\'':
      $myroots(state) |= ED_PREV;
      return EXIT_THIS;

    case 'l':
      $myroots(state) |= ED_PREV_FOCUSED;
      return EXIT_THIS;
  }

  return NOTHING_TODO;
}

private int buf_handle_ctrl_x (buf_t **thisp) {
  buf_t *this = *thisp;
  utf8 c = Input.get ($my(term_ptr));
  switch (c) {
    case 'l':
    case CTRL('l'):
      return buf_complete_line (this);

    case 'f':
    case CTRL('f'):
      return buf_insert_complete_filename (thisp);
  }

  return NOTHING_TODO;
}

private int buf_delete_word (buf_t *this, int regidx) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  char word[MAXLEN_WORD]; int fidx, lidx;
  if (NULL is buf_get_current_word (this, word, Notword, Notword_len, &fidx, &lidx))
    return NOTHING_TODO;

  Action_t *Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);
  self(undo.push, Action);

  int len = $mycur(cur_col_idx) - fidx;
  char buf[len + 1];
  Cstring.cp (buf, len + 1, $mycur(data)->bytes + fidx, len);

  self(normal.left, char_num (buf, len), DONOT_DRAW);
  String.delete_numbytes_at ($mycur(data), bytelen (word), fidx);

  ed_reg_set_with ($my(root), regidx, CHARWISE, word, 0);

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_current_row);
  return DONE;
}

private int buf_delete_line (buf_t *this, int count, int regidx) {
  if (count > this->num_items - this->cur_idx)
    count = this->num_items - this->cur_idx;

  int currow_idx = this->cur_idx;

  int nth = THIS_LINE_PTR_IS_AT_NTH_POS;
  int isatend = $my(state) & PTR_IS_AT_EOL;

  Action_t *Action = self(Action.new);

  int ridx = regidx;
  Reg_t *rg = NULL;
  int reg_append = ('A' <= REGISTERS[regidx] and REGISTERS[regidx] <= 'Z');
                                       /* optimization for large buffers */
  int perfom_reg = (regidx isnot REG_UNNAMED or count < ($my(dim)->num_rows * 20)) and
                    regidx isnot REG_INTERNAL;

  if (perfom_reg) {
    if (regidx isnot REG_STAR and regidx isnot REG_PLUS) {
      if (reg_append) {
        ridx = REG_INTERNAL;
        rg = ed_reg_new ($my(root), ridx);
      } else {
        rg = ed_reg_new ($my(root), regidx);
      }
    }
  }

  int fidx = this->cur_idx;
  int lidx = fidx + count - 1;

  for (int idx = fidx; idx <= lidx; idx++) {
    action_t *action = self(action.new_with, DELETE_LINE, this->cur_idx,
        $mycur(data)->bytes, $mycur(data)->num_bytes);
    stack_push (Action, action);

    /* with large buffers, this really slowdown a lot the operation */
    if (perfom_reg) {
      rg = ed_reg_push_with (
        $my(root), ridx, LINEWISE, $mycur(data)->bytes, REVERSE_ORDER);
      rg->cur_col_idx = $mycur(cur_col_idx);
      rg->first_col_idx = $mycur(first_col_idx);
      rg->col_pos = $my(cur_video_col);
    }

    if (NULL is self(current.delete)) break;
  }

  if (this->num_items is 0) buf_on_no_length (this);

  $my(video)->col_pos = $my(cur_video_col) = self(adjust.col, nth, isatend);
  self(adjust.marks, DELETE_LINE, fidx, lidx);

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
    self(adjust.view);

theend:
  $my(flags) |= BUF_IS_MODIFIED;
  if (perfom_reg) {
    if (reg_append) {
      ed_reg_append ($my(root), regidx, LINEWISE, rg->head);
      $myroots(regs)[REG_INTERNAL].head = NULL;
    }

    MSG("deleted into register [%c]%s", REGISTERS[regidx],
        reg_append ? " [appended]" : "");
  }

  self(draw);

  self(undo.push, Action);
  return DONE;
}

private int buf_normal_change_case (buf_t *this) {
  ed_record ($my(root), "buf_normal_change_case (buf)");
  utf8 c = CUR_UTF8_CODE;
  char buf[5]; int len;
  Action_t *Action;

  if ('a' <= c and c <= 'z')
    buf[0] = c - ('a' - 'A');
  else if ('A' <= c and c <= 'Z')
    buf[0] = c + ('a' - 'A');
  else {
    char *p = Cstring.byte.in_str (CASE_A, $mycur(data)->bytes[$mycur(cur_col_idx)]);
    if (NULL is p) {
      if (c > 0x80) {
        utf8 new;
        if (Ustring.is_lower (c))
          new = Ustring.to_upper (c);
        else
          new = Ustring.to_lower (c);

        if (new is c) goto theend;
        Ustring.character (new, buf, &len);
        goto setaction;
      } else
        goto theend;
    }

    buf[0] = CASE_B[p-CASE_A];
  }

  buf[1] = '\0';

setaction:
  Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);
  self(undo.push, Action);

  String.replace_numbytes_at_with ($mycur(data), bytelen (buf),
      $mycur(cur_col_idx), buf);
  self(draw_current_row);

theend:
  $my(flags) |= BUF_IS_MODIFIED;
  self(normal.right, 1, DRAW);
  return DONE;
}

private int buf_indent (buf_t *this, int count, utf8 com) {
  if (com is '<') {
    if ($mycur(data)->num_bytes is 0 or (IS_SPACE ($mycur(data)->bytes[0]) is 0))
      return NOTHING_TODO;
  } else
    if (Cstring.eq (VISUAL_MODE_LW, $my(mode)))
      ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  Action_t *Action = self(Action.new);
  action_t *action = self(action.new_with, REPLACE_LINE, this->cur_idx,
      $mycur(data)->bytes, $mycur(data)->num_bytes);

  int shiftwidth = ($my(ftype)->shiftwidth ? $my(ftype)->shiftwidth : DEFAULT_SHIFTWIDTH);

  if ((shiftwidth * count) > $my(dim)->num_cols)
    count = $my(dim)->num_cols / shiftwidth;

  if (com is '>') {
    int len = shiftwidth * count;
    char s[len + 1];
    int i;
    for (i = 0; i < len; i++) { s[i] = ' '; } s[i] = '\0';
    String.prepend ($mycur(data), s);
  } else {
    char *s = $mycur (data)->bytes;
    int i = 0;
    while (IS_SPACE (*s) and i < shiftwidth * count) {
      s++; i++;
    }

    String.clear ($mycur(data));
    String.append ($mycur(data), s);
    if ($mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes) {
      $mycur(cur_col_idx) = $mycur(first_col_idx) = 0;
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
      self(normal.eol, DRAW);
    }
  }

  stack_push (Action, action);
  self(undo.push, Action);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_current_row);
  return DONE;
}

private int buf_normal_Yank (buf_t *this, int count, int regidx) {
  if (count > this->num_items - this->cur_idx)
    count = this->num_items - this->cur_idx;

  int currow_idx = this->cur_idx;

  int ridx = regidx;
  Reg_t *rg = NULL;
  int reg_append = ('A' <= REGISTERS[regidx] and REGISTERS[regidx] <= 'Z');

  if (regidx isnot REG_STAR and regidx isnot REG_PLUS) {
    if (reg_append) {
      ridx = REG_INTERNAL;
      rg = ed_reg_new ($my(root), ridx);
    } else {
      rg = ed_reg_new ($my(root), regidx);
    }
  }

  String.clear ($my(shared_str));

  for (int i = 0; i < count; i++) {
    self(current.set, (currow_idx + count - 1) - i);
    if (regidx isnot REG_STAR and regidx isnot REG_PLUS) {
      rg = ed_reg_push_with (
          $my(root), ridx, LINEWISE, $mycur(data)->bytes, DEFAULT_ORDER);
      rg->cur_col_idx = $mycur(cur_col_idx);
      rg->first_col_idx = $mycur(first_col_idx);
      rg->col_pos = $my(cur_video_col);
    } else {
      String.prepend_fmt ($my(shared_str), "%s\n", $mycur(data)->bytes);
    }
  }

  if (regidx is REG_STAR or regidx is REG_PLUS)
    ed_selection_to_X ($my(root), $my(shared_str)->bytes, $my(shared_str)->num_bytes,
        (REG_STAR is regidx ? X_PRIMARY : X_CLIPBOARD));

  if (reg_append) {
    ed_reg_append ($my(root), regidx, LINEWISE, rg->head);
    $myroots(regs)[REG_INTERNAL].head = NULL;
  }

  MSG("yanked [linewise] into register [%c]%s", REGISTERS[regidx],
      reg_append ? " [appended]" : "");
  return DONE;
}

private int buf_normal_yank (buf_t *this, int count, int regidx) {
  if (count > (int) $mycur(data)->num_bytes - $mycur(cur_col_idx))
    count = $mycur(data)->num_bytes - $mycur(cur_col_idx);

  char buf[(count * 4) + 1];
  char *bytes = $mycur(data)->bytes + $mycur(cur_col_idx);

  int bufidx = 0;
  for (int i = 0; i < count and *bytes; i++) {
    int clen = Ustring.charlen ((uchar) *bytes);
    loop (clen) buf[bufidx + $i] = bytes[$i];
    bufidx += clen;
    bytes += clen;
    i += clen - 1;
  }

  buf[bufidx] = '\0';

  if (regidx isnot REG_STAR and regidx isnot REG_PLUS) {
    ed_reg_set_with ($my(root), regidx, CHARWISE, buf, 0);
  } else
    ed_selection_to_X ($my(root), buf, bufidx, (REG_STAR is regidx
        ? X_PRIMARY : X_CLIPBOARD));

  MSG("yanked [charwise] into register [%c]", REGISTERS[regidx]);
  return DONE;
}

private int buf_normal_put (buf_t *this, int regidx, utf8 com) {
  if (ERROR is ed_reg_special_set ($my(root), this, regidx))
    return NOTHING_TODO;

  Reg_t *rg = &$my(regs)[regidx];
  reg_t *reg = rg->head;

  if (NULL is reg) return NOTHING_TODO;

  Action_t *Action = self(Action.new);

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
    action_t *action = self(action.new);
    if (rg->type is LINEWISE) {
      row_t *row = self(row.new_with, reg->data->bytes);
      undo_set (action, INSERT_LINE);
      linewise_num++;

      if ('p' is com)
        current_list_append (this, row);
      else
        current_list_prepend (this, row);

      action->idx = this->cur_idx;
    } else {
      undo_set (action, REPLACE_LINE);
      action->idx = this->cur_idx;
      action->bytes = Cstring.dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
      String.insert_at ($mycur(data), reg->data->bytes, $mycur(cur_col_idx) +
        (('P' is com or 0 is $mycur(data)->num_bytes) ? 0 :
           Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)])));
    }

    stack_push (Action, action);
    if (com is 'P')
      reg = reg->prev;
    else
      reg = reg->next;
  }

  self(undo.push, Action);

  if (rg->type is LINEWISE) {
    self(adjust.marks, INSERT_LINE, this->cur_idx, this->cur_idx + linewise_num);

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

private int buf_delete_inner (buf_t *this, utf8 c, int regidx) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  Ustring.encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
        CLEAR, $my(ftype)->tabwidth, $mycur(cur_col_idx));

  ustring_t *it = $my(line)->current;

  int cur_idx = $mycur(cur_col_idx);
  int fidx = cur_idx;
  int lidx = cur_idx;
  int found = 0;
  int left = 0;

  int isc = it->code is c;
  int len = it->code isnot c;

  while (it isnot $my(line)->head) {
    it = it->prev;
    if (it->code is c) {
      found = 1;
      break;
    }
    fidx -= it->len;
    len += it->len;
    left++;
  }

  ifnot (found) {
    ifnot (isc)
      return NOTHING_TODO;

    len = 0;
    fidx = cur_idx + 1;
    left = 0;
  }

  if (found is 0 or isc is 0) {
    found = 0;
    it = $my(line)->current;

    while (it isnot $my(line)->tail) {
      it = it->next;
      if (it->code is c) {
        found = 1;
        break;
      }
      lidx += it->len;
      len += it->len;
    }

    ifnot (found) return NOTHING_TODO;
  }

  ifnot (len) return NOTHING_TODO;

  char word[len + 1];
  for (int i = 0; i < len; i++)
    word[i] = $mycur(data)->bytes[fidx + i];
  word[len] = '\0';

  Action_t *Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);
  self(undo.push, Action);

  if (fidx > cur_idx)
    self(normal.right, 1, DONOT_DRAW);
  else
    self(normal.left, left, DONOT_DRAW);

  String.delete_numbytes_at ($mycur(data), bytelen (word), fidx);

  ed_reg_set_with ($my(root), regidx, CHARWISE, word, 0);

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_current_row);
  return DONE;
}

private int buf_normal_handle_c (buf_t **thisp, int count, int regidx) {
  (void) count;
  buf_t *this = *thisp;
  utf8 c = Input.get ($my(term_ptr));
  switch (c) {
    case 'w':
      self(delete.word, regidx);
      return selfp(insert.mode, 'c', NULL);

    case 'i':
      c = Input.get ($my(term_ptr));
      if (NOTHING_TODO is buf_delete_inner (this, c, regidx))
        return NOTHING_TODO;

      return selfp(insert.mode, 'c', NULL);
  }

  return NOTHING_TODO;
}

private int buf_normal_handle_d (buf_t *this, int count, int reg) {
  utf8 c = Input.get ($my(term_ptr));
  switch (c) {
    case 'G':
    case END_KEY:
      count = (this->num_items) - this->cur_idx;
      break;
    case 'g':
    case HOME_KEY:
      count = this->cur_idx + 1;
      self(normal.bof, DRAW);
  }

  switch (c) {
    case 'G':
    case END_KEY:
    case 'g':
    case HOME_KEY:
    case 'd':
      return self(delete.line, count, reg);

    case 'w':
      return self(delete.word, reg);

    default:
      return NOTHING_TODO;
   }
}

private int buf_insert_change_line (buf_t *this, utf8 c, Action_t **action, int draw) {
  if ($mycur(data)->num_bytes) RM_TRAILING_NEW_LINE;

  if (c is ARROW_UP_KEY) self(normal.up, 1, ADJUST_COL, draw);
  else if (c is ARROW_DOWN_KEY) self(normal.down, 1, ADJUST_COL, draw);
  else if (c is PAGE_UP_KEY) self(normal.page_up, 1, draw);
  else if (c is PAGE_DOWN_KEY) self(normal.page_down, 1, draw);
  else {
    int isatend = $mycur(cur_col_idx) is (int) $mycur(data)->num_bytes;
    if (isatend) {
      self(current.append_with, $my(ftype)->autoindent (this, this->current)->bytes);
    } else {
      char bytes[MAXLEN_LINE];
      int len = $mycur(data)->num_bytes - $mycur(cur_col_idx);
      Cstring.cp (bytes, MAXLEN_LINE, $mycur(data)->bytes + $mycur(cur_col_idx), len);

      String.clear_at ($mycur(data), $mycur(cur_col_idx));
      self(current.append_with, STR_FMT ("%s%s",
          $my(ftype)->autoindent (this, this->current)->bytes, bytes));
    }

    this->current = this->current->prev;
    this->cur_idx--;
    self(adjust.marks, INSERT_LINE, this->cur_idx, this->cur_idx + 1);

    action_t *act = self(action.new);
    undo_set (act, INSERT_LINE);

    $my(cur_video_col) = $my(video)->col_pos = $my(video)->first_col;
    $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;
    self(normal.down, 1, DONOT_ADJUST_COL, 0);
    if ($mycur(data)->num_bytes) ADD_TRAILING_NEW_LINE;
    self(normal.right, $my(shared_int) + isatend, DONOT_DRAW);

    act->idx = this->cur_idx;
    stack_push (*action, act);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw);
    return DONE;
  }

  action_t *act = self(action.new);
  undo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = Cstring.dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (*action, act);
  $my(flags) |= BUF_IS_MODIFIED;
  return DONE;
}

private int buf_insert_character (buf_t *this, utf8 *c) {
  int has_pop_pup = 1;
  *c = BUF_GET_AS_NUMBER (has_pop_pup, $my(video)->row_pos - 1,
      $my(video)->col_pos + 1, $my(video)->num_cols, "utf8 code:");
  ifnot (*c) return NOTHING_TODO;
  if (0x07F <= *c and *c < 0x0A0) return NOTHING_TODO;
  if (*c < ' ' and '\t' isnot *c) return NOTHING_TODO;
  $my(state) |= ACCEPT_TAB_WHEN_INSERT;
  return NEWCHAR;
}

private int buf_insert_handle_ud_line_completion (buf_t *this, utf8 *c) {
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
    Ustring.encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
        CLEAR, $my(ftype)->tabwidth, $mycur(cur_col_idx));
    if (0 is $mycur(data)->bytes[$mycur(cur_col_idx)])
      nth = $my(line)->num_items + 1;
    else
      nth = $my(line)->cur_idx + 1;
  }

  Ustring.encode ($my(line), line, len, CLEAR, $my(ftype)->tabwidth, 0);
  if ($my(line)->num_items < nth) return NOTHING_TODO;
  int n = 1;

  ustring_t *it = $my(line)->head;
  while (++n <= nth) it = it->next;

  ifnot (it->code) return NOTHING_TODO;

  *c = it->code;

  return NEWCHAR;
}

private int buf_insert_string (buf_t *this, char *str, size_t len, int draw) {
  String.insert_at ($mycur(data), str, $mycur(cur_col_idx));
  self(normal.right, char_num (str, len), 1);
  if (draw) self(draw_current_row);
  return DONE;
}

private int buf_insert_last_insert (buf_t *this) {
  if ($my(last_insert)->num_bytes is 0) return NOTHING_TODO;
  return self(insert.string, $my(last_insert)->bytes, $my(last_insert)->num_bytes, DRAW);
}

typedef void (*draw_buf) (buf_t *);
typedef void (*draw_current_row) (buf_t *);

private void buf_draw_void (buf_t *this) {(void) this;}
private void buf_draw_current_row_void (buf_t *this) {(void) this;}

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
   self(current.set, (mark__).cur_idx);     \
   this->cur_idx = (mark__).cur_idx

#define VISUAL_ADJUST_COL(idx)                                                      \
({                                                                                  \
  Ustring.encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,          \
      CLEAR, $my(ftype)->tabwidth, idx);                                            \
  int nth_ = $my(line)->cur_idx + 1;                                                \
    self(normal.bol, DONOT_DRAW);                                                               \
    self(normal.right, nth_ - 1, DRAW);                                             \
})

#define VISUAL_INIT_FUN(fmode, parse_fun)                                           \
  char prev_mode[MAXLEN_MODE];                                                      \
  Cstring.cp (prev_mode, MAXLEN_MODE, $my(mode), MAXLEN_MODE - 1);                      \
  self(set.mode, (fmode));                                                          \
  buf_set_draw_topline (this);                                                      \
  mark_t mark; state_set (&mark); mark.cur_idx = this->cur_idx;                     \
  draw_buf dbuf = $self(draw);                                                      \
  draw_current_row drow = $self(draw_current_row);                                  \
  $self(draw) = buf_draw_current_row_void;                                          \
  $self(draw_current_row) = buf_draw_current_row_void;                              \
  int reg = -1;  int count = 1;  (void) count;                                      \
  $my(vis)[1] = (vis_t) {                                                           \
    .fidx = this->cur_idx, .lidx = this->cur_idx, .orig_syn_parser = $my(syn)->parse};                                  \
  $my(vis)[0] = (vis_t) {                                                           \
    .fidx = $mycur(cur_col_idx), .lidx = $mycur(cur_col_idx), .orig_syn_parser = $my(syn)->parse};\
  $my(syn)->parse = (parse_fun)

#define VIS_HNDL_CASE_REG(reg)                                              \
  case '"':                                                                 \
    if (-1 isnot (reg)) goto theend;                                        \
    (reg) = ed_reg_get_idx ($my(root), Input.get ($my(term_ptr))); \
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

private char *buf_syn_parse_visual_lw (buf_t *this, char *line, int len, int idx, row_t *currow) {
  (void) len;

  if ((idx is $my(vis)[0].fidx) or
      (idx > $my(vis)[0].fidx and $my(vis)[0].fidx < $my(vis)[0].lidx and
       idx <= $my(vis)[0].lidx) or
      (idx < $my(vis)[0].fidx and $my(vis)[0].fidx > $my(vis)[0].lidx and
       idx >= $my(vis)[0].lidx) or
      (idx > $my(vis)[0].lidx and $my(vis)[0].lidx < $my(vis)[0].fidx and
       idx < $my(vis)[0].fidx)) {

    Ustring.encode ($my(line), line, len,
        CLEAR, $my(ftype)->tabwidth, currow->first_col_idx);

    ustring_t *it = $my(line)->current;

    String.replace_with_fmt ($my(shared_str), "%s%s",
         TERM_LINE_CLR_EOL, TERM_MAKE_COLOR(HL_VISUAL));

    int num = 0;

    while (num < $my(dim)->num_cols and it) {
      if (it->buf[0] is '\t') goto handle_tab;
      num += it->width;
      String.append ($my(shared_str), it->buf);
      goto next;

handle_tab:
      for (int i = 0; i < $my(ftype)->tabwidth and num < $my(dim)->num_cols; i++) {
        num++;
        String.append_byte ($my(shared_str), ' ');
      }

next:
      it = it->next;
    }

    String.append ($my(shared_str), TERM_COLOR_RESET);
    Cstring.cp (line, MAXLEN_LINE, $my(shared_str)->bytes, $my(shared_str)->num_bytes);
    return $my(shared_str)->bytes;
  }

  return $my(vis)[0].orig_syn_parser (this, line, len, idx, currow);
}

private int ed_visual_complete_actions_cb (menu_t *menu) {
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  buf_t *this = menu->this;

  Vstring_t *items;
  if (IS_MODE(VISUAL_MODE_CW))
    items = Cstring.chop ($myroots(cw_mode_actions), '\n', NULL, ed_actions_token_cb, menu);
  else if (IS_MODE(VISUAL_MODE_LW))
    items = Cstring.chop ($myroots(lw_mode_actions), '\n', NULL, ed_actions_token_cb, menu);
  else
    items = Cstring.chop (
      "insert text in front of the selected block\n"
      "change/replace selected block\n"
      "delete selected block\n", '\n', NULL,
      ed_actions_token_cb, menu);

  menu->list = items;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private utf8 buf_visual_complete_actions (buf_t *this, char *action) {
  int retval = DONE;
  utf8 c = ESCAPE_KEY;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
    $my(video)->col_pos, ed_visual_complete_actions_cb, NULL, 0);
  menu->this = this;
  menu->return_if_one_item = 1;

  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *item = menu_create ($my(root), menu);
  if (item isnot NULL) {
    c = *item;
    char *tmp = item;
    int i = 0;
    for (; i < MAXLEN_WORD_ACTION - 1 and *tmp; i++)
      action[i] = *tmp++;
    action[i] = '\0';
  }

theend:
  menu_free (menu);
  return c;
}

private int buf_normal_visual_lw (buf_t **thisp) {
  buf_t *this = *thisp;
  VISUAL_INIT_FUN (VISUAL_MODE_LW, buf_syn_parse_visual_lw);

  utf8 c = ESCAPE_KEY;
  int goto_cb = 0;

  $my(vis)[0] = $my(vis)[1];

  if ($my(state) & BUF_LW_RESELECT) {
    $my(state) &= ~BUF_LW_RESELECT;
    if ($my(lw_vis_prev)[0].fidx isnot -1 and
        $my(lw_vis_prev)[0].lidx isnot -1 and
        $my(lw_vis_prev)[0].fidx < this->num_items and
        $my(lw_vis_prev)[0].lidx < this->num_items) {
      $my(vis)[0].fidx = $my(lw_vis_prev)[0].fidx;
      $my(vis)[0].lidx = $my(lw_vis_prev)[0].lidx;
      buf_normal_goto_linenr (this, $my(lw_vis_prev)[0].fidx + 1, DONOT_DRAW);
      state_set (&mark); mark.cur_idx = this->cur_idx;
      self(normal.goto_linenr, $my(lw_vis_prev)[0].lidx + 1, DONOT_DRAW);
    }
  }

  char vis_action[MAXLEN_WORD_ACTION];
  vis_action[0] = '\0';

  for (;;) {
    $my(vis)[0].lidx = this->cur_idx;
    dbuf (this);

    c = Input.get ($my(term_ptr));

handle_char:
    switch (c) {
      case '\t':
        c = buf_visual_complete_actions (this, vis_action);
        goto handle_char;

      VIS_HNDL_CASE_REG(reg);
      VIS_HNDL_CASE_INT(count);

      case ARROW_DOWN_KEY:
        self(normal.down, 1, ADJUST_COL, DONOT_DRAW);
        continue;

      case ARROW_UP_KEY:
        self(normal.up, 1, ADJUST_COL, DONOT_DRAW);
        continue;

      case PAGE_DOWN_KEY:
        self(normal.page_down, 1, DRAW);
        continue;

      case PAGE_UP_KEY:
        self(normal.page_up, 1, DRAW);
        continue;

      case ESCAPE_KEY:
        VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        goto theend;

      case HOME_KEY:
        self(normal.bof, DONOT_DRAW);
        continue;

      case 'G':
      case END_KEY:
        self(normal.eof, DONOT_DRAW);
        continue;

      case '>':
      case '<':
        if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        } else {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
          self(current.set, $my(vis)[0].fidx);
          this->cur_idx = $my(vis)[0].fidx;
        }

        {
          Action_t *Action = self(Action.new);

          for (int i = $my(vis)[0].fidx; i <= $my(vis)[0].lidx; i++) {
            if (DONE is self(indent, count, c)) {
              Action_t *Laction = self(undo.pop);
              action_t *action = stack_pop (Laction, action_t);
              stack_push (Action, action);
              free (Laction);
            }

            ifnot (this->current is this->tail) {
              this->current = this->current->next;
              this->cur_idx++;
            }
          }

          self(undo.push, Action);

          VISUAL_RESTORE_STATE ($my(vis)[1], mark);
          if (c is '<' and $mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes - 1) {
            self(normal.noblnk);
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
          self(current.set, $my(vis)[0].fidx);
          this->cur_idx = $my(vis)[0].fidx;
        }

        if (-1 is reg or c is 'Y') reg = (c is 'Y' ? REG_STAR : REG_UNNAMED);

        if (c is 'd')
          self(delete.line, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);
        else {
          self(normal.Yank, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);
          VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        }

        goto theend;

      case 's':
        VISUAL_ADJUST_IDXS($my(vis)[0]);
        {
          rline_t *rl = Ed.rline.new ($my(root));
          string_t *str = String.new_with_fmt ("substitute --range=%d,%d --global -i --pat=",
              $my(vis)[0].fidx + 1, $my(vis)[0].lidx + 1);
          BYTES_TO_RLINE (rl, str->bytes, (int) str->num_bytes);
          Rline.write_and_break (rl);
          buf_rline (&this, rl);
          String.free (str);
        }
        goto theend;

      case 'w':
        VISUAL_ADJUST_IDXS($my(vis)[0]);
        {
          rline_t *rl = Ed.rline.new ($my(root));
          string_t *str = String.new_with_fmt ("write --range=%d,%d ",
              $my(vis)[0].fidx + 1, $my(vis)[0].lidx + 1);
          BYTES_TO_RLINE (rl, str->bytes, (int) str->num_bytes);
          Rline.write_and_break (rl);
          buf_rline (&this, rl);
          String.free (str);
        }
        goto theend;

      case '+':
      case '*':
        if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        } else {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
          self(current.set, $my(vis)[0].fidx);
          this->cur_idx = $my(vis)[0].fidx;
        }

        {
          row_t *row = this->current;
          Vstring_t *rows = vstring_new ();
          for (int ii = $my(vis)[0].fidx; ii <= $my(vis)[0].lidx; ii++) {
            Vstring.current.append_with (rows, row->data->bytes);
            row = row->next;
          }
          string_t *str = Vstring.join (rows, "\n");
          ed_selection_to_X ($my(root), str->bytes, str->num_bytes,
              ('*' is c ? X_PRIMARY : X_CLIPBOARD));
          String.free (str);
          Vstring.free (rows);
          goto theend;
        }

      case REG_SHARED_CHR:
        if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        } else {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
          self(current.set, $my(vis)[0].fidx);
          this->cur_idx = $my(vis)[0].fidx;
        }

        {
          ed_reg_new ($my(root), REG_SHARED);
          row_t *row = this->current;

          for (int i = $my(vis)[0].fidx; i <= $my(vis)[0].lidx; i++) {
            ed_reg_push_with ($my(root), REG_SHARED, LINEWISE,
                row->data->bytes, REVERSE_ORDER);
            row = row->next;
          }
        }
        goto theend;

      default:
        for (int i = 0; i < $myroots(lw_mode_chars_len); i++)
          if (c is $myroots(lw_mode_chars)[i]) {
            if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
              VISUAL_RESTORE_STATE ($my(vis)[0], mark);
            } else {
              VISUAL_ADJUST_IDXS($my(vis)[0]);
              self(current.set, $my(vis)[0].fidx);
              this->cur_idx = $my(vis)[0].fidx;
            }

            goto_cb = 1;
            goto theend;

            callback:;

            row_t *row = this->current;
            Vstring_t *rows = Vstring.new ();
            for (int ii = $my(vis)[0].fidx; ii <= $my(vis)[0].lidx; ii++) {
              Vstring.current.append_with (rows, row->data->bytes);
              row = row->next;
            }

            for (int j = 0; j < $myroots(num_lw_mode_cbs); j++) {
              int retval = $myroots(lw_mode_cbs)[j] (thisp,
            	  $my(vis)[0].fidx, $my(vis)[0].lidx, rows, c, vis_action);
              if (retval isnot NO_CALLBACK_FUNCTION)
                break;
            }

            Vstring.free (rows);
            goto thereturn;
          }

        continue;
    }
  }

theend:
  $my(syn)->parse = $my(vis)[0].orig_syn_parser;
  $self(draw) = dbuf;
  $self(draw_current_row) = drow;
  self(set.mode, prev_mode);
  self(draw);

  if (goto_cb) goto callback;

thereturn:
  if (c isnot ESCAPE_KEY) {
    $my(lw_vis_prev)[0].fidx = $my(vis)[0].fidx;
    $my(lw_vis_prev)[0].lidx = $my(vis)[0].lidx;
  }

  return DONE;
}

private char *buf_syn_parse_visual_line (buf_t *this, char *line, int len, row_t *currow) {
  ifnot (len) return line;

  int fidx = $my(vis)[0].fidx;
  int lidx = $my(vis)[0].lidx;
  if (fidx > lidx)
    {int t = fidx; fidx = lidx; lidx = t;}

  Ustring.encode ($my(line), line, len,
      CLEAR, $my(ftype)->tabwidth, currow->first_col_idx);

  ustring_t *it = $my(line)->current;

  String.replace_with ($my(shared_str), TERM_LINE_CLR_EOL);

  int num = 0;
  int idx = currow->first_col_idx;

  if (idx > fidx)
    String.append_fmt ($my(shared_str), "%s", TERM_MAKE_COLOR(HL_VISUAL));

  while (num < $my(dim)->num_cols and it) {
    if (idx is fidx)
      String.append_fmt ($my(shared_str), "%s", TERM_MAKE_COLOR(HL_VISUAL));

    if (it->buf[0] is '\t') goto handle_tab;

    num += it->width; // (artifact) if a character occupies > 1 width and is the last
    // char, the code will do the wrong thing and probably will mess up the screen

    String.append ($my(shared_str), it->buf);

    goto next;

handle_tab:
    for (int i = 0; i < $my(ftype)->tabwidth and num < $my(dim)->num_cols; i++) {
      num++;
      String.append_byte ($my(shared_str), ' ');
    }

next:
    if (idx is lidx)
      String.append ($my(shared_str), TERM_COLOR_RESET);
    idx += it->len;
    it = it->next;
  }

  Cstring.cp (line, MAXLEN_LINE, $my(shared_str)->bytes, $my(shared_str)->num_bytes);
  return $my(shared_str)->bytes;
}

private char *buf_syn_parse_visual_cw (buf_t *this, char *line, int len, int idx, row_t *row) {
  (void) idx;
  return buf_syn_parse_visual_line (this, line, len, row);
}

private int buf_normal_visual_cw (buf_t **thisp) {
  buf_t *this = *thisp;

  VISUAL_INIT_FUN (VISUAL_MODE_CW, buf_syn_parse_visual_cw);

  $my(vis)[1] = $my(vis)[0];
  $my(vis)[0] = (vis_t) {.fidx = $mycur(cur_col_idx), .lidx = $mycur(cur_col_idx),
     .orig_syn_parser = $my(vis)[1].orig_syn_parser};

  int goto_cb = 0;
  char vis_action[MAXLEN_WORD_ACTION];
  vis_action[0] = '\0';

  for (;;) {
    $my(vis)[0].lidx = $mycur(cur_col_idx);
    drow (this);
    utf8 c = Input.get ($my(term_ptr));

handle_char:
    switch (c) {
      case '\t':
        c = buf_visual_complete_actions (this, vis_action);
        goto handle_char;

      VIS_HNDL_CASE_REG(reg);
      VIS_HNDL_CASE_INT(count);

      case ARROW_LEFT_KEY:
        self(normal.left, 1, DONOT_DRAW);
        continue;

      case ARROW_RIGHT_KEY:
        self(normal.right, 1, DONOT_DRAW);
        continue;

      case ESCAPE_KEY:
        goto theend;

      case HOME_KEY:
        self(normal.bol, DRAW);
        continue;

      case END_KEY:
        self(normal.eol, DRAW);
        continue;

      case 'e':
        VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        {
          int len = $my(vis)[0].lidx - $my(vis)[0].fidx + 1;
          char fname[len + 1];
          Cstring.cp (fname, len + 1, $mycur(data)->bytes + $my(vis)[0].fidx, len);
          ifnot (File.exists (fname)) goto theend;
          win_edit_fname ($my(parent), thisp, fname, $myparents(cur_frame), 0, 0, 0);
        }

        goto theend;

      case 'd':
      case 'x':
      case 'y':
      case 'Y':
        if (-1 is reg or c is 'Y') reg = (c is 'Y' ? REG_STAR : REG_UNNAMED);

        if ($my(vis)[0].lidx < $my(vis)[0].fidx) {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
        } else {   /* MACRO BLOCKS ARE EVIL */
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        }

        if (c is 'd' or c is 'x')
          self(normal.delete, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg, DONOT_DRAW);
        else
          self(normal.yank, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);

        goto theend;

      case '+':
      case '*':
        if ($my(vis)[0].lidx < $my(vis)[0].fidx) {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
        } else {   /* MACRO BLOCKS ARE EVIL */
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        }

        {
          string_t *str = String.new (($my(vis)[0].lidx - $my(vis)[0].fidx) + 2);
          for (int ii = $my(vis)[0].fidx; ii <= $my(vis)[0].lidx; ii++)
            String.append_byte (str, $mycur(data)->bytes[ii]);
          ed_selection_to_X ($my(root), str->bytes, str->num_bytes,
              ('*' is c ? X_PRIMARY : X_CLIPBOARD));
          String.free (str);
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

            string_t *str = String.new (($my(vis)[0].lidx - $my(vis)[0].fidx) + 2);
            for (int ii = $my(vis)[0].fidx; ii <= $my(vis)[0].lidx; ii++)
              String.append_byte (str, $mycur(data)->bytes[ii]);

            for (int j = 0; j < $myroots(num_cw_mode_cbs); j++) {
              int retval = $myroots(cw_mode_cbs)[j] (thisp,
                  $my(vis)[0].fidx, $my(vis)[0].lidx, str, c, vis_action);
              if (retval isnot NO_CALLBACK_FUNCTION)
                break;
            }

            String.free (str);
            goto thereturn;
          }

        continue;

    }
  }

theend:
  $my(syn)->parse = $my(vis)[0].orig_syn_parser;
  $self(draw) = dbuf;
  $self(draw_current_row) = drow;
  self(set.mode, prev_mode);
  this = *thisp;
  self(draw);

  if (goto_cb) goto callback;

thereturn:
  return DONE;
}

private char *buf_syn_parse_visual_bw (buf_t *this, char *line, int len, int idx, row_t *row) {
  (void) len;

  if ((idx is $my(vis)[1].fidx) or
      (idx > $my(vis)[1].fidx and $my(vis)[1].fidx < $my(vis)[1].lidx and
       idx <= $my(vis)[1].lidx) or
      (idx < $my(vis)[1].fidx and $my(vis)[1].fidx > $my(vis)[1].lidx and
       idx >= $my(vis)[1].lidx) or
      (idx > $my(vis)[1].lidx and $my(vis)[1].lidx < $my(vis)[1].fidx and
       idx < $my(vis)[1].fidx)) {
    return buf_syn_parse_visual_line (this, line, len, row);
  }

  return $my(vis)[0].orig_syn_parser (this, line, len, idx, row);
}


private int buf_normal_visual_bw (buf_t *this) {
  VISUAL_INIT_FUN (VISUAL_MODE_BW, buf_syn_parse_visual_bw);

  char vis_action[MAXLEN_WORD_ACTION];
  vis_action[0] = '\0';

  for (;;) {
    $my(vis)[1].lidx = this->cur_idx;
    $my(vis)[0].lidx = $mycur(cur_col_idx);
    dbuf (this);

    utf8 c = Input.get ($my(term_ptr));

handle_char:
    switch (c) {
      case '\t':
        c = buf_visual_complete_actions (this, vis_action);
        goto handle_char;

      VIS_HNDL_CASE_REG(reg);
      VIS_HNDL_CASE_INT(count);

      case ARROW_DOWN_KEY:
        self(normal.down, 1, ADJUST_COL, DONOT_DRAW);
        continue;

      case ARROW_UP_KEY:
        self(normal.up, 1, ADJUST_COL, DONOT_DRAW);
        continue;

      case PAGE_DOWN_KEY:
        self(normal.page_down, 1, DRAW);
        continue;

      case PAGE_UP_KEY:
        self(normal.page_up, 1, DRAW);
        continue;

      case ESCAPE_KEY:
        VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        goto theend;

      case HOME_KEY:
        self(normal.bof, DONOT_DRAW);
        continue;

      case 'G':
      case END_KEY:
        self(normal.eof, DONOT_DRAW);
        continue;

      case ARROW_LEFT_KEY:
        self(normal.left, 1, DONOT_DRAW);
        continue;

      case ARROW_RIGHT_KEY:
        self(normal.right, 1, DONOT_DRAW);
        continue;

      case 'i':
      case 'I':
      case 'c':
        {
          int row = $my(cur_video_row) - (2 < $my(cur_video_row));
          int index = $my(vis)[1].lidx;
          if (index > $my(vis)[1].fidx)
            while (row > 2 and index > $my(vis)[1].fidx) {
              row--; index--;
            }

          VISUAL_ADJUST_IDXS($my(vis)[1]);
          string_t *str = self(input_box, row, $my(vis)[0].fidx + 1,
              DONOT_ABORT_ON_ESCAPE, NULL);

          Action_t *Action = self(Action.new);
          Action_t *Baction =self(Action.new);

          for (int idx = $my(vis)[1].fidx; idx <= $my(vis)[1].lidx; idx++) {
            self(current.set, idx);
            self(adjust.view);
            VISUAL_ADJUST_COL ($my(vis)[0].fidx);

            if (c is 'c') {
              if ((int) $mycur(data)->num_bytes < $my(vis)[0].fidx)
                continue;
              else
                self(normal.delete, $my(vis)[0].lidx - $my(vis)[0].fidx + 1,
                    REG_BLACKHOLE, DONOT_DRAW);

              Action_t *Paction = self(undo.pop);
              action_t *action = stack_pop (Paction, action_t);
              stack_push (Baction, action);
              free (Paction);
            } else {
              action_t *action = self(action.new_with, REPLACE_LINE, this->cur_idx,
                  $mycur(data)->bytes, $mycur(data)->num_bytes);
              stack_push (Baction, action);
            }

            String.insert_at ($mycur(data), str->bytes, $my(vis)[0].fidx);
            $my(flags) |= BUF_IS_MODIFIED;
          }

          String.free (str);

          action_t *baction = stack_pop (Baction, action_t);
          while (baction isnot NULL) {
            stack_push (Action, baction);
            baction = stack_pop (Baction, action_t);
          }

          free (Baction);
          self(undo.push, Action);
        }

        VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        goto theend;

      case 'x':
      case 'd':
        if (-1 is reg) reg = REG_UNNAMED;
        {
          Action_t *Action = self(Action.new);
          Action_t *Baction =self(Action.new);

          VISUAL_ADJUST_IDXS($my(vis)[0]);
          VISUAL_ADJUST_IDXS($my(vis)[1]);

          for (int idx = $my(vis)[1].fidx; idx <= $my(vis)[1].lidx; idx++) {
            self(current.set, idx);
            self(adjust.view);
            VISUAL_ADJUST_COL ($my(vis)[0].fidx);
            if ((int) $mycur(data)->num_bytes < $my(vis)[0].fidx + 1) continue;
            int lidx__ = (int) $mycur(data)->num_bytes < $my(vis)[0].lidx ?
              (int) $mycur(data)->num_bytes : $my(vis)[0].lidx;
            self(normal.delete, lidx__ - $my(vis)[0].fidx + 1, reg, DONOT_DRAW);
            Action_t *Paction = self(undo.pop);
            action_t *action = stack_pop (Paction, action_t);

            stack_push (Baction, action);
            free (Paction);
            $my(flags) |= BUF_IS_MODIFIED;
          }

          action_t *action = stack_pop (Baction, action_t);
          while (action isnot NULL) {
            stack_push (Action, action);
            action = stack_pop (Baction, action_t);
          }

          free (Baction);
          self(undo.push, Action);
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
  $self(draw_current_row) = drow;
  self(set.mode, prev_mode);
  self(draw);

  return DONE;
}

private int win_edit_fname (win_t *win, buf_t **thisp, char *fname, int frame,
                                             int reload, int draw, int reopen) {
  buf_t *this = *thisp;
  if (fname is NULL and 0 is reload) return NOTHING_TODO;

  if (fname isnot NULL) {
    ifnot (reopen) {
      int idx;
      buf_t *bn = Win.get.buf_by_name (win, fname, &idx);
      ifnot (NULL is bn) {
        *thisp = Win.set.current_buf (win, idx, DRAW);
        return DONE;
      }
    }

    if ($my(at_frame) is frame) $my(flags) &= ~BUF_IS_VISIBLE;

    buf_t *that = Win.buf.new (win, BufOpts (
        .fname = fname,
        .at_frame = frame));

    current_list_set (that, 0);

    int cur_idx = win->cur_idx;
    int idx = Win.append_buf (win, that);
    current_list_set (win, cur_idx);
    Win.set.current_buf (win, idx, (draw ? DRAW : DONOT_DRAW));

    *thisp = that;
    this = that;

    if (draw) self(draw);
    goto theend;
  }

  int cur_idx = this->cur_idx;
  self(normal.bof, DONOT_DRAW);
  self(delete.line, this->num_items, REG_BLACKHOLE);
  self(read.fname);

  self(normal.bof, DONOT_DRAW);
  Action_t *action = self(undo.pop);

  for (;;) {
    action_t *act = self(action.new);
    undo_set (act, INSERT_LINE);
    act->idx = this->cur_idx;
    act->bytes = Cstring.dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
    stack_push (action, act);
    if (NOTHING_TODO is buf_normal_down (this, 1, DONOT_ADJUST_COL, 0)) break;
  }

  self(current.set, 0);
  self(delete.line, 1, REG_BLACKHOLE);
  Action_t *baction = self(undo.pop);
  action_t *act = self(action.new);
  state_cp (act, baction->head);
  self(Action.free, baction);

  stack_push (action, act);
  self(undo.push, action);

  if (this->num_items is 0) buf_on_no_length (this);

  $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;
  $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
  $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
  $my(video_first_row) = this->current;
  $my(video_first_row_idx) = this->cur_idx;

  if (cur_idx >= this->num_items) cur_idx = this->num_items - 1;
  self(normal.goto_linenr, cur_idx + 1, draw);

  if (draw) self(draw);
  MSG("reloaded");
  return DONE;

theend:
  return DONE;
}

private char *buf_get_current_dir (buf_t *this, int new_allocation) {
  char *cwd = Dir.current ();
  if (NULL is cwd) {
    MSG_ERRNO (MSG_CAN_NOT_DETERMINATE_CURRENT_DIR);
    free (cwd);
    return NULL;
  }

  if (new_allocation) return cwd;

  String.replace_with ($my(shared_str), cwd);
  free (cwd);
  /* dangerous because it is used in so many cases but effective, */
  return $my(shared_str)->bytes;
  /* as cut a lot of alloc's, this is like a static string, with local scope, it
   * doesn't really mind, as long is under control and do not used at the same time */
}

private int buf_open_fname_under_cursor (buf_t **thisp, int frame,
                             int force_open, int reopen, int draw) {
  buf_t *this = *thisp;

  char fname[PATH_MAX];
  int fidx, lidx;

  if (NULL is buf_get_current_word (this, fname, Notfname, Notfname_len, &fidx, &lidx))
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

  ifnot (File.exists (fname))
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
        char tmp[PATH_MAX + 1];
        Path.real (nfn, tmp);
        bn = Win.get.buf_by_name ($my(parent), tmp,  &idx);
      } else
        bn = Win.get.buf_by_name ($my(parent), fname, &idx);

      if (NULL is bn) break;

      *thisp = Win.set.current_buf ($my(parent), idx, DONOT_DRAW);
      this = *thisp;

      if (NOTHING_TODO is self(normal.goto_linenr, lnr, draw))
        if (draw) self(draw);

      return DONE;
    } while (0);

  if (frame is AT_CURRENT_FRAME) frame = $myparents(cur_frame);

  if (NOTHING_TODO is Win.edit_fname ($my(parent), thisp, fname, frame, 0,
  	DONOT_DRAW, 1))
    return NOTHING_TODO;

  this = *thisp;

  if (NOTHING_TODO is self(normal.goto_linenr, lnr, draw))
    if (draw) self(draw);

  return DONE;
}

private int buf_split (buf_t **thisp, char *fname) {
  buf_t *this = *thisp;
  buf_t *that = this;

  if (win_add_frame ($my(parent)) is NOTHING_TODO)
    return NOTHING_TODO;

  win_edit_fname ($my(parent), thisp, fname, $myparents(num_frames) - 1, 1, 0, 1);

  $myparents(cur_frame) = $myparents(num_frames) - 1;

  $from(that, flags) |= BUF_IS_VISIBLE;

  Win.draw ($my(parent));
  return DONE;
}

private int buf_enew_fname (buf_t **thisp, char *fname) {
  buf_t *this = *thisp;
  win_t *w = Ed.win.new ($my(root), NULL, 1);
  $my(root)->prev_idx = $my(root)->cur_idx;
  Ed.append.win ($my(root), w);
  win_edit_fname (w, thisp, fname, 0, 1, 1, 1);
  return DONE;
}

private int ed_win_change (ed_t *this, buf_t **bufp, int com, char *name,
                                            int accept_rdonly, int force) {
  if (this->num_items is 1)
    ifnot (force) return NOTHING_TODO;

  int idx = this->cur_idx;
  int cidx = idx;

  ifnot (NULL is name) {
    if (Cstring.eq ($from($from((*bufp), parent), name), name))
      return NOTHING_TODO;
    win_t *w = self(get.win_by_name, name, &idx);
    if (NULL is w) return NOTHING_TODO;
  } else {
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
      int tmp_idx = cidx;
      for (;;) {
        win_t *w = self(get.win_by_idx, idx);
        if (idx is tmp_idx or w is NULL)
          return NOTHING_TODO;

        if (win_isit_special_type (w)) {
          if (com is VED_COM_WIN_CHANGE_PREV) {
            if (idx is 0) idx = this->num_items - 1;
            else idx--;
          } else {
            if (idx is this->num_items - 1) idx = 0;
            else idx++;
          }
        } else break;
      }
    }
  }

  self(set.current_win, idx);
  win_t *parent = this->current;
  *bufp = parent->current;

  $from((*bufp), parent) = parent;
  *bufp = Win.set.current_buf (parent, parent->cur_idx, DONOT_DRAW);
  Win.set.video_dividers (parent);
  Win.draw (parent);
  return DONE;
}

private int ed_win_delete (ed_t *this, buf_t **thisp, int count_special) {
  if (1 is self(get.num_win, count_special))
    return EXIT_THIS;

  win_t *parent = current_list_pop (this, win_t);
  win_free (parent);

  parent = this->current;

  *thisp = Win.set.current_buf (parent, parent->cur_idx, DONOT_DRAW);

  Win.set.video_dividers (parent);
  Win.draw (parent);
  return DONE;
}

private int buf_write_to_fname (buf_t *this, char *fname, int append, int fidx,
                                            int lidx, int force, int verbose) {
  if (NULL is fname) return NOTHING_TODO;
  int retval = NOTHING_TODO;

  string_t *fnstr = String.new_with (fname);
  ifnot (fnstr->num_bytes) goto theend;

  if (fnstr->bytes[fnstr->num_bytes - 1] is '%' and (NULL isnot $my(basename))) {
    String.clear_at(fnstr, fnstr->num_bytes - 1);
    String.append (fnstr, $my(basename));
  }

  int fexists = File.exists (fnstr->bytes);
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
  String.free (fnstr);
  return retval;
}

private int buf_write (buf_t *this, int force) {
  if (Cstring.eq ($my(fname), UNNAMED)) {
    VED_MSG_ERROR(MSG_CAN_NOT_WRITE_AN_UNNAMED_BUFFER);
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
      utf8 c = buf_quest (this, STR_FMT (
          "[Warning]\n%s: removed from filesystem since last operation\n"
          "continue writing? [yY|nN]", $my(fname)), chars, ARRLEN (chars));
      switch (c) {case 'n': case 'N': return NOTHING_TODO;};
    } else {
      if (2 isnot force) {
  #if defined(__MACH__)
        if ($my(st).st_mtimespec.tv_sec isnot st.st_mtimespec.tv_sec) {
  #else
        if ($my(st).st_mtim.tv_sec isnot st.st_mtim.tv_sec) {
  #endif
          utf8 chars[] = {'y', 'Y', 'n', 'N'};
          utf8 c = buf_quest (this, STR_FMT (
              "[Warning]%s: has been modified since last operation\n"
              "continue writing? [yY|nN]", $my(fname)), chars, ARRLEN (chars));
          switch (c) {case 'n': case 'N': return NOTHING_TODO;};
        }
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

private int buf_read_from_fp (buf_t *this, FILE *stream, fp_t *fp) {
  (void) stream;
  mark_t t;  state_set (&t);  t.cur_idx = this->cur_idx;
  row_t *row = this->current;
  Action_t *Action = self(Action.new);

  char *line = NULL;
  size_t len = 0;
  size_t t_len = 0;
  ssize_t nread;
  while (-1 isnot (nread = ed_readline_from_fp (&line, &len, fp->fp))) {
    t_len += nread;
    action_t *action = self(action.new);
    undo_set (action, INSERT_LINE);
    self(current.append_with, line);
    action->idx = this->cur_idx;
    stack_push (Action, action);
  }

  ifnot (NULL is line) free (line);

  ifnot (t_len)
    free (Action);
  else
    self(undo.push, Action);

  $my(flags) |= BUF_IS_MODIFIED;
  state_restore (&t);
  this->current = row; this->cur_idx = t.cur_idx;
  self(draw);
  return DONE;
}

private int buf_read_from_file (buf_t *this, char *fname) {
  if (0 isnot access (fname, R_OK|F_OK)) return NOTHING_TODO;
  ifnot (File.is_reg (fname)) return NOTHING_TODO;

  fp_t fp = (fp_t) {.fp = fopen (fname, "r")};
  if (NULL is fp.fp) {
    MSG_ERRNO(errno);
    return NOTHING_TODO;
  }

  int retval = self(read.from_fp, NULL, &fp);
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

  char *alpath = cstring_dup (path, p_len);
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

private int buf_read_from_shell (buf_t *this, char *com, int rlcom) {
  ifnot ($my(ftype)->read_from_shell) return NOTHING_TODO;
  return Ed.sh.popen ($my(root), this, com, rlcom is VED_COM_READ_SHELL, 0, NULL);
}

private int buf_change_bufname (buf_t **thisp, char *bufname) {
  buf_t *this = *thisp;
  if (cstring_eq ($my(fname), bufname)) return NOTHING_TODO;
  int idx;
  buf_t *buf = Win.get.buf_by_name ($my(parent), bufname, &idx);
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

  *thisp = Win.set.current_buf ($my(parent), idx, DONOT_DRAW);
  this = *thisp;
  $my(video)->row_pos = $my(cur_video_row);
  Win.draw ($my(parent));
  return DONE;
}

private int buf_change (buf_t **thisp, int com) {
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
  *thisp = Win.set.current_buf ($my(parent), idx, DRAW);
  return DONE;
}

private void buf_clear (buf_t *this) {
  self(free.rows);
  self(undo.clear);
  this->head = this->tail = this->current = NULL;
  this->cur_idx = 0; this->num_items = 0;
  buf_on_no_length (this);

  $my(video_first_row) = this->head;
  $my(video_first_row_idx) = 0;

  self(current.set, 0);
  $mycur(cur_col_idx) = $mycur(first_col_idx) = 0;
  $my(cur_video_row) = $my(dim)->first_row;
  $my(cur_video_col) = 1;
}

private int buf_delete (buf_t **thisp, int idx, int force) {
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
    Win.pop.current_buf (parent);
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

    Win.pop.current_buf (parent);
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

  *thisp = Win.set.current_buf (parent, parent->cur_idx, DONOT_DRAW);

  if (cur_idx is idx) {
    if (found is 1 or $from(parent, num_frames) is 1) {
      if (NOTHING_TODO is buf_change (thisp, VED_COM_BUF_CHANGE_PREV_FOCUSED))
        should_draw = 1;
    } else {
      int frame = WIN_CUR_FRAME(parent) + 1;
      if (frame > WIN_LAST_FRAME(parent)) frame = FIRST_FRAME;
      *thisp = Win.frame.change ($my(parent), frame, DONOT_DRAW);
    }
  } else should_draw = 1;

  if (0 is found or should_draw) Win.draw (parent);

  return DONE;
}

private int ed_complete_digraph_callback (menu_t *menu) {
  buf_t *this = menu->this;
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  Vstring_t *items = Vstring.new ();
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
      if (Cstring.eq_n (digraphs[i], menu->pat, menu->patlen))
        Vstring.current.append_with (items, digraphs[i]);
    } else
      Vstring.current.append_with (items, digraphs[i]);

  menu->list = items;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private int buf_complete_digraph (buf_t *this, utf8 *c) {
  int retval = DONE;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
    $my(video)->col_pos, ed_complete_digraph_callback, NULL, 0);
  menu->next_key = CTRL('k');
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *item = menu_create ($my(root), menu);
  *c = 0;
  if (item isnot NULL)
    while (*item isnot ' ') *c = (10 * *c) + (*item++ - '0');

theend:
  menu_free (menu);
  return NEWCHAR;
}

private int ed_complete_arg (menu_t *menu) {
  buf_t *this = menu->this;

  int com = $my(shared_int);
  char *line = $my(shared_str)->bytes;

  if ($myroots(commands)[com]->args is NULL) {
    if (0 is $myroots(has_ed_rline_commands) or
        (com < VED_COM_BUF_DELETE_FORCE or
         com > VED_COM_BUF_CHANGE_ALIAS)) {
      menu->state |= MENU_QUIT;
      return NOTHING_TODO;
    }
  }

  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  Vstring_t *args = Vstring.new ();

  if ($myroots(has_ed_rline_commands)) {
    int patisopt = (menu->patlen ? Cstring.eq (menu->pat, "--bufname=") : 0);

    if ((com >= VED_COM_BUF_DELETE_FORCE and
         com <= VED_COM_BUF_CHANGE_ALIAS) or patisopt) {
      char *cur_fname = $my(fname);

      buf_t *it = $my(parent)->head;
      while (it) {
        ifnot (Cstring.eq (cur_fname, $from(it, fname))) {
          if ((0 is menu->patlen or 1 is patisopt) or
               Cstring.eq_n ($from(it, fname), menu->pat, menu->patlen)) {
            ifnot (patisopt) {
              size_t len = bytelen ($from(it, fname)) + 10 + 2;
              char bufn[len + 1];
              snprintf (bufn, len + 1, "--bufname=\"%s\"", $from(it, fname));
              Vstring.add.sort_and_uniq (args, bufn);
            } else
              Vstring.add.sort_and_uniq (args, $from(it, fname));
          }
        }
        it = it->next;
      }

      goto check_list;
    }
  }

  int i = 0;

  ifnot (menu->patlen) {
    while ($myroots(commands)[com]->args[i])
      Vstring.add.sort_and_uniq (args, $myroots(commands)[com]->args[i++]);
  } else {
    while ($myroots(commands)[com]->args[i]) {
      if (Cstring.eq_n ($myroots(commands)[com]->args[i], menu->pat, menu->patlen))
        if (NULL is strstr (line, $myroots(commands)[com]->args[i]) or
        	Cstring.eq ($myroots(commands)[com]->args[i], "--fname=") or
        	Cstring.eq_n ($myroots(commands)[com]->args[i], "--command=", 10))
          Vstring.add.sort_and_uniq (args, $myroots(commands)[com]->args[i]);
      i++;
    }
  }

check_list:
  ifnot (args->num_items) {
    menu->state |= MENU_QUIT;
    Vstring.free (args);
    return NOTHING_TODO;
  }

  menu->list = args;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  String.replace_with (menu->header, menu->pat);

  return DONE;
}

private int ed_complete_command (menu_t *menu) {
  buf_t *this = menu->this;

  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  Vstring_t *coms = Vstring.new ();
  int i = 0;

  ifnot (menu->patlen) {
    while ($myroots(commands)[i])
      Vstring.add.sort_and_uniq (coms, $myroots(commands)[i++]->com);
  } else {
    while ($myroots(commands)[i]) {
      ifnot (Cstring.cmp_n ($myroots(commands)[i]->com, menu->pat, menu->patlen))
        Vstring.add.sort_and_uniq (coms, $myroots(commands)[i]->com);
      i++;
    }
  }

  ifnot (coms->num_items) {
    menu->state |= MENU_QUIT;
    Vstring.free (coms);
    return NOTHING_TODO;
  }

  menu->list = coms;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  String.replace_with (menu->header, menu->pat);

  return DONE;
}

private int ed_complete_filename (menu_t *menu) {
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
    char *cwd = Dir.current ();
    Cstring.cp (dir, PATH_MAX, cwd, PATH_MAX - 1);
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

    Cstring.cp (dir, PATH_MAX, $myroots(env)->home_dir->bytes, $myroots(env)->home_dir->num_bytes);

    joinpath = 1;
    goto getlist;
  }

  if (Dir.is_directory (menu->pat) and bytelen (Path.basename (menu->pat)) > 1) {
    Cstring.cp (dir, PATH_MAX, menu->pat, menu->patlen);
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
      char *cwd = Dir.current ();
      Cstring.cp (dir, PATH_MAX, cwd, PATH_MAX - 1);
      free (cwd);
      end = sp;
    }

    goto getlist;
  }

  if (*sp is DIR_SEP) {
    Cstring.cp (dir, PATH_MAX, menu->pat, menu->patlen);
    end = NULL;
    joinpath = 1;
    goto getlist;
  }

  while (sp > menu->pat and *(sp - 1) isnot DIR_SEP) sp--;
  if (sp is menu->pat) {
    end = sp;
    char *cwd = Dir.current ();
    Cstring.cp (dir, PATH_MAX, cwd, PATH_MAX - 1);
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
  dirlist_t *dlist = Dir.list (dir, 0);

  if (NULL is dlist) {
    menu->state |= MENU_QUIT;
    return NOTHING_TODO;
  }

  Vstring_t *vs = Vstring.new ();
  vstring_t *it = dlist->list->head;

  $my(shared_int) = joinpath;
  String.replace_with ($my(shared_str), dir);

  while (it) {
    if (end is NULL or (Cstring.eq_n (it->data->bytes, end, endlen))) {
      Vstring.add.sort_and_uniq (vs, it->data->bytes);
    }

    it = it->next;
  }

  dlist->free (dlist);

  menu->list = vs;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  String.replace_with (menu->header, menu->pat);

  return DONE;

finalize:
  menu->state &= ~MENU_FINALIZE;

  if ($my(shared_int)) {
    if ($my(shared_str)->bytes[$my(shared_str)->num_bytes - 1] is DIR_SEP)
      String.clear_at ($my(shared_str), $my(shared_str)->num_bytes - 1);

    int len = menu->patlen + $my(shared_str)->num_bytes + 1;
    char tmp[len + 1];
    snprintf (tmp, len + 1, "%s%c%s", $my(shared_str)->bytes, DIR_SEP, menu->pat);
    String.replace_with ($my(shared_str), tmp);
  } else
    String.replace_with ($my(shared_str), menu->pat);

  if (Dir.is_directory ($my(shared_str)->bytes))
    menu->state |= MENU_REDO;
  else
    menu->state |= MENU_DONE;

  return DONE;
}

private int buf_insert_complete_filename (buf_t **thisp) {
  buf_t *this = *thisp;
  int retval = DONE;
  int fidx = 0; int lidx = 0;
  size_t WORD_LEN = PATH_MAX + 1;
  char word[WORD_LEN]; word[0] = '\0';
  if (IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)]) and (
      $mycur(data)->num_bytes > 1 and 0 is IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx) - 1])))
    self(normal.left, 1, DRAW);

  self(get.current_word, word, Notfname, Notfname_len, &fidx, &lidx);
  size_t len = bytelen (word);
  size_t orig_len = len;

redo:;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
      $my(video)->col_pos,  ed_complete_filename, word, len);

  menu->next_key = CTRL('f');
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  for (;;) {
    char *item  = menu_create ($my(root), menu);
    if (NULL is item) { retval = NOTHING_TODO; goto theend; }

    menu->patlen = bytelen (item);
    Cstring.cp (menu->pat, MAXLEN_PAT, item, menu->patlen);
    menu->state |= MENU_FINALIZE;

    ed_complete_filename (menu);

    if (menu->state & MENU_DONE) break;

    len = $my(shared_str)->num_bytes;
    Cstring.cp (word, WORD_LEN, $my(shared_str)->bytes, $my(shared_str)->num_bytes);
    menu_free (menu);
    goto redo;
  }

  String.replace_numbytes_at_with ($mycur(data), orig_len, fidx,
      $my(shared_str)->bytes);

  $my(flags) |= BUF_IS_MODIFIED;
  self(normal.end_word, 1, 0, DONOT_DRAW);
  self(draw_current_row);

theend:
  menu_free (menu);
  return retval;
}

private void rline_clear (rline_t *rl) {
  ed_t *this = rl->ed;
  rl->state &= ~RL_CLEAR;
  int row = rl->first_row;
  while (row < rl->prompt_row)
    Video.draw.row_at (rl->cur_video, row++);

  if (rl->prompt_row is $from(rl->ed, prompt_row))
    Video.set.row_with (rl->cur_video, rl->prompt_row - 1, " ");

  Video.draw.row_at (rl->cur_video, rl->prompt_row);

  if (rl->state & RL_CLEAR_FREE_LINE) {
    Vstring.free (rl->line);
    rl->num_items = 0;
    rl->cur_idx = 0;
    rl->line = Vstring.new ();
    vstring_t *vstr = AllocType (vstring);
    vstr->data = String.new_with_len (" ", 1);
    current_list_append (rl->line, vstr);
  }

  rl->state &= ~RL_CLEAR_FREE_LINE;
}

private void rline_free_members (rline_t *rl) {
  ed_t *this = rl->ed;
  Vstring.free (rl->line);
  arg_t *it = rl->head;
  while (it isnot NULL) {
    arg_t *tmp = it->next;
    String.free (it->argname);
    String.free (it->argval);
    free (it);
    it = tmp;
  }
  rl->num_items = 0;
  rl->cur_idx = 0;
}

private void rline_release (rline_t *rl) {
  rline_free_members (rl);
  string_free (rl->render);
  free (rl);
}

private void rline_free (rline_t *rl) {
  rline_clear (rl);
  rline_release (rl);
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

private int rline_last_arg_at_beg (rline_t **rl) {
  switch ((*rl)->c) {
    case LAST_ARG_KEY:
    (*rl)->state |= RL_POST_PROCESS;
    return RL_POST_PROCESS;
  }

  (*rl)->state |= RL_OK;
  return RL_OK;
}

private rline_t *rline_complete_last_arg (rline_t *rl) {
  ed_t *this = rl->ed;
  ifnot ($my(rl_last_component)->num_items)
    return rl;

  $my(rl_last_component)->current = $my(rl_last_component)->head;

  rline_t *lrl = rline_new (this, $my(term), Input.get, $my(prompt_row),
      1, $my(dim)->num_cols, $my(video));

  lrl->at_beg = rline_last_arg_at_beg;
  lrl->at_end = rline_break;

  lrl->prompt_row = rl->first_row - 1;
  lrl->prompt_char = 0;

loop_again:
  if (lrl->line isnot NULL)
    Vstring.free (lrl->line);

  lrl->line = Vstring.dup (rl->line);
  BYTES_TO_RLINE (lrl, $my(rl_last_component)->current->data->bytes,
                 (int) $my(rl_last_component)->current->data->num_bytes);
  Rline.write_and_break (lrl);

get_char:;
  utf8 c = Rline.edit (lrl)->c;
  switch (c) {
    case ESCAPE_KEY:
      goto theend;

    case ' ':
    case '\r': goto thesuccess;
    case LAST_ARG_KEY:
      if ($my(rl_last_component)->current is $my(rl_last_component)->tail)
        goto theend;

      $my(rl_last_component)->current = $my(rl_last_component)->current->next;
      goto loop_again;

    default: goto get_char;
  }

thesuccess:
  Vstring.free (rl->line);
  rl->line = Vstring.dup (lrl->line);

theend:
  Rline.clear (lrl);
  rline_release (lrl);
  Video.draw.row_at (rl->cur_video, rl->first_row); // is minus one
  return rl;
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

  rline_t *lrl = rline_new (this, $my(term), Input.get, $my(prompt_row),
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
#define __free_strings__ String.free (str); String.free (cur)

  string_t *str = vstring_join (it->data->line, "");
  string_t *cur = vstring_join (rl->line, "");
  if (cur->bytes[cur->num_bytes - 1] is ' ')
    String.clear_at (cur, cur->num_bytes - 1);
  int match = (cstring_eq_n (str->bytes, cur->bytes, cur->num_bytes));

  if (0 is cur->num_bytes or $my(history)->rline->num_items is 1 or match) {
    __free_strings__;
    goto theinput;
  }

  __free_strings__;
  goto theiter;

theinput:
  rline_free_members (lrl);
  lrl->line = vstring_dup (it->data->line);
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
  rl->line = Vstring.dup (it->data->line);
  rl->first_row = it->data->first_row;
  rl->row_pos = it->data->row_pos;

theend:
  Rline.clear (lrl);
  Video.draw.row_at (rl->cur_video, rl->first_row); // is minus one
  rline_release (lrl);
  rl->at_beg = at_beg;
  rl->at_end = at_end;
  return rl;
}

private void rline_history_push (rline_t *rl) {
  ed_t *this = rl->ed;
  if ($my(max_num_hist_entries) < $my(history)->rline->num_items) {
    h_rlineitem_t *tmp = list_pop_tail ($my(history)->rline, h_rlineitem_t);
    rline_release (tmp->data);
    free (tmp);
  }

  h_rlineitem_t *hrl = AllocType (h_rlineitem);
  hrl->data = rl;
  current_list_prepend ($my(history)->rline, hrl);
}

private void rline_history_push_api (rline_t *rl) {
  rline_clear (rl);
  rl->state &= ~RL_CLEAR_FREE_LINE;
  rline_history_push (rl);
}

private void rline_last_component_push (rline_t *rl) {
  if (rl->tail is NULL) return;
  if (rl->tail->argval is NULL) return;

  ed_t *this = rl->ed;
  vstring_prepend_current_with ($my(rl_last_component), rl->tail->argval->bytes);

  if ($my(rl_last_component)->num_items > RLINE_LAST_COMPONENT_NUM_ENTRIES) {
    vstring_t *item = list_pop_tail ($my(rl_last_component), vstring_t);
    string_free (item->data);
    free (item);
  }
}

private vstring_t *rline_parse_command (rline_t *rl) {
  vstring_t *it = rl->line->head;
  char com[MAXLEN_COM]; com[0] = '\0';
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
    if (cstring_eq (rl->commands[i]->com, com)) {
      rl->com = i;
      break;
    }
  }

  return it;
}

private void rline_set_line (rline_t *rl, char *bytes, size_t len) {
  BYTES_TO_RLINE (rl, bytes, (int) len);
  rline_write_and_break (rl);
}

private string_t *rline_get_line (rline_t *rl) {
  return vstring_join (rl->line, "");
}

private int rline_get_state (rline_t *rl) {
  return rl->state;
}

private void *rline_get_user_object (rline_t *rl) {
  return rl->user_object;
}

private int rline_get_opts (rline_t *rl) {
  return rl->opts;
}

private string_t *rline_get_command (rline_t *rl) {
  string_t *str = string_new (8);
  vstring_t *it = rl->line->head;

  while (it isnot NULL and it->data->bytes[0] isnot ' ') {
    string_append (str, it->data->bytes);
    it = it->next;
  }

  return str;
}

private int rline_tab_completion (rline_t *rl) {
  ed_t *this = rl->ed;
  ifnot (rl->line->num_items) return RL_OK;
  int retval = RL_OK;
  buf_t *curbuf = this->current->current;

  string_t *currline = NULL;  // otherwise segfaults on certain conditions
redo:;
  currline = Vstring.join (rl->line, "");
  char *sp = currline->bytes + rl->line->cur_idx;
  char *cur = sp;

  while (sp > currline->bytes and *(sp - 1) isnot ' ') sp--;
  int fidx = sp - currline->bytes;
  size_t tok_stacklen = (cur - sp) + 1;
  char tok[tok_stacklen];
  int toklen = 0;
  while (sp < cur) tok[toklen++] = *sp++;
  tok[toklen] = '\0';

  int orig_len = toklen;
  int type = 0;

  if (fidx is 0) {
    type |= RL_TOK_COMMAND;
  } else {
    rline_parse_command (rl);

    $from(curbuf, shared_int) = rl->com;
    String.replace_with ($from(curbuf, shared_str), currline->bytes);

    if (rl->com isnot RL_NO_COMMAND) {
      if (Cstring.eq_n (tok, "--fname=", 8)) {
        type |= RL_TOK_ARG_FILENAME;
        int len = 8 + (tok[8] is '"');
        char tmp[toklen - len + 1];
        int i;
        for (i = len; i < toklen; i++) tmp[i-len] = tok[i];
        tmp[i-len] = '\0';
        Cstring.cp (tok, tok_stacklen, tmp, i-len);
        toklen = i-len;
      } else if (tok[0] is '-') {
        type |= RL_TOK_ARG;
        ifnot (NULL is Cstring.byte.in_str (tok, '='))
          type |= RL_TOK_ARG_OPTION;
      } else {
        if ($my(has_ed_rline_commands)) {
          if (rl->com >= VED_COM_BUF_DELETE_FORCE and
              rl->com <= VED_COM_BUF_CHANGE_ALIAS)
            type |= RL_TOK_ARG;
          else
            type |= RL_TOK_ARG_FILENAME;
        } else
          type |= RL_TOK_ARG_FILENAME;
      }
    }
  }

  String.free (currline);

  ifnot (type) return retval;

  int (*process_list) (menu_t *) = NULL;

  if (type & RL_TOK_ARG_FILENAME)
    process_list = ed_complete_filename;
  else if (type & RL_TOK_COMMAND)
    process_list = ed_complete_command;
  else if (type & RL_TOK_ARG)
    process_list = ed_complete_arg;

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
    if (type & RL_TOK_ARG and Cstring.eq_n ("--command=", item, 10))
       Msg.send (this, COLOR_WARNING, "--command argument should be enclosed in a pair of '{}' braces");

    if (type & RL_TOK_COMMAND or type & RL_TOK_ARG) break;

    menu->patlen = bytelen (item);
    Cstring.cp (menu->pat, MAXLEN_PAT, item, menu->patlen);

    if (type & RL_TOK_ARG_FILENAME) menu->state |= MENU_FINALIZE;

    if (menu->process_list (menu) is NOTHING_TODO) goto theend;

    if (menu->state & (MENU_REDO|MENU_DONE)) break;
    if (menu->state & MENU_QUIT) goto theend;
  }

  if (type & RL_TOK_ARG_FILENAME) {
    ifnot (menu->state & MENU_REDO)
      if (rl->com isnot VED_COM_READ_SHELL and rl->com isnot VED_COM_SHELL) {
        String.prepend ($from(curbuf, shared_str), "--fname=\"");
        String.append_byte ($from(curbuf, shared_str), '"');
      }

    item = $from(curbuf, shared_str)->bytes;
  }

  ifnot (type & RL_TOK_ARG_OPTION) {
    current_list_set (rl->line, fidx);
    int lidx = fidx + orig_len;
    while (fidx++ < lidx) {
      vstring_t *tmp = current_list_pop (rl->line, vstring_t);
      String.free (tmp->data);
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

private int rline_tab_completion_void (rline_t *rl) {
  (void) rl;
  return NOTHING_TODO;
}

private void ed_deinit_commands (ed_t *this) {
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

  free ($my(commands));
  $my(commands) = NULL;
  $my(num_commands) = 0;
  $my(has_ed_rline_commands) = 0;
}

private void ed_realloc_command_arg (rlcom_t *rlcom, int num) {
  int orig_num = rlcom->num_args;
  rlcom->num_args = num;
  rlcom->args = Realloc (rlcom->args, sizeof (char *) * (rlcom->num_args + 1));
  for (int i = orig_num; i <= num; i++)
    rlcom->args[i] = NULL;
}

private void ed_add_command_arg (rlcom_t *rlcom, int flags) {
#define ADD_ARG(arg, len, idx) ({                             \
  if (idx is rlcom->num_args)                                 \
    ed_realloc_command_arg (rlcom, idx);                     \
  rlcom->args[idx] = cstring_dup (arg, len);                      \
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

private void ed_append_command_arg (ed_t *this, char *com, char *argname, size_t len) {
  if (len <= 0) len = bytelen (argname);

  int i = 0;
  while (i < $my(num_commands)) {
    if (cstring_eq ($my(commands)[i]->com, com)) {
      int idx = 0;
      int found = 0;
      while (idx < $my(commands)[i]->num_args) {
        if (NULL is $my(commands)[i]->args[idx]) {
          $my(commands)[i]->args[idx] = cstring_dup (argname, len);
          found = 1;
          break;
        }
        idx++;
      }

      ifnot (found) {
        ed_realloc_command_arg ($my(commands)[i], $my(commands)[i]->num_args + 1);
        $my(commands)[i]->args[$my(commands)[i]->num_args - 1] = cstring_dup (argname, len);
      }

      return;
    }
    i++;
  }
}

private void ed_append_rline_commands (ed_t *this, char **commands,
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
    size_t clen = bytelen (commands[j]);
    $my(commands)[i]->com = Alloc (clen + 1);
    cstring_cp ($my(commands)[i]->com, clen + 1, commands[j], clen);

    ifnot (num_args[j]) {
      $my(commands)[i]->args = NULL;
      continue;
    }

    $my(commands)[i]->args = Alloc (sizeof (char *) * ((int) num_args[j] + 1));
    $my(commands)[i]->num_args = num_args[j];
    for (int k = 0; k <= num_args[j]; k++)
      $my(commands)[i]->args[k] = NULL;

    ed_add_command_arg ($my(commands)[i], flags[j]);
  }

  $my(commands)[len] = NULL;
  $my(num_commands) = len;
}

private void ed_append_rline_command (ed_t *this, char *name, int args, int flags) {
  char *commands[2] = {name, NULL};
  int largs[] = {args, 0};
  int lflags[] = {flags, 0};
  ed_append_rline_commands (this, commands, 1, largs, lflags);
}

private int ed_get_num_rline_commands (ed_t *this) {
  return $my(num_commands);
}

private void ed_init_commands (ed_t *this) {
  ifnot (NULL is $my(commands)) return;

  $my(has_ed_rline_commands) = 1;

  char *ed_commands[VED_COM_END + 1] = {
    [VED_COM_BUF_BACKUP] = "@bufbackup",
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
    [VED_COM_BUF_CHECK_BALANCED] = "@balanced_check",
    [VED_COM_BUF_SET] = "set",
    [VED_COM_DIFF_BUF] = "diffbuf",
    [VED_COM_DIFF] = "diff",
    [VED_COM_EDIT_FORCE] = "edit!",
    [VED_COM_EDIT_FORCE_ALIAS] = "e!",
    [VED_COM_EDIT] = "edit",
    [VED_COM_EDIT_ALIAS] = "e",
    [VED_COM_EDIT_IMAGE] = "@edit_image",
    [VED_COM_EDNEW] = "ednew",
    [VED_COM_ENEW] = "enew",
    [VED_COM_EDNEXT] = "ednext",
    [VED_COM_EDPREV] = "edprev",
    [VED_COM_EDPREV_FOCUSED] = "edprevfocused",
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
    [VED_COM_SAVE_IMAGE] = "@save_image",
    [VED_COM_TEST_KEY] = "testkey",
    [VED_COM_VALIDATE_UTF8] = "@validate_utf8",
    [VED_COM_WIN_CHANGE_NEXT] = "winnext",
    [VED_COM_WIN_CHANGE_NEXT_ALIAS] = "wn",
    [VED_COM_WIN_CHANGE_PREV_FOCUSED] = "winprevfocused",
    [VED_COM_WIN_CHANGE_PREV_FOCUSED_ALIAS] = "w`",
    [VED_COM_WIN_CHANGE_PREV] = "winprev",
    [VED_COM_WIN_CHANGE_PREV_ALIAS] = "wp",
    [VED_COM_WRITE_FORCE] = "write!",
    [VED_COM_WRITE_FORCE_ALIAS] = "w!",
    [VED_COM_WRITE_FORCE_FORCE] = "write!!",
    [VED_COM_WRITE_FORCE_FORCE_ALIAS] = "w!!",
    [VED_COM_WRITE] = "write",
    [VED_COM_WRITE_ALIAS] = "w",
    [VED_COM_WRITE_QUIT_FORCE] = "wq!",
    [VED_COM_WRITE_QUIT] = "wq",
    [VED_COM_END] = NULL
  };

  int num_args[VED_COM_END + 1] = {
    [VED_COM_BUF_DELETE_FORCE ... VED_COM_BUF_DELETE_ALIAS] = 1,
    [VED_COM_BUF_CHANGE ... VED_COM_BUF_CHANGE_ALIAS] = 1,
    [VED_COM_BUF_CHECK_BALANCED] = 1,
    [VED_COM_EDIT ... VED_COM_ENEW] = 1,
    [VED_COM_GREP] = 3,
    [VED_COM_QUIT_FORCE ... VED_COM_QUIT_ALIAS] = 1,
    [VED_COM_READ ... VED_COM_READ_ALIAS] = 1,
    [VED_COM_SPLIT] = 1,
    [VED_COM_SUBSTITUTE ... VED_COM_SUBSTITUTE_ALIAS] = 5,
    [VED_COM_SAVE_IMAGE] = 1,
    [VED_COM_VALIDATE_UTF8] = 1,
    [VED_COM_WRITE_FORCE ... VED_COM_WRITE_ALIAS] = 4,
    [VED_COM_WRITE_QUIT_FORCE ... VED_COM_WRITE_QUIT] = 1
  };

  int flags[VED_COM_END + 1] = {
    [VED_COM_BUF_DELETE_FORCE ... VED_COM_BUF_DELETE_ALIAS] = RL_ARG_BUFNAME,
    [VED_COM_BUF_CHANGE ... VED_COM_BUF_CHANGE_ALIAS] = RL_ARG_BUFNAME,
    [VED_COM_BUF_CHECK_BALANCED] = RL_ARG_RANGE,
    [VED_COM_EDIT ... VED_COM_ENEW] = RL_ARG_FILENAME,
    [VED_COM_GREP] = RL_ARG_FILENAME|RL_ARG_PATTERN|RL_ARG_RECURSIVE,
    [VED_COM_QUIT_FORCE ... VED_COM_QUIT_ALIAS] = RL_ARG_GLOBAL,
    [VED_COM_READ ... VED_COM_READ_ALIAS] = RL_ARG_FILENAME,
    [VED_COM_SPLIT] = RL_ARG_FILENAME,
    [VED_COM_SUBSTITUTE ... VED_COM_SUBSTITUTE_ALIAS] =
      RL_ARG_RANGE|RL_ARG_GLOBAL|RL_ARG_PATTERN|RL_ARG_SUB|RL_ARG_INTERACTIVE,
    [VED_COM_VALIDATE_UTF8] = RL_ARG_FILENAME,
    [VED_COM_WRITE_FORCE ... VED_COM_WRITE_ALIAS] =
      RL_ARG_FILENAME|RL_ARG_RANGE|RL_ARG_BUFNAME|RL_ARG_APPEND,
    [VED_COM_WRITE_QUIT_FORCE ... VED_COM_WRITE_QUIT] = RL_ARG_GLOBAL
  };

  $my(commands) = Alloc (sizeof (rlcom_t) * (VED_COM_END + 1));

  int i = 0;
  for (i = 0; i < VED_COM_END; i++) {
    $my(commands)[i] = AllocType (rlcom);
    size_t clen = bytelen (ed_commands[i]);
    $my(commands)[i]->com = Alloc (clen + 1);
    cstring_cp ($my(commands)[i]->com, clen + 1, ed_commands[i], clen);

    ifnot (num_args[i]) {
      $my(commands)[i]->args = NULL;
      continue;
    }

    $my(commands)[i]->args = Alloc (sizeof (char *) * (num_args[i] + 1));
    $my(commands)[i]->num_args = num_args[i];
    for (int j = 0; j <= num_args[i]; j++)
      $my(commands)[i]->args[j] = NULL;

    ed_add_command_arg ($my(commands)[i], flags[i]);
  }

  $my(commands)[i] = NULL;
  $my(num_commands) = VED_COM_END;

  ed_append_command_arg (this, "set", "--persistent-layout=", 20);
  ed_append_command_arg (this, "set", "--enable-writing", 16);
  ed_append_command_arg (this, "set", "--backup-suffix=", 16);
  ed_append_command_arg (this, "set", "--no-backupfile", 15);
  ed_append_command_arg (this, "set", "--shiftwidth=", 13);
  ed_append_command_arg (this, "set", "--save-image=", 13);
  ed_append_command_arg (this, "set", "--image-file=", 13);
  ed_append_command_arg (this, "set", "--image-name=", 13);
  ed_append_command_arg (this, "set", "--backupfile", 12);
  ed_append_command_arg (this, "set", "--tabwidth=", 11);
  ed_append_command_arg (this, "set", "--autosave=", 11);
  ed_append_command_arg (this, "set", "--ftype=", 8);
  ed_append_command_arg (this, "diff", "--origin", 8);
  ed_append_command_arg (this, "substitute", "--remove-doseol", 15);
  ed_append_command_arg (this, "s%",         "--remove-doseol", 15);
  ed_append_command_arg (this, "substitute", "--remove-tabs", 13);
  ed_append_command_arg (this, "s%",         "--remove-tabs", 13);
  ed_append_command_arg (this, "substitute", "--shiftwidth=", 13);
  ed_append_command_arg (this, "s%",         "--shiftwidth=", 13);
  ed_append_command_arg (this, "@save_image", "--as=", 5);
}

private void rline_write_and_break (rline_t *rl){
  rl->state |= (RL_WRITE|RL_BREAK);
  rline_edit (rl);
}

private void rline_insert_char_and_break (rline_t *rl) {
  rl->state |= (RL_INSERT_CHAR|RL_BREAK);
  rline_edit (rl);
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
  rl->tab_completion = rline_tab_completion_void;

  rl->prompt_char = DEFAULT_PROMPT_CHAR;
  rl->prompt_row = prompt_row;
  rl->first_row = prompt_row;
  rl->first_col = first_col;
  rl->num_cols = rline_calc_columns (rl, num_cols);
  rl->row_pos = rl->prompt_row;
  rl->fd = $my(out_fd);
  rl->user_object = NULL;
  rl->render = string_new (num_cols);
  rl->line = vstring_new ();
  vstring_t *s = AllocType (vstring);
  s->data = string_new_with_len (" ", 1);
  current_list_append (rl->line, s);

  rl->state |= (RL_OK|RL_IS_VISIBLE);
  rl->opts |= (RL_OPT_HAS_HISTORY_COMPLETION|RL_OPT_HAS_TAB_COMPLETION);
  return rl;
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

  while (cidx++ < rl->num_cols - 1) string_append_byte (rl->render, ' ');

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
  ed_t *this = rl->ed;
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
    Video.draw.row_at (rl->cur_video, orig_first_row++);

  rline_render (rl);
  fd_write (rl->fd, rl->render->bytes, rl->render->num_bytes);
  rl->state &= ~RL_WRITE;
}

private void rline_reg (rline_t *rl) {
  ed_t *this = rl->ed;
  int regidx = ed_reg_get_idx (this, Input.get ($my(term)));
  if (NOTOK is regidx) return;

  buf_t *buf = self(get.current_buf);
  if (ERROR is ed_reg_special_set (this, buf, regidx))
    return;

  Reg_t *rg = &$my(regs)[regidx];
  if (rg->type is LINEWISE) return;

  reg_t *reg = rg->head;
  while (reg isnot NULL) {
    BYTES_TO_RLINE (rl, reg->data->bytes, (int) reg->data->num_bytes);
    reg = reg->next;
  }
}

private int array_any_int (int *ar, size_t len, int c) {
  for (size_t i = 0; i < len; i++)
    if (ar[i] is c)
      return 1;

  return 0;
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

    if (rl->state & RL_BREAK)
      goto theend;
  }

  if (rl->state & RL_PROCESS_CHAR)
    goto process_char;

  if (rl->state & RL_INSERT_CHAR)
    goto insert_char;

  for (;;) {
thecontinue:
    rl->state &= ~RL_CONTINUE;

    if (rl->state & RL_IS_VISIBLE) {
      ed_check_msg_status (rl->ed);
      rline_write (rl);
    }

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

    if (rl->line->num_items is 1) {
      if (array_any_int (rl->first_chars, rl->first_chars_len, rl->c)) {
        if (rl->opts & RL_OPT_HAS_TAB_COMPLETION) {
          if (rl->trigger_first_char_completion) {
            rl->state |= (RL_INSERT_CHAR|RL_FIRST_CHAR_COMPLETION);
            goto insert_char;
          }
        }
      }
    }

theloop:
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

      case LAST_ARG_KEY:
        rl = rline_complete_last_arg (rl);
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
        rline_reg (rl);
        goto post_process;

      case '\t':
        ifnot (rl->opts & RL_OPT_HAS_TAB_COMPLETION)
          goto post_process;

        retval = rl->tab_completion (rl);

        if (rl->opts & RL_OPT_RETURN_AFTER_TAB_COMPLETION) {
          rl->c = '\r';
          goto theend;
        }

        switch (retval) {
          case RL_PROCESS_CHAR:
            goto process_char;
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
          ch->data = String.new_with_fmt ("%c", rl->c);
        } else {
          ch = AllocType (vstring);
          char buf[5]; int len;
          ch->data = String.new_with (Ustring.character (rl->c, buf, &len));
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

        if (rl->state & RL_BREAK)
          goto theend;

        if (rl->state & RL_FIRST_CHAR_COMPLETION) {
            rl->state &= ~RL_FIRST_CHAR_COMPLETION;
            rl->c = '\t';
            goto theloop;
        }

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

theend:
  rl->state &= ~RL_BREAK;
  return rl;
}

private vstring_t *rline_quote_subcommand (vstring_t *it) {
  if (it is NULL) return NULL;

  vstring_t *it_head = it->prev;

  char c = it->data->bytes[0];

  ifnot (c is '{') goto theend;

  // quote by default
  if (c isnot '"') it->data->bytes[0] = '"';

  it = it->next;

  char pc = 0, ppc = 0;
  int num_quotes = 0;

  while (it) {
    c = it->data->bytes[0];

    if (c is '}') {
      ifnot (num_quotes % 2) {
        it->data->bytes[0] = '"';
        break;
      }

      // allow to fail from my caller
      if (it->next is NULL)
        it->prev->next = NULL;
      else {
        it->next->prev = it->prev;
        it->prev->next = it->next;
      }

      break;
    }

    if (pc) ppc = pc;
    pc = c;

    if (c is '"' and
       (pc isnot '\\' or (pc is '\\' and ppc isnot '\\'))) {
       num_quotes++;

      vstring_t *vs = AllocType (vstring);
      vs->data = string_new_with ("\\");

      vs->next = it;
      it->prev->next = vs;
      vs->prev = it->prev;
      it->prev = vs;
    }

    it = it->next;
  }

theend:
  return it_head;
}

private rline_t *rline_parse (rline_t *rl, buf_t *this) {
  vstring_t *it = rline_parse_command (rl);

  while (it) {
    int type = 0;
    if (cstring_eq (it->data->bytes, " ")) goto itnext;

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
        if (this)
          MSG_ERRNO (RL_ARGUMENT_MISSING_ERROR);
        rl->com = RL_ARGUMENT_MISSING_ERROR;
        goto theerror;
      }

      opt = string_new (8);
      while (it) {
        if (it->data->bytes[0] is ' ')
          goto arg_type;

        if (it->data->bytes[0] is '=') {
          if (cstring_eq (opt->bytes, "command"))
            it = rline_quote_subcommand (it->next);

          if (it->next is NULL) {
            if (this)
              MSG_ERRNO (RL_ARG_AWAITING_STRING_OPTION_ERROR);
            rl->com = RL_ARG_AWAITING_STRING_OPTION_ERROR;
            goto theerror;
          }

          it = it->next;

          arg->argval = string_new (8);
          int is_quoted = '"' is it->data->bytes[0];
          if (is_quoted) it = it->next;

          while (it) {
            if (' ' is it->data->bytes[0]) {
              ifnot (is_quoted) {
                type |= RL_TOK_ARG_OPTION;
                goto arg_type;
              }
            }

            if ('"' is it->data->bytes[0]) {
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
            }

            string_append (arg->argval, it->data->bytes);
            it = it->next;
          }

          if (is_quoted){
            if (this)
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
        string_replace_with (arg->argname, opt->bytes);
      } else
        arg->argname = string_new_with (opt->bytes);

      if (type & RL_TOK_ARG_OPTION) {
        if (cstring_eq (opt->bytes, "pat"))
          arg->type |= RL_ARG_PATTERN;
        else if (cstring_eq (opt->bytes, "sub"))
          arg->type |= RL_ARG_SUB;
        else if (cstring_eq (opt->bytes, "range"))
          arg->type |= RL_ARG_RANGE;
        else if (cstring_eq (opt->bytes, "bufname"))
          arg->type |= RL_ARG_BUFNAME;
        else if (cstring_eq (opt->bytes, "fname"))
          arg->type |= RL_ARG_FILENAME;
        else {
          arg->type |= RL_ARG_ANYTYPE;
          int found_arg = 0;
          if (rl->com < rl->commands_len) {
            int idx = 0;
            while (idx < rl->commands[rl->com]->num_args) {
              ifnot (NULL is rl->commands[rl->com]->args[idx]) {
                if (cstring_eq_n (opt->bytes, rl->commands[rl->com]->args[idx]+2, opt->num_bytes)) {
                  found_arg = 1;
                  break;
                }
              }
              idx++;
            }
          }

          ifnot (found_arg) {
            if (this)
              MSG_ERRNO (RL_UNRECOGNIZED_OPTION);
            rl->com = RL_UNRECOGNIZED_OPTION;
          }
        }

        goto argtype_succeed;
      } else {
        if (cstring_eq (opt->bytes, "i") or cstring_eq (opt->bytes, "interactive"))
          arg->type |= RL_ARG_INTERACTIVE;
        else if (cstring_eq (opt->bytes, "global"))
          arg->type |= RL_ARG_GLOBAL;
        else if (cstring_eq (opt->bytes, "append"))
          arg->type |= RL_ARG_APPEND;
        else if (cstring_eq (opt->bytes, "verbose"))
          arg->type |= RL_ARG_VERBOSE;
        else if (cstring_eq (opt->bytes, "r") or cstring_eq (opt->bytes, "recursive"))
          arg->type |= RL_ARG_RECURSIVE;
        else
          arg->type |= RL_ARG_ANYTYPE;

        goto argtype_succeed;
      }

theerror:
      string_free (opt);
      string_free (arg->argname);
      string_free (arg->argval);
      free (arg);
      goto theend;

argtype_succeed:
      string_free (opt);
      goto append_arg;
    } else {
      arg->argname = string_new_with (it->data->bytes);
      it = it->next;
      while (it isnot NULL and 0 is (cstring_eq (it->data->bytes, " "))) {
        string_append (arg->argname, it->data->bytes);
        it = it->next;
      }

      if (rl->com is VED_COM_BUF_CHANGE or rl->com is VED_COM_BUF_CHANGE_ALIAS) {
        opt = string_new_with ("bufname");
        arg->argval = string_new_with (arg->argname->bytes);
        type |= RL_TOK_ARG_OPTION;
        goto arg_type;
      }

      char *glob = cstring_byte_in_str (arg->argname->bytes, '*');
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
        dlist = dir_list (dir->bytes, 0);
        if (NULL is dlist or dlist->list->num_items is 0) goto free_strings;
        vstring_t *fit = dlist->list->head;

        while (fit) {
          char *fname = fit->data->bytes;
           /* matter to change */
          if (fname[fit->data->num_bytes - 1] is DIR_SEP) goto next_fname;

          if (pre isnot NULL)
            ifnot (cstring_eq_n (fname, pre->bytes, pre->num_bytes)) goto next_fname;

          if (post isnot NULL) {
            int pi; int fi = fit->data->num_bytes - 1;
            for (pi = post->num_bytes - 1; pi >= 0 and fi >= 0; pi--, fi--)
              if (fname[fi] isnot post->bytes[pi]) break;

            if (pi isnot -1) goto next_fname;
          }
          arg_t *larg = AllocType (arg);
          ifnot (cstring_eq (dir->bytes, "."))
            larg->argval = string_new_with_fmt ("%s/%s", dir->bytes, fname);
          else
            larg->argval = string_new_with (fname);

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
        arg->argval = string_new_with (arg->argname->bytes);
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

private Vstring_t *rline_get_arg_fnames (rline_t *rl, int num) {
  Vstring_t *fnames = vstring_new ();
  arg_t *arg = rl->head;
  if (num < 0) num = 256000;
  while (arg and num) {
    if (arg->type & RL_ARG_FILENAME) {
      vstring_append_uniq (fnames, arg->argval->bytes);
      num--;
    }
    arg = arg->next;
  }

  ifnot (fnames->num_items) {
    free (fnames);
    return NULL;
  }

  fnames->current = fnames->head;
  fnames->cur_idx = 0;
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
      if (cstring_eq (arg->argname->bytes, argname))
        return arg->argval;
    }
    arg = arg->prev;
  }

  return NULL;
}

private Vstring_t *rline_get_anytype_args (rline_t *rl, char *argname) {
  Vstring_t *args = NULL;
  arg_t *arg = rl->head;
  while (arg) {
    if (arg->type & RL_ARG_ANYTYPE) {
      if (cstring_eq (arg->argname->bytes, argname)) {
        if (NULL is args) args = vstring_new ();
        vstring_current_append_with (args, arg->argval->bytes);
      }
    }
    arg = arg->next;
  }

  return args;
}

private int rline_arg_exists (rline_t *rl, char *argname) {
  arg_t *arg = rl->head;
  while (arg) {
    if (cstring_eq (arg->argname->bytes, argname)) return 1;
    arg = arg->next;
  }

  return 0;
}

private int rline_parse_arg_buf_range (rline_t *rl, arg_t *arg, buf_t *this) {
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

  char *sp = Cstring.byte.in_str (arg->argval->bytes, ',');

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

  String.clear_at (arg->argval, diff);
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

private int rline_get_buf_range (rline_t  *rl, buf_t *this, int *range) {
  arg_t *arg = rline_get_arg (rl, RL_ARG_RANGE);
  if (NULL is arg or (NOTOK is rline_parse_arg_buf_range (rl, arg, this))) {
    range[0] = -1; range[1] = -1;
    return NOTOK;
  }

  range[0] = rl->range[0];
  range[1] = rl->range[1];

  return OK;
}

private int buf_test_key (buf_t *this) {
  utf8 c;
  MSG("press any key to test, press escape to end the test");
  char str[128]; char bin[32]; char chr[8]; int len;
  for (;;) {
    Cursor.hide ($my(term_ptr));
    c = Input.get ($my(term_ptr));
    snprintf (str, 128, "dec: %d hex: 0x%x octal: 0%o bin: %s char: %s",
        c, c, c, cstring_itoa (c, bin, 2), ustring_character (c, chr, &len));
    MSG(str);
    if (c is ESCAPE_KEY) break;
  }

  Cursor.show ($my(term_ptr));
  return DONE;
}

private int __validate_utf8_cb__ (Vstring_t *messages, char *line, size_t len,
                                                        int lnr, void *obj) {
  int *retval = (int *) obj;
  char *message;
  int num_faultbytes;
  int cur_idx = 0;
  char *bytes = line;
  size_t orig_len = len;
  size_t index;

check_utf8:
  index = ustring_validate ((unsigned char *) bytes, len, &message, &num_faultbytes);

  ifnot (index) return OK;

  vstring_append_with_fmt (messages,
      "--== Invalid UTF8 sequence ==-\n"
      "message: %s\n"
      "%s\nat line number %d, at index %zd, num invalid bytes %d\n",
      message, line, lnr, index + cur_idx, num_faultbytes);

  *retval = NOTOK;
  cur_idx += index + num_faultbytes;
  len = orig_len - cur_idx;
  bytes = line + cur_idx;
  num_faultbytes = 0;
  message = NULL;
  goto check_utf8;

  return *retval;
}

private int buf_com_validate_utf8 (buf_t **thisp, rline_t *rl) {
  buf_t *this = *thisp;

  int retval = OK;

  Vstring_t *fnames = NULL;
  if (NULL is rl or (NULL is (fnames = Rline.get.arg_fnames (rl, -1)))) {
    fnames = Vstring.new ();
    Vstring.append_with (fnames, $my(fname));
  }

  Vstring_t *err_messages = Vstring.new ();

  vstring_t *it = fnames->head;

  while (it) {
    char *fname = it->data->bytes;
    ifnot (file_exists (fname)) {
      Vstring.append_with_fmt (err_messages, "%s doesn't exists", fname);
      retval = NOTOK;
      goto next;
    }

    ifnot (file_is_readable (fname)) {
      Vstring.append_with_fmt (err_messages, "%s is not readable", fname);
      retval = NOTOK;
      goto next;
    }

    File.readlines (fname, err_messages, __validate_utf8_cb__, &retval);

    if (retval is OK)
      Msg.send_fmt ($my(root), COLOR_SUCCESS, "Validating %s ... OK", fname);

next:
    it = it->next;
  }

  if (retval is NOTOK) {
    Ed.append.toscratch ($my(root), CLEAR, "ERROR MESSAGES:");
    it = err_messages->head;
    while (it) {
      Ed.append.toscratch ($my(root), DONOT_CLEAR, it->data->bytes);
      it = it->next;
    }

    Ed.scratch ($my(root), thisp, NOT_AT_EOF);
    retval = OK;
  }

  Vstring.free (fnames);
  Vstring.free (err_messages);
  return retval;
}

int buf_validate_utf8_lw_mode_cb (buf_t **thisp, int fidx, int lidx,
                             Vstring_t *rows, utf8 c, char *action) {
  (void) action;
  if (c isnot 'v') return NO_CALLBACK_FUNCTION;

  buf_t *this = *thisp;

  int retval = OK;
  int count = lidx - fidx + 1;

  Vstring_t *err_messages = vstring_new ();
  vstring_t *it = rows->head;

  int i = 0;
  while (it and i++ < count) {
    __validate_utf8_cb__ (err_messages, it->data->bytes,
        it->data->num_bytes, fidx + i, &retval);
    it = it->next;
  }

  if (retval is NOTOK) {
    Ed.append.toscratch ($my(root), CLEAR, "ERROR MESSAGES:");
    it = err_messages->head;
    while (it) {
      Ed.append.toscratch ($my(root), DONOT_CLEAR, it->data->bytes);
      it = it->next;
    }

    Ed.scratch ($my(root), thisp, NOT_AT_EOF);
  } else
    Msg.send ($my(root), COLOR_SUCCESS, "Validating text ... OK");

  return retval;
}

private int rline_exec (rline_t *this, buf_t **thisp) {
  this->state |= RL_EXEC;
  return buf_rline (thisp, this);
}

private void rline_set_prompt_char (rline_t *rl, char c) {
  rl->prompt_char = c;
}

private void rline_set_user_object (rline_t *rl, void *obj) {
  rl->user_object = obj;
}

private void rline_set_opts (rline_t *rl, int opts) {
  rl->opts = opts;
}

private void rline_set_opts_bit (rline_t *rl, int bit) {
  rl->opts |= bit;
}

private void rline_set_state (rline_t *rl, int state) {
  rl->state = state;
}

private void rline_set_state_bit (rline_t *rl, int bit) {
  rl->state |= bit;
}

private void rline_set_visibility (rline_t *rl, int visible) {
  if (YES is visible)
    rl->state |= RL_IS_VISIBLE;
  else if (NO is visible)
    rl->state &= ~RL_IS_VISIBLE;
}

private rline_T __init_rline__ (void) {
  return ClassInit (rline,
    .self = SelfInit (rline,
      .edit = rline_edit,
      .free = rline_free,
      .exec = rline_exec,
      .clear = rline_clear,
      .parse = rline_parse,
      .write_and_break = rline_write_and_break,
      .get = SubSelfInit (rline, get,
        .arg = rline_get_arg,
        .opts = rline_get_opts,
        .line = rline_get_line,
        .state = rline_get_state,
        .command = rline_get_command,
        .buf_range = rline_get_buf_range,
        .arg_fnames = rline_get_arg_fnames,
        .user_object = rline_get_user_object,
        .anytype_arg = rline_get_anytype_arg,
        .anytype_args = rline_get_anytype_args
      ),
      .set = SubSelfInit (rline, set,
        .line = rline_set_line,
        .opts = rline_set_opts,
        .state = rline_set_state,
        .opts_bit = rline_set_opts_bit,
        .state_bit = rline_set_state_bit,
        .visibility = rline_set_visibility,
        .prompt_char = rline_set_prompt_char,
        .user_object = rline_set_user_object
      ),
      .arg = SubSelfInit (rline, arg,
       .exists = rline_arg_exists
      ),
      .history = SubSelfInit (rline, history,
        .push = rline_history_push
      )
    )
  );
}

private void __deinit_rline__ (rline_T *this) {
  (void) this;
}

private void ed_set_rline_cb (ed_t *this, Rline_cb cb) {
  $my(num_rline_cbs)++;
  ifnot ($my(num_rline_cbs) - 1)
    $my(rline_cbs) = Alloc (sizeof (Rline_cb));
  else
    $my(rline_cbs) = Realloc ($my(rline_cbs), sizeof (Rline_cb) * $my(num_rline_cbs));

  $my(rline_cbs)[$my(num_rline_cbs) - 1] = cb;
}

private void ed_free_rline_cbs (ed_t *this) {
  ifnot ($my(num_rline_cbs)) return;
  free ($my(rline_cbs));
}

private void ed_set_normal_on_g_cb (ed_t *this, BufNormalOng_cb cb) {
  $my(num_on_normal_g_cbs)++;
  ifnot ($my(num_on_normal_g_cbs) - 1)
    $my(on_normal_g_cbs) = Alloc (sizeof (BufNormalOng_cb));
  else
    $my(on_normal_g_cbs) = Realloc ($my(on_normal_g_cbs), sizeof (BufNormalOng_cb) * $my(num_on_normal_g_cbs));

  $my(on_normal_g_cbs)[$my(num_on_normal_g_cbs) - 1] = cb;
}

private void ed_free_on_normal_g_cbs (ed_t *this) {
  ifnot ($my(num_on_normal_g_cbs)) return;
  free ($my(on_normal_g_cbs));
}

private void ed_set_record_cb (ed_t *this, Record_cb cb) {
  $my(record_cb) = cb;
}

private void ed_set_i_record_cb (ed_t *this, IRecord_cb cb) {
  $my(i_record_cb) = cb;
}

private void ed_set_init_record_cb (ed_t *this, InitRecord_cb cb) {
  $my(init_record_cb) = cb;
}

private void ed_set_expr_reg_cb (ed_t *this, ExprRegister_cb cb) {
  $my(num_expr_reg_cbs)++;
  ifnot ($my(num_expr_reg_cbs) - 1)
    $my(expr_reg_cbs) = Alloc (sizeof (ExprRegister_cb));
  else
    $my(expr_reg_cbs) = Realloc ($my(expr_reg_cbs), sizeof (ExprRegister_cb) * $my(num_expr_reg_cbs));

  $my(expr_reg_cbs)[$my(num_expr_reg_cbs) - 1] = cb;
}

private void ed_free_expr_reg_cbs (ed_t *this) {
  ifnot ($my(num_expr_reg_cbs)) return;
  free ($my(expr_reg_cbs));
}

private int buf_rline (buf_t **thisp, rline_t *rl) {
  buf_t *this = *thisp;

  int retval = NOTHING_TODO;

  int is_special_win = win_isit_special_type ($my(parent));

  if (rl->state & RL_EXEC) goto exec;

  rl = Rline.edit (rl);

  if (rl->c isnot '\r') goto theend;

exec:
  rl->state &= ~RL_EXEC;

  if (rl->line->head is NULL or rl->line->head->data->bytes[0] is ' ')
    goto theend;

  rline_parse (rl, this);

  for (int i = 0; i < $myroots(num_rline_cbs); i++) {
    retval = $myroots(rline_cbs)[i] (thisp, rl, rl->com);
    if (retval isnot RLINE_NO_COMMAND) goto theend;
  }

redo:
  switch (rl->com) {
    case VED_COM_WRITE_FORCE_ALIAS:
      rl->com = VED_COM_WRITE_FORCE;
      goto redo;

    case VED_COM_WRITE_FORCE_FORCE_ALIAS:
      rl->com = VED_COM_WRITE_FORCE_FORCE;
      goto redo;

    case VED_COM_WRITE_ALIAS:
      rl->com = VED_COM_WRITE;

    case VED_COM_WRITE_FORCE_FORCE:
    case VED_COM_WRITE_FORCE:
    case VED_COM_WRITE:
      if ($my(enable_writing)) {
        arg_t *fname = Rline.get.arg (rl, RL_ARG_FILENAME);
        arg_t *range = Rline.get.arg (rl, RL_ARG_RANGE);
        arg_t *append = Rline.get.arg (rl, RL_ARG_APPEND);
        if (NULL is fname) {
          if (is_special_win) goto theend;
          if (NULL isnot range or NULL isnot append) goto theend;
          retval = self(write,
              (rl->com is VED_COM_WRITE ? 0 :
                (rl->com is VED_COM_WRITE_FORCE ? 1 : 2)));
        } else {
          if (NULL is range) {
            rl->range[0] = 0;
            rl->range[1] = this->num_items - 1;
          } else
            if (NOTOK is rline_parse_arg_buf_range (rl, range, this))
              goto theend;

          retval = buf_write_to_fname (this, fname->argval->bytes, NULL isnot append,
            rl->range[0], rl->range[1], VED_COM_WRITE_FORCE is rl->com, VERBOSE_ON);
        }
      } else
        Msg.error ($my(root), "writing is disabled, use :set --enable_writing to enable");

      goto theend;

    case VED_COM_EDIT_FORCE_ALIAS:
      rl->com = VED_COM_EDIT_FORCE; //__fallthrough__;
    case VED_COM_EDIT_FORCE:
    case VED_COM_EDIT:
    case VED_COM_EDIT_ALIAS:
      if (is_special_win) goto theend;
      if ($my(is_sticked)) goto theend;
      {
        arg_t *fname = Rline.get.arg (rl, RL_ARG_FILENAME);
        retval = Win.edit_fname ($my(parent), thisp, (NULL is fname ? NULL: fname->argval->bytes),
           $myparents(cur_frame), VED_COM_EDIT_FORCE is rl->com, 1, 1);
      }
      goto theend;

    case VED_COM_ETAIL:
      retval = win_edit_fname ($my(parent), thisp, NULL, $myparents(cur_frame), 1, 0, 1);
      self(normal.eof, DONOT_DRAW);
      goto theend;

    case VED_COM_QUIT_FORCE_ALIAS:
      rl->com = VED_COM_QUIT_FORCE; //__fallthrough__;
    case VED_COM_QUIT_FORCE:
    case VED_COM_QUIT:
    case VED_COM_QUIT_ALIAS:
      retval = ed_quit ($my(root), VED_COM_QUIT_FORCE is rl->com,
          Rline.arg.exists (rl, "global"));
      goto theend;

    case VED_COM_WRITE_QUIT:
    case VED_COM_WRITE_QUIT_FORCE:
      self(write, NO_FORCE);
      retval = ed_quit ($my(root), VED_COM_WRITE_QUIT_FORCE is rl->com,
          Rline.arg.exists (rl, "global"));
      goto theend;

    case VED_COM_EDNEW:
       {
         arg_t *fname = Rline.get.arg (rl, RL_ARG_FILENAME);
         if (NULL isnot fname)
           String.replace_with ($myroots(ed_str), fname->argval->bytes);
         else
           String.replace_with ($myroots(ed_str), UNNAMED);

          retval = EXIT_THIS;
          $myroots(state) |= ED_NEW;
        }

        goto theend;

    case VED_COM_EDNEXT:
      retval = EXIT_THIS;
      $myroots(state) |= ED_NEXT;
      goto theend;

    case VED_COM_EDPREV:
      retval = EXIT_THIS;
      $myroots(state) |= ED_PREV;
      goto theend;

    case VED_COM_EDPREV_FOCUSED:
      retval = EXIT_THIS;
      $myroots(state) |= ED_PREV_FOCUSED;
      goto theend;

    case VED_COM_ENEW:
       {
         arg_t *fname = Rline.get.arg (rl, RL_ARG_FILENAME);
         if (NULL isnot fname)
           retval = buf_enew_fname (thisp, fname->argval->bytes);
         else
           retval = buf_enew_fname (thisp, UNNAMED);
        }
        goto theend;

    case VED_COM_REDRAW:
       Win.draw ($my(root)->current);
       retval = DONE;
       goto theend;

    case VED_COM_BUF_CHECK_BALANCED:
      if ($my(ftype)->balanced isnot NULL) {
        int range[2];
        if (NOTOK is Rline.get.buf_range (rl, this, range)) {
          range[0] = 0;
          range[1] = this->num_items - 1;
        }

        retval = $my(ftype)->balanced (thisp, range[0], range[1]);
      }
      goto theend;

    case VED_COM_MESSAGES:
      retval = Ed.messages ($my(root), thisp, AT_EOF);
      goto theend;

    case VED_COM_SCRATCH:
      retval = Ed.scratch ($my(root), thisp, AT_EOF);
      goto theend;

    case VED_COM_SEARCHES:
      retval = ed_buf_change ($my(root), thisp, VED_SEARCH_WIN, VED_SEARCH_BUF);
      goto theend;

    case VED_COM_DIFF_BUF:
      retval = ed_buf_change ($my(root), thisp, VED_DIFF_WIN, VED_DIFF_BUF);
      goto theend;

    case VED_COM_DIFF:
      retval = buf_com_diff (thisp, rl, 0);
      goto theend;

    case VED_COM_GREP:
      {
        arg_t *pat = Rline.get.arg (rl, RL_ARG_PATTERN);
        if (NULL is pat) break;

        Vstring_t *fnames = Rline.get.arg_fnames (rl, -1);
        dirlist_t *dlist = NULL;

        if (NULL is fnames) {
          dlist = Dir.list (".", 0);
          if (NULL is dlist) break;
          fnames = dlist->list;
        }

        arg_t *rec = Rline.get.arg (rl, RL_ARG_RECURSIVE);
        dirwalk_t *dw = NULL;

        ifnot (NULL is rec) {
          dw = Dir.walk.new (NULL, NULL);
          dw->depth = DIRWALK_MAX_DEPTH;
          vstring_t *it = fnames->head;
          while (it) {
            Dir.walk.run (dw, it->data->bytes);
            it = it->next;
          }

          ifnot (NULL is dlist) {
            dlist->free (dlist);
            dlist = NULL;
          } else
            Vstring.free (fnames);

          fnames = dw->files;
        }

        retval = buf_grep (thisp, pat->argval->bytes, fnames);

        ifnot (NULL is dlist)
          dlist->free (dlist);
        else
          if (NULL is dw)
            Vstring.free (fnames);

        ifnot (NULL is dw)
          Dir.walk.free (&dw);
      }
      goto theend;

    case VED_COM_SPLIT:
      {
        if (is_special_win) goto theend;
        arg_t *fname = Rline.get.arg (rl, RL_ARG_FILENAME);
        if (NULL is fname)
          retval = buf_split (thisp, UNNAMED);
        else
          retval = buf_split (thisp, fname->argval->bytes);
      }
      goto theend;


    case VED_COM_READ:
    case VED_COM_READ_ALIAS:
      {
        arg_t *fname = Rline.get.arg (rl, RL_ARG_FILENAME);
        if (NULL is fname) goto theend;
        retval = buf_read_from_file (this, fname->argval->bytes);
      }
      goto theend;

    case VED_COM_SHELL:
    case VED_COM_READ_SHELL:
      {
        string_t *com = Vstring.join (rl->line, "");
        String.delete_numbytes_at (com, (rl->com is VED_COM_SHELL ? 1 : 3), 0);
        retval = buf_read_from_shell (this, com->bytes, rl->com);
        if (retval > OK) {
          Ed.append.message_fmt ($my(root), "%s exit_status %d\n", com->bytes, retval);
          retval = OK; // in case command exit_status is > 0
          // as on internal error NOTOK is returned
        }

        string_free (com);
      }
      goto theend;

    case VED_COM_SUBSTITUTE:
    case VED_COM_SUBSTITUTE_WHOLE_FILE_AS_RANGE:
    case VED_COM_SUBSTITUTE_ALIAS:
      retval = buf_com_substitute (*thisp, rl, &retval);
      goto theend;

    case VED_COM_BUF_CHANGE_PREV_ALIAS:
      rl->com = VED_COM_BUF_CHANGE_PREV; //__fallthrough__;
    case VED_COM_BUF_CHANGE_PREV:
      if ($my(flags) & BUF_IS_SPECIAL) goto theend;
      if ($my(is_sticked)) goto theend;
      retval = buf_change (thisp, rl->com);
      goto theend;

    case VED_COM_BUF_CHANGE_NEXT_ALIAS:
      rl->com = VED_COM_BUF_CHANGE_NEXT; //__fallthrough__;
    case VED_COM_BUF_CHANGE_NEXT:
      if ($my(flags) & BUF_IS_SPECIAL) goto theend;
      if ($my(is_sticked)) goto theend;
      retval = buf_change (thisp, rl->com);
      goto theend;

    case VED_COM_BUF_CHANGE_PREV_FOCUSED_ALIAS:
      rl->com = VED_COM_BUF_CHANGE_PREV_FOCUSED; //__fallthrough__;
    case VED_COM_BUF_CHANGE_PREV_FOCUSED:
      if (is_special_win) goto theend;
      if ($my(is_sticked)) goto theend;
      retval = buf_change (thisp, rl->com);
      goto theend;

    case VED_COM_BUF_CHANGE_ALIAS:
      rl->com = VED_COM_BUF_CHANGE; //__fallthrough__;
    case VED_COM_BUF_CHANGE:
      if ($my(flags) & BUF_IS_SPECIAL) goto theend;
      if ($my(is_sticked)) goto theend;
      {
        arg_t *bufname = Rline.get.arg (rl, RL_ARG_BUFNAME);
        if (NULL is bufname) goto theend;
        retval = buf_change_bufname (thisp, bufname->argval->bytes);
      }
      goto theend;

    case VED_COM_BUF_SET:
      retval = buf_com_set (*thisp, rl);
      goto theend;

    case VED_COM_BUF_BACKUP:
      retval = buf_com_backupfile (*thisp);
      goto theend;

    case VED_COM_WIN_CHANGE_PREV_ALIAS:
      rl->com = VED_COM_WIN_CHANGE_PREV; //__fallthrough__;
    case VED_COM_WIN_CHANGE_PREV:
      retval = Ed.win.change ($my(root), thisp, rl->com, NULL, 0, NO_FORCE);
      goto theend;

    case VED_COM_WIN_CHANGE_NEXT_ALIAS:
      rl->com = VED_COM_WIN_CHANGE_NEXT; //__fallthrough__;
    case VED_COM_WIN_CHANGE_NEXT:
      retval = Ed.win.change ($my(root), thisp, rl->com, NULL, 0, NO_FORCE);
      goto theend;

    case VED_COM_WIN_CHANGE_PREV_FOCUSED_ALIAS:
      rl->com = VED_COM_WIN_CHANGE_PREV_FOCUSED; //__fallthrough__;
    case VED_COM_WIN_CHANGE_PREV_FOCUSED:
      retval = Ed.win.change ($my(root), thisp, rl->com, NULL, 0, NO_FORCE);
      goto theend;

    case VED_COM_BUF_DELETE_FORCE_ALIAS:
      rl->com = VED_COM_BUF_DELETE_FORCE; //__fallthrough__;
    case VED_COM_BUF_DELETE_FORCE:
    case VED_COM_BUF_DELETE_ALIAS:
    case VED_COM_BUF_DELETE:
      {
        int idx;
        arg_t *bufname = Rline.get.arg (rl, RL_ARG_BUFNAME);
        if (NULL is bufname)
          idx = $my(parent)->cur_idx;
        else
          if (NULL is Win.get.buf_by_name ($my(parent), bufname->argval->bytes, &idx))
            idx = $my(parent)->cur_idx;

        retval = buf_delete (thisp, idx, rl->com is VED_COM_BUF_DELETE_FORCE);
      }
      goto theend;

    case VED_COM_TEST_KEY:
      buf_test_key (this);
      retval = DONE;
      goto theend;

    case VED_COM_SAVE_IMAGE:
      retval = buf_com_save_image (this, rl);
      goto theend;

    case VED_COM_EDIT_IMAGE:
      retval = buf_com_edit_image (thisp, rl);
      goto theend;

    case VED_COM_VALIDATE_UTF8:
      retval = buf_com_validate_utf8 (thisp, rl);
      goto theend;

    default:
     // for (int i = 0; i < $myroots(num_rline_cbs); i++) {
     //   retval = $myroots(rline_cbs)[i] (thisp, rl, rl->com);
     //   if (retval isnot RLINE_NO_COMMAND) break;
     // }
      goto theend;
  }

theend:
  this = *thisp; // as thisp can be modified, "this" might end up as NULL reference

  Rline.clear (rl);

  ifnot (DONE is retval)
    Rline.free (rl);
  else {
    rl->state &= ~RL_CLEAR_FREE_LINE;
    Rline.history.push (rl);
    rline_last_component_push (rl);
  }

  return retval;
}

private buf_t *buf_insert_char_rout (buf_t *this, utf8 c, string_t *cur_insert) {
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
      width = Ustring.width (buf, $my(ftype)->tabwidth);
    } else {
      $mycur(cur_col_idx)++;
      if (c < 0x10000) {
        buf[0] = (c >> 12) | 0xE0;
        buf[1] = ((c >> 6) & 0x3F) | 0x80;
        buf[2] = (c & 0x3F) | 0x80;
        buf[3] = '\0';
        width = Ustring.width (buf, $my(ftype)->tabwidth);
      } else if (c < 0x110000) {
        $mycur(cur_col_idx)++;
        buf[0] = (c >> 18) | 0xF0;
        buf[1] = ((c >> 12) & 0x3F) | 0x80;
        buf[2] = ((c >> 6) & 0x3F) | 0x80;
        buf[3] = (c & 0x3F) | 0x80;
        buf[4] = '\0';
        width = Ustring.width (buf, $my(ftype)->tabwidth);
      }
    }
  }

  String.insert_at ($mycur(data), buf, orig_col);
  String.append (cur_insert, buf);
  if ($my(cur_video_col) is $my(dim)->num_cols or
      $my(cur_video_col) + width > $my(dim)->num_cols) {
    $mycur(first_col_idx) = $mycur(cur_col_idx);
    $my(video)->col_pos = $my(cur_video_col) = $my(video)->first_col;
  } else
    $my(video)->col_pos = $my(cur_video_col) = $my(cur_video_col) + width;

  return this;
}

private int buf_insert_reg (buf_t **thisp, string_t *cur_insert) {
  buf_t *this = *thisp;
  MSG ("insert register (charwise mode):");
  int regidx = ed_reg_get_idx ($my(root), Input.get ($my(term_ptr)));
  if (NOTOK is regidx) return NOTHING_TODO;

  if (ERROR is ed_reg_special_set ($my(root), this, regidx))
    return NOTHING_TODO;

  Reg_t *rg = &$my(regs)[regidx];
  if (rg->type is LINEWISE) return NOTHING_TODO;

  reg_t *reg = rg->head;
  if (NULL is reg) return NOTHING_TODO;

  utf8 c = 0;
  while (reg isnot NULL) {
    char *sp = reg->data->bytes;
    while (*sp) {
      int clen = Ustring.charlen ((uchar) *sp);
      c = utf8_code (sp);
      sp += clen;
      this = buf_insert_char_rout (this, c, cur_insert);
    }
    reg = reg->next;
  }

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_current_row);
  return DONE;
}

private int buf_insert_mode (buf_t **thisp, utf8 com, char *bytes) {
  buf_t *this = *thisp;
  utf8 c = 0;
  if (com is '\n') {
    com = 'i';
    c = '\n';
  }

  String.clear ($my(cur_insert));

  char prev_mode[MAXLEN_MODE];
  Cstring.cp (prev_mode, MAXLEN_MODE, $my(mode), MAXLEN_MODE - 1);

  Action_t *Action = self(Action.new);
  self(Action.set_with_current, Action, REPLACE_LINE);

  if (com is 'A' or ((com is 'a' or com is 'C') and $mycur(cur_col_idx) is (int)
      $mycur(data)->num_bytes - Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]))) {
    ADD_TRAILING_NEW_LINE;

    self(normal.eol, DRAW);
  } else if (com is 'a')
    self(normal.right, 1, DRAW);

  if (com is 'i' or com is 'a') {
    int width = Ustring.width ($mycur(data)->bytes + $mycur(cur_col_idx),
        $my(ftype)->tabwidth);

    if (width > 1) {
      if ($my(cur_video_col) - width + 1 < 1) {
        Ustring.encode ($my(line), $mycur(data)->bytes, $mycur(data)->num_bytes,
            CLEAR, $my(ftype)->tabwidth, $mycur(first_col_idx));
        ustring_t *it = $my(line)->current;
        while ($mycur(first_col_idx) > 0 and $my(cur_video_col) < 1) {
          $mycur(first_col_idx) -= it->len;
          $my(video)->col_pos = $my(cur_video_col) =
            $my(cur_video_col) - it->width + 1;
          it = it->prev;
        }
      } else
        $my(video)->col_pos = $my(cur_video_col) =
          $my(cur_video_col) - width + 1;
      self(draw_current_row);
    }
  }

  if (bytes isnot NULL) {
    size_t blen = bytelen (bytes);
    for (size_t i = 0; i < blen; i++)
      this = buf_insert_char_rout (this, bytes[i], $my(cur_insert));

    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_current_row);
    goto theend;
  }

  Cstring.cp ($my(mode), MAXLEN_MODE, INSERT_MODE, MAXLEN_MODE - 1);
  self(set.mode, INSERT_MODE);

  if ($my(show_statusline) is UNSET) buf_set_draw_statusline (this);
  buf_set_draw_topline (this);

  if (c isnot 0) goto handle_char;

theloop:
  for (;;) {

get_char:
    ed_check_msg_status ($my(root));
    c = Input.get ($my(term_ptr));

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
        buf_complete_word (thisp);
        goto get_char;

      case CTRL('x'):
        buf_handle_ctrl_x (thisp);
        goto get_char;

      case CTRL('a'):
        buf_insert_last_insert (this);
        goto get_char;

      case CTRL('v'):
        if (NEWCHAR is buf_insert_character (this, &c))
          goto new_char;
        goto get_char;

      case CTRL('r'):
        buf_insert_reg (thisp, $my(cur_insert));
        goto get_char;

      case CTRL('k'):
        if (NEWCHAR is buf_complete_digraph (this, &c))
          goto new_char;
        goto get_char;

      case CTRL('y'):
      case CTRL('e'):
        if (NEWCHAR is buf_insert_handle_ud_line_completion (this, &c)) {
          $my(state) |= ACCEPT_TAB_WHEN_INSERT;
          goto new_char;
        }

        goto get_char;

      case BACKSPACE_KEY:
        if ($mycur(cur_col_idx)) {
          int retv = self(normal.left, 1, DONOT_DRAW);
          if (NOTHING_TODO is self(normal.delete, 1, -1, DRAW))
            if (retv) self(draw_current_row);
          goto get_char;
        }

        if (0 is $my(ftype)->backspace_on_insert_goes_up_and_join or
            this->cur_idx is 0)
          goto get_char;


        buf_insert_change_line (this, ARROW_UP_KEY, &Action, DONOT_DRAW);

        self(normal.eol, DONOT_DRAW);
        ADD_TRAILING_NEW_LINE;

        if (DONE is self(normal.join, DONOT_DRAW)) {
          Action_t *Laction = self(undo.pop);
          action_t *laction = Laction->head->next;
          self(action.free, laction);
          stack_push (Action, Laction->head);
          free (Laction);
        }

        self(normal.right, 1, DONOT_DRAW);
        self(draw);

        goto get_char;

      case DELETE_KEY:
        if ($mycur(data)->num_bytes is 0 or
          // HAS_THIS_LINE_A_TRAILING_NEW_LINE or
            $mycur(data)->bytes[$mycur(cur_col_idx)] is 0 or
            $mycur(data)->bytes[$mycur(cur_col_idx)] is '\n') {
          if (HAS_THIS_LINE_A_TRAILING_NEW_LINE) {
            if ($mycur(cur_col_idx) + Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)])
                is (int) $mycur(data)->num_bytes)
              self(normal.left, 1, DONOT_DRAW);
            RM_TRAILING_NEW_LINE;
          }

          if (DONE is self(normal.join, DONOT_DRAW)) {
            action_t *laction = stack_pop (Action, action_t);
            self(action.free, laction);
          }

          if ($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes -
              Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]))
            ADD_TRAILING_NEW_LINE;

          if ($mycur(cur_col_idx) isnot 0 or
              NOTHING_TODO is self(normal.right, 1, DRAW))
            self(draw);

          goto get_char;
        }

        self(normal.delete, 1, -1, DRAW);

        goto get_char;

      case HOME_KEY:
        self(normal.bol, DRAW);
        goto get_char;

      case END_KEY:
        ADD_TRAILING_NEW_LINE;
        self(normal.eol, DRAW);
        goto get_char;

      case ARROW_RIGHT_KEY:
        if($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes -
            Ustring.charlen ((uchar) $mycur(data)->bytes[$mycur(cur_col_idx)]))
          ADD_TRAILING_NEW_LINE;
        self(normal.right, 1, DRAW);
        goto get_char;

      case ARROW_LEFT_KEY:
        self(normal.left, 1, DRAW);
        goto get_char;

      case ARROW_UP_KEY:
      case ARROW_DOWN_KEY:
      case PAGE_UP_KEY:
      case PAGE_DOWN_KEY:
      case  '\r':
      case  '\n':
        buf_insert_change_line (this, c, &Action, DRAW);

        if ('\r' is c and $my(cur_insert)->num_bytes) {
          String.replace_with ($my(last_insert), $my(cur_insert)->bytes);
          String.clear ($my(cur_insert));
        }

        if ('\n' is c) goto theend;

        goto theloop;

      default:
        goto get_char;
    }

new_char:
    this = buf_insert_char_rout (this, c, $my(cur_insert));
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_current_row);
    goto get_char;
  }

theend:
  self(normal.left, 1, DRAW);
  if ($mycur(data)->num_bytes)
    RM_TRAILING_NEW_LINE;
  self(set.mode, prev_mode);

  if ($mycur(data)->num_bytes) {
    $my(video)->col_pos = $my(cur_video_col) = $my(cur_video_col) +
       (Ustring.width ($mycur(data)->bytes + $mycur(cur_col_idx),
       $my(ftype)->tabwidth) - 1);
  }

  if ($my(cur_video_col) > $my(dim)->num_cols) {
    $mycur(first_col_idx) = $mycur(cur_col_idx);
      $my(video)->col_pos = $my(cur_video_col) =
        Ustring.width ($mycur(data)->bytes + $mycur(cur_col_idx),
           $my(ftype)->tabwidth - 1);
  }

  self(draw_current_row);

  buf_set_draw_topline (this);

  if ($my(cur_insert)->num_bytes)
    String.replace_with ($my(last_insert), $my(cur_insert)->bytes);

  if (NULL isnot Action->head)
    self(undo.push, Action);
  else
    free (Action);

  if ($my(flags) & BUF_IS_MODIFIED)
    if ($my(autosave) > 0) {
      long cur_sec = $my(saved_sec);
      $my(saved_sec) = Vsys.get.clock_sec (DEFAULT_CLOCK);
      if (cur_sec > 0) {
        if ($my(saved_sec) - cur_sec > $my(autosave))
          self(write, FORCE);
        else
          $my(saved_sec) = cur_sec;
      }
    }

  return DONE;
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
    if (Cstring.eq ($from(it, name), name)) return it;
    (*idx)++;
    it = it->next;
  }

  return NULL;
}

private win_t *ed_get_win_by_idx (ed_t *this, int idx) {
  if (idx >= this->num_items or idx < 0) return NULL;
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

private void *ed_get_callback_fun (ed_t *this, char *fun) {
  (void) this;
  if (Cstring.eq (fun, "autoindent_c")) return buf_autoindent_c;
  if (Cstring.eq (fun, "autoindent_default")) return buf_ftype_autoindent;
  return NULL;
}

private string_t *ed_get_topline (ed_t *this) {
  return $my(topline);
}

private video_t *ed_get_video (ed_t *this) {
  return $my(video);
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

private int ed_get_num_win (ed_t *this, int count_special) {
  if (count_special) return this->num_items;
  int num = 0;
  win_t *it = this->head;
  while (it) {
    ifnot (win_isit_special_type (it)) num++;
    it = it->next;
  }
  return num;
}

private int ed_get_min_rows (ed_t *this) {
  (void) this;
  return MIN_ROWS;
}

private int ed_get_num_special_win (ed_t *this) {
  return $my(num_special_win);
}

private buf_t *ed_get_bufname (ed_t *this, char *fname) {
  buf_t *buf = NULL;
  win_t *w = this->head;
  int idx;
  while (w) {
    buf = Win.get.buf_by_name (w, fname, &idx);
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

private int ed_check_sanity (ed_t *this) {
  if ($my(dim)->num_rows < MIN_ROWS) return NOTOK;
  if ($my(dim)->num_cols < MIN_COLS) return NOTOK;
  return OK;
}

private void ed_set_screen_size (ed_t *this, screen_dim_opts opts) {
  int num_rows = opts.num_rows;
  int num_cols = opts.num_cols;
  int frow = opts.first_row;
  int fcol = opts.first_col;
  int last_row = opts.last_row;

  if (opts.init_term or num_rows is 0 or num_cols is 0) {
    Term.reset ($my(term));
    Term.set ($my(term));
  } else {
    $from($my(term), num_rows) = num_rows;
    $from($my(term), num_cols) = num_cols;
  }

  if (last_row < 1)
    last_row = frow + $from($my(term), num_rows);

  ifnot (NULL is $my(dim)) {
    free ($my(dim));
    $my(dim) = NULL;
  }

  $my(dim) = ed_dim_new (this, frow, last_row, fcol, $from($my(term), num_cols));

  $my(msg_row)     = $my(dim)->num_rows;
  $my(topline_row) = $my(dim)->first_row;
  $my(prompt_row)  = $my(msg_row) - 1;

  Video.free ($my(video));
  $my(video) = Video.new (OUTPUT_FD, $my(dim)->num_rows, $my(dim)->num_cols, $my(dim)->first_row, $my(dim)->first_col);
}

private void ed_set_exit_quick (ed_t *this, int val) {
  $my(exit_quick) = val;
}

private dim_t *ed_set_dim (ed_t *this, dim_t *dim, int f_row, int l_row,
                                                   int f_col, int l_col) {
  (void) this;
  return dim_set (dim, f_row, l_row, f_col, l_col);
}

private int ed_append_win (ed_t *this, win_t *w) {
  current_list_append (this, w);
  return this->cur_idx;
}

private string_t *ed_history_get_search_file (ed_t *this) {
  return $my(hs_file);
}

private string_t *ed_history_get_rline_file (ed_t *this) {
  return $my(hrl_file);
}

private string_t *ed_history_set_search_file (ed_t *this, char *file) {
  string_t *hs_file = $my(hs_file);
  ifnot (NULL is file) {
    if (NULL is hs_file)
      hs_file = String.new_with (file);
    else
      hs_file = String.replace_with (hs_file, file);
  } else {
    if (NULL is hs_file)
      hs_file = String.new_with (
          Root.get.env ($OurRoot, "data_dir")->bytes);
    else
      hs_file = String.replace_with (hs_file,
          Root.get.env ($OurRoot, "data_dir")->bytes);

    String.append_fmt (hs_file, "/.%s_libved_hist_search",
        Root.get.env ($OurRoot, "user_name")->bytes);
  }

  $my(hs_file) = hs_file;

  return $my(hs_file);
}

private string_t *ed_history_set_rline_file (ed_t *this, char *file) {
  string_t *hrl_file = $my(hrl_file);
  ifnot (NULL is file) {
    if (NULL is hrl_file)
      hrl_file = String.new_with (file);
    else
      hrl_file = String.replace_with (hrl_file, file);
  } else {
    if (NULL is hrl_file)
      hrl_file = String.new_with (
          Root.get.env ($OurRoot, "data_dir")->bytes);
    else
      hrl_file = String.replace_with (hrl_file,
          Root.get.env ($OurRoot, "data_dir")->bytes);

    String.append_fmt (hrl_file, "/.%s_libved_hist_rline",
        Root.get.env ($OurRoot, "user_name")->bytes);
  }

  $my(hrl_file) = hrl_file;

  return $my(hrl_file);
}

private void ed_history_add (ed_t *this, char *bytes, size_t num_bytes) {
  rline_t *rl = self(rline.new);
  BYTES_TO_RLINE (rl, bytes, (int) num_bytes);
  Rline.history.push (rl);
}

private void ed_history_add_lines (ed_t *this, Vstring_t *hist, int what) {
  if (NULL is hist) return;

  if (what is RLINE_HISTORY) {
    vstring_t *it = hist->head;
    while (it) {
      char *sp = Cstring.trim.end (it->data->bytes, '\n');
      sp = Cstring.trim.end (sp, ' ');
      self(history.add, it->data->bytes, bytelen (sp));
      it = it->next;
    }
    return;
  }

  if (what is SEARCH_HISTORY) {
    vstring_t *it = hist->head;
    while (it) {
      ed_search_history_push (this, it->data->bytes, it->data->num_bytes);
      it = it->next;
    }
    return;
  }
}

private void ed_history_read (ed_t *this) {
  if (0 is $my(env)->uid) return;

  Vstring_t *lines = NULL;

  if (File.exists ($my(hs_file)->bytes)) {
    lines = File.readlines ($my(hs_file)->bytes, NULL, NULL, NULL);
    self(history.add_lines, lines, SEARCH_HISTORY);
  }

  Vstring.clear (lines);

  if (File.exists ($my(hrl_file)->bytes)) {
    File.readlines ($my(hrl_file)->bytes, lines, NULL, NULL);
    self(history.add_lines, lines, RLINE_HISTORY);
  }

  Vstring.free (lines);
}

private void ed_history_write (ed_t *this) {
  if (0 is $my(env)->uid) return;

  FILE *fp = fopen ($my(hs_file)->bytes, "w");
  if (NULL is fp) goto hrl_file;

  histitem_t *it = $my(history)->search->tail;
  while (it) {
    fprintf (fp, "%s\n", it->data->bytes);
    it = it->prev;
  }

  fclose (fp);

hrl_file:
  fp = fopen ($my(hrl_file)->bytes, "w");
  if (NULL is fp) return;

  h_rlineitem_t *hrl = $my(history)->rline->tail;
  while (hrl) {
    string_t *line = Vstring.join (hrl->data->line, "");
    fprintf (fp, "%s\n", line->bytes);
    String.free (line);
    hrl = hrl->prev;
  }

  fclose (fp);
}

private int buf_word_actions_cb (buf_t **thisp, int fidx, int lidx,
                  bufiter_t *it, char *word, utf8 c, char *action) {
  buf_t *this = *thisp;
  int type = TO_UPPER; (void) type;

  switch (c) {
    case '+':
    case '*':
      return buf_selection_to_X_word_actions_cb (thisp, fidx, lidx, it, word, c, action);

   case '`':
     ed_reg_new ($my(root), REG_SHARED);
     ed_reg_push_with ($my(root), REG_SHARED, CHARWISE,
         word, NORMAL_ORDER);
     return DONE;

   case '~': {
       size_t len = lidx - fidx + 1;
       char buf[len+1]; // though it alwars returns ok, this might change in future
       if (OK isnot Ustring.swap_case (buf, word, len)) // to support malformed wstrings
         return NOTHING_TODO;

       buf[len] = '\0';
       self(delete.word, REG_UNNAMED);
       return selfp(insert.mode, 0, buf);
     }

   case 'L':
     type = TO_LOWER;
   case 'U': {
      size_t len = lidx - fidx + 1;
      char buf[len+1];
      ifnot (Ustring.change_case (buf, word, len, type))
        return NOTHING_TODO;
      buf[len] = '\0';
      self(delete.word, REG_UNNAMED);
      return selfp(insert.mode, 0, buf);
    }
  }
  return NOTHING_TODO;
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

  if (NULL is cb) cb = buf_word_actions_cb;

  for (int i = $my(word_actions_chars_len), j = 0; i < tlen; i++, j++) {
    $my(word_actions_chars)[i] = chars[j];
    $my(word_actions_cb)[i] = cb;
  }

  $my(word_actions_chars_len) = tlen;
  $my(word_actions) = cstring_chop (actions, '\n', $my(word_actions), NULL, NULL);
}

private void ed_set_word_actions_default (ed_t *this) {
  $my(word_actions) = vstring_new ();
  $my(word_actions_chars) = NULL;
  $my(word_actions_chars_len) = 0;
  utf8 chars[] = {'+', '*', '`', '~', 'L', 'U', ESCAPE_KEY};
  char actions[] =
    "+send selected word to XA_CLIPBOARD\n"
    "*send selected word to XA_PRIMARY\n"
    "`send selected word to shared register\n"
    "~swap case\n"
    "Lower (convert word to lower case)\n"
    "Upper (convert word to upper case)";

  self(set.word_actions, chars, ARRLEN(chars), actions, buf_word_actions_cb);
}

private void ed_set_cw_mode_actions (ed_t *this, utf8 *chars, int len,
                 char *actions, VisualCwMode_cb cb) {
  ifnot (len) return;
  int tlen = $my(cw_mode_chars_len) + len;

  ifnot ($my(cw_mode_chars_len)) {
    $my(cw_mode_chars) = Alloc (sizeof (int *) * len);
    $my(cw_mode_actions) = cstring_dup (actions, bytelen (actions));
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

  if (NULL is cb) return;

  $my(num_cw_mode_cbs)++;
  ifnot ($my(num_cw_mode_cbs) - 1)
    $my(cw_mode_cbs) = Alloc (sizeof (VisualCwMode_cb));
  else
    $my(cw_mode_cbs) = Realloc ($my(cw_mode_cbs), sizeof (VisualCwMode_cb) * $my(num_cw_mode_cbs));

  $my(cw_mode_cbs)[$my(num_cw_mode_cbs) -1] = cb;
}

private void ed_free_cw_mode_cbs (ed_t *this) {
  ifnot ($my(num_cw_mode_cbs)) return;
  free ($my(cw_mode_cbs));
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

  self(set.cw_mode_actions, chars, ARRLEN(chars), actions, NULL);
}

private void ed_set_lw_mode_actions (ed_t *this, utf8 *chars, int len,
                                   char *actions, VisualLwMode_cb cb) {
  ifnot (len) return;
  int tlen = $my(lw_mode_chars_len) + len;

  ifnot ($my(lw_mode_chars_len)) {
    $my(lw_mode_chars) = Alloc (sizeof (int *) * len);
    $my(lw_mode_actions) = Cstring.dup (actions, bytelen (actions));
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

  if (NULL is cb) return;

  $my(num_lw_mode_cbs)++;
  ifnot ($my(num_lw_mode_cbs) - 1)
    $my(lw_mode_cbs) = Alloc (sizeof (VisualLwMode_cb));
  else
    $my(lw_mode_cbs) = Realloc ($my(lw_mode_cbs), sizeof (VisualLwMode_cb) * $my(num_lw_mode_cbs));

  $my(lw_mode_cbs)[$my(num_lw_mode_cbs) -1] = cb;
}

private void ed_free_lw_mode_cbs (ed_t *this) {
  ifnot ($my(num_lw_mode_cbs)) return;
  free ($my(lw_mode_cbs));
}

private void ed_set_lw_mode_actions_default (ed_t *this) {
  utf8 chars[] = {'s', 'w', 'd', 'y', '>', '<', '+', '*', '`', 033};
  char actions[] =
    "substitute command for the selected lines\n"
    "write selected lines to file\n"
    "delete selected lines\n"
    "yank selected lines\n"
    "Yank selected and also send selected lines to XA_PRIMARY\n"
    ">indent in\n"
    "<indent out\n"
    "+send selected lines to XA_CLIPBOARD\n"
    "*send selected lines to XA_PRIMARY\n"
    "`send selected lines to the shared register";

  self(set.lw_mode_actions, chars, ARRLEN(chars), actions, NULL);

  utf8 bc[] = {'b'}; char bact[] = "balanced objects";
  self(set.lw_mode_actions, bc, 1, bact, buf_balanced_lw_mode_cb);

  utf8 vc[] = {'v'}; char vact[] = "validate string for invalid utf8 sequences";
  self(set.lw_mode_actions, vc, 1, vact, buf_validate_utf8_lw_mode_cb);

  if ($my(env)->uid) {
    utf8 ev[] = {'@'}; char evact[] = "@evaluate selected lines";
    self(set.lw_mode_actions, ev, 1, evact, buf_evaluate_lw_mode_cb);
  }
}

private void ed_free_line_mode_cbs (ed_t *this) {
  ifnot ($my(num_line_mode_cbs)) return;
  free ($my(line_mode_cbs));
  free ($my(line_mode_actions));
  free ($my(line_mode_chars));
}

private void ed_set_line_mode_actions
(ed_t *this, utf8 *chars, int len, char *actions, LineMode_cb cb) {
  ifnot (len) return;
  int tlen = $my(line_mode_chars_len) + len;

  ifnot ($my(line_mode_chars_len)) {
    $my(line_mode_chars) = Alloc (sizeof (int *) * len);
    $my(line_mode_actions) = Cstring.dup (actions, bytelen (actions));
  } else {
    $my(line_mode_chars) = Realloc ($my(line_mode_chars), sizeof (int *) * tlen);
    size_t alen = bytelen (actions);
    size_t plen = bytelen ($my(line_mode_actions));
    $my(line_mode_actions) = Realloc ($my(line_mode_actions), alen + plen + 2);
    $my(line_mode_actions)[plen] = '\n';
    for (size_t i = plen + 1, j = 0; i < alen + plen + 2; i++, j++) {
      $my(line_mode_actions)[i] = actions[j];
    }

    $my(line_mode_actions)[alen + plen + 1] = '\0';
  }

  for (int i = $my(line_mode_chars_len), j = 0; i < tlen; i++, j++)
    $my(line_mode_chars)[i] = chars[j];
  $my(line_mode_chars_len) = tlen;

  if (NULL is cb) return;

  $my(num_line_mode_cbs)++;
  ifnot ($my(num_line_mode_cbs) - 1)
    $my(line_mode_cbs) = Alloc (sizeof (LineMode_cb));
  else
    $my(line_mode_cbs) = Realloc ($my(line_mode_cbs), sizeof (LineMode_cb) * $my(num_line_mode_cbs));

  $my(line_mode_cbs)[$my(num_line_mode_cbs) -1] = cb;
}

private int buf_line_mode_actions_cb (buf_t **thisp, utf8 c, char *action, char *line, size_t size) {
  buf_t *this = *thisp;

  int retval = NO_CALLBACK_FUNCTION;
  switch (c) {
    case '+':
    case '*':
      goto x_selection;

    case '`':
      ed_reg_new ($my(root), REG_SHARED);
      ed_reg_push_with ($my(root), REG_SHARED, LINEWISE,
          line, NORMAL_ORDER);
     return DONE;

    default:
      goto theend;
  }

x_selection:
  switch (action[1]) {
    case '-':
      ed_selection_to_X ($my(root), line, size,
          ('*' is c ? X_PRIMARY : X_CLIPBOARD));
      break;

    default:
      ed_selection_to_X ($my(root), STR_FMT ("%s\n", line), size + 1,
          ('*' is c ? X_PRIMARY : X_CLIPBOARD));
  }
  return DONE;

theend:
  return retval;
}

private void ed_set_line_mode_actions_default (ed_t *this) {
  ifnot ($my(env)->uid) return;

  utf8 chars[] = {'`', '+', '*'};
  char actions[] =
    "`send current line to shared register\n"
    "+send current line to XA_CLIPBOARD (a LN appended)\n"
    "+-send current line to XA_CLIPBOARD (NO LN appended)\n"
    "*send current line to XA_PRIMARY (a LN appended)\n"
    "*-send current line to XA_PRIMARY (NO LN appended)"
    ;

  self(set.line_mode_actions, chars, ARRLEN(chars), actions, buf_line_mode_actions_cb);
}

private void ed_free_file_mode_cbs (ed_t *this) {
  ifnot ($my(num_file_mode_cbs)) return;
  free ($my(file_mode_cbs));
  free ($my(file_mode_actions));
  free ($my(file_mode_chars));
}

private void ed_set_file_mode_actions (ed_t *this, utf8 *chars, int len,
                                       char *actions, FileActions_cb cb) {
  ifnot (len) return;
  int tlen = $my(file_mode_chars_len) + len;

  ifnot ($my(file_mode_chars_len)) {
    $my(file_mode_chars) = Alloc (sizeof (int *) * len);
    $my(file_mode_actions) = Cstring.dup (actions, bytelen (actions));
  } else {
    $my(file_mode_chars) = Realloc ($my(file_mode_chars), sizeof (int *) * tlen);
    size_t alen = bytelen (actions);
    size_t plen = bytelen ($my(file_mode_actions));
    $my(file_mode_actions) = Realloc ($my(file_mode_actions), alen + plen + 2);
    $my(file_mode_actions)[plen] = '\n';
    for (size_t i = plen + 1, j = 0; i < alen + plen + 2; i++, j++) {
      $my(file_mode_actions)[i] = actions[j];
    }

    $my(file_mode_actions)[alen + plen + 1] = '\0';
  }

  for (int i = $my(file_mode_chars_len), j = 0; i < tlen; i++, j++)
    $my(file_mode_chars)[i] = chars[j];
  $my(file_mode_chars_len) = tlen;

  if (NULL is cb) return;

  $my(num_file_mode_cbs)++;
  ifnot ($my(num_file_mode_cbs) - 1)
    $my(file_mode_cbs) = Alloc (sizeof (FileActions_cb));
  else
    $my(file_mode_cbs) = Realloc ($my(file_mode_cbs), sizeof (FileActions_cb) * $my(num_file_mode_cbs));

  $my(file_mode_cbs)[$my(num_file_mode_cbs) -1] = cb;
}

private int buf_file_mode_actions_cb (buf_t **thisp, utf8 c, char *action) {
  (void) action;
  buf_t *this = *thisp;

  int retval = NO_CALLBACK_FUNCTION;
  switch (c) {
    case 'w':
      retval = self(write, VED_COM_WRITE_FORCE);
      break;

     case 'v':
       retval = buf_com_validate_utf8 (thisp, NULL);
       break;

     case '@':
      retval = NOTOK;
      ifnot ($from($my(root), env)->uid) break;

      if (0 is (($my(flags) & BUF_IS_SPECIAL)) and
          0 is Cstring.eq ($my(basename), UNNAMED)) {
        Vstring_t *lines = File.readlines ($my(fname), NULL, NULL, NULL);
        char *buf = Vstring.to.cstring (lines, ADD_NL);
        if (NULL is buf)
          buf = self(get.contents, ADD_NL);

        retval = buf_interpret (thisp, buf);
        Vstring.free (lines);
        break;
      }

      if (Cstring.eq ($my(basename), UNNAMED) or
          Cstring.eq ($my(basename), VED_SCRATCH_BUF)) {
        char *buf = self(get.contents, ADD_NL);
        retval = buf_interpret (thisp, buf);
      }

      break;

    default:
      break;
  }

  return retval;
}

private void ed_set_file_mode_actions_default (ed_t *this) {
  ifnot ($my(env)->uid) return;

  utf8 chars[] = {'w', 'v', '@'};
  char actions[] =
     "write buffer\n"
     "validate file for invalid utf8 sequences\n"
     "@evaluate buffer";
  self(set.file_mode_actions, chars, ARRLEN(chars), actions, buf_file_mode_actions_cb);

}

private void ed_free_at_exit_cbs (ed_t *this) {
  ifnot ($my(num_at_exit_cbs)) return;
  free ($my(at_exit_cbs));
}

private void ed_set_at_exit_cb (ed_t *this, EdAtExit_cb cb) {
  if (NULL is cb) return;
  $my(num_at_exit_cbs)++;
  ifnot ($my(num_at_exit_cbs) - 1)
    $my(at_exit_cbs) = Alloc (sizeof (EdAtExit_cb));
  else
    $my(at_exit_cbs) = Realloc ($my(at_exit_cbs), sizeof (EdAtExit_cb) * $my(num_at_exit_cbs));

  $my(at_exit_cbs)[$my(num_at_exit_cbs) -1] = cb;
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
    char *sp = cstring_byte_in_str (chars, c_); \
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

  ifnot (NULL is $my(syntaxes)[$my(num_syntaxes)].multiline_comment_continuation)
    $my(syntaxes)[$my(num_syntaxes)].multiline_comment_continuation_len =
      bytelen ($my(syntaxes)[$my(num_syntaxes)].multiline_comment_continuation);
  $my(num_syntaxes)++;
#undef whereis_c
}

private void ed_init_syntaxes (ed_t *this) {
  for (size_t i = 0; i < ARRLEN(HL_DB); i++)
    self(syn.append, HL_DB[i]);
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
    for (int i = 0; i < $my(num_at_exit_cbs); i++)
      $my(at_exit_cbs)[i] (this);

    ed_free_at_exit_cbs (this);

    free ($my(name));
    free ($my(dim));
    free ($my(saved_cwd));

    String.free ($my(topline));
    String.free ($my(msgline));
    String.free ($my(last_insert));
    String.free ($my(ed_str));
    String.free ($my(hs_file));
    String.free ($my(hrl_file));
    Ustring.free ($my(uline));
    Vstring.free ($my(rl_last_component));
    Video.free ($my(video));

    history_free (&$my(history));
    //venv_free (&$my(env));

    for (int i = 0; i < NUM_REGISTERS; i++) {
      if (i is REG_SHARED) continue;
      reg_free (&$my(regs)[i]);
    }

    for (int i = 0; i <= NUM_RECORDS; i++)
      vstring_free ($my(records)[i]);

    for (int i = 0; i < $my(num_syntaxes); i++) {
      free ($my(syntaxes)[i].keywords_len);
      free ($my(syntaxes)[i].keywords_colors);
    }

    free ($my(cw_mode_chars)); free ($my(cw_mode_actions));
    free ($my(lw_mode_chars)); free ($my(lw_mode_actions));
    free ($my(word_actions_chars));
    free ($my(word_actions_cb));

    ed_free_rline_cbs (this);
    ed_free_on_normal_g_cbs (this);
    ed_free_expr_reg_cbs (this);
    ed_free_lw_mode_cbs (this);
    ed_free_cw_mode_cbs (this);
    /* if there is a wonder about the tendency to alling lines
     * into the same group of functionality, is because in cases
     * like this (that introduced the line mode); this and one another
     * tendency to group the function(s|ality) close to its other scope,
     * allow me to follow and copy all the functions from the "old"
     * file mode function(s|lity), to develop a bright new functionality in a matter
     * of minutes and without think a bit, by just copying the file mode
     * function and substitute in visual mode "file/line", and
     * using for the rest (proper function calling) C-E in normal mode
     * (all that alling the line mode additions, previous to file mode) 
     */
    ed_free_line_mode_cbs (this);
    ed_free_file_mode_cbs (this);

    vstring_free ($my(word_actions));

    ed_deinit_commands (this);

    free ($myprop);
  }

  free (this);
}

private int  ed_test_state_bit (ed_t *this, int bit) {
  return $my(state) & (bit);
}

private void ed_unset_state_bit (ed_t *this, int bit) {
  $my(state) &= ~(bit);
}

private void ed_set_state_bit (ed_t *this, int bit) {
  $my(state) |= (bit);
}

private void ed_set_state (ed_t *this, int state) {
  $my(state) = state;
}

private void ed_set_lang_map (ed_t *this, int lmap[][26]) {
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < ('z' - 'a') + 1; j++)
      $my(lmap)[i][j] = lmap[i][j];
}

private void ed_init_special_win (ed_t *this) {
  ed_get_scratch_buf (this);
  ed_msg_buf (this);
  ed_diff_buf (this);
  ed_search_buf (this);
  $my(num_special_win) = 4;
}

private int ed_i_record_default (ed_t *this, Vstring_t *rec) {
  char *str = Vstring.to.cstring (rec, ADD_NL);

  if (bytelen (str) is (size_t) $my(record_header_len)) return NOTOK;

  i_t *in = I.get.current ($my(__I__));
  ifnot (in)
    in = I.init_instance ($my(__I__), IOpts());

  int retval = I.eval_string (in, str, 1, 1);

  free (str);

  if (retval is I_ERR_SYNTAX) {
    buf_t *buf = this->current->current;
    ed_messages (this, &buf, AT_EOF);
  } else
    Msg.send_fmt (this, COLOR_MSG, "Executed record [%d]", $my(record_idx) + 1);

  return (retval isnot OK ? NOTOK : OK);
}

private void ed_record_default (ed_t *this, char *bytes) {
  if ($my(repeat_mode)) return;

  if ($my(record))
    vstring_append_with ($my(records)[$my(record_idx)], bytes);
  else
    String.replace_with ($my(records)[NUM_RECORDS]->head->next->data, bytes);
}

private void ed_record (ed_t *this, char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE(fmt);
  char bytes[len+1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  $my(record_cb) (this, bytes);
}

private char *ed_init_record_default (ed_t *);

private ssize_t __record_header_len__ (ed_t *this) {
  char *buf = ed_init_record_default (this);
  char *sp = buf;

  size_t len = bytelen (buf);

  while (*sp and (buf = Cstring.byte.in_str (sp, '\n')) isnot NULL) {
    len++;
    sp = buf + 1;
  }

  return len;
}

private char *ed_init_record_default (ed_t *this) {
  if ($my(record_header_len) is -1) {
      $my(record_header_len) = 0;
      $my(record_header_len) = __record_header_len__ (this);
  }

  return
    "var ed = e_get_ed_current ()\n"
    "var win = ed_get_current_win (ed)\n"
    "var buf = win_get_current_buf (win)\n"
    "var draw = 1";
}

private void ed_init_record (ed_t *this, int idx) {
  if (0 > idx or idx > NUM_RECORDS - 1) idx = 0;
  $my(record_idx) = idx;
  $my(record) = 1;

  Vstring_t *rec = $my(records)[$my(record_idx)];
  if (rec)
    Vstring.clear (rec);
  else
    rec = Vstring.new ();

  Vstring.append_with (rec, $my(init_record_cb) (this));

  $my(records)[$my(record_idx)] = rec;

  Msg.send_fmt (this, COLOR_MSG, "Recording into [%d]", $my(record_idx) + 1);
}

private int ed_interpr_record (ed_t *this, int idx) {
  if ($my(record)) return NOTHING_TODO;

  if (0 > idx or idx > NUM_RECORDS) idx = 0;

  if (NULL is $my(records)[idx])
    return NOTHING_TODO;

  $my(repeat_mode) = 1;
  int retval = $my(i_record_cb) (this, $my(records)[idx]);
  $my(repeat_mode) = 0;

  return retval;
}

private void ed_deinit_record (ed_t *this) {
  $my(record) = 0;
  Msg.send_fmt (this, COLOR_MSG, "End of Recording into [%d]", $my(record_idx) + 1);
}

private int ed_buf_normal_cmd (ed_t *ed, buf_t **thisp, utf8 com, int count, int regidx) {
  buf_t *this = *thisp;

  if (0 >= count or count > this->num_items)
     return NOTHING_TODO;

  if (regidx < 0 or regidx >= NUM_REGISTERS)
    regidx = REG_UNNAMED;

  int retval = NOTHING_TODO;
  switch (this->on_normal_beg (thisp, com, count, regidx)) {
    case -1: goto theend;
    case IGNORE_BLOCK: goto atend;
    case EXIT_THIS: return EXIT_THIS;
    default: break;
  }

  if (com > 'z') {
    if ($from(ed, lmap)[0][0] isnot 0) {
      for (int i = 0; i < 2; i++) {
        for (int j = 0; j < ('z' - 'a') + 1; j++) {
          if ($from(ed, lmap)[i][j] is com) {
            com = (i is 1 ? 'a' : 'A') + j;
            goto handle_com;
          }
        }
      }
    }
  }

handle_com:
  switch (com) {
    case 'q':
      if ($my(flags) & BUF_IS_PAGER) return BUF_QUIT;

    case 'Q':
      if ($from(ed, record))
        ed_deinit_record (ed);
      else
        ed_init_record (ed, Input.get ($my(term_ptr)) - '1');
      break;

    case '@':
      retval = ed_interpr_record (ed, Input.get ($my(term_ptr)) - '1');
      break;

    case '.':
      String.replace_with ($from(ed, records)[NUM_RECORDS]->head->data,
          $from(ed, init_record_cb) (ed));
      retval = ed_interpr_record (ed, NUM_RECORDS);
      break;

    case ':':
      {
      rline_t *rl = Ed.rline.new (ed);
      retval = buf_rline (thisp, rl);
      this = *thisp;
      }

      goto theend;

    case '/':
    case '?':
    case '*':
    case '#':
    case 'n':
    case 'N':
      retval = self(search, com, NULL, -1); break;

    case 'm':
      retval = self(mark.set, -1); break;

    case '`':
      retval = self(mark.jump); break;

    case CTRL('o'):
      retval = self(jump.to, RIGHT_DIRECTION); break;

    case CTRL('i'):
      retval = self(jump.to, LEFT_DIRECTION); break;

    case '~':
      retval = self(normal.change_case); break;

    case CTRL('a'):
    case CTRL('x'):
      retval = buf_word_math (this, count, com); break;

    case '^':
      retval = self(normal.noblnk); break;

    case '>':
    case '<':
      retval = self(indent, count, com); break;

    case 'y':
      retval = self(normal.yank, count, regidx); break;

    case 'Y':
      retval = self(normal.Yank, count, regidx); break;

    case ' ':
      ifnot ($my(ftype)->space_on_normal_is_like_insert_mode) break;

      (&$my(regs)[REG_RDONLY])->type = CHARWISE;
      (&$my(regs)[REG_RDONLY])->head->data->bytes[0] = ' ';
      (&$my(regs)[REG_RDONLY])->head->data->bytes[1] = '\0';
      (&$my(regs)[REG_RDONLY])->head->data->num_bytes = 1;
      (&$my(regs)[REG_RDONLY])->head->next = NULL;
      (&$my(regs)[REG_RDONLY])->head->prev = NULL;
      retval = self(normal.put, REG_RDONLY, 'P'); break;

    case 'p':
    case 'P':
      retval = self(normal.put, regidx, com); break;

    case 'x':
    case DELETE_KEY:
      retval = self(normal.delete, count, regidx, DRAW); break;

    case BACKSPACE_KEY:
      ifnot ($mycur(cur_col_idx)) {
        if ($my(ftype)->backspace_on_first_idx_remove_trailing_spaces) {
          size_t len = $mycur(data)->num_bytes;
          for (int i = $mycur(data)->num_bytes - 1; i > 0; i--)
            if ($mycur(data)->bytes[i] is ' ')
              String.clear_at ($mycur(data), i);
            else
              break;

          if (len isnot $mycur(data)->num_bytes) {
            $my(flags) |= BUF_IS_MODIFIED;
            self(draw_current_row);
          }

          retval = DONE;
          break;
        }

        ifnot ($my(ftype)->backspace_on_normal_goes_up)
          break;

        if (NOTHING_TODO is self(normal.up, 1, DONOT_ADJUST_COL, DONOT_DRAW))
          break;

        if (NOTHING_TODO is self(normal.eol, DRAW))
          self(draw);

        break;
      }

      ifnot ($my(ftype)->backspace_on_normal_is_like_insert_mode) break;

      //__fallthrough__;

    case 'X':
       if (DONE is self(normal.left, 1, DONOT_DRAW))
         if (NOTHING_TODO is (retval = self(normal.delete, count, regidx, DRAW)))
           self(draw_current_row);
       break;

    case 'J':
      retval = self(normal.join, DRAW); break;

    case '$':
      retval = self(normal.eol, DRAW); break;

    case CTRL('w'):
      retval = selfp(normal.handle.ctrl_w); break;

    case 'g':
      retval = selfp(normal.handle.g, count); break;

    case 'd':
      retval = self(normal.handle.d, count, regidx); break;

    case 'G':
      retval = self(normal.handle.G, count); break;

    case ',':
      retval = selfp(normal.handle.comma); break;

    case 'c':
      retval = selfp(normal.handle.c, count, regidx); break;

    case 'W':
      retval = selfp(normal.handle.W);
      break;

    case 'L':
      retval = selfp(normal.handle.L);
      break;

    case 'F':
      retval = selfp(normal.handle.F);
      break;

    case '0':
      retval = self(normal.bol, DRAW); break;

    case 'E':
    case 'e':
      retval = self(normal.end_word, count,
        ($my(ftype)->small_e_on_normal_goes_insert_mode is 1 and 'e' is com), DRAW);
      this = *thisp;
      break;

    case ARROW_RIGHT_KEY:
    case 'l':
      retval = self(normal.right, count, DRAW); break;

    case ARROW_LEFT_KEY:
    case 'h':
      retval = self(normal.left, count, DRAW); break;

    case ARROW_UP_KEY:
    case 'k':
      retval = self(normal.up, count, ADJUST_COL, DRAW); break;

    case ARROW_DOWN_KEY:
    case 'j':
      retval = self(normal.down, count, ADJUST_COL, DRAW); break;

    case PAGE_DOWN_KEY:
    case CTRL('f'):
      retval = self(normal.page_down, count, DRAW); break;

    case PAGE_UP_KEY:
    case CTRL('b'):
      retval = self(normal.page_up, count, DRAW); break;

    case HOME_KEY:
      retval = self(normal.bof, DRAW); break;

    case END_KEY:
      retval = self(normal.eof, DRAW); break;

    case CTRL('v'):
      retval = self(normal.visual.bw); break;

    case 'V':
      retval = selfp(normal.visual.lw); break;

    case 'v':
      retval = selfp(normal.visual.cw); break;

    case 'D':
      retval = self(normal.delete_eol, regidx, DRAW); break;

    case 'r':
      retval = self(normal.replace_character); break;

    case 'C':
       self(normal.delete_eol, regidx, DRAW);
       retval = selfp(insert.mode, com, NULL); break;

    case 'o':
    case 'O':
      retval = selfp(insert.new_line, com); break;

    case '\r':
      ifnot ($my(ftype)->cr_on_normal_is_like_insert_mode) break;

      com = '\n';
//      __fallthrough__;
    case 'i':
    case 'a':
    case 'A':
      retval = selfp(insert.mode, com, NULL); break;

    case CTRL('r'):
    case 'u':
      retval = self(undo.exec, com); break;

    case CTRL('l'):
      Win.draw (ed->current);
      retval = DONE; break;

    case CTRL('j'):
      $myroots(state) |= ED_SUSPENDED;
      return EXIT_THIS;

    case '-':
    case '_':
      if ('A' <= REGISTERS[regidx] and REGISTERS[regidx] <= 'Z') {
        reg_free (&$myroots(regs)[regidx]);
        retval = DONE;
      } else
          retval = NOTHING_TODO;

      break;

    default:
      break;
  }

atend:
  this->on_normal_end (thisp, com, count, regidx);

theend:
  return retval;
}

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
    count = atoi (intbuf);                \
    cc;                                   \
    })

private int ed_loop (ed_t *ed, buf_t *this) {
  int retval = NOTOK;
  int count = 1;
  utf8 c;
  int cmd_retv;
  int regidx = -1;

  for (;;) {
 new_state:
    regidx = -1;
    count = 1;

get_char:
    ed_check_msg_status (ed);
    c = Input.get ($my(term_ptr));

handle_char:
    switch (c) {
      case NOTOK: goto theend;

      case '"':
        if (-1 isnot regidx) goto exec_block;
        regidx = ed_reg_get_idx (ed, Input.get ($my(term_ptr)));
        goto get_char;

      case '1'...'9':
        NORMAL_GET_NUMBER;
        goto handle_char;

      default:
exec_block:

        cmd_retv = ed_buf_normal_cmd (ed, &this, c, count, regidx);

        if (cmd_retv is DONE or cmd_retv is NOTHING_TODO)
          goto new_state;

        if (cmd_retv is BUF_QUIT) {
          if ($from(ed, exit_quick)) {
            cmd_retv = EXIT_ALL_FORCE;
            goto exit_this;
          }

          retval = buf_change (&this, VED_COM_BUF_CHANGE_PREV_FOCUSED);
          if (retval is NOTHING_TODO) {
            retval = Ed.win.change ($my(root), &this, VED_COM_WIN_CHANGE_PREV_FOCUSED,
                NULL, 0, NO_FORCE);
            if (retval is NOTHING_TODO)
              cmd_retv = EXIT_THIS;
            else
              goto new_state;
          } else
            goto new_state;
        }

exit_this:
        if (cmd_retv is EXIT_THIS or cmd_retv is EXIT_ALL or cmd_retv is EXIT_ALL_FORCE) {
          ifnot (($myroots(state) & ED_SUSPENDED)) {
            if (cmd_retv is EXIT_THIS)
              $myroots(state) |= ED_EXIT;
            else if (cmd_retv is EXIT_ALL)
              $myroots(state) |= ED_EXIT_ALL;
            else
              $myroots(state) |= ED_EXIT_ALL_FORCE;
          }

          retval = OK;
          goto theend;
        }

        if (cmd_retv is WIN_EXIT) {
          /* at this point this (probably) is a null reference */
          retval = ed_win_delete (ed, &this, NO_COUNT_SPECIAL);

          if (retval is DONE) goto new_state;

          ed->prop->state |= ED_EXIT;
          retval = OK;
          goto theend;
        }
    }
  }

theend:
  return retval;
}

private void ed_suspend (ed_t *this) {
  if ($my(state) & ED_SUSPENDED) return;
  $my(state) |= ED_SUSPENDED;
  Screen.clear ($my(term));
  Term.reset ($my(term));
}

private void ed_resume (ed_t *this) {
  ifnot ($my(state) & ED_SUSPENDED) return;
  $my(state) &= ~ED_SUSPENDED;
  Term.set ($my(term));
  Win.set.current_buf (this->current, this->current->cur_idx, DONOT_DRAW);
  Win.draw (this->current);
}

private rline_t *ed_rline_new (ed_t *this) {
  rline_t *rl = rline_new (this, $my(term), Input.get, $my(prompt_row),
      1, $my(dim)->num_cols, $my(video)) ;
  if ($my(commands) is NULL) ed_init_commands (this);
  rl->commands = $my(commands);
  rl->commands_len = $my(num_commands);
  rl->tab_completion = rline_tab_completion;
  rl->trigger_first_char_completion = 1;
  rl->first_chars[0] = '~';
  rl->first_chars[1] = '`';
  rl->first_chars[2] = '@';
  rl->first_chars_len = 3;

  return rl;
}

private rline_t *ed_rline_new_with (ed_t *this, char *bytes) {
  rline_t *rl = self(rline.new);
  Rline.set.line (rl, bytes, bytelen (bytes));
  return rl;
}

private Class (ed) *editor_new (void) {
  ed_T *this = AllocClass (ed);
  *this = ClassInit (ed,
    .prop = this->prop,
    .self = SelfInit (ed,
      .quit = ed_quit,
      .record = ed_record,
      .resume = ed_resume,
      .scratch = ed_scratch,
      .suspend = ed_suspend,
      .question = ed_question,
      .messages = ed_messages,
      .dim_calc = ed_dim_calc,
      .free_info = ed_free_info,
      .dims_init = ed_dims_init,
      .check_sanity = ed_check_sanity,
      .deinit_commands = ed_deinit_commands,
      .set = SubSelfInit (ed, set,
        .dim = ed_set_dim,
        .state = ed_set_state,
        .topline = ed_set_topline,
        .rline_cb = ed_set_rline_cb,
        .lang_map = ed_set_lang_map,
        .state_bit = ed_set_state_bit,
        .record_cb = ed_set_record_cb,
        .at_exit_cb = ed_set_at_exit_cb,
        .exit_quick = ed_set_exit_quick,
        .screen_size = ed_set_screen_size,
        .current_win = ed_set_current_win,
        .expr_reg_cb = ed_set_expr_reg_cb,
        .i_record_cb = ed_set_i_record_cb,
        .word_actions = ed_set_word_actions,
        .on_normal_g_cb = ed_set_normal_on_g_cb,
        .init_record_cb = ed_set_init_record_cb,
        .lw_mode_actions = ed_set_lw_mode_actions,
        .cw_mode_actions = ed_set_cw_mode_actions,
        .line_mode_actions = ed_set_line_mode_actions,
        .file_mode_actions = ed_set_file_mode_actions
      ),
      .unset = SubSelfInit (ed, unset,
        .state_bit = ed_unset_state_bit
      ),
      .test = SubSelfInit (ed, test,
        .state_bit = ed_test_state_bit
      ),
      .get = SubSelfInit (ed, get,
        .info = SubSelfInit (edget, info,
          .as_type = ed_get_info_as_type
        ),
        .term = ed_get_term,
        .state = ed_get_state,
        .video = ed_get_video,
        .topline = ed_get_topline,
        .num_win = ed_get_num_win,
        .bufname = ed_get_bufname,
        .min_rows = ed_get_min_rows,
        .win_head = ed_get_win_head,
        .win_next = ed_get_win_next,
        .win_by_idx = ed_get_win_by_idx,
        .win_by_name = ed_get_win_by_name,
        .current_buf = ed_get_current_buf,
        .scratch_buf = ed_get_scratch_buf,
        .current_win = ed_get_current_win,
        .callback_fun = ed_get_callback_fun,
        .current_win_idx = ed_get_current_win_idx,
        .num_special_win = ed_get_num_special_win,
        .num_rline_commands = ed_get_num_rline_commands
      ),
      .syn = SubSelfInit (ed, syn,
        .append = ed_syn_append,
        .get_ftype_idx = ed_syn_get_ftype_idx
      ),
      .reg = SubSelfInit (ed, reg,
        .set = ed_reg_set,
        .setidx = ed_reg_setidx
      ),
      .append = SubSelfInit (ed, append,
        .win = ed_append_win,
        .message = ed_append_message,
        .message_fmt = ed_append_message_fmt,
        .toscratch = ed_append_toscratch,
        .toscratch_fmt = ed_append_toscratch_fmt,
        .command_arg = ed_append_command_arg,
        .rline_commands = ed_append_rline_commands,
        .rline_command = ed_append_rline_command
      ),
      .readjust = SubSelfInit (ed, readjust,
        .win_size =ed_win_readjust_size
      ),
      .rline = SubSelfInit (ed, rline,
        .new = ed_rline_new,
        .new_with = ed_rline_new_with
      ),
      .buf = SubSelfInit (ed, buf,
        .change = ed_buf_change,
        .get = ed_buf_get
      ),
      .win = SubSelfInit (ed, win,
        .init = ed_win_init,
        .new = ed_win_new,
        .new_special = ed_win_new_special,
        .change = ed_win_change
      ),
      .menu = SubSelfInit (ed, menu,
        .new = ed_menu_new,
        .free = ed_menu_free,
        .create = menu_create
      ),
      .sh = SubSelfInit (ed, sh,
        .popen = ed_sh_popen
      ),
      .history = SubSelfInit (ed, history,
        .add = ed_history_add,
        .read = ed_history_read,
        .write = ed_history_write,
        .add_lines = ed_history_add_lines,
        .set = SubSelfInit (edhistory, set,
          .rline_file = ed_history_set_rline_file,
          .search_file = ed_history_set_search_file
        ),
        .get = SubSelfInit (edhistory, get,
          .rline_file = ed_history_get_rline_file,
          .search_file = ed_history_get_search_file
        )
      ),
      .draw = SubSelfInit (ed, draw,
        .current_win = ed_draw_current_win
      ),
    ),
    .__Win__ = ClassInit (win,
      .self = SelfInit (win,
        .set = SubSelfInit (win, set,
          .current_buf = win_set_current_buf,
          .previous_idx = win_set_previous_idx,
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
          .info = SubSelfInit (winget, info,
            .as_type = win_get_info_as_type
          ),
          .current_buf = win_get_current_buf,
          .current_buf_idx = win_get_current_buf_idx,
          .buf_by_idx = win_get_buf_by_idx,
          .buf_by_name = win_get_buf_by_name,
          .buf_head = win_get_buf_head,
          .buf_next = win_get_buf_next,
          .num_buf = win_get_num_buf,
          .num_cols = win_get_num_cols
        ),
        .isit = SubSelfInit (win, isit,
          .special_type = win_isit_special_type
        ),
        .pop = SubSelfInit (win, pop,
          .current_buf = win_pop_current_buf
        ),
        .adjust = SubSelfInit (win, adjust,
          .buf_dim = win_adjust_buf_dim,
        ),
        .edit_fname = win_edit_fname,
        .draw = win_draw,
        .free_info = win_free_info,
        .append_buf = win_append_buf,
        .dim_calc = win_dim_calc
      ),
    ),
    .__Buf__ = ClassInit (buf,
      .self = SelfInit (buf,
        .free = SubSelfInit (buf, free,
          .row = buf_free_row,
          .rows = buf_free_rows,
          .info = buf_free_info,
          .line = buf_free_line
        ),
        .get = SubSelfInit (buf, get,
          .info = SubSelfInit (bufget, info,
            .as_type = buf_get_info_as_type,
          ),
          .prop = SubSelfInit (bufget, prop,
            .tabwidth = buf_get_prop_tabwidth
          ),
          .row = SubSelfInit (bufget, row,
            .at = buf_get_row_at,
            .current = buf_get_row_current,
            .current_bytes = buf_get_row_current_bytes,
            .bytes_at = buf_get_row_bytes_at,
            .col_idx = buf_get_row_col_idx
          ),
          .my_parent = buf_get_my_parent,
          .my_root = buf_get_my_root,
          .basename = buf_get_basename,
          .fname = buf_get_fname,
          .ftype_name = buf_get_ftype_name,
          .shared_str = buf_get_shared_str,
          .flags = buf_get_flags,
          .num_lines = buf_get_num_lines,
          .size = buf_get_size,
          .contents = buf_get_contents,
          .current_word = buf_get_current_word,
          .current_row_idx = buf_get_current_row_idx,
          .current_col_idx = buf_get_current_col_idx,
          .current_video_row = buf_get_current_video_row,
          .current_video_col = buf_get_current_video_col,
        ),
        .set = SubSelfInit (buf, set,
          .mode = buf_set_mode,
          .fname = buf_set_fname,
          .ftype = buf_set_ftype,
          .backup = buf_set_backup,
          .modified = buf_set_modified,
          .autosave = buf_set_autosave,
          .autochdir = buf_set_autochdir,
          .on_emptyline = buf_set_on_emptyline,
          .video_first_row = buf_set_video_first_row,
          .show_statusline = buf_set_show_statusline,
          .as = SubSelfInit (bufset, as,
            .unnamed = buf_set_as_unnamed,
            .non_existant = buf_set_as_non_existant,
            .pager = buf_set_as_pager
          ),
          .normal = SubSelfInit (bufset, normal,
            .at_beg_cb = buf_set_normal_beg_cb,
            .at_end_cb = buf_set_normal_end_cb
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
          .set = buf_ftype_set,
          .init = buf_ftype_init,
          .free = buf_ftype_free
        ),
        .to = SubSelfInit (buf, to,
          .video = buf_to_video
        ),
        .isit = SubSelfInit (buf, isit,
          .special_type = buf_isit_special_type
        ),
        .current = SubSelfInit (buf, current,
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
          .from_fp = buf_read_from_fp
        ),
        .iter = SubSelfInit (buf, iter,
          .free = buf_iter_free,
          .new = buf_iter_new,
          .next = buf_iter_next
        ),
        .Action = SubSelfInit (buf, Action,
          .new = buf_Action_new,
          .free = buf_Action_free,
          .set_with = buf_Action_set_with,
          .set_with_current = buf_Action_set_with_current
        ),
        .action = SubSelfInit (buf, action,
          .new = buf_action_new,
          .free = buf_action_free,
          .new_with = buf_action_new_with
        ),
        .undo = SubSelfInit (buf, undo,
          .pop = buf_undo_pop,
          .push = buf_undo_push,
          .free = buf_undo_free,
          .init = buf_undo_init,
          .exec = buf_undo_exec,
          .clear = buf_undo_clear,
          .insert = buf_undo_insert,
          .delete_line = buf_undo_delete_line,
          .replace_line = buf_undo_replace_line
        ),
        .redo = SubSelfInit (buf, redo,
          .pop = buf_redo_pop,
          .push = buf_redo_push
        ),
        .normal = SubSelfInit (buf, normal,
          .handle = SubSelfInit (bufnormal, handle,
            .d = buf_normal_handle_d,
            .G = buf_normal_handle_G,
            .g = buf_normal_handle_g,
            .c = buf_normal_handle_c,
            .W = buf_normal_handle_W,
            .F = buf_normal_handle_F,
            .L = buf_normal_handle_L,
            .comma = buf_normal_handle_comma,
            .ctrl_w = buf_normal_handle_ctrl_w,
          ),
          .visual = SubSelfInit (bufnormal, visual,
            .bw = buf_normal_visual_bw,
            .lw = buf_normal_visual_lw,
            .cw = buf_normal_visual_cw
          ),
          .up = buf_normal_up,
          .bof = buf_normal_bof,
          .bol = buf_normal_bol,
          .eof = buf_normal_eof,
          .eol = buf_normal_eol,
          .put = buf_normal_put,
          .join = buf_normal_join,
          .yank = buf_normal_yank,
          .Yank = buf_normal_Yank,
          .down = buf_normal_down,
          .left = buf_normal_left,
          .right = buf_normal_right,
          .delete = buf_normal_delete,
          .noblnk =  buf_normal_noblnk,
          .page_up = buf_normal_page_up,
          .end_word = buf_normal_end_word,
          .page_down = buf_normal_page_down,
          .delete_eol = buf_normal_delete_eol,
          .goto_linenr = buf_normal_goto_linenr,
          .change_case = buf_normal_change_case,
          .replace_character = buf_normal_replace_character,
          .replace_character_with = buf_normal_replace_character_with
        ),
        .delete = SubSelfInit (buf, delete,
          .word = buf_delete_word,
          .line = buf_delete_line
        ),
        .insert = SubSelfInit (buf, insert,
          .string = buf_insert_string,
          .mode = buf_insert_mode,
          .new_line = buf_insert_new_line,
        ),
        .adjust = SubSelfInit (buf, adjust,
          .col = buf_adjust_col,
          .view = buf_adjust_view,
          .marks = buf_adjust_marks
        ),
        .jump = SubSelfInit (buf, jump,
          .to = buf_jump_to,
          .push = buf_jump_push
        ),
        .jumps = SubSelfInit (buf, jumps,
          .free = buf_jumps_free,
          .init = buf_jumps_init
        ),
        .mark = SubSelfInit (buf, mark,
          .set = buf_mark_set,
          .jump = buf_mark_jump,
          .restore = buf_mark_restore
        ),
        .draw = buf_draw,
        .clear = buf_clear,
        .flush = buf_flush,
        .rline = buf_rline,
        .write = buf_write,
        .search = buf_search,
        .indent = buf_indent,
        .input_box = buf_input_box,
        .init_fname = buf_init_fname,
        .substitute = buf_substitute,
        .append_with = buf_append_with,
        .backupfile = buf_com_backupfile,
        .draw_current_row = buf_draw_current_row
      ),
    ),
    .__Msg__ = ClassInit (msg,
      .self = SelfInit (msg,
        .set = ed_msg_set,
        .set_fmt = ed_msg_set_fmt,
        .line = ed_msg_line,
        .send = ed_msg_send,
        .send_fmt = ed_msg_send_fmt,
        .error = ed_msg_error,
        //.error_fmt = ed_msg_error_fmt,
        .fmt = ed_msg_fmt,
        .write = ed_msg_write,
        .write_fmt = ed_msg_write_fmt
      ),
    ),
    .__Error__ = ClassInit (error,
      .self = SelfInit (error,
        .string = ed_error_string
      ),
    ),
    .__Re__ = __init_re__ (),
    .__Fd__ = __init_fd__ (),
    .__Dir__ = __init_dir__ (),
    .__File__ = __init_file__ (),
    .__Path__ = __init_path__ (),
    .__Vsys__ = __init_vsys__ (),
    .__Smap__ = __init_smap__ (),
    .__Imap__ = __init_imap__ (),
    .__Term__ = __init_term__ (),
    .__Video__ = __init_video__ (),
    .__Rline__ = __init_rline__ (),
    .__String__ = __init_string__ (),
    .__Cstring__ = __init_cstring__ (),
    .__Vstring__ = __init_vstring__ (),
    .__Ustring__ = __init_ustring__ ()
  );

  this->__Cursor__.self = this->__Term__.self.__Cursor__;
  this->__Screen__.self = this->__Term__.self.__Screen__;
  this->__Input__.self = this->__Term__.self.__Input__;

  $my(video) = NULL;
  $my(term)  = NULL;
  $my(env)   = NULL;

  return this;
}

private int fp_tok_cb (Vstring_t *unused, char *bytes, void *fp_type) {
  (void) unused;
  fp_t *fpt = (fp_t *) fp_type;
  ssize_t num = fprintf (fpt->fp, "%s\n", bytes);
  if   (0 > num) fpt->error = errno;
  else fpt->num_bytes += num;
  return STRCHOP_OK;
}

public mutable size_t tostderr (char *bytes) {
  Vstring_t unused;
  fp_t fpt = {.fp = stderr};
  cstring_chop (bytes, '\n', &unused, fp_tok_cb, &fpt);
  return fpt.num_bytes;
}

private int ed_main (ed_t *this, buf_t *buf) {
  if ($my(state) & ED_SUSPENDED)
    self(resume);
  else {
    $from(buf, flags) |= BUF_IS_VISIBLE;
    Win.draw (this->current);
  }

  $my(state) = UNSET;

/*
  Msg.send (this, COLOR_CYAN,
      "Υγειά σου Κόσμε και καλό ταξίδι στο ραντεβού με την αιωνιότητα");
 */

  return ed_loop (this, buf);
}

private ed_t *ed_init (E_T *E, ed_opts opts) {
  ed_t *this = AllocType (ed);
  $myprop = AllocProp (ed);

  $my(Me) = E->__Ed__;

  $my(term) = $from($my(Me), term);
  $my(env)  = $from($my(Me), env);

  $my(__Win__)     = &E->__Ed__->__Win__;
  $my(__Buf__)     = &E->__Ed__->__Buf__;
  $my(__I__)       = &E->__Ed__->__I__;
  $my(__E__)       = E;
  $my(__Re__)      = &E->__Ed__->__Re__;
  $my(__Msg__)     = &E->__Ed__->__Msg__;
  $my(__Dir__)     = &E->__Ed__->__Dir__;
  $my(__Term__)    = &E->__Ed__->__Term__;
  $my(__File__)    = &E->__Ed__->__File__;
  $my(__Path__)    = &E->__Ed__->__Path__;
  $my(__Vsys__)    = &E->__Ed__->__Vsys__;
  $my(__Smap__)    = &E->__Ed__->__Smap__;
  $my(__Imap__)    = &E->__Ed__->__Imap__;
  $my(__Input__)   = &E->__Ed__->__Input__;
  $my(__Video__)   = &E->__Ed__->__Video__;
  $my(__Rline__)   = &E->__Ed__->__Rline__;
  $my(__Error__)   = &E->__Ed__->__Error__;
  $my(__Screen__)  = &E->__Ed__->__Screen__;
  $my(__Cursor__)  = &E->__Ed__->__Cursor__;
  $my(__String__)  = &E->__Ed__->__String__;
  $my(__Cstring__) = &E->__Ed__->__Cstring__;
  $my(__Vstring__) = &E->__Ed__->__Vstring__;
  $my(__Ustring__) = &E->__Ed__->__Ustring__;

  $my(has_topline) = $my(has_msgline) = $my(has_promptline) = 1;

  Term.set ($my(term));

  int frow = opts.first_row;
  int fcol = opts.first_col;
  int num_rows = opts.num_rows;
  int num_cols = opts.num_cols;

  ifnot (num_rows) num_rows = $from($my(term), num_rows);
  ifnot (num_cols) num_cols = $from($my(term), num_cols);

  $my(dim) = ed_dim_new (this, frow, num_rows, fcol, num_cols);

  $my(video) = Video.new (OUTPUT_FD, $my(dim)->num_rows, $my(dim)->num_cols, $my(dim)->first_row, $my(dim)->first_col);

  $my(msg_row)     = $my(dim)->num_rows;
  $my(prompt_row)  = $my(msg_row) - 1;
  $my(topline_row) = $my(dim)->first_row;

  $my(ed_str)  = String.new ($my(dim)->num_cols);
  $my(topline) = String.new ($my(dim)->num_cols);
  $my(msgline) = String.new ($my(dim)->num_cols);
  $my(last_insert) = String.new ($my(dim)->num_cols);
  $my(rl_last_component) = Vstring.new ();
  $my(uline) = Ustring.new ();

  $my(state) = UNSET;
  $my(msg_tabwidth) = 2;
  $my(saved_cwd) = Dir.current ();

  $my(history) = AllocType (hist);
  $my(history)->search = AllocType (h_search);
  $my(history)->rline = AllocType (h_rline);
  $my(history)->rline->history_idx = 0;
  $my(max_num_hist_entries) = RLINE_HISTORY_NUM_ENTRIES;
  $my(max_num_undo_entries) = UNDO_NUM_ENTRIES;
  $my(hs_file) = self(history.set.search_file, opts.hs_file);
  $my(hrl_file) = self(history.set.rline_file, opts.hrl_file);

  if (opts.flags & ED_INIT_OPT_LOAD_HISTORY)
    self(history.read);

  $my(has_ed_rline_commands) = 0;
  $my(exit_quick) = 0;
  $my(repeat_mode) = 0;
  $my(record) = 0;
  $my(record_idx) = -1;
  $my(record_header_len) = -1;
  $my(record_cb) = ed_record_default;
  $my(i_record_cb) = ed_i_record_default;
  $my(init_record_cb) = ed_init_record_default;
  $my(records)[NUM_RECORDS] = vstring_new ();
  Vstring.append_with_len ($my(records)[NUM_RECORDS], " ", 1);
  Vstring.append_with_len ($my(records)[NUM_RECORDS], " ", 1);

  this->name_gen = ('z' - 'a') + 1;

  ed_reg_init_all (this);

  ed_init_syntaxes (this);

  ed_init_commands (this);

  ed_init_special_win (this);

  $my(num_rline_cbs) = $my(num_on_normal_g_cbs) =
  $my(num_lw_mode_cbs) = $my(num_cw_mode_cbs) =
  $my(num_at_exit_cbs) = $my(num_file_mode_cbs) = 0;

  ed_set_cw_mode_actions_default (this);
  ed_set_lw_mode_actions_default (this);
  ed_set_word_actions_default (this);
  ed_set_line_mode_actions_default (this);
  ed_set_file_mode_actions_default (this);

  $my(name) = ed_name_gen (&$OurRoots(name_gen), "ed:", 3);
  $OurRoots(orig_num_items)++;

  return this;
}

private ed_t *E_init (E_T *this, ed_opts opts) {
  ed_t *ed = ed_init (this, opts);

  int cur_idx = $my(cur_idx);

  current_list_append ($myprop, ed);

  if ($my(prev_idx) is -1)
    $my(prev_idx) = 0;
  else
    $my(prev_idx) = cur_idx;

  if (opts.init_cb isnot NULL) {
    opts.init_cb (ed, opts);

    if (NULL is $my(at_init_cb))
      $my(at_init_cb) = opts.init_cb;
  } else {
    ifnot (NULL is $my(at_init_cb))
      $my(at_init_cb) (ed, opts);
  }

  $from(ed, root) = this;
  $from(ed, E) = this->self;

  string_replace_with ($from(ed, records)[NUM_RECORDS]->head->data,
      $from(ed, init_record_cb) (ed));

  return ed;
}

private ed_t *E_new (Class (E) *this, ed_opts opts) {
  if (opts.term_flags)
    term_set_state_bit (self(get.term), opts.term_flags);

  ed_t *ed = E_init (this, opts);

  int num_win = opts.num_win;
  if (num_win <= 0) num_win = 1;

  int num_frames = 1;

  win_t *w;
  loop (num_win) {
    w = ed_win_new (ed, NULL, num_frames);
    ed_append_win (ed, w);
  }

  ed_set_current_win (ed, $from(ed, num_special_win));

  return ed;
}

private int E_delete (E_T *this, int idx, int force_current) {
  $my(error_state) = 0;

  if (1 is $my(num_items) and 0 is force_current) {
    $my(error_state) |= LAST_INSTANCE_ERROR_STATE;
    return NOTOK;
  }

  if (idx < 0 or idx >= $my(num_items)) {
    $my(error_state) |= IDX_OUT_OF_BOUNDS_ERROR_STATE;
    return NOTOK;
  }

  if (idx is $my(cur_idx)) {
    ifnot (force_current) {
      $my(error_state) |= ARG_IDX_IS_CUR_IDX_ERROR_STATE;
      return NOTOK;
    } else
      $my(prev_idx) = $my(cur_idx) - 1;
  } else
    $my(prev_idx) = $my(cur_idx);

  ed_t *ed = list_pop_at ($myprop, ed_t, idx);

  ifnot (NULL is ed) ed_free (ed);

  if ($my(prev_idx) is -1)
    if ($my(num_items))
      $my(prev_idx) = 0;

  return OK;
}

private int E_set_i_dir (Class (E) *__e__, char *dir) {
  Class (ed) *this = __e__->__Ed__;
  if (NOTOK is __env_check_directory__ (dir, "interpreter directory", 0, 0, 0))
    return NOTOK;

  string_replace_with ($my(env)->i_dir, dir);
  return OK;
}

private string_t *__venv_get__ (Class (ed) *this, string_t *v) {
  String.replace_with_len ($my(env)->env_str, v->bytes, v->num_bytes);
  return $my(env)->env_str;
}

private string_t *E_get_env (Class (E) *__e__, char *name) {
  Class (ed) *this = __e__->__Ed__;
  if (cstring_eq (name, "group_name"))  return __venv_get__ (this, $my(env)->group_name);
  if (cstring_eq (name, "user_name"))  return __venv_get__ (this, $my(env)->user_name);
  if (cstring_eq (name, "term_name")) return __venv_get__ (this, $my(env)->term_name);
  if (cstring_eq (name, "home_dir")) return __venv_get__ (this, $my(env)->home_dir);
  if (cstring_eq (name, "path")) return __venv_get__ (this, $my(env)->path);
  if (cstring_eq (name, "i_dir")) return __venv_get__(this, $my(env)->i_dir);
  if (cstring_eq (name, "my_dir")) return __venv_get__ (this,  $my(env)->my_dir);
  if (cstring_eq (name, "tmp_dir")) return __venv_get__ (this, $my(env)->tmp_dir);
  if (cstring_eq (name, "data_dir")) return __venv_get__ (this, $my(env)->data_dir);
  if (cstring_eq (name, "diff_exec")) return __venv_get__ (this, $my(env)->diff_exec);
  if (cstring_eq (name, "xclip_exec")) return __venv_get__ (this,  $my(env)->xclip_exec);
  return NULL;
}

private ed_t *E_set_current (E_T *this, int idx) {
  int cur_idx = $my(cur_idx);
  if (cur_idx is idx) return $my(current);

  if (INDEX_ERROR is current_list_set ($myprop, idx))
    return NULL;

  $my(prev_idx) = cur_idx;

  return $my(current);
}

private ed_t *E_set_next (E_T *this) {
  int idx = $my(cur_idx);
  if (idx is $my(num_items) - 1)
    idx = 0;
  else
    idx++;

  return E_set_current (this, idx);
}

private ed_t *E_set_prev (E_T *this) {
  int idx = $my(cur_idx);
  if (idx is 0)
    idx = $my(num_items) - 1;
  else
    idx--;

  return E_set_current (this, idx);
}

private string_t *E_create_image (Class (E) *this) {
  if (NULL is $my(image_name) or NULL is $my(image_file))
    return NULL;

  string_t *img = string_new_with_fmt (
       "var ed_instances = %d\n"
       "var flags = 0\n"
       "var frame_zero = 0\n"
       "var draw = 1\n"
       "var donot_draw = 0\n"
       "var save_image = %d\n"
       "var persistent_layout = %d\n"
       "var ed_cur_idx = %d\n"
       "var win_cur_idx = %d\n"
       "var buf_cur_idx = %d\n",
    $my(num_items),
    $my(save_image),
    $my(persistent_layout),
    $my(cur_idx),
    $my(current)->cur_idx,
    $my(current)->current->cur_idx);

  int g_num_buf = 0;
  int g_num_win = 0;
  int g_num_ed = 0;

  ed_t *ed = self(get.head);

  while (ed isnot NULL) {
    int num_win = ed_get_num_win (ed, NO_COUNT_SPECIAL);

    ifnot (g_num_ed) {
      string_append (img, "var ");
      g_num_ed++;
    }

    string_append_fmt (img,
       "ed = ed_new (%d)\n", num_win);

    ifnot (g_num_win) {
      string_append (img, "var ");
      g_num_win++;
    }

    string_append (img, "cwin = ed_get_current_win (ed)\n");

    int l_num_win = 0;
    win_t *cwin = ed_get_win_head (ed);

    while (cwin) {
      if (win_isit_special_type (cwin)) goto next_win;

      if (l_num_win++)
        string_append (img, "cwin = ed_get_win_next (ed, cwin)\n\n");

      buf_t *buf = win_get_buf_head (cwin);
      while (buf) {
        if (($from(buf, flags) & BUF_IS_SPECIAL)) goto next_buf;
        char *bufname = $from(buf, fname);
        char *ftype_name = $from(buf, ftype)->name;
        int cur_row_idx = buf->cur_idx;
        long autosave = $from(buf, autosave);
        if (autosave > 0) autosave /= 60;
        string_append (img, "\n");

        ifnot (g_num_buf) {
          string_append (img, "var ");
          g_num_buf++;
        }

        string_append_fmt (img,
              "buf = win_buf_init (cwin, frame_zero, flags)\n"
              "buf_init_fname (buf, \"%s\")\n"
              "buf_set_ftype (buf, \"%s\")\n"
              "buf_set_autosave (buf, %ld)\n"
              "buf_set_row_idx (buf, %d)\n"
              "win_append_buf (cwin, buf)\n",
            bufname,
            ftype_name,
            autosave,
            cur_row_idx);

next_buf:
        buf = win_get_buf_next (cwin, buf);
      }

next_win:
      cwin = ed_get_win_next (ed, cwin);
    }

    ed = ed->next;
    ifnot (NULL is ed)
      string_append (img,
         "ed = e_set_ed_next ()\n");
  }

  string_append_fmt (img, "\n"
        "e_set_image_file (\"%s\")\n"
        "e_set_image_name (\"%s\")\n"
        "e_set_save_image (save_image)\n"
        "e_set_persistent_layout (persistent_layout)\n\n"
        "ed = e_set_ed_by_idx (ed_cur_idx)\n"
        "cwin = ed_set_current_win (ed, win_cur_idx)\n"
        "win_set_current_buf (cwin, buf_cur_idx, donot_draw)\n\n"
        "win_draw (cwin)\n",
     $my(image_file),
     $my(image_name));

  return img;
}

private int E_save_image (Class (E) *this, char *name) {
  ifnot ($my(num_items)) return NOTOK;

  if (NULL is name and NULL is $my(image_file))
    if ($my(current) isnot NULL)
      if ($my(current)->current isnot NULL)
        if ($my(current)->current->current isnot NULL)
          name = path_basename ($from ($my(current)->current->current, fname));

  string_t *fname = NULL;
  char *iname = NULL;

  ifnot (NULL is name) {
    fname = string_new_with (name);
    iname = cstring_dup (name, bytelen (name));
  } else {
    if (NULL is $my(image_file))
      return NOTOK;

    ifnot (NULL is $my(image_name))
      iname = cstring_dup ($my(image_name), bytelen ($my(image_name)));
    else
      iname = path_basename_sans_extname ($my(image_file));

    fname = string_new_with ($my(image_file));
  }

  int retval = NOTOK;

  char *extname = path_extname (fname->bytes);

  size_t exlen = bytelen (extname);
  if (exlen) {
    if (exlen isnot fname->num_bytes) {
      char *p = fname->bytes + fname->num_bytes - 1;
      while (*p isnot '.') {
        p--;
        string_clear_at (fname, fname->num_bytes - 1);
      }
    } else  // .file
      string_append_byte (fname, '.');
  } else
    string_append_byte (fname, '.');

  ifnot (path_is_absolute (fname->bytes)) {
    string_prepend (fname, "/scripts/");
    string_prepend (fname, self(get.env, "i_dir")->bytes);
  }

  string_append (fname, "i");

  self(set.image_file, fname->bytes);
  self(set.image_name, iname);

  string_t *image = self(create_image);

  FILE *fp = fopen (fname->bytes, "w");
  if (NULL is fp) goto theend;

  fprintf (fp, "%s\n", image->bytes);
  fclose (fp);
  retval = OK;

theend:
  free (iname);
  string_free (fname);
  string_free (image);
  return retval;
}

private void E_set_image_file (E_T *this, char *name) {
  if (NULL is name) return;

  ifnot (NULL is $my(image_file))
    free ($my(image_file));

  char *cwd = NULL;

  size_t len = bytelen (name);

  char *extname = path_extname (name);
  size_t exlen = bytelen (extname);

  int hasnot_ext = (0 is exlen or (exlen and 0 is cstring_eq (extname, ".i")));

  if (hasnot_ext) len += 2;

  ifnot (path_is_absolute (name)) {
    cwd = dir_current ();
    if (NULL is cwd) return;
    len += bytelen (cwd) + 1;
  }

  $my(image_file) = Alloc (len + 1);

  ifnot (path_is_absolute (name))
    cstring_cp_fmt ($my(image_file), len + 1, "%s/%s", cwd, name);
  else
    cstring_cp ($my(image_file), len + 1, name, len - (hasnot_ext ? 2 : 0));

  if (hasnot_ext)
    cstring_cat ($my(image_file), len + 1, ".i");
}

private void E_set_image_name (E_T *this, char *name) {
  if (NULL is name) return;

  ifnot (NULL is $my(image_name))
    free ($my(image_name));

  $my(image_name) = cstring_dup (name, bytelen (name));
}

private void E_set_save_image (E_T *this, int val) {
  $my(save_image) = val;
}

private void E_set_persistent_layout (E_T *this, int val) {
  $my(persistent_layout) = val;
}

private void E_set_state (E_T *this, int state) {
  $my(state) = state;
}

private void E_set_state_bit (E_T *this, int bit) {
  $my(state) |= bit;
}

private void E_unset_state_bit (E_T *this, int bit) {
  $my(state) &= ~(bit);
}

private int E_test_state_bit (E_T *this, int bit) {
  return $my(state) & (bit);
}

private Class (i) *E_get_i_class (E_T *this) {
  return $my(__I__);
}

private int E_get_state (E_T *this) {
  return $my(state);
}

private term_t *E_get_term (E_T *this) {
  return this->__Ed__->prop->term;
}

private ed_t *E_get_current (Class (E) *this) {
  return $my(current);
}

private ed_t *E_get_head (Class (E) *this) {
  return $my(head);
}

private ed_t *E_get_next (Class (E) *this, ed_t *ed) {
  (void) this;
  return ed->next;
}

private int E_get_num (Class (E) *this) {
  return $my(num_items);
}

private int E_get_idx (Class (E) *this, ed_t *ed) {
  int retval = NOTOK;
  ed_t *it = $my(head);
  int idx = -1;
  while (it) {
    idx++;
    if (ed is it) {
      retval = idx;
      break;
    }
    it = it->next;
  }

  return retval;
}

private int E_get_current_idx (Class (E) *this) {
  return $my(cur_idx);
}

private int E_get_prev_idx (Class (E) *this) {
  return $my(prev_idx);
}

private int E_get_error_state (Class (E) *this) {
  return $my(error_state);
}

private void E_set_at_init_cb (Class (E) *this, EdAtInit_cb cb) {
  $my(at_init_cb) = cb;
}

private void E_set_at_exit_cb (Class (E) *this, EAtExit_cb cb) {
  if (NULL is cb) return;
  $my(num_at_exit_cbs)++;
  ifnot ($my(num_at_exit_cbs) - 1)
    $my(at_exit_cbs) = Alloc (sizeof (EAtExit_cb));
  else
    $my(at_exit_cbs) = Realloc ($my(at_exit_cbs), sizeof (EAtExit_cb) * $my(num_at_exit_cbs));

  $my(at_exit_cbs)[$my(num_at_exit_cbs) -1] = cb;
}

private ed_t *E_next_editor (E_T *this, buf_t **thisp) {
  ifnot ($my(num_items)) return $my(current);
  ed_t *ed = E_set_next (this);
  win_t * w = ed_get_current_win (ed);
  *thisp = win_get_current_buf (w);
  win_set_current_buf (w, w->cur_idx, DRAW);
  return ed;
}

private ed_t *E_prev_editor (E_T *this, buf_t **thisp) {
  ifnot ($my(num_items)) return $my(current);
  ed_t *ed = E_set_prev (this);
  win_t * w = ed_get_current_win (ed);
  *thisp = win_get_current_buf (w);
  win_set_current_buf (w, w->cur_idx, DRAW);
  return ed;
}

private ed_t *E_prev_focused_editor (E_T *this, buf_t **thisp) {
  ifnot ($my(num_items)) return $my(current);
  ed_t *ed = E_set_current (this, $my(prev_idx));
  win_t * w = ed_get_current_win (ed);
  *thisp = win_get_current_buf (w);
  win_set_current_buf (w, w->cur_idx, DRAW);
  return ed;
}

private ed_t *E_new_editor (E_T *this, buf_t **thisp, char *fname) {
  ed_t *ed = self(new, EdOpts());
  win_t *w = ed_get_current_win (ed);

  *thisp = win_buf_new (w, BufOpts (.fname = fname));

  current_list_set (*thisp, 0);

  win_append_buf (w, *thisp);
  win_set_current_buf (w, 0, DRAW);
  return ed;
}

private int E_exit_all (Class (E) *this) {
  int force = ($my(state) & ED_EXIT_ALL_FORCE);

  for (;;) {
    ed_t *ed = self(set.current, 0);
    if (NULL is ed) return OK;

    ifnot (force)
      if (NOTHING_TODO is ed_quit (ed, force, 1)) {
        self(set.current, $my(cur_idx));
        return NOTOK;
      }

    self(delete, $my(cur_idx), FORCE);
  }

  return OK;
}

private int E_main (E_T *this, buf_t *buf) {
  ed_t *ed = $from(buf, root);

  int retval = 0;
  int state = 0;

  if ($my(state) & (E_EXIT_ALL|E_EXIT_ALL_FORCE)) {
    if ($my(state) & (E_EXIT_ALL))
      state |= ED_EXIT_ALL;
    else
      state |= ED_EXIT_ALL_FORCE;

    goto exit_all;
  }

main:
  $my(state) &= ~(E_SUSPENDED|E_EXIT|E_PAUSE);
  if (ed_test_state_bit (ed, ED_PAUSE))
    $my(state) |= E_PAUSE;

  retval = ed_main (ed, buf);

  if ($my(state) & E_PAUSE)
    ed_set_state_bit (ed, ED_PAUSE);
  else
    if (ed_test_state_bit (ed, ED_PAUSE))
      $my(state) |= E_PAUSE;

  state = ed_get_state (ed);

  if (state & ED_EXIT)
    $my(state) |= E_EXIT;

  if (state & ED_SUSPENDED) {
    $my(state) |= E_SUSPENDED;
    ed_unset_state_bit (ed, ED_SUSPENDED);
    ed_suspend (ed);
    return retval;
  }

  if (state & ED_NEW) {
    if ($my(state) & E_DONOT_CHANGE_FOCUS)
      goto main;

    ed = E_new_editor (this, &buf, $from(ed, ed_str)->bytes);
    goto main;
  }

  if (state & ED_NEXT) {
    if ($my(state) & E_DONOT_CHANGE_FOCUS)
      goto main;

    ed = E_next_editor (this, &buf);
    goto main;
  }

  if (state & ED_PREV) {
    if ($my(state) & E_DONOT_CHANGE_FOCUS)
      goto main;

    ed = E_prev_editor (this, &buf);
    goto main;
  }

  if (state & ED_PREV_FOCUSED) {
    if ($my(state) & E_DONOT_CHANGE_FOCUS)
      goto main;

    ed = E_prev_focused_editor (this, &buf);
    goto main;
  }

exit_all:
  if ((state & ED_EXIT_ALL) or (state & ED_EXIT_ALL_FORCE)) {
    if ($my(save_image)) {
      if (NULL is $my(image_file))
        self(save_image, $my(image_name));
      else
        self(save_image, NULL);
    }

    if (NOTOK is self(exit_all)) {
      ed = self(get.current);
      buf = win_get_current_buf (ed_get_current_win (ed));
      goto main;
    }

    $my(state) |= E_EXIT;
    return retval;
  }

  if (($my(state) & E_EXIT)) {
    if ($my(state) & E_PAUSE)
      return retval;

    if ($my(save_image))
      if (
          ($my(num_items) is 1 and $my(orig_num_items) is 1) or
          ($my(num_items) is 1 and $my(persistent_layout) is 0) or
          ($my(num_items) is $my(orig_num_items) and $my(persistent_layout) is 1)) {
        if (NULL is $my(image_file))
          self(save_image, $my(image_name));
        else
          self(save_image, NULL);
      }

    self(delete, $my(cur_idx), FORCE);

    ifnot ($my(state) & E_DONOT_CHANGE_FOCUS)
      if ($my(num_items)) {
        ed = self(get.current);
        buf = win_get_current_buf (ed_get_current_win (ed));
        goto main;
      }
  }

  return retval;
}

private ed_T *ed_init_prop (ed_T *this) {
  $my(__Win__)     = &this->__Win__;
  $my(__Buf__)     = &this->__Buf__;
  $my(__I__)       = &this->__I__;
  $my(__E__)       = $my(__E__);
  $my(__Re__)      = &this->__Re__;
  $my(__Msg__)     = &this->__Msg__;
  $my(__Dir__)     = &this->__Dir__;
  $my(__Path__)    = &this->__Path__;
  $my(__File__)    = &this->__File__;
  $my(__Term__)    = &this->__Term__;
  $my(__Vsys__)    = &this->__Vsys__;
  $my(__Smap__)    = &this->__Smap__;
  $my(__Imap__)    = &this->__Imap__;
  $my(__Input__)   = &this->__Input__;
  $my(__Error__)   = &this->__Error__;
  $my(__Rline__)   = &this->__Rline__;
  $my(__Video__)   = &this->__Video__;
  $my(__Screen__)  = &this->__Screen__;
  $my(__Cursor__)  = &this->__Cursor__;
  $my(__String__)  = &this->__String__;
  $my(__Cstring__) = &this->__Cstring__;
  $my(__Vstring__) = &this->__Vstring__;

  $my(Me) = this;
  $my(video) = NULL;
  $my(num_at_exit_cbs) = 0;
  $my(at_exit_cbs) = NULL;
  return this;
}

private int Ed_init (ed_T *this) {
  ed_init_prop (this);

  $my(term) = Term.new ();
  $my(env)  = venv_new ();

  Term.set_state_bit ($my(term), (TERM_DONOT_CLEAR_SCREEN|TERM_DONOT_SAVE_SCREEN|TERM_DONOT_RESTORE_SCREEN));

  if (NOTOK is Term.set ($my(term)))
    return NOTOK;

  Term.reset ($my(term));

  Term.unset_state_bit ($my(term), (TERM_DONOT_CLEAR_SCREEN|TERM_DONOT_SAVE_SCREEN|TERM_DONOT_RESTORE_SCREEN));

  setvbuf (stdin, 0, _IONBF, 0);

  return OK;
}

public Class (E) *__init_ed__ (char *name) {
  Class (E) *this = AllocClass (E);

  *this = ClassInit (E,
    .self = SelfInit (E,
      .new = E_new,
      .init = E_init,
      .main = E_main,
      .delete = E_delete,
      .exit_all = E_exit_all,
      .save_image = E_save_image,
      .create_image = E_create_image,
      .get = SubSelfInit (E, get,
        .num = E_get_num,
        .env = E_get_env,
        .idx = E_get_idx,
        .head = E_get_head,
        .next = E_get_next,
        .term =  E_get_term,
        .state = E_get_state,
        .iclass = E_get_i_class,
        .current = E_get_current,
        .prev_idx = E_get_prev_idx,
        .current_idx = E_get_current_idx,
        .error_state = E_get_error_state
      ),
      .set = SubSelfInit (E, set,
        .i_dir = E_set_i_dir,
        .state = E_set_state,
        .state_bit = E_set_state_bit,
        .image_name = E_set_image_name,
        .image_file = E_set_image_file,
        .save_image = E_set_save_image,
        .persistent_layout = E_set_persistent_layout,
        .at_exit_cb = E_set_at_exit_cb,
        .at_init_cb = E_set_at_init_cb,
        .next = E_set_next,
        .prev = E_set_prev,
        .current = E_set_current
      ),
      .unset = SubSelfInit (E, unset,
        .state_bit = E_unset_state_bit
      ),
      .test = SubSelfInit (E, test,
        .state_bit = E_test_state_bit
      )
    ),
    .prop = $myprop,
    .__Ed__ =  editor_new ()
   );

  $my(Me) = this;

  $my(state) = 0;

  if (NOTOK is Ed_init (this->__Ed__)) {
    __deinit_ed__ (&this);
    return NULL;
  }

  $my(__Ed__) = this->__Ed__->self;
  $my(__I__) = __init_i__ (this);
  this->__Ed__->__I__ = *$my(__I__);

  cstring_cp ($my(name), MAXLEN_ED_NAME, name, MAXLEN_ED_NAME - 1);
  $my(name_gen) = ('z' - 'a') + 1;
  $my(cur_idx) = $my(prev_idx) = -1;
  $my(shared_reg)[0] = (Reg_t) {.reg = REG_SHARED_CHR};
  $my(image_name) = NULL;
  $my(save_image) = 0;
  $my(persistent_layout) = 0;
  $my(orig_num_items) = 0;

  return this;
}

private void ed_deallocate_prop (ed_T *this) {
  if ($myprop is NULL) return;
  Term.free (&$my(term));
  venv_free (&$my(env));
  free ($myprop);
  $myprop = NULL;
}

private void deinit_ed (ed_T *this, int restore_state) {
  if (restore_state)
    Term.reset ($my(term));
  ed_deallocate_prop (this);
}

public void __deinit_ed__ (Class (E) **thisp) {
  if (NULL is *thisp) return;

  Class (E) *this = *thisp;

  E_exit_all (this);

  for (int i = 0; i < $my(num_at_exit_cbs); i++)
    $my(at_exit_cbs)[i] ();

  reg_free (&$my(shared_reg)[0]);

  if ($my(image_name) isnot NULL)
    free ($my(image_name));

  if ($my(image_file) isnot NULL)
    free ($my(image_file));

  deinit_ed (this->__Ed__, 0 is ($my(state) & E_DONOT_RESTORE_TERM_STATE));

  free (this->__Ed__);

  __deinit_i__ (&$my(__I__));

  if ($my(num_at_exit_cbs)) free ($my(at_exit_cbs));

  free ($myprop);
  free (this);
  *thisp = NULL;
}

/* interpreter */

/* Derived from Tinyscript project at:
 * https://github.com/totalspectrum/
 * 
 * Many Thanks
 *
 * - added dynamic allocation
 * - ability to create more than one instance
 * - an instance can be passed as a first argument
 * - \ as the last character in the line is a continuation character
 * - syntax_error() got a char * argument, that describes the error
 * - added ignore_next_char() to ignore next char in parseptr
 * - print* function references got a FILE * argument
 * - remove abort() and disable exit()
 * - add is, isnot, true, false, ifnot, OK and NOTOK keywords
 * - add println (that emit a newline character, while print does not)
 * - add the ability to pass literal strings when calling C defined functions
 *   (note that they should be free'd after usage)
 * - print functions can print multibyte characters
 * - quite of many changes that integrates Tinyscript to this environment
 */

/* Tinyscript interpreter
 *
 * Copyright 2016 Total Spectrum Software Inc.
 *
 * +--------------------------------------------------------------------
 * ¦  TERMS OF USE: MIT License
 * +--------------------------------------------------------------------
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * +--------------------------------------------------------------------
 */

#define ERROR_COLOR "\033[31m"
#define TERM_RESET  "\033[m"
#define MAX_EXPR_LEVEL 5

private int i_parse_stmt (i_t *, int);
private int i_parse_expr (i_t *, ival_t *);

static inline unsigned i_StringGetLen (Istring_t s) {
  return (unsigned)s.len_;
}

static inline const char *i_StringGetPtr (Istring_t s) {
  return (const char *)(ival_t)s.ptr_;
}

static inline void i_StringSetLen (Istring_t *s, unsigned len) {
  s->len_ = len;
}

static inline void i_StringSetPtr (Istring_t *s, const char *ptr) {
  s->ptr_ = ptr;
}

private int i_StringEq (Istring_t ai, Istring_t bi) {
  const char *a, *b;
  size_t i, len;

  len = i_StringGetLen (ai);
  if (len isnot i_StringGetLen (bi))
    return 0;

  a = i_StringGetPtr (ai);
  b = i_StringGetPtr (bi);

  for (i = 0; i < len; i++) {
    if (*a isnot *b) return 0;
    a++; b++;
  }

  return 1;
}

private void i_print_string (i_t *this, FILE *fp, Istring_t s) {
  unsigned len = i_StringGetLen (s);
  const char *ptr = (const char *) i_StringGetPtr (s);
  while (len > 0) {
    this->print_byte (fp, *ptr);
    ptr++;
    --len;
  }
}

private void i_print_number (i_t *this, ival_t v) {
  unsigned long x;
  unsigned base = 10;
  int prec = 1;
  int digits = 0;
  int c;
  char buf[32];

  if (v < 0) {
    this->print_byte (this->out_fp, '-');
    x = -v;
  } else
    x = v;

  while (x > 0 or digits < prec) {
    c = x % base;
    x = x / base;
    if (c < 10) c += '0';
    else c = (c - 10) + 'a';
    buf[digits++] = c;
  }

  while (digits > 0) {
    --digits;
    this->print_byte (this->out_fp, buf[digits]);
  }
}

private char *i_stack_alloc (i_t *this, int len) {
  int mask = sizeof (ival_t) -1;
  ival_t base;

  len = (len + mask) & ~mask;
  base = ((ival_t) this->valptr) - len;
  if (base < (ival_t) this->symptr)
    return NULL;

  this->valptr = (ival_t *) base;
  return (char *) base;
}

private Istring_t i_dup_string (i_t *this, Istring_t orig) {
  Istring_t x;
  char *ptr;
  unsigned len = i_StringGetLen (orig);
  ptr = i_stack_alloc (this, len);
  if (ptr)
    byte_cp (ptr, i_StringGetPtr (orig), len);

  i_StringSetLen (&x, len);
  i_StringSetPtr (&x, ptr);
  return x;
}

private int i_err_ptr (i_t *this, int err) {
  i_print_string (this, this->err_fp, this->parseptr);
  this->print_bytes (this->err_fp, TERM_RESET "\n");
  return err;
}

private int i_syntax_error (i_t *this, const char *msg) {
  this->print_fmt_bytes (this->err_fp, "\n" ERROR_COLOR "SYNTAX ERROR: %s\nbefore: ", msg);
  return i_err_ptr (this, I_ERR_SYNTAX);
}

private int i_syntax_error_to_ed (i_t *this, const char *msg) {
  if (NULL is $OurRoots(current))
    return i_syntax_error (this, msg);

  const char *ptr = (const char *) i_StringGetPtr (this->parseptr);
  Msg.write_fmt ($OurRoots(current), "\nSYNTAX ERROR: %s\nbefore:\n%s\n", msg, ptr);
  return I_ERR_SYNTAX;
}

private int i_arg_mismatch (i_t *this) {
  this->print_fmt_bytes (this->err_fp, "\n" ERROR_COLOR "argument mismatch before:");
  return i_err_ptr (this, I_ERR_BADARGS);
}

private int i_too_many_args (i_t *this) {
  this->print_fmt_bytes (this->err_fp, "\n" ERROR_COLOR "too many arguments before:");
  return i_err_ptr (this, I_ERR_TOOMANYARGS);
}

private int i_unknown_symbol (i_t *this) {
  this->print_fmt_bytes (this->err_fp, "\n" ERROR_COLOR "unknown symbol before:");
  return i_err_ptr (this, I_ERR_UNKNOWN_SYM);
}

private int i_out_of_mem (i_t *this) {
  this->print_fmt_bytes (this->err_fp, ERROR_COLOR "out of memory" TERM_RESET "\n");
  return I_ERR_NOMEM;
}

private void i_reset_token (i_t *this) {
  i_StringSetLen (&this->token, 0);
  i_StringSetPtr (&this->token, i_StringGetPtr (this->parseptr));
}

private void i_ignore_last_token (i_t *this) {
  i_StringSetLen (&this->token, i_StringGetLen (this->token) - 1);
}

private void i_ignore_first_token (i_t *this) {
  i_StringSetPtr (&this->token, i_StringGetPtr (this->token) + 1);
  i_StringSetLen (&this->token, i_StringGetLen (this->token) - 1);
}

private void i_ignore_next_char (i_t *this) {
  i_StringSetPtr (&this->parseptr, i_StringGetPtr (this->parseptr) + 1);
  i_StringSetLen (&this->parseptr, i_StringGetLen (this->parseptr) - 1);
}

private int i_peek_char (i_t *this, unsigned int n) {
  if (i_StringGetLen (this->parseptr) <= n) return -1;
  return *(i_StringGetPtr (this->parseptr) + n);
}

private int i_get_char (i_t *this) {
  unsigned int len = i_StringGetLen (this->parseptr);

  ifnot (len) return -1;

  const char *ptr = i_StringGetPtr (this->parseptr);
  int c = *ptr++;

  --len;

  i_StringSetPtr (&this->parseptr, ptr);
  i_StringSetLen (&this->parseptr, len);
  i_StringSetLen (&this->token, i_StringGetLen (this->token) + 1);
  return c;
}

private void i_unget_char (i_t *this) {
  i_StringSetLen (&this->parseptr, i_StringGetLen (this->parseptr) + 1);
  i_StringSetPtr (&this->parseptr, i_StringGetPtr (this->parseptr) - 1);
  i_ignore_last_token (this);
}

private int i_ignore_ws (i_t *this) {
  int c;
  for (;;) {
    c = i_get_char (this);

    if (is_space (c)) {
      i_reset_token (this);
    } else
      break;
  }

  return c;
}

private void i_get_span (i_t *this, int (*testfn) (int)) {
  int c;
  do
    c = i_get_char (this);
  while (testfn (c));

  if (c isnot -1) i_unget_char (this);
}

private int isoperator (int c) {
  return NULL isnot cstring_byte_in_str ("+-/*=<>&|^", c);
}

private Sym * i_lookup_sym (i_t *this, Istring_t name) {
  Sym *s = this->symptr;

  while ((ival_t) s > (ival_t) this->arena) {
    --s;
    if (i_StringEq (s->name, name))
      return s;
  }

  return NULL;
}

private int i_do_next_token (i_t *this, int israw) {
  int c;
  int r = -1;

  Sym *sym = NULL;

  this->tokenSym = NULL;
  i_reset_token (this);

  c = i_ignore_ws (this);

  if (c is '\\' and i_peek_char (this, 0) is '\n') {
    this->linenum++;
    i_ignore_next_char (this);
    i_reset_token (this);
    c = i_ignore_ws (this);
  }

  if (c is '#') {
    do
      c = i_get_char (this);
    while (c >= 0 and c isnot '\n');
    this->linenum++;

    r = c;

  } else if (IS_DIGIT (c)) {
    if (c is '0' and NULL isnot Cstring.byte.in_str ("xX", i_peek_char (this, 0))
        and is_hexchar (i_peek_char(this, 1))) {
      i_get_char (this);
      i_ignore_first_token (this);
      i_ignore_first_token (this);
      i_get_span (this, is_hexchar);
      r = I_TOK_HEX_NUMBER;
    } else {
      i_get_span (this, is_digit);
      r = I_TOK_NUMBER;
    }

  } else if (c is '\'') {
      c = i_get_char (this); // get first
      if (c is '\\') i_get_char (this);
      int max = 4;
      r = I_TOK_SYNTAX_ERR;

      /* multibyte support */
      do {
        c = i_get_char (this);
        if (c is '\'') {
          i_ignore_first_token (this);
          i_ignore_last_token (this);
          r = I_TOK_CHAR;
          break;
        }
      } while (--max isnot 0);

  } else if (is_alpha (c)) {
    i_get_span (this, is_identifier);
    r = I_TOK_SYMBOL;
    // check for special tokens
    ifnot (israw) {
      this->tokenSym = sym = i_lookup_sym (this, this->token);
      if (sym) {
        r = sym->type & 0xff;
        this->tokenArgs = (sym->type >> 8) & 0xff;
        if (r < '@')
          r = I_TOK_VAR;
        this->tokenVal = sym->value;
      }
    }

  } else if (isoperator (c)) {
    i_get_span (this, isoperator);
    this->tokenSym = sym = i_lookup_sym (this, this->token);
    if (sym) {
      r = sym->type;
      this->tokenVal = sym->value;
    } else
      r = I_TOK_SYNTAX_ERR;

  } else if (c == '{') {
    int bracket = 1;
    i_reset_token (this);
    while (bracket > 0) {
      c = i_get_char (this);
      if (c < 0) return I_TOK_SYNTAX_ERR;

      if (c is '}')
        --bracket;
      else if (c == '{')
        ++bracket;
    }

    i_ignore_last_token (this);
    r = I_TOK_STRING;

  } else if (c is '"') {
    if (0 is (this->state & FUNCTION_ARGUMENT_SCOPE) or
        (this->state & FUNC_CALL_USRFUNC)) {
      this->state &= ~(FUNC_CALL_USRFUNC);
      i_reset_token (this);
      i_get_span (this, is_notquote);
      c = i_get_char (this);
      if (c < 0) return I_TOK_SYNTAX_ERR;
      i_ignore_last_token (this);
    } else {
      this->state &= ~(FUNC_CALL_BUILTIN);
      size_t len = 0;
      int pc = 0;
      int cc = 0;

      while (pc = cc, (cc = i_peek_char (this, len)) isnot -1) {
        if ('"' is cc and pc isnot '\\') break;
        len++;
      }

      if (cc is -1) return I_TOK_SYNTAX_ERR;

      char *ibuf = Alloc (len + 1);
      for (size_t i = 0; i < len; i++) {
        c = i_get_char (this);
        ibuf[i] = c;
      }
      ibuf[len] = '\0';

      c = i_get_char (this);
      this->tokenVal = (ival_t) ibuf;
      i_reset_token (this);
    }

    r = I_TOK_STRING;

  } else
    r = c;

  this->curToken = r;
  return r;
}

private int i_next_token (i_t *this) {
  return i_do_next_token (this, 0);
}

private int i_next_raw_token (i_t *this) {
  return i_do_next_token (this, 1);
}

private int i_push (i_t *this, ival_t x) {
  --this->valptr;
  if ((ival_t) this->valptr < (ival_t) this->symptr)
    return I_ERR_NOMEM;

  *this->valptr = x;
  return I_OK;
}

private ival_t i_pop (i_t *this) {
  ival_t r = 0;
  if ((ival_t) this->valptr < (ival_t) (this->arena + this->mem_size))
    r = *(this)->valptr++;

  return r;
}

private ival_t i_string_to_num (Istring_t s) {
  ival_t r = 0;
  int c;
  const char *ptr = i_StringGetPtr (s);
  int len = i_StringGetLen (s);
  while (len-- > 0) {
    c = *ptr++;
    ifnot (is_digit (c)) break;
    r = 10 * r + (c - '0');
  }
  return r;
}

private ival_t i_HexStringToNum (Istring_t s) {
  ival_t r = 0;
  int c;
  const char *ptr = i_StringGetPtr (s);
  int len = i_StringGetLen (s);
  while (len-- > 0) {
    c = *ptr++;
    ifnot (is_hexchar (c)) break;
    if (c <= '9')
      r = 16 * r + (c - '0');
    else if (c <= 'F')
      r = 16 * r + (c - 'A' + 10);
    else
      r = 16 * r + (c - 'a' + 10);
  }
  return r;
}

private Sym *i_define_sym (i_t *this, Istring_t name, int typ, ival_t value) {
  Sym *s = this->symptr;
  if (i_StringGetPtr (name) is NULL) return NULL;

  this->symptr++;
  if ((ival_t) this->symptr >= (ival_t) this->valptr)
    return NULL;

  s->name = name;
  s->value = value;
  s->type = typ;
  return s;
}

private Sym * i_define_var (i_t *this, Istring_t name) {
  return i_define_sym (this, name, INT, 0);
}

private Istring_t i_cstring_new (const char *str) {
  Istring_t x;
  i_StringSetLen (&x, bytelen (str));
  i_StringSetPtr (&x, str);
  return x;
}

private int i_parse_expr_list (i_t *this) {
  int err, c;
  int count = 0;
  ival_t v;

  do {
    err = i_parse_expr (this, &v);
    if (err isnot I_OK) return err;

      err = i_push (this, v);

    if (err isnot I_OK) return err;

    count++;

    c = this->curToken;
    if (c is ',') i_next_token (this);
  } while (c is ',');

  return count;
}

private int i_parse_char (i_t *this, ival_t *vp, Istring_t token) {
  const char *ptr = i_StringGetPtr (token);

  if (ptr[0] is '\'') return this->syntax_error (this, "error while getting a char token ");
  if (ptr[0] is '\\') {
    /* if (i_StringGetLen(token) != 2) return SyntaxError(); */
    if (ptr[1] is 'n') { *vp = '\n'; return I_OK; }
    if (ptr[1] is 't') { *vp = '\t'; return I_OK; }
    if (ptr[1] is 'r') { *vp = '\r'; return I_OK; }
    if (ptr[1] is '\\') { *vp = '\\'; return I_OK; }
    if (ptr[1] is '\'') { *vp = '\''; return I_OK; }
    return this->syntax_error (this, "unknown escape sequence");
  }

  if (ptr[0] >= ' ' and ptr[0] <= '~') {
    if (ptr[1] is '\'') {
      *vp = ptr[0];
      return I_OK;
    } else {
      return this->syntax_error (this, "error while taking character literal");
    }
  }

  /* multibyte support */
  int len = 0;
  utf8 c = Ustring.get.code_at ((char *) ptr, 4, 0, &len);

  if ('\'' isnot ptr[len])
    return this->syntax_error (this, "error while taking character literal");

  *vp = c;
  return I_OK;
}

private int i_parse_string (i_t *this, Istring_t str, int saveStrings, int topLevel) {
  int c,  r;
  Istring_t savepc = this->parseptr;
  Sym* savesymptr = this->symptr;

  this->parseptr = str;

  for (;;) {
    c = i_next_token (this);

    while (c is '\n' or c is ';') {
      if (c is '\n') this->linenum++;
      c = i_next_token (this);
     }

    if (c < 0) break;

    r = i_parse_stmt (this, saveStrings);
    if (r isnot I_OK) return r;

    c = this->curToken;
    if (c is '\n' or c is ';' or c < 0) {
      if (c is '\n') this->linenum++;
      continue;
    }
    else
      return this->syntax_error (this, "evaluated string failed, unknown token");
  }

  this->parseptr = savepc;

  ifnot (topLevel)
    this->symptr = savesymptr;

  return I_OK;
}

private int i_parse_func_call (i_t *this, Cfunc op, ival_t *vp, UserFunc *uf) {
  int paramCount = 0;
  int expectargs;
  int c;

  if (uf)
    expectargs = uf->nargs;
  else
    expectargs = this->tokenArgs;

  c = i_next_token (this);
  if (c isnot '(') return this->syntax_error (this, "expected open parentheses");

  this->state |= FUNCTION_ARGUMENT_SCOPE;

  c = i_next_token (this);
  if (c isnot ')') {
    paramCount = i_parse_expr_list (this);
    c = this->curToken;
    if (paramCount < 0) {
      this->state &= ~(OUT_OF_FUNCTION_ARGUMENT_SCOPE);
      return paramCount;
    }
  }

  this->state &= ~(OUT_OF_FUNCTION_ARGUMENT_SCOPE);

  if (c isnot ')')
    return this->syntax_error (this, "expected closed parentheses");

  if (expectargs isnot paramCount)
    return i_arg_mismatch (this);

  // we now have "paramCount" items pushed on to the stack
  // pop em off
  while (paramCount > 0) {
    --paramCount;
    this->fArgs[paramCount] = i_pop (this);
  }

  if (uf) {
    // need to invoke the script here
    // set up an environment for the script
    int i;
    int err;
    Sym* savesymptr = this->symptr;
    for (i = 0; i < expectargs; i++)
      i_define_sym (this, uf->argName[i], INT, this->fArgs[i]);

    err = i_parse_string (this, uf->body, 0, 0);
    *vp = this->fResult;
    this->symptr = savesymptr;
    this->state &= ~(FUNC_CALL_USRFUNC);
    return err;
  } else {
    *vp = op (this, this->fArgs[0], this->fArgs[1], this->fArgs[2],
                    this->fArgs[3], this->fArgs[4], this->fArgs[5],
                    this->fArgs[6], this->fArgs[7], this->fArgs[8]);

    this->state &= ~(FUNC_CALL_BUILTIN);
  }

  i_next_token (this);
  return I_OK;
}

// parse a primary value; for now, just a number or variable
// returns 0 if valid, non-zero if syntax error
// puts result into *vp
private int i_parse_primary (i_t *this, ival_t *vp) {
  int c, err;

  c = this->curToken;
  if (c is '(') {
    this->state |= FUNCTION_ARGUMENT_SCOPE;
    i_next_token (this);
    err = i_parse_expr (this, vp);

    if (err is I_OK) {
      c = this->curToken;

      if (c is ')') {
        i_next_token (this);
        this->state &= ~(OUT_OF_FUNCTION_ARGUMENT_SCOPE);
        return I_OK;
      }
    }

    return err;

  } else if (c is I_TOK_NUMBER) {
    *vp = i_string_to_num (this->token);
    i_next_token (this);
    return I_OK;

  } else if (c is I_TOK_HEX_NUMBER) {
    *vp = i_HexStringToNum (this->token);
    i_next_token (this);
    return I_OK;

  } else if (c is I_TOK_CHAR) {
      err = i_parse_char (this, vp, this->token);
      i_next_token (this);
      return err;

  } else if (c is I_TOK_VAR) {
    *vp = this->tokenVal;
    i_next_token (this);
    return I_OK;

  } else if (c is I_TOK_BUILTIN) {
    Cfunc op = (Cfunc) this->tokenVal;
    this->state |= FUNC_CALL_BUILTIN;
    return i_parse_func_call (this, op, vp, NULL);

  } else if (c is I_TOK_USRFUNC) {
    Sym *sym = this->tokenSym;
    ifnot (sym) return this->syntax_error (this, "user defined function, not declared");
    this->state |= FUNC_CALL_USRFUNC;
    err = i_parse_func_call (this, NULL, vp, (UserFunc *)sym->value);
    i_next_token (this);
    return err;

  } else if ((c & 0xff) is I_TOK_BINOP) {
    // binary operator
    Opfunc op = (Opfunc) this->tokenVal;
    ival_t v;
    i_next_token (this);
    err = i_parse_expr (this, &v);
    if (err is I_OK)
      *vp = op (0, v);

    return err;

  } else if (c is I_TOK_STRING) {
    *vp = this->tokenVal;
    i_next_token (this);
    return I_OK;

  } else
    return this->syntax_error (this, "syntax error");
}

/* parse one statement
 * 1 is true if we need to save strings we encounter (we've been passed
 * a temporary string)
 */

private int i_parse_stmt (i_t *this, int saveStrings) {
  int c;
  Istring_t name;
  ival_t val;
  int err = I_OK;

  c = this->curToken;

  if (c is I_TOK_VARDEF) {
    // a definition var a=x
    c = i_next_raw_token (this); // we want to get VAR_SYMBOL directly
    if (c isnot I_TOK_SYMBOL)
      return this->syntax_error (this, "expected symbol");

    if (saveStrings)
      name = i_dup_string (this, this->token);
    else
      name = this->token;

    this->tokenSym = i_define_var (this, name);

    ifnot (this->tokenSym) return I_ERR_NOMEM;

    c = I_TOK_VAR;
    /* fall through */
  }

  if (c is I_TOK_VAR) {
    // is this a=expr?
    Sym *s;
    name = this->token;
    s = this->tokenSym;
    c = i_next_token (this);
    // we expect the "=" operator, so verify that it is "="
    if (i_StringGetPtr (this->token)[0] isnot '=' or
        i_StringGetLen(this->token) isnot 1)
      return this->syntax_error (this, "expected =");

    ifnot (s) {
      i_print_string (this, this->err_fp, name);
      return i_unknown_symbol (this);
    }

    i_next_token (this);
    err = i_parse_expr (this, &val);
    if (err isnot I_OK) return err;

    s->value = val;

  } else if (c is I_TOK_BUILTIN or c is USRFUNC) {
    err = i_parse_primary (this, &val);
    return err;

  } else if (this->tokenSym and this->tokenVal) {
    int (*func) (i_t *, int) = (void *) this->tokenVal;
    err = (*func) (this, saveStrings);

  } else return this->syntax_error (this, "unknown token");

  if (err is I_ERR_OK_ELSE)
    err = I_OK;

  return err;
}

// parse a level n expression
// level 0 is the lowest level (highest precedence)
// result goes in *vp
private int i_parse_expr_level (i_t *this, int max_level, ival_t *vp) {
  int err = I_OK;
  int c;
  ival_t lhs;
  ival_t rhs;

  lhs = *vp;
  c = this->curToken;

  while ((c & 0xff) is I_TOK_BINOP) {
    int level = (c >> 8) & 0xff;
    if (level > max_level) break;

    Opfunc op = (Opfunc) this->tokenVal;
    i_next_token (this);
    err = i_parse_primary (this, &rhs);
    if (err isnot I_OK) return err;

    c = this->curToken;
    while ((c & 0xff) is I_TOK_BINOP) {
      int nextlevel = (c >> 8) & 0xff;
      if (level <= nextlevel) break;

      err = i_parse_expr_level (this, nextlevel, &rhs);
      if (err isnot I_OK) return err;

      c = this->curToken;
    }

    lhs = op (lhs, rhs);
  }

  *vp = lhs;
  return err;
}

private int i_parse_expr (i_t *this, ival_t *vp) {
  int err = i_parse_primary (this, vp);
  if (err is I_OK)
    err = i_parse_expr_level (this, MAX_EXPR_LEVEL, vp);

  return err;
}

private int i_parse_if_rout (i_t *this, ival_t *cond, int *haveelse, Istring_t *ifpart, Istring_t *elsepart) {
  int c;
  int err;

  *haveelse = 0;

  c = i_next_token (this);
  err = i_parse_expr (this, cond);
  if (err isnot I_OK) return err;

  c = this->curToken;
  if (c isnot I_TOK_STRING) return this->syntax_error (this, "parsing if, not a string");

  *ifpart = this->token;
  c = i_next_token (this);
  if (c is I_TOK_ELSE) {
    c = i_next_token (this);
    if (c isnot I_TOK_STRING) return this->syntax_error (this, "parsing else, not a string");

    *elsepart = this->token;
    *haveelse = 1;
    i_next_token (this);
  }

  return I_OK;
}

private int i_parse_if (i_t *this) {
  Istring_t ifpart, elsepart;
  int haveelse = 0;
  ival_t cond;
  int err = i_parse_if_rout (this, &cond, &haveelse, &ifpart, &elsepart);
  if (err isnot I_OK)
    return err;

  if (cond)
    err = i_parse_string (this, ifpart, 0, 0);
  else if (haveelse)
    err = i_parse_string(this, elsepart, 0, 0);

  if (err is I_OK and 0 is cond) err = I_ERR_OK_ELSE;
  return err;
}

private int i_parse_ifnot (i_t *this) {
  Istring_t ifpart, elsepart;
  int haveelse = 0;
  ival_t cond;
  int err = i_parse_if_rout (this, &cond, &haveelse, &ifpart, &elsepart);
  if (err isnot I_OK)
    return err;

  if (!cond)
    err = i_parse_string (this, ifpart, 0, 0);
  else if (haveelse)
    err = i_parse_string(this, elsepart, 0, 0);

  if (err is I_OK and 0 isnot cond) err = I_ERR_OK_ELSE;
  return err;
}

private int i_parse_var_list (i_t *this, UserFunc *uf, int saveStrings) {
  int c;
  int nargs = 0;
  c = i_next_raw_token (this);

  for (;;) {
    if (c is I_TOK_SYMBOL) {
      Istring_t name = this->token;
      if (saveStrings)
        name = i_dup_string (this, name);

      if (nargs >= MAX_BUILTIN_PARAMS)
        return i_too_many_args (this);

      uf->argName[nargs] = name;
      nargs++;
      c = i_next_token (this);
      if (c is ')') break;

      if (c is ',')
        c = i_next_token (this);

    } else if (c is ')')
      break;

    else
      return this->syntax_error (this, "var definition, unxpected token");
  }

  uf->nargs = nargs;
  return nargs;
}

private int i_parse_func_def (i_t *this, int saveStrings) {
  Sym *sym;
  Istring_t name;
  Istring_t body;
  int c;
  int nargs = 0;
  UserFunc *uf;

  c = i_next_raw_token (this); // do not interpret the symbol
  if (c isnot I_TOK_SYMBOL) return this->syntax_error (this, "fun definition, not a symbol");

  name = this->token;
  c = i_next_token (this);
  uf = (UserFunc *) i_stack_alloc (this, sizeof (*uf));
  ifnot (uf) return i_out_of_mem (this);

  uf->nargs = 0;
  if (c is '(') {
    nargs = i_parse_var_list (this, uf, saveStrings);
    if (nargs < 0) return nargs;

    c = i_next_token (this);
  }

  if (c isnot I_TOK_STRING) return this->syntax_error (this, "fun definition, not a string");

  body = this->token;

  if (saveStrings) {
    name = i_dup_string(this, name);
    body = i_dup_string(this, body);
  }

  uf->body = body;
  sym = i_define_sym (this, name, USRFUNC | (nargs << 8), (ival_t) uf);
  ifnot (sym) return i_out_of_mem (this);

  i_next_token (this);
  return I_OK;
}

private int i_parse_print_rout (i_t *this) {
  int c;
  int err = I_OK;

print_more:
  c = i_next_token (this);
  if (c is I_TOK_STRING) {
    i_print_string (this, this->out_fp, this->token);
    i_next_token (this);
  } else {
    ival_t val;
    err = i_parse_expr (this, &val);
    if (err isnot I_OK) return err;

    i_print_number (this, val);
  }

  if (this->curToken is ',') goto print_more;
  return err;
}

private int i_parse_println (i_t *this) {
  int err = i_parse_print_rout (this);
  this->print_byte (this->out_fp, '\n');
  return err;
}

private int i_parse_print (i_t *this) {
  return i_parse_print_rout (this);
}

private int i_parse_return (i_t *this) {
  int err;
  i_next_token (this);
  err = i_parse_expr (this, &this->fResult);
  // terminate the script
  i_StringSetLen (&this->parseptr, 0);
  return err;
}

private int i_parse_while (i_t *this) {
  int err;
  Istring_t savepc = this->parseptr;

again:
  err = i_parse_if (this);
  if (err is I_ERR_OK_ELSE) {
    return I_OK;
  } else if (err is I_OK) {
    this->parseptr = savepc;
    goto again;
  }

  return err;
}

// builtin
private ival_t i_prod (ival_t x, ival_t y) { return x*y; }
private ival_t i_quot (ival_t x, ival_t y) { return x/y; }
private ival_t i_sum (ival_t x, ival_t y) { return x+y; }
private ival_t i_diff (ival_t x, ival_t y) { return x-y; }
private ival_t i_bitand (ival_t x, ival_t y) { return x&y; }
private ival_t i_bitor (ival_t x, ival_t y) { return x|y; }
private ival_t i_bitxor (ival_t x, ival_t y) { return x^y; }
private ival_t i_shl (ival_t x, ival_t y) { return x<<y; }
private ival_t i_shr (ival_t x, ival_t y) { return x>>y; }
private ival_t i_equals (ival_t x, ival_t y) { return x==y; }
private ival_t i_ne (ival_t x, ival_t y) { return x!=y; }
private ival_t i_lt (ival_t x, ival_t y) { return x<y; }
private ival_t i_le (ival_t x, ival_t y) { return x<=y; }
private ival_t i_gt (ival_t x, ival_t y) { return x>y; }
private ival_t i_ge (ival_t x, ival_t y) { return x>=y; }

private struct def {
  const char *name;
  int toktype;
  ival_t val;
} idefs[] = {
  { "var",     I_TOK_VARDEF,  (ival_t) 0 },
  { "else",    I_TOK_ELSE,    (ival_t) 0 },
  { "if",      I_TOK_IF,      (ival_t) i_parse_if },
  { "ifnot",   I_TOK_IFNOT,   (ival_t) i_parse_ifnot },
  { "while",   I_TOK_WHILE,   (ival_t) i_parse_while },
  { "println", I_TOK_PRINTLN, (ival_t) i_parse_println },
  { "print",   I_TOK_PRINT,   (ival_t) i_parse_print },
  { "func",    I_TOK_FUNCDEF, (ival_t) i_parse_func_def },
  { "return",  I_TOK_RETURN,  (ival_t) i_parse_return },
  { "true",    INT, (ival_t) 1},
  { "false",   INT, (ival_t) 0},
  { "OK",      INT, (ival_t) 0},
  { "NOTOK",   INT, (ival_t) -1},
  // operators
  { "*",     BINOP(1), (ival_t) i_prod },
  { "/",     BINOP(1), (ival_t) i_quot },
  { "+",     BINOP(2), (ival_t) i_sum },
  { "-",     BINOP(2), (ival_t) i_diff },
  { "&",     BINOP(3), (ival_t) i_bitand },
  { "|",     BINOP(3), (ival_t) i_bitor },
  { "^",     BINOP(3), (ival_t) i_bitxor },
  { ">>",    BINOP(3), (ival_t) i_shr },
  { "<<",    BINOP(3), (ival_t) i_shl },
  { "=",     BINOP(4), (ival_t) i_equals },
  { "is",    BINOP(4), (ival_t) i_equals },
  { "isnot", BINOP(4), (ival_t) i_ne },
  { "<>",    BINOP(4), (ival_t) i_ne },
  { "<",     BINOP(4), (ival_t) i_lt },
  { "<=",    BINOP(4), (ival_t) i_le },
  { ">",     BINOP(4), (ival_t) i_gt },
  { ">=",    BINOP(4), (ival_t) i_ge },
  { NULL, 0, 0 }
};

private int i_define (i_t *this, const char *name, int typ, ival_t val) {
  Sym *s;
  s = i_define_sym (this, i_cstring_new (name), typ, val);
  if (NULL is s) return i_out_of_mem (this);
  return I_OK;
}

private int i_eval_string (i_t *this, const char *buf, int saveStrings, int topLevel) {
  Istring_t x = i_cstring_new (buf);
  return i_parse_string (this, x, saveStrings, topLevel);
}

private int i_eval_file (i_t *this, const char *filename) {
  char script[this->max_script_size];

  ifnot (File.exists (filename)) {
    this->print_fmt_bytes (this->err_fp, "%s: doesn't exists\n", filename);
    return NOTOK;
  }

  int r = OK;
  FILE *fp = fopen (filename, "r");
  if (NULL is fp) {
    this->print_fmt_bytes (this->err_fp, "%s\n",
        Error.string ($OurRoots(current), errno));
    return NOTOK;
  }

  r = fread (script, 1, this->max_script_size, fp);
  fclose (fp);

  if (r <= 0) {
    this->print_fmt_bytes (this->err_fp, "Couldn't read script\n");
    return NOTOK;
  }

  script[r] = 0;
  r = i_eval_string (this, script, 0, 1);
  if (r isnot I_OK) {
    char *err_msg[] = {"NO MEMORY", "SYNTAX ERROR", "UNKNOWN SYMBOL",
        "BAD ARGUMENTS", "TOO MANY ARGUMENTS"};
    this->print_fmt_bytes (this->err_fp, "%s\n", err_msg[-r - 2]);
  }

  return r;
}

private void i_free (i_t **thisp) {
  if (NULL is *thisp) return;
  i_t *this = *thisp;
  free (this->arena);
  free (this);
  *thisp = NULL;
}

private void i_free_instance (i_t **thisp) {
  i_free (thisp);
}

private i_t *i_new (Class (i) *__i__) {
  Type (i) *this = AllocType (i);
  this->prop = __i__->prop;
  return this;
}

private char *i_name_gen (char *name, int *name_gen, char *prefix, size_t prelen) {
  size_t num = (*name_gen / 26) + prelen;
  uidx_t i = 0;
  for (; i < prelen; i++) name[i] = prefix[i];
  for (; i < num; i++) name[i] = 'a' + ((*name_gen)++ % 26);
  name[num] = '\0';
  return name;
}

private void i_remove_instance (Class (i) *this, Type (i) *instance) {
  Type (i) *it = $my(head);
  Type (i) *prev = NULL;

  int idx = 0;
  while (it isnot instance) {
    prev = it;
    idx++;
    it = it->next;
  }

  if (it is NULL) return;
  if (idx >= $my(current_idx)) $my(current_idx)--;
  $my(num_instances)--;

  ifnot ($my(num_instances)) {
    $my(head) = NULL;
    goto theend;
  }

  if (1 is $my(num_instances)) {
    if (it->next is NULL) {
      $my(head) = prev;
      $my(head)->next = NULL;
    } else
      $my(head) = it->next;
    goto theend;
  }

  prev->next = it->next;

theend:
  i_free_instance (&it);
}

private Type (i) *i_append_instance (Class (i) *this, Type (i) *instance) {
  instance->next = NULL;
  $my(current_idx) = $my(num_instances);
  $my(num_instances)++;

  if (NULL is $my(head))
    $my(head) = instance;
  else {
    Type (i) *it = $my(head);
    while (it) {
      if (it->next is NULL) break;
      it = it->next;
    }

    it->next = instance;
  }

  return instance;
}

private Type (i) *i_set_current (Class (i) *this, int idx) {
  if (idx >= $my(num_instances)) return NULL;
  Type (i) *it = $my(head);
  int i = 0;
  while (i++ < idx) it = it->next;

  return it;
}

private void i_set_object (i_t *this, void *obj) {
  this->object = obj;
}

private void *i_get_object (i_t *this) {
  return this->object;
}

private Type (i) *i_get_current (Class (i) *this) {
  Type (i) *it = $my(head);
  int i = 0;
  while (i++ < $my(current_idx)) it = it->next;

  return it;
}

private int i_get_current_idx (Class (i) *this) {
  return $my(current_idx);
}

private ival_t i_exit (ival_t inst, int code) {
  (void) inst;
  return I_OK; // ignore
  exit (code);
}

private void i_print_bytes (FILE *fp, const char *bytes) {
  fprintf (fp, "%s", bytes);
}

private void i_print_fmt_bytes (FILE *fp, const char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE (fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  i_print_bytes (fp, bytes);
}

private void i_print_byte (FILE *fp, int c) {
  i_print_fmt_bytes (fp, "%c", c);
}

ival_t i_e_get_ed_num (i_t *this) {
  return Root.get.num ($OurRoot);
}

ival_t i_e_set_ed_next (i_t *this) {
  return (ival_t) Root.set.next ($OurRoot);
}

ival_t i_e_set_ed_by_idx (i_t *this, int idx) {
  return (ival_t) Root.set.current ($OurRoot, idx);
}

ival_t i_e_set_save_image (i_t *this, int val) {
  Root.set.save_image ($OurRoot, val);
  return I_OK;
}

ival_t i_e_set_persistent_layout (i_t *this, int val) {
  Root.set.persistent_layout ($OurRoot, val);
  return I_OK;
}

ival_t i_e_set_image_name (i_t *this, char *name) {
  Root.set.image_name ($OurRoot, name);
  free (name);
  return I_OK;
}

ival_t i_e_set_image_file (i_t *this, char *file) {
  Root.set.image_file ($OurRoot, file);
  free (file);
  return I_OK;
}

ival_t i_e_get_ed_current_idx (i_t *this) {
  return Root.get.current_idx ($OurRoot);
}

ival_t i_e_get_ed_current (i_t *this) {
  return (ival_t) Root.get.current ($OurRoot);
}

ival_t i_ed_new (i_t *this, int num_win) {
  return (ival_t) Root.new ($OurRoot, EdOpts(.num_win = num_win));
}

ival_t i_ed_get_num_win (i_t *this, ed_t *ed) {
  return Ed.get.num_win (ed, 0);
}

ival_t i_ed_get_win_next (i_t *this, ed_t *ed, win_t *win) {
  return (ival_t) Ed.get.win_next (ed, win);
}

ival_t i_ed_get_current_win (i_t *this, ed_t *ed) {
  return (ival_t) Ed.get.current_win (ed);
}

ival_t i_ed_set_current_win (i_t *this, ed_t *ed, int idx) {
  return (ival_t) Ed.set.current_win (ed, idx);
}

ival_t i_buf_init_fname (i_t *this, buf_t *buf, char *fn) {
  Buf.init_fname (buf, fn);
  free (fn);
  return OK;
}

ival_t i_buf_set_ftype (i_t *this, buf_t *buf, char *ftype) {
  Buf.set.ftype (buf, Ed.syn.get_ftype_idx ($OurRoots(current), ftype));
  free (ftype);
  return OK;
}

ival_t i_buf_set_autosave (i_t *this, buf_t *buf, long minutes) {
  (void) this;
  buf_set_autosave (buf, minutes);
  return OK;
}

ival_t i_buf_set_row_idx (i_t *this, buf_t *buf, int row) {
  Buf.set.row.idx (buf, row, NO_OFFSET, 1);
  return OK;
}

ival_t i_buf_draw (i_t *this, buf_t *buf) {
  Buf.draw (buf);
  return OK;
}

ival_t i_buf_substitute (i_t *this, buf_t *buf, char *pat, char *sub, int global,
                                            int interactive, int fidx, int lidx) {
  if (fidx is lidx) fidx = lidx = buf->cur_idx;

  ival_t val = Buf.substitute (buf, pat, sub, global, interactive, fidx, lidx);
  free (sub);
  free (pat);
  return val;
}

ival_t i_win_buf_init (i_t *this, win_t *win, int frame, int flags) {
  return (ival_t) Win.buf.init (win, frame, flags);
}

ival_t i_win_set_current_buf (i_t *this, win_t *win, int idx, int draw) {
  return (ival_t) Win.set.current_buf (win, idx, draw);
}

ival_t i_win_get_current_buf (i_t *this, win_t *win) {
  return (ival_t) Win.get.current_buf (win);
}

ival_t i_win_draw (i_t *this, win_t *win) {
  Win.draw (win);
  return OK;
}

ival_t i_win_append_buf (i_t *this, win_t *win, buf_t *buf) {
  Win.append_buf (win, buf);
  return OK;
}

ival_t i_buf_normal_page_down (i_t *this, buf_t *buf, int count, int draw) {
  return Buf.normal.page_down (buf, count, draw);
}

ival_t i_buf_normal_page_up (i_t *this, buf_t *buf, int count, int draw) {
  return Buf.normal.page_up (buf, count, draw);
}

ival_t i_buf_normal_goto_linenr (i_t *this, buf_t *buf, int linenum, int draw) {
  return Buf.normal.goto_linenr (buf, linenum, draw);
}

ival_t i_buf_normal_replace_character_with (i_t *this, buf_t *buf, utf8 c) {
  return Buf.normal.replace_character_with (buf, c);
}

ival_t i_buf_normal_change_case (i_t *this, buf_t *buf) {
  return Buf.normal.change_case (buf);
}

ival_t i_buf_insert_string (i_t *this, buf_t *buf, char *str, int draw) {
  int retval = Buf.insert.string (buf, str, bytelen (str), draw);
  free (str);
  return retval;
}

ival_t i_buf_search (i_t *this, buf_t *buf, char com, char *str, int c) {
  int retval = Buf.search (buf, com, str, c);
  free (str);
  return retval;
}

ival_t i_print_env (i_t *i, ed_t *this, char *str) {
  string_t *env = E_get_env ($OurRoot, str);
  free (str);
  i->print_bytes (i->out_fp, env->bytes);
  return I_OK;
}

ival_t i_print_str (i_t *this, ival_t str) {
  char *sp = (char *) str;
  while (*sp)
    this->print_byte (this->out_fp, *sp++);
  return I_OK;
}

ival_t i_println_str (i_t *this, ival_t str) {
  i_print_str (this, str);
  this->print_byte (this->out_fp, '\n');
  return I_OK;
}

ival_t i_free_str (i_t *this, ival_t str) {
  (void) this;
  char *sp = (char *) str;
  free (sp);
  return I_OK;
}

struct ifun_t {
  const char *name;
  ival_t val;
  int nargs;
} ifuns[] = {
 // { "init_this",           (ival_t) i_init_this, 0},
 // { "deinit_this",         (ival_t) i_deinit_this, 0},
  { "e_set_ed_next",         (ival_t) i_e_set_ed_next, 0},
  { "e_set_ed_by_idx",       (ival_t) i_e_set_ed_by_idx, 1},
  { "e_set_save_image",      (ival_t) i_e_set_save_image, 1},
  { "e_set_persistent_layout", (ival_t) i_e_set_persistent_layout, 1},
  { "e_set_image_name",      (ival_t) i_e_set_image_name, 1},
  { "e_set_image_file",      (ival_t) i_e_set_image_file, 1},
  { "e_get_ed_num",          (ival_t) i_e_get_ed_num, 0},
  { "e_get_ed_current",      (ival_t) i_e_get_ed_current, 0},
  { "e_get_ed_current_idx",  (ival_t) i_e_get_ed_current_idx, 0},
  { "ed_new",                (ival_t) i_ed_new, 1},
  { "ed_get_num_win",        (ival_t) i_ed_get_num_win, 1},
  { "ed_get_win_next",       (ival_t) i_ed_get_win_next, 2},
  { "ed_get_current_win",    (ival_t) i_ed_get_current_win, 1},
  { "ed_set_current_win",    (ival_t) i_ed_set_current_win, 2},
  { "buf_set_ftype",         (ival_t) i_buf_set_ftype, 2},
  { "buf_set_autosave",      (ival_t) i_buf_set_autosave, 2},
  { "buf_set_row_idx",       (ival_t) i_buf_set_row_idx, 2},
  { "buf_normal_page_up",    (ival_t) i_buf_normal_page_up, 3},
  { "buf_normal_page_down",  (ival_t) i_buf_normal_page_down, 3},
  { "buf_normal_change_case",(ival_t) i_buf_normal_change_case, 1},
  { "buf_normal_goto_linenr",(ival_t) i_buf_normal_goto_linenr, 3},
  { "buf_normal_replace_character_with", (ival_t) i_buf_normal_replace_character_with, 2},
  { "buf_insert_string",     (ival_t) i_buf_insert_string, 3},
  { "buf_search",            (ival_t) i_buf_search, 4},
  { "buf_draw",              (ival_t) i_buf_draw, 1},
  { "buf_init_fname",        (ival_t) i_buf_init_fname, 2},
  { "buf_substitute",        (ival_t) i_buf_substitute, 7},
  { "win_buf_init",          (ival_t) i_win_buf_init, 3},
  { "win_draw",              (ival_t) i_win_draw, 1},
  { "win_append_buf",        (ival_t) i_win_append_buf, 2},
  { "win_set_current_buf",   (ival_t) i_win_set_current_buf, 3},
  { "win_get_current_buf",   (ival_t) i_win_get_current_buf, 1},
  { "print_env",             (ival_t) i_print_env, 2},
  { NULL, 0, 0}
};

struct i_def_fun_t {
  const char *name;
  ival_t val;
  int nargs;
} i_def_funs[] = {
  { "print_str",             (ival_t) i_print_str, 1},
  { "println_str",           (ival_t) i_println_str, 1},
  { "free_str",              (ival_t) i_free_str, 1},
  { NULL, 0, 0}
};

private int i_define_funs_default_cb (i_t *this) {
  int err;
  for (int i = 0; ifuns[i].name; i++) {
    if (I_OK isnot (err = i_define (this, ifuns[i].name, CFUNC (ifuns[i].nargs), ifuns[i].val)))
      return err;
  }

  return I_OK;
}

private i_opts i_default_options (i_t *this, i_opts opts) {
  if (0 >= opts.mem_size)
    opts.mem_size = $my(default_size);

  if (NULL is opts.print_bytes)
    opts.print_bytes = i_print_bytes;

  if (NULL is opts.print_byte)
    opts.print_byte = i_print_byte;

  if (NULL is opts.print_fmt_bytes)
    opts.print_fmt_bytes = i_print_fmt_bytes;

  if (NULL is opts.syntax_error)
    opts.syntax_error = i_syntax_error_to_ed;

  if (NULL is opts.err_fp)
    opts.err_fp = stderr;

  if (NULL is opts.define_funs_cb)
    opts.define_funs_cb = i_define_funs_default_cb;

  return opts;
}

private int i_init (Class (i) *interp, i_t *this, i_opts opts) {
  int i;
  int err = 0;

  if (NULL is opts.name)
    i_name_gen (this->name, &$my(name_gen), "i:", 2);
  else
    cstring_cp (this->name, 32, opts.name, 31);

  opts = i_default_options (this, opts);

  this->arena = (char *) Alloc (opts.mem_size);
  this->mem_size = opts.mem_size;
  this->print_byte = opts.print_byte;
  this->print_fmt_bytes = opts.print_fmt_bytes;
  this->print_bytes = opts.print_bytes;
  this->syntax_error = opts.syntax_error;
  this->err_fp = opts.err_fp;
  this->out_fp = opts.out_fp;
  this->max_script_size = opts.max_script_size;
  this->object = opts.object;
  this->state = 0;

  this->symptr = (Sym *) this->arena;
  this->valptr = (ival_t *) (this->arena + this->mem_size);

  for (i = 0; idefs[i].name; i++) {
    err = i_define (this, idefs[i].name, idefs[i].toktype, idefs[i].val);

    if (err isnot I_OK) {
      i_free (&this);
      return err;
    }
  }

  for (i = 0; i_def_funs[i].name; i++) {
    err = i_define (this, i_def_funs[i].name, CFUNC (i_def_funs[i].nargs), i_def_funs[i].val);

    if (err isnot I_OK) {
      i_free (&this);
      return err;
    }
  }

  if (I_OK isnot opts.define_funs_cb (this)) {
    i_free (&this);
    return err;
  }

  i_append_instance (interp, this);

  return I_OK;
}

private i_t *i_init_instance (Class (i) *__i__, i_opts opts) {
  i_t *this = i_new (__i__);

  i_init (__i__, this, opts);

  return this;
}

private int i_load_file (Class (i) *__i__, i_t *this, char *fn) {
  if (this is NULL)
    this = i_init_instance (__i__, IOpts());

  ifnot (Path.is_absolute (fn)) {
    if (File.exists (fn) and File.is_reg (fn))
      return self(eval_file, fn);

    size_t fnlen = bytelen (fn);
    char fname[fnlen+3];
    Cstring.cp (fname, fnlen + 1, fn, fnlen);

    char *extname = Path.extname (fname);
    size_t extlen = bytelen (extname);

    if (0 is extlen or 0 is Cstring.eq (".i", extname)) {
      fname[fnlen] = '.'; fname[fnlen+1] = 'i'; fname[fnlen+2] = '\0';
      if (File.exists (fname))
        return self(eval_file, fname);
      fname[fnlen] = '\0';
    }

    string_t *ddir = Root.get.env ($OurRoot, "i_dir");
    size_t len = ddir->num_bytes + bytelen (fname) + 2 + 7;
    char tmp[len + 3];
    Cstring.cp_fmt (tmp, len + 1, "%s/scripts/%s", ddir->bytes, fname);
    if (0 is File.exists (tmp) or 0 is File.is_reg (tmp)) {
      tmp[len] = '.'; tmp[len+1] = 'i'; tmp[len+2] = '\0';
    }

    ifnot (File.exists (tmp)) return NOTOK;

    return self(eval_file, tmp);
  }

  ifnot (File.exists (fn)) return NOTOK;

  return self(eval_file, fn);
}

private Class (i) *__init_i__ (Class (E) *e) {
  Class (i) *this =  AllocClass (i);

  *this = ClassInit (i,
    .self = SelfInit (i,
      .new = i_new,
      .free = i_free,
      .init = i_init,
      .def =  i_define,
      .init_instance = i_init_instance,
      .load_file = i_load_file,
      .remove_instance = i_remove_instance,
      .append_instance = i_append_instance,
      .eval_string =  i_eval_string,
      .eval_file = i_eval_file,
      .get = SubSelfInit (i, get,
        .object = i_get_object,
        .current = i_get_current,
        .current_idx = i_get_current_idx
      ),
      .set = SubSelfInit (i, set,
        .object = i_set_object,
        .current = i_set_current
      )
    ),
    .prop = $myprop,
  );

  $my(name_gen) = ('z' - 'a') + 1;
  $my(default_size) = 1 << 12;

  $my(head) = NULL;
  $my(num_instances) = 0;
  $my(current_idx) = -1;

  $my(Me)          =  this;
  $OurRoot         =  e;
  $my(__Ed__)      =  e->__Ed__;
  $my(__Win__)     = &e->__Ed__->__Win__;
  $my(__Buf__)     = &e->__Ed__->__Buf__;
  $my(__Re__)      = &e->__Ed__->__Re__;
  $my(__Msg__)     = &e->__Ed__->__Msg__;
  $my(__Dir__)     = &e->__Ed__->__Dir__;
  $my(__Term__)    = &e->__Ed__->__Term__;
  $my(__File__)    = &e->__Ed__->__File__;
  $my(__Path__)    = &e->__Ed__->__Path__;
  $my(__Vsys__)    = &e->__Ed__->__Vsys__;
  $my(__Smap__)    = &e->__Ed__->__Smap__;
  $my(__Imap__)    = &e->__Ed__->__Imap__;
  $my(__Input__)   = &e->__Ed__->__Input__;
  $my(__Video__)   = &e->__Ed__->__Video__;
  $my(__Rline__)   = &e->__Ed__->__Rline__;
  $my(__Error__)   = &e->__Ed__->__Error__;
  $my(__Screen__)  = &e->__Ed__->__Screen__;
  $my(__Cursor__)  = &e->__Ed__->__Cursor__;
  $my(__String__)  = &e->__Ed__->__String__;
  $my(__Cstring__) = &e->__Ed__->__Cstring__;
  $my(__Vstring__) = &e->__Ed__->__Vstring__;
  $my(__Ustring__) = &e->__Ed__->__Ustring__;

  return this;
}

private void __deinit_i__ (Class (i) **thisp) {
  if (NULL is *thisp) return;
  Class (i) *this = *thisp;

  Type (i) *it = $my(head);
  while (it) {
    Type (i) *tmp = it->next;
    i_free_instance (&it);
    it = tmp;
  }

  free ($myprop);
  free (this);
  *thisp = NULL;
}

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Testing strstr() implementations.
 * 
 * Results and conclusion at the end of this unit.
 * Author: Agathoklis Chatzimanikas aga dot chatzimanikas at gmail
 * First version at Sat 01 Aug 2020.
 */

#define NUM_TESTS 20000000

/* Four strstr() implementations from techiedelight blog, see at:
   https://www.techiedelight.com/implement-strstr-function-c-iterative-recursive
   Many thanks.
*/

/* 1. recursive version */
char* tdelr_strstr(const char* X, const char* Y);
char* tdelr_strstr(const char* X, const char* Y)
{
	if (*Y == '\0')
		return (char *) X;

	for (int i = 0; i < strlen(X); i++)
	{
		if (*(X + i) == *Y)
		{
			char* ptr = tdelr_strstr(X + i + 1, Y + 1);
			return (ptr) ? ptr - 1 : NULL;
		}
	}

	return NULL;
}

/* 2. memcmp version */
char *tdelm_strstr(const char *X, const char *Y)
{
	size_t n = strlen(Y);
    if (0 == n) return (char *) X;

	while(*X)
	{
		if (!memcmp(X, Y, n))
			return (char *) X;

		X++;
	}

	return 0;
}

/* 3. iterative version (two functions interface) */
int compare(const char *X, const char *Y)
{
	while (*X && *Y)
	{
		if (*X != *Y)
			return 0;

		X++;
		Y++;
	}

	return (*Y == '\0');
}

char* tdel_strstr(const char* X, const char* Y)
{
    if (*Y == '\0') return (char *) X;

	while (*X != '\0')
	{
		if ((*X == *Y) && compare(X, Y))
			return ((char *) X);
		X++;
	}

	return NULL;
}

/* 4. the KMP algorithm version */
char* tdelKMP_strstr(const char* X, const char* Y)
{
    int m = strlen (X);
    int n = strlen (Y);

	// Base Case 1: Y is NULL or empty
	if (*Y == '\0' || n == 0)
		return (char *) X;

	// Base Case 2: X is NULL or X's length is less than that of Y's
	if (*X == '\0' || n > m)
		return NULL;

	// next[i] stores the index of next best partial match
	int next[n + 1];

	for (int i = 0; i < n + 1; i++)
		next[i] = 0;

	for (int i = 1; i < n; i++)
	{
		int j = next[i + 1];

		while (j > 0 && Y[j] != Y[i])
			j = next[j];

		if (j > 0 || Y[j] == Y[i])
			next[i + 1] = j + 1;
	}

	for (int i = 0, j = 0; i < m; i++)
	{
		if (*(X + i) == *(Y + j))
		{
			if (++j == n)
				return (char *) (X + i - j + 1);
		}
		else if (j > 0) {
			j = next[j];
			i--;	// since i will be incremented in next iteration
		}
	}

	return NULL;
}

/*
 Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 * FreeBSD: src/lib/libc/string/strstr.c,v 1.6 2009/02/03 17:58:20;
 */

char *freebsd_strstr(const char *s, const char *find)
{
  char c, sc;
  size_t len;
  if ((c = *find++) != '\0') {
    len = strlen(find);
    do {
      do {
        if ((sc = *s++) == '\0')
          return (NULL);
       } while (sc != c);
     } while (strncmp(s, find, len) != 0);
    s--;
  }

  return ((char *)s);
}

/* this is my first attempt, which is an explainable freebsd's code (slower than
 * the original
 */
char *bytes_in_str_a (const char *spa, const char *spb) {
  if (*spb == 0) return ((char *) spa);

  int c = *spb;

  size_t len = strlen (spb);
  while (1) {
    while (*spa && c != *spa) spa++;
    if (0 == (strncmp (spa, spb, len)))
      return ((char *) spa);
    if (*spa == 0) return NULL;
    spa++;
  }

  return NULL;
}

/* musl implementation */
char *twobyte_strstr(const unsigned char *h, const unsigned char *n)
{
	uint16_t nw = n[0]<<8 | n[1], hw = h[0]<<8 | h[1];
	for (h++; *h && hw != nw; hw = hw<<8 | *++h);
	return *h ? (char *)h-1 : 0;
}

char *threebyte_strstr(const unsigned char *h, const unsigned char *n)
{
	uint32_t nw = (uint32_t)n[0]<<24 | n[1]<<16 | n[2]<<8;
	uint32_t hw = (uint32_t)h[0]<<24 | h[1]<<16 | h[2]<<8;
	for (h+=2; *h && hw != nw; hw = (hw|*++h)<<8);
	return *h ? (char *)h-2 : 0;
}

char *fourbyte_strstr(const unsigned char *h, const unsigned char *n)
{
	uint32_t nw = (uint32_t)n[0]<<24 | n[1]<<16 | n[2]<<8 | n[3];
	uint32_t hw = (uint32_t)h[0]<<24 | h[1]<<16 | h[2]<<8 | h[3];
	for (h+=3; *h && hw != nw; hw = hw<<8 | *++h);
	return *h ? (char *)h-3 : 0;
}

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))

#define BITOP(a,b,op) \
 ((a)[(size_t)(b)/(8*sizeof *(a))] op (size_t)1<<((size_t)(b)%(8*sizeof *(a))))

char *twoway_strstr(const unsigned char *h, const unsigned char *n)
{
	const unsigned char *z;
	size_t l, ip, jp, k, p, ms, p0, mem, mem0;
	size_t byteset[32 / sizeof(size_t)] = { 0 };
	size_t shift[256];

	/* Computing length of needle and fill shift table */
	for (l=0; n[l] && h[l]; l++)
		BITOP(byteset, n[l], |=), shift[n[l]] = l+1;
	if (n[l]) return 0; /* hit the end of h */

	/* Compute maximal suffix */
	ip = -1; jp = 0; k = p = 1;
	while (jp+k<l) {
		if (n[ip+k] == n[jp+k]) {
			if (k == p) {
				jp += p;
				k = 1;
			} else k++;
		} else if (n[ip+k] > n[jp+k]) {
			jp += k;
			k = 1;
			p = jp - ip;
		} else {
			ip = jp++;
			k = p = 1;
		}
	}
	ms = ip;
	p0 = p;

	/* And with the opposite comparison */
	ip = -1; jp = 0; k = p = 1;
	while (jp+k<l) {
		if (n[ip+k] == n[jp+k]) {
			if (k == p) {
				jp += p;
				k = 1;
			} else k++;
		} else if (n[ip+k] < n[jp+k]) {
			jp += k;
			k = 1;
			p = jp - ip;
		} else {
			ip = jp++;
			k = p = 1;
		}
	}
	if (ip+1 > ms+1) ms = ip;
	else p = p0;

	/* Periodic needle? */
	if (memcmp(n, n+p, ms+1)) {
		mem0 = 0;
		p = MAX(ms, l-ms-1) + 1;
	} else mem0 = l-p;
	mem = 0;

	/* Initialize incremental end-of-haystack pointer */
	z = h;

	/* Search loop */
	for (;;) {
		/* Update incremental end-of-haystack pointer */
		if (z-h < l) {
			/* Fast estimate for MIN(l,63) */
			size_t grow = l | 63;
			const unsigned char *z2 = memchr(z, 0, grow);
			if (z2) {
				z = z2;
				if (z-h < l) return 0;
			} else z += grow;
		}

		/* Check last byte first; advance by shift on mismatch */
		if (BITOP(byteset, h[l-1], &)) {
			k = l-shift[h[l-1]];
			if (k) {
				if (k < mem) k = mem;
				h += k;
				mem = 0;
				continue;
			}
		} else {
			h += l;
			mem = 0;
			continue;
		}

		/* Compare right half */
		for (k=MAX(ms+1,mem); n[k] && n[k] == h[k]; k++);
		if (n[k]) {
			h += k-ms;
			mem = 0;
			continue;
		}
		/* Compare left half */
		for (k=ms+1; k>mem && n[k-1] == h[k-1]; k--);
		if (k <= mem) return (char *)h;
		h += p;
		mem = mem0;
	}
}

char *musl_strstr(const char *h, const char *n)
{
	/* Return immediately on empty needle */
	if (!n[0]) return (char *)h;

	/* Use faster algorithms for short needles */
	h = strchr(h, *n);
	if (!h || !n[1]) return (char *)h;
	if (!h[1]) return 0;
	if (!n[2]) return twobyte_strstr((void *)h, (void *)n);
	if (!h[2]) return 0;
	if (!n[3]) return threebyte_strstr((void *)h, (void *)n);
	if (!h[3]) return 0;
	if (!n[4]) return fourbyte_strstr((void *)h, (void *)n);

	return twoway_strstr((void *)h, (void *)n);
}

/* this is the faster implementation */
/* Copyright (c) 1988-1993 The Regents of the University of California.
 * Copyright (c) 1994 Sun Microsystems, Inc.
 */
char* apple_strstr(const char* string, const char* substring)
{
	const char *a, *b;

	/* First scan quickly through the two strings looking for a
	 * single-character match.  When it's found, then compare the
	 * rest of the substring.
	 */

	b = substring;

	if(*b == 0)
	{
		return (char*)string;
	}

	for(; *string != 0; string += 1)
	{
		if(*string != *b)
		{
			continue;
		}

		a = string;

		while(1)
		{
			if(*b == 0)
			{
				return (char*)string;
			}
			if(*a++ != *b++)
			{
				break;
			}
		}

		b = substring;
	}

	return NULL;
}

/* This is the second attempt, which is a modifiable tdel_strstr() implementation
 * and comparable in execution time with the one from apple (almost the same).
 * This might be (by far :)) the most readable and understandable implementation */
char *bytes_in_str (const char *str, const char *substr) {
  if (*substr == '\0')
    return (char *) str;

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


/* The exact same function with bytes_in_str() above.
 * Calling this as strstr() and without magic, this results in about 25-30
 * times faster execution time.
 */
char *strstr (const char *str, const char *substr) {
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

struct test {
  const char *a;
  const char *b;
  const char *r;
}tests[] = {
  {"asdf", "a", "asdf"},
  {"asdf", "s", "sdf"},
  {"asdf", "f", "f"},
  {"asdf", "asdf", "asdf"},
  {"aasdf", "asdf", "asdf"},
  {"aasdf", "asdf", "asdf"},
  {"aasasasdf", "asdf", "asdf"},
  {"aasasasdf", "asdg", NULL},
  {"", "a", NULL},
  {"a", "aa", NULL},
  {"a", "b", NULL},
  {"aa", "ab", NULL},
  {"aa", "aaa", NULL},
  {"abba", "aba", NULL},
  {"abc abc", "abcd", NULL},
  {"0-1-2-3-4-5-6-7-8-9", "-3-4-56-7-8-", NULL},
  {"0-1-2-3-4-5-6-7-8-9", "-3-4-5+6-7-8-", NULL},
  {"_ _ _\xff_ _ _", "_\x7f_", NULL},
  {"_ _ _\x7f_ _ _", "_\xff_", NULL},
  {"", "", ""},
  {"abcd", "", "abcd"},
  {"abcd", "a", "abcd"},
  {"abcd", "b", "bcd"},
  {"abcd", "c", "cd"},
  {"abcd", "d", "d"},
  {"abcd", "ab", "abcd"},
  {"abcd", "bc", "bcd"},
  {"abcd", "cd", "cd"},
  {"ababa", "baba", "baba"},
  {"ababab", "babab", "babab"},
  {"abababa", "bababa", "bababa"},
  {"abababab", "bababab", "bababab"},
  {"ababababa", "babababa", "babababa"},
  {"abbababab", "bababa", "bababab"},
  {"abbababab", "ababab", "ababab"},
  {"abacabcabcab", "abcabcab", "abcabcab"},
  {"nanabanabanana", "aba", "abanabanana"},
  {"nanabanabanana", "ban", "banabanana"},
  {"nanabanabanana", "anab", "anabanabanana"},
  {"nanabanabanana", "banana", "banana"},
  {"_ _\xff_ _", "_\xff_", "_\xff_ _"}
};

int TEST_LEN = 41;

typedef char *(*fun) (const char *, const char *);

void test_correctness (char *implem, fun fn) {
  fprintf (stdout, "Testing %s implementation: ", implem);
  fflush (stdout);

  int retval = 0;

  char *sp;
  for (int i = 0; i < TEST_LEN; i++) {
    sp = fn (tests[i].a, tests[i].b);
    if (NULL != sp) {
      if (NULL == tests[i].r || strcmp (sp, tests[i].r)) {
        fprintf (stderr, "%s", (retval == 0 ? "\033[31mNOTOK\033[m\n" : ""));
        fprintf (stderr, "\033[33mCalled with args: %s, %s\n", tests[i].a, tests[i].b);
        fprintf (stderr, "Awaiting %s got %s\033[m\n\n", tests[i].r, sp);
        retval = -1;
      }
    } else {
      if (NULL != tests[i].r) {
        fprintf (stderr, "%s", (retval == 0 ? "\033[31mNOTOK\033[m\n" : ""));
        fprintf (stderr, "\033[33mCalled with args: %s, %s\n", tests[i].a, tests[i].b);
        fprintf (stderr, "Awaiting %s got %s\033[m\n\n", tests[i].r, sp);
        retval = -1;
      }
    }
  }

  fprintf (stdout, " %s\n", (retval == 0 ? "\033[32mOK\033[m" : ""));
}

void timeout (char *implem, fun fn) {
  fprintf (stdout, "Time for %s implementation: ", implem);
  fflush (stdout);

  time_t end;
  time_t start;

  time (&start);
  for (int i = 0; i < TEST_LEN; i++) {
    for (int j = 0; j < NUM_TESTS; j++) {
      fn (tests[i].a, tests[i].b);
    }
  }

  time (&end);
  fprintf (stdout, " %d seconds\n", end - start);
}

int timeout_strstr (void) {
  fprintf (stdout, "Time for directly calling strstr() as strstr(): ");
  fflush (stdout);

  time_t end;
  time_t start;

  time (&start);
  for (int i = 0; i < TEST_LEN; i++) {
    for (int j = 0; j < NUM_TESTS; j++) {
      strstr (tests[i].a, tests[i].b);
    }
  }

  time (&end);
  fprintf (stdout, " %d seconds\n", end - start);
}

int main (int argc, char **argv) {
  char *sp;

  test_correctness ("apple_strstr()", apple_strstr);
  test_correctness ("freebsd_strstr()", freebsd_strstr);
  test_correctness ("bytes_in_str()", bytes_in_str);
  test_correctness ("bytes_in_str_a()", bytes_in_str_a);
  test_correctness ("musl_strstr()", musl_strstr);
  test_correctness ("tdel_strstr()", tdel_strstr);
  test_correctness ("tdelm_strstr()", tdelm_strstr);
  test_correctness ("tdelKMP_strstr()", tdelKMP_strstr);
  test_correctness ("tdelr_strstr()", tdelr_strstr);

  /* call directly this strstr() implementation */
  timeout_strstr ();

  timeout ("strstr() as callback", strstr);
  timeout ("apple_strstr()", apple_strstr);
  timeout ("bytes_in_str()", bytes_in_str);
  timeout ("tdel_strstr()", tdel_strstr);
  timeout ("freebsd_strstr()", freebsd_strstr);
  timeout ("bytes_in_str_a()", bytes_in_str_a);
  timeout ("tdelm_strstr()", tdelm_strstr);
  timeout ("tdelKMP_strstr()", tdelKMP_strstr);
  timeout ("musl_strstr()", musl_strstr);
  timeout ("tdelr_strstr()", tdelr_strstr);

  return 0;
}

/* This is on an dual core 64bit chromebook of 2014:
     model name: Intel(R) Celeron(R) CPU  N2830  @ 2.16GHz
        cpu MHz: 499.677
     cache size: 1024 KB

  Nunber of calls: 20000000 (iterations) * 41 (tests) = 820 000 000 calls

  Time for directly calling strstr() as strstr(): 3 seconds
  Time for apple_strstr() implementation:         68 seconds
  Time for strstr() as callback implementation:   70 seconds
  Time for bytes_in_str() implementation:         70 seconds
  Time for tdel_strstr() implementation:          90 seconds
  Time for freebsd_strstr() implementation:       97 seconds
  Time for bytes_in_str_a() implementation:       115 seconds
  Time for tdelm_strstr() implementation:         119 seconds
  Time for tdelKMP_strstr() implementation:       166 seconds
  Time for musl_strstr() implementation:          170 seconds
  Time for tdelr_strstr implementation:           174 seconds
 */

/*
  Fast conclusions.

   - all the implementations than one (tdelr_strstr()), passing the tests

   - calling the exact same function as strstr(), results in about 25-30 times
     faster execution time (this is the same for all the implementations)

   - bytes_in_str() that mimics the tdel_strstr() is 20 seconds faster

   - bytes_in_str_a() that implements the freebsd_strstr() is 18 seconds slower

   - for such many calls the difference is quite negligible

*/

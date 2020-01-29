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
 * - added ignore_next_char() to ignore next token
 * - print* function references got a FILE * argument
 * - it is possible to call a C function, with a literal string as argument
 * - many changes that integrates Tinyscript to this environment
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "../../libved.h"
#include "../../libved+.h"

#define mystrings this->strings
#define istring_pop()                        \
({                                           \
  char *ibuf_ = NULL;                        \
  istring_t *node_ = mystrings->head;        \
  if (node_ != NULL) {                       \
    mystrings->head = mystrings->head->next; \
    ibuf_ = node_->ibuf;                     \
    free (node_);                            \
  }                                          \
  ibuf_;                                     \
})

#define istring_push(alloc_bytes_)           \
({                                           \
  istring_t *node_ = AllocType (istring);    \
  node_->ibuf = alloc_bytes_;                \
  if (this->strings->head == NULL) {         \
    this->strings->head = node_;             \
    this->strings->head->next = NULL;        \
  } else {                                   \
    node_->next = this->strings->head;       \
    this->strings->head = node_;             \
  }                                          \
})

public char *i_pop_string (ival_t instance) {
  i_t *this = (i_t *) instance;
  return istring_pop ();
}

private int i_parse_stmt (i_t *, int);
private int i_parse_expr (i_t *, ival_t *);

static inline unsigned StringGetLen (String_t s) { return (unsigned)s.len_; }
static inline const char *StringGetPtr (String_t s) { return (const char *)(ival_t)s.ptr_; }
static inline void StringSetLen (String_t *s, unsigned len) { s->len_ = len; }
static inline void StringSetPtr (String_t *s, const char *ptr) { s->ptr_ = ptr; }

#define MAX_EXPR_LEVEL 5

private int stringeq (String_t ai, String_t bi) {
  const char *a, *b;
  size_t i, len;

  len = StringGetLen (ai);
  if (len isnot StringGetLen (bi))
    return 0;

  a = StringGetPtr (ai);
  b = StringGetPtr (bi);
  for (i = 0; i < len; i++) {
    if (*a isnot *b) return 0;
    a++; b++;
  }

  return 1;
}

private void i_print_string (i_t *this, FILE *fp, String_t s) {
  unsigned len = StringGetLen (s);
  const char *ptr = (const char *) StringGetPtr (s);
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

private String_t i_dup_string (i_t *this, String_t orig) {
  String_t x;
  char *ptr;
  unsigned len = StringGetLen (orig);
  ptr = i_stack_alloc (this, len);
  if (ptr)
    memcpy (ptr, StringGetPtr (orig), len);

  StringSetLen (&x, len);
  StringSetPtr (&x, ptr);
  return x;
}

private int i_err_ptr (i_t *this, int err) {
  i_print_string (this, this->err_fp, this->parseptr);
  this->print_byte (this->err_fp, '\n');
  return err;
}

private int i_syntax_error (i_t *this, const char *msg) {
  this->print_bytes (this->err_fp, msg);
  this->print_fmt_bytes (this->err_fp, "syntax error before:");
  return i_err_ptr (this, I_ERR_SYNTAX);
}

private int i_arg_mismatch (i_t *this) {
  this->print_fmt_bytes (this->err_fp, "argument mismatch before:");
  return i_err_ptr (this, I_ERR_BADARGS);
}

private int i_too_many_args (i_t *this) {
  this->print_fmt_bytes (this->err_fp, "too many arguments before:");
  return i_err_ptr (this, I_ERR_TOOMANYARGS);
}

private int i_unknown_symbol (i_t *this) {
  this->print_fmt_bytes (this->err_fp, "unknown symbol before:");
  return i_err_ptr (this, I_ERR_UNKNOWN_SYM);
}

private int i_out_of_mem (i_t *this) {
  this->print_fmt_bytes (this->err_fp, "out of memory\n");
  return I_ERR_NOMEM;
}

private int i_peek_char (i_t *this, unsigned int n) {
  if (StringGetLen (this->parseptr) <= n) return -1;
  return *(StringGetPtr (this->parseptr) + n);
}

private void i_reset_token (i_t *this) {
  StringSetLen (&this->token, 0);
  StringSetPtr (&this->token, StringGetPtr (this->parseptr));
}

private void i_ignore_last_char (i_t *this) {
  StringSetLen (&this->token, StringGetLen (this->token) - 1);
}

private void i_ignore_first_char (i_t *this) {
  StringSetPtr (&this->token, StringGetPtr (this->token) + 1);
  StringSetLen (&this->token, StringGetLen (this->token) - 1);
}

private void i_ignore_next_char (i_t *this) {
  StringSetPtr (&this->parseptr, StringGetPtr (this->parseptr) + 1);
  StringSetLen (&this->parseptr, StringGetLen (this->parseptr) - 1);
}

private int i_get_char (i_t *this) {
  int c;
  unsigned len = StringGetLen (this->parseptr);
  const char *ptr;
  ifnot (len) return -1;

  ptr = StringGetPtr (this->parseptr);
  c = *ptr++;
  --len;

  StringSetPtr (&this->parseptr, ptr);
  StringSetLen (&this->parseptr, len);
  StringSetLen (&this->token, StringGetLen (this->token) + 1);
  return c;
}

private void i_unget_char (i_t *this) {
  StringSetLen (&this->parseptr, StringGetLen (this->parseptr) + 1);
  StringSetPtr (&this->parseptr, StringGetPtr (this->parseptr) - 1);
  i_ignore_last_char (this);
}

private int char_in (int c, const char *str) {
  while (*str) if (c is *str++) return 1;
  return 0;
}

private int isspace (int c) {
  return (c is ' ') or (c is '\t');
}

private int isdigit (int c) {
  return (c >= '0' and c <= '9');
}

private int ishexchar (int c) {
  return (c >= '0' and c <= '9') or char_in (c, "abcdefABCDEF");
}

private int islower (int c) {
  return (c >= 'a' and c <= 'z');
}

private int isupper (int c) {
  return (c >= 'A' and c <= 'Z');
}

private int isalpha (int c) {
  return islower (c) or isupper (c);
}

private int isidpunct (int c) {
  return char_in (c, ".:_");
}

private int isidentifier (int c) {
  return isalpha (c) or isidpunct (c);
}

private int notquote (int c) {
  return (c >= 0) and 0 is char_in (c, "\"\n");
}

private void i_get_span (i_t *this, int (*testfn) (int)) {
  int c;
  do c = i_get_char (this);  while (testfn (c));
  if (c isnot -1) i_unget_char (this);
}

private int isoperator (int c) {
  return char_in (c, "+-/*=<>&|^");
}

private Sym * i_lookup_sym (i_t *this, String_t name) {
  Sym *s = this->symptr;
  while ((ival_t) s > (ival_t) this->arena) {
    --s;
    if (stringeq (s->name, name))
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
  for (;;) {
    c = i_get_char (this);

    if (isspace (c))
      i_reset_token (this);
    else
      break;
  }

  if (c is '\\' and i_peek_char (this, 0) is '\n') {
    this->linenum++;
    i_ignore_next_char (this);
    i_reset_token (this);
    c = i_get_char (this);
  }

  if (c is '#') {
    do
      c = i_get_char (this);
    while (c >= 0 and c isnot '\n');
    this->linenum++;

    r = c;

  } else if (isdigit (c)) {
    if (c is '0' and char_in (i_peek_char (this, 0), "xX")
        and ishexchar (i_peek_char(this, 1))) {
      i_get_char (this);
      i_ignore_first_char (this);
      i_ignore_first_char (this);
      i_get_span (this, ishexchar);
      r = I_TOK_HEX_NUMBER;
    } else {
      i_get_span (this, isdigit);
      r = I_TOK_NUMBER;
    }

  } else if (isalpha (c)) {
    i_get_span (this, isidentifier);
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

    i_ignore_last_char (this);
    r = I_TOK_STRING;

  } else if (c is '"') {
    if (this->scope isnot FUNCTION_ARGUMENT_SCOPE) {
      i_reset_token (this);
      i_get_span (this, notquote);
      c = i_get_char (this);
      if (c < 0) return I_TOK_SYNTAX_ERR;
      i_ignore_last_char (this);
    } else {
      size_t len = 0;
      while ('"' isnot i_peek_char (this, len)) len++;
      char *ibuf = Alloc (len + 1);
      for (size_t i = 0; i < len; i++) {
        c = i_get_char (this);
        ibuf[i] = c;
      }
      ibuf[len] = '\0';

      istring_push (ibuf);
      c = i_get_char (this);
      this->tokenVal = STRING_TYPE_FUNC_ARGUMENT;
      i_reset_token (this);
    }

    r = I_TOK_STRING;

  } else
    r = c;

#ifdef DEBUG_INTERPRETER_OUTPUT
  this->print_fmt_bytes (this->err_fp, "Token[%c / %x] = ", r & 0xff, r);
  i_print_string (this, this->err_fp, this->token);
  this->print_byte (this->err_fp, '\n');
#endif

  this->curToken = r;
  return r;
}

private int i_next_token (i_t *this) { return i_do_next_token (this, 0); }
private int i_next_raw_token (i_t *this) { return i_do_next_token (this, 1); }

private void i_push (i_t *this, ival_t x) {
  --this->valptr;
  if ((ival_t) this->valptr < (ival_t) this->symptr)
    abort ();  // out of memory

  *this->valptr = x;
}

private ival_t i_pop (i_t *this) {
  ival_t r = 0;
  if ((ival_t) this->valptr < (ival_t) (this->arena + this->mem_size))
    r = *(this)->valptr++;

  return r;
}

private ival_t i_string_to_num (String_t s) {
  ival_t r = 0;
  int c;
  const char *ptr = StringGetPtr (s);
  int len = StringGetLen (s);
  while (len-- > 0) {
    c = *ptr++;
    ifnot (isdigit (c)) break;
    r = 10 * r + (c - '0');
  }
  return r;
}

private ival_t HexStringToNum (String_t s) {
  ival_t r = 0;
  int c;
  const char *ptr = StringGetPtr (s);
  int len = StringGetLen (s);
  while (len-- > 0) {
    c = *ptr++;
    ifnot (ishexchar (c)) break;
    if (c <= '9')
      r = 16 * r + (c - '0');
    else if (c <= 'F')
      r = 16 * r + (c - 'A' + 10);
    else
      r = 16 * r + (c - 'a' + 10);
  }
  return r;
}

private Sym *i_define_sym (i_t *this, String_t name, int typ, ival_t value) {
  Sym *s = this->symptr;
  if (StringGetPtr (name) is NULL) return NULL;

  this->symptr++;
  if ((ival_t) this->symptr >= (ival_t) this->valptr)
    return NULL;

  s->name = name;
  s->value = value;
  s->type = typ;
  return s;
}

private Sym * i_define_var (i_t *this, String_t name) {
  return i_define_sym (this, name, INT, 0);
}

private String_t cstring_new (const char *str) {
  String_t x;
  StringSetLen (&x, bytelen (str));
  StringSetPtr (&x, str);
  return x;
}

private int i_parse_expr_list (i_t *this) {
  int err, c;
  int count = 0;
  ival_t v;

  do {
    err = i_parse_expr (this, &v);
    if (err isnot I_OK) return err;

    if (v is STRING_TYPE_FUNC_ARGUMENT) {
      i_push (this, (ival_t) this);
    } else
      i_push (this, v);

    count++;

    c = this->curToken;
    if (c is ',') i_next_token (this);
  } while (c is ',');

  return count;
}

private int i_parse_string (i_t *this, String_t str, int saveStrings, int topLevel) {
  int c,  r;
  String_t savepc = this->parseptr;
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
      return i_syntax_error (this, "evaluated string failed, unkown token");
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

FILE *fp = fopen("/tmp/deb", "a+");
ifnot (uf) {
  fprintf (fp, "tArgs %d\n", this->tokenArgs);
  fflush (fp); }
  if (uf)
    expectargs = uf->nargs;
  else
    expectargs = this->tokenArgs;

  c = i_next_token (this);
  if (c isnot '(') return i_syntax_error (this, "expected open parentheses");
  this->scope = FUNCTION_ARGUMENT_SCOPE;
  c = i_next_token (this);
  if (c isnot ')') {
    paramCount = i_parse_expr_list (this);
    c = this->curToken;
    if (paramCount < 0) return paramCount;
  }

  this->scope = OUT_OF_FUNCTION_SCOPE;

  if (c isnot ')')
    return i_syntax_error (this, "expected closed parentheses");

fprintf (fp, "exp %d count %d\n", expectargs, paramCount);fflush (fp);
fclose (fp);
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
    return err;
  } else
    *vp = op ((ival_t) this, this->fArgs[0], this->fArgs[1], this->fArgs[2]);
    //, this->fArgs[3]);

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
    this->scope = FUNCTION_ARGUMENT_SCOPE;
    i_next_token (this);
    err = i_parse_expr (this, vp);

    if (err is I_OK) {
      c = this->curToken;

      if (c is ')') {
        i_next_token (this);
        this->scope = OUT_OF_FUNCTION_SCOPE;
        return I_OK;
      }
    }

    return err;

  } else if (c is I_TOK_NUMBER) {
    *vp = i_string_to_num(this->token);
    i_next_token (this);
    return I_OK;

  } else if (c is I_TOK_HEX_NUMBER) {
    *vp = HexStringToNum (this->token);
    i_next_token (this);
    return I_OK;

  } else if (c is I_TOK_VAR) {
    *vp = this->tokenVal;
    i_next_token (this);
    return I_OK;

  } else if (c is I_TOK_BUILTIN) {
    Cfunc op = (Cfunc) this->tokenVal;
    return i_parse_func_call (this, op, vp, NULL);

  } else if (c is USRFUNC) {
    Sym *sym = this->tokenSym;
    ifnot (sym) return i_syntax_error (this, "user defined function, not declared");

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
    return i_syntax_error (this, "syntax error");
}

/* parse one statement
 * 1 is true if we need to save strings we encounter (we've been passed
 * a temporary string)
 */

private int i_parse_stmt (i_t *this, int saveStrings) {
  int c;
  String_t name;
  ival_t val;
  int err = I_OK;

  c = this->curToken;

  if (c is I_TOK_VARDEF) {
    // a definition var a=x
    c = i_next_raw_token (this); // we want to get VAR_SYMBOL directly
    if (c isnot I_TOK_SYMBOL) return i_syntax_error (this, "expected symbol");

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
    // we expect the "=" operator
    // verify that it is "="
    if (StringGetPtr (this->token)[0] isnot '=' or
        StringGetLen(this->token) isnot 1)
      return i_syntax_error (this, "expected =");

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

  } else return i_syntax_error (this, "unknown token");

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

private int i_parse_if (i_t *this) {
  String_t ifpart, elsepart;
  int haveelse = 0;
  ival_t cond;
  int c;
  int err;

  c = i_next_token (this);
  err = i_parse_expr (this, &cond);
  if (err isnot I_OK) return err;

  c = this->curToken;
  if (c isnot I_TOK_STRING) return i_syntax_error (this, "parsing if, not a string");

  ifpart = this->token;
  c = i_next_token (this);
  if (c is I_TOK_ELSE) {
    c = i_next_token (this);
    if (c isnot I_TOK_STRING) return i_syntax_error(this, "parsing else, not a string");

    elsepart = this->token;
    haveelse = 1;
    i_next_token (this);
  }

  if (cond)
    err = i_parse_string (this, ifpart, 0, 0);
  else if (haveelse)
    err = i_parse_string(this, elsepart, 0, 0);

  if (err is I_OK and 0 is cond) err = I_ERR_OK_ELSE;
  return err;
}

private int i_parse_var_list (i_t *this, UserFunc *uf, int saveStrings) {
  int c;
  int nargs = 0;
  c = i_next_raw_token (this);

  for (;;) {
    if (c is I_TOK_SYMBOL) {
      String_t name = this->token;
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
      return i_syntax_error (this, "var definition, unxpected token");
  }

  uf->nargs = nargs;
  return nargs;
}

private int i_parse_func_def (i_t *this, int saveStrings) {
  Sym *sym;
  String_t name;
  String_t body;
  int c;
  int nargs = 0;
  UserFunc *uf;

  c = i_next_raw_token (this); // do not interpret the symbol
  if (c isnot I_TOK_SYMBOL) return i_syntax_error (this, "fun definition, not a symbol");

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

  if (c isnot I_TOK_STRING) return i_syntax_error (this, "fun definition, not a string");

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

private int i_parse_print (i_t *this) {
  int c;
  int err = I_OK;

print_more:
  c = i_next_token (this);
  if (c is I_TOK_STRING) {
    i_print_string(this, this->out_fp, this->token);
    i_next_token(this);
  } else {
    ival_t val;
    err = i_parse_expr (this, &val);
    if (err isnot I_OK) return err;

    i_print_number (this, val);
  }

  if (this->curToken is ',') goto print_more;

  this->print_byte (this->out_fp, '\n');
  return err;
}

private int i_parse_return (i_t *this) {
  int err;
  i_next_token (this);
  err = i_parse_expr (this, &this->fResult);
  // terminate the script
  StringSetLen (&this->parseptr, 0);
  return err;
}

private int
i_parse_while (i_t *this) {
  int err;
  String_t savepc = this->parseptr;

again:
  err = i_parse_if (this);
  if (err == I_ERR_OK_ELSE) {
    return I_OK;
  } else if (err == I_OK) {
    this->parseptr = savepc;
    goto again;
  }
  return err;
}

// builtin
private ival_t prod(ival_t x, ival_t y) { return x*y; }
private ival_t quot(ival_t x, ival_t y) { return x/y; }
private ival_t sum(ival_t x, ival_t y) { return x+y; }
private ival_t diff(ival_t x, ival_t y) { return x-y; }
private ival_t bitand(ival_t x, ival_t y) { return x&y; }
private ival_t bitor(ival_t x, ival_t y) { return x|y; }
private ival_t bitxor(ival_t x, ival_t y) { return x^y; }
private ival_t shl(ival_t x, ival_t y) { return x<<y; }
private ival_t shr(ival_t x, ival_t y) { return x>>y; }
private ival_t equals(ival_t x, ival_t y) { return x==y; }
private ival_t ne(ival_t x, ival_t y) { return x!=y; }
private ival_t lt(ival_t x, ival_t y) { return x<y; }
private ival_t le(ival_t x, ival_t y) { return x<=y; }
private ival_t gt(ival_t x, ival_t y) { return x>y; }
private ival_t ge(ival_t x, ival_t y) { return x>=y; }

private struct def {
  const char *name;
  int toktype;
  ival_t val;
} idefs[] = {
  { "var",    I_TOK_VARDEF,  (ival_t) 0 },
  { "else",   I_TOK_ELSE,    (ival_t) 0 },
  { "if",     I_TOK_IF,      (ival_t) i_parse_if },
  { "while",  I_TOK_WHILE,   (ival_t) i_parse_while },
  { "print",  I_TOK_PRINT,   (ival_t) i_parse_print },
  { "func",   I_TOK_FUNCDEF, (ival_t) i_parse_func_def },
  { "return", I_TOK_RETURN,  (ival_t) i_parse_return },
  // operators
  { "*",     BINOP(1), (ival_t) prod },
  { "/",     BINOP(1), (ival_t) quot },
  { "+",     BINOP(2), (ival_t) sum },
  { "-",     BINOP(2), (ival_t) diff },
  { "&",     BINOP(3), (ival_t) bitand },
  { "|",     BINOP(3), (ival_t) bitor },
  { "^",     BINOP(3), (ival_t) bitxor },
  { ">>",    BINOP(3), (ival_t) shr },
  { "<<",    BINOP(3), (ival_t) shl },
  { "=",     BINOP(4), (ival_t) equals },
  { "<>",    BINOP(4), (ival_t) ne },
  { "<",     BINOP(4), (ival_t) lt },
  { "<=",    BINOP(4), (ival_t) le },
  { ">",     BINOP(4), (ival_t) gt },
  { ">=",    BINOP(4), (ival_t) ge },
  { NULL, 0, 0 }
};

private int i_define (i_t *this, const char *name, int typ, ival_t val) {
  Sym *s;
  s = i_define_sym (this, cstring_new (name), typ, val);
  if (NULL is s) return i_out_of_mem (this);
  return I_OK;
}

private int i_eval_string (i_t *this, const char *buf, int saveStrings, int topLevel) {
  return i_parse_string (this, cstring_new (buf), saveStrings, topLevel);
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
    this->print_fmt_bytes (this->err_fp, "%s\n", Error.string (E(get.current), errno));
    return NOTOK;
  }

  r = fread (script, 1, this->max_script_size, fp);
  fclose (fp);

  if (r <= 0) {
    this->print_fmt_bytes (this->err_fp, "Couldn't read script\n");
    return NOTOK;
  }

  script[r] = 0;
  r = In.eval_string (this, script, 0, 1);
  if (r isnot I_OK) {
    char *err_msg[] = {"NO MEMORY", "SYNTAX ERROR", "UNKNOWN SYMBOL",
        "BAD ARGUMENTS", "TOO MANY ARGUMENTS"};
    this->print_fmt_bytes (this->err_fp, "%s\n", err_msg[-r - 1]);
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

private i_t *i_new (void) {
  Type (i) *this = AllocType (i);
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

private Type (i) *i_append_instance (Class (I) *this, Type (i) *instance) {
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

private Type (i) *i_set_current (Class (I) *this, int idx) {
  if (idx >= $my(num_instances)) return NULL;
  Type (i) *it = $my(head);
  int i = 0;
  while (i++ < idx) it = it->next;

  return it;
}

private Type (i) *i_get_current (Class (I) *this) {
  Type (i) *it = $my(head);
  int i = 0;
  while (i++ < $my(current_idx)) it = it->next;

  return it;
}

private int i_get_current_idx (Class (I) *this) {
  return $my(current_idx);
}

private ival_t i_exit (ival_t inst, int code) {
  (void) inst;
  __deinit_this__ (&__THIS__);
  exit (code);
}

struct ifun_t {
  const char *name;
  ival_t val;
  int nargs;
} i_funs[] = {
  { "exit",  (ival_t) i_exit, 1},
  { NULL, 0, 0}
};

private int i_init (Class (I) *interp, i_t *this, I_INIT opts) {
  int i;
  int err = 0;

  if (NULL is opts.name)
    i_name_gen (this->name, &interp->prop->name_gen, "i:", 2);
  else
    Cstring.cp (this->name, 32, opts.name, 31);

  this->arena = (char *) Alloc (opts.mem_size);
  this->mem_size = opts.mem_size;
  this->print_byte = opts.print_byte;
  this->print_fmt_bytes = opts.print_fmt_bytes;
  this->print_bytes = opts.print_bytes;
  this->err_fp = opts.err_fp;
  this->out_fp = opts.out_fp;
  this->max_script_size = opts.max_script_size;

  this->symptr = (Sym *) this->arena;
  this->valptr = (ival_t *) (this->arena + this->mem_size);
  this->strings = AllocType (Istrings);

  for (i = 0; idefs[i].name; i++) {
    err = i_define (this, idefs[i].name, idefs[i].toktype, idefs[i].val);

    if (err isnot I_OK) {
      i_free (&this);
      return err;
    }
  }

  for (i = 0; i_funs[i].name; i++) {
    err = i_define (this, i_funs[i].name, CFUNC (i_funs[i].nargs), i_funs[i].val);
    if (err isnot I_OK) {
      i_free (&this);
      return err;
    }
  }

  i_append_instance (interp, this);
  return I_OK;
}

public Class (I) *__init_i__ (void) {
  Class (I) *this =  AllocClass (I);

  *this = ClassInit (I,
    .self = SelfInit (i,
      .new = i_new,
      .free = i_free,
      .init = i_init,
      .def =  i_define,
      .append_instance = i_append_instance,
      .eval_string =  i_eval_string,
      .eval_file = i_eval_file,
      .get = SubSelfInit (i, get,
        .current = i_get_current,
        .current_idx = i_get_current_idx
      ),
      .set = SubSelfInit (i, set,
        .current = i_set_current
      )
    ),
    .prop = $myprop,
  );

  $my(name_gen) = ('z' - 'a') + 1;
  $my(head) = NULL;
  $my(num_instances) = 0;
  $my(current_idx) = -1;

  string_t *ddir = E(get.env, "data_dir");
  size_t len = ddir->num_bytes + 1 + 8;
  char profiles[len + 1];
  Cstring.cp_fmt (profiles, len + 1, "%s/profiles", ddir->bytes);
  ifnot (File.exists (profiles))
    mkdir (profiles, S_IRWXU);

  return this;
}

private void i_free_strings (Type (i) *this) {
  Type (istring) *it = mystrings->head;
  while (it) {
    Type (istring) *next = it->next;
    free (it->ibuf);
    free (it);
    it = next;
  }
}

public void __deinit_i__ (Class (I) **thisp) {
  if (NULL is *thisp) return;
  Class (I) *this = *thisp;

  Type (i) *it = $my(head);
  while (it) {
    Type (i) *tmp = it->next;
    i_free_strings (it);
    free (it->strings);

#if DEBUG_INTERPRETER
    fclose (it->err_fp);
#endif

    i_free (&it);
    it = tmp;
  }

  free ($myprop);
  free (this);
  *thisp = NULL;
}

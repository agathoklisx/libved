#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <locale.h>
#include <errno.h>

#include "libved.h"
#include "libved+.h"
#include "lib/lib+.h"

public Class (this) *__This__ = NULL;

#if HAS_USER_EXTENSIONS
  #include "usr/usr.c"
#endif

#if HAS_LOCAL_EXTENSIONS
  #include "local/local.c"
#endif

/* Argparse:
  https://github.com/cofyc/argparse

  forked commit: fafc503d23d077bda40c29e8a20ea74707452721
  (HEAD -> master, origin/master, origin/HEAD)
*/

/**
 * Copyright (C) 2012-2015 Yecheng Fu <cofyc.jackson at gmail dot com>
 * The MIT License (MIT)
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

#define OPT_UNSET 1
#define OPT_LONG  (1 << 1)

private const char *argparse_prefix_skip (const char *str, const char *prefix) {
  size_t len = bytelen (prefix);
  return Cstring.cmp_n (str, prefix, len) ? NULL : str + len;
}

private int argparse_error (argparse_t *self, const argparse_option_t *opt,
                                            const char *reason, int flags) {
  (void) self;
  if (flags & OPT_LONG) {
    fprintf(stderr, "error: option `--%s` %s\n", opt->long_name, reason);
  } else {
    fprintf(stderr, "error: option `-%c` %s\n", opt->short_name, reason);
  }
  return -1;
}

private int argparse_getvalue (argparse_t *self, const argparse_option_t *opt, int flags) {
  const char *s = NULL;
  if (!opt->value)
    goto skipped;

  switch (opt->type) {
    case ARGPARSE_OPT_BOOLEAN:
      if (flags & OPT_UNSET) {
        *(int *)opt->value = *(int *)opt->value - 1;
      } else {
        *(int *)opt->value = *(int *)opt->value + 1;
      }
      if (*(int *)opt->value < 0) {
        *(int *)opt->value = 0;
      }
      break;

    case ARGPARSE_OPT_BIT:
      if (flags & OPT_UNSET) {
        *(int *)opt->value &= ~opt->data;
      } else {
        *(int *)opt->value |= opt->data;
      }
      break;

    case ARGPARSE_OPT_STRING:
      if (self->optvalue) {
        *(const char **)opt->value = self->optvalue;
        self->optvalue             = NULL;
      } else if (self->argc > 1) {
        self->argc--;
        *(const char **)opt->value = *++self->argv;
      } else {
        return argparse_error (self, opt, "requires a value", flags);
      }
      break;

    case ARGPARSE_OPT_INTEGER:
      errno = 0;
      if (self->optvalue) {
        *(int *)opt->value = strtol(self->optvalue, (char **)&s, 0);
        self->optvalue     = NULL;
      } else if (self->argc > 1) {
        self->argc--;
        *(int *)opt->value = strtol (*++self->argv, (char **)&s, 0);
      } else {
        return argparse_error (self, opt, "requires a value", flags);
      }
      if (errno)
        return argparse_error (self, opt, strerror(errno), flags);
      if (s[0] != '\0')
        return argparse_error (self, opt, "expects an integer value", flags);
      break;

    case ARGPARSE_OPT_FLOAT:
      errno = 0;
      if (self->optvalue) {
        *(float *)opt->value = strtof (self->optvalue, (char **)&s);
        self->optvalue       = NULL;
      } else if (self->argc > 1) {
        self->argc--;
        *(float *)opt->value = strtof (*++self->argv, (char **)&s);
      } else {
        return argparse_error (self, opt, "requires a value", flags);
        }
      if (errno)
        return argparse_error (self, opt, strerror(errno), flags);
      if (s[0] != '\0')
        return argparse_error (self, opt, "expects a numerical value", flags);
      break;

    default:
      exit (0);
    }

skipped:
  if (opt->callback) {
    return opt->callback(self, opt);
  }

  return 0;
}

private void argparse_options_check (const argparse_option_t *options) {
  for (; options->type != ARGPARSE_OPT_END; options++) {
    switch (options->type) {
    case ARGPARSE_OPT_END:
    case ARGPARSE_OPT_BOOLEAN:
    case ARGPARSE_OPT_BIT:
    case ARGPARSE_OPT_INTEGER:
    case ARGPARSE_OPT_FLOAT:
    case ARGPARSE_OPT_STRING:
    case ARGPARSE_OPT_GROUP:
      continue;
    default:
      fprintf(stderr, "wrong option type: %d", options->type);
      break;
    }
  }
}

private int argparse_short_opt (argparse_t *self, const argparse_option_t *options) {
  for (; options->type != ARGPARSE_OPT_END; options++) {
    if (options->short_name == *self->optvalue) {
      self->optvalue = self->optvalue[1] ? self->optvalue + 1 : NULL;
      return argparse_getvalue (self, options, 0);
    }
  }
  return -2;
}

private int argparse_long_opt (argparse_t *self, const argparse_option_t *options) {
  for (; options->type != ARGPARSE_OPT_END; options++) {
    const char *rest;
    int opt_flags = 0;
    if (!options->long_name)
      continue;

    rest =  argparse_prefix_skip (self->argv[0] + 2, options->long_name);
    if (!rest) {
      // negation disabled?
      if (options->flags & OPT_NONEG) {
        continue;
      }
      // only OPT_BOOLEAN/OPT_BIT supports negation
      if (options->type != ARGPARSE_OPT_BOOLEAN && options->type !=  ARGPARSE_OPT_BIT) {
        continue;
      }

      ifnot (Cstring.eq_n (self->argv[0] + 2, "no-", 3)) {
        continue;
      }

      rest = argparse_prefix_skip (self->argv[0] + 2 + 3, options->long_name);
      if (!rest)
        continue;

      opt_flags |= OPT_UNSET;
    }

    if (*rest) {
      if (*rest != '=')
        continue;
      self->optvalue = rest + 1;
    }
    return argparse_getvalue (self, options, opt_flags | OPT_LONG);
  }

  return -2;
}

private int argparse_init (argparse_t *self, argparse_option_t *options,
                                  const char *const *usages, int flags) {
  memset (self, 0, sizeof(*self));
  self->options     = options;
  self->usages      = usages;
  self->flags       = flags;
  self->description = NULL;
  self->epilog      = NULL;
  return 0;
}

private void argparse_describe (argparse_t *self, const char *description,
                                                      const char *epilog) {
  self->description = description;
  self->epilog      = epilog;
}

private void argparse_usage (argparse_t *self) {
  if (self->usages) {
    fprintf(stdout, "Usage: %s\n", *self->usages++);
    while (*self->usages && **self->usages)
      fprintf(stdout, "   or: %s\n", *self->usages++);
  } else {
    fprintf(stdout, "Usage:\n");
  }

  // print description
  if (self->description)
    fprintf(stdout, "%s\n", self->description);

  fputc('\n', stdout);

  const argparse_option_t *options;

  // figure out best width
  size_t usage_opts_width = 0;
  size_t len;
  options = self->options;
  for (; options->type != ARGPARSE_OPT_END; options++) {
    len = 0;
    if ((options)->short_name) {
      len += 2 - ((options)->flags & SHORT_OPT_HAS_NO_DASH);
    }
    if ((options)->short_name && (options)->long_name) {
      len += 2;           // separator ", "
    }
    if ((options)->long_name) {
      len += bytelen ((options)->long_name) + 2;
    }
    if (options->type == ARGPARSE_OPT_INTEGER) {
      len += bytelen ("=<int>");
    }
    if (options->type == ARGPARSE_OPT_FLOAT) {
      len += bytelen ("=<flt>");
    } else if (options->type == ARGPARSE_OPT_STRING) {
      len += bytelen ("=<str>");
    }
    len = (len + 3) - ((len + 3) & 3);
    if (usage_opts_width < len) {
      usage_opts_width = len;
    }
  }
  usage_opts_width += 4;      // 4 spaces prefix

  options = self->options;
  for (; options->type != ARGPARSE_OPT_END; options++) {
    size_t pos = 0;
    int pad    = 0;
    if (options->type == ARGPARSE_OPT_GROUP) {
      fputc('\n', stdout);
      fprintf(stdout, "%s", options->help);
      fputc('\n', stdout);
      continue;
    }
    pos = fprintf(stdout, "    ");
    if (options->short_name) {
      pos += fprintf(stdout, "%s%c", // extension
      (options)->flags & SHORT_OPT_HAS_NO_DASH ? "" : "-",
      options->short_name);
    }
    if (options->long_name && options->short_name) {
      pos += fprintf (stdout, ", ");
    }
    if (options->long_name) {
      pos += fprintf (stdout, "--%s", options->long_name);
    }
    if (options->type == ARGPARSE_OPT_INTEGER) {
      pos += fprintf (stdout, "=<int>");
    } else if (options->type == ARGPARSE_OPT_FLOAT) {
      pos += fprintf (stdout, "=<flt>");
    } else if (options->type == ARGPARSE_OPT_STRING) {
      pos += fprintf (stdout, "=<str>");
    }
    if (pos <= usage_opts_width) {
      pad = usage_opts_width - pos;
    } else {
      fputc('\n', stdout);
      pad = usage_opts_width;
    }
    fprintf (stdout, "%*s%s\n", pad + 2, "", options->help);
  }

  // print epilog
  if (self->epilog)
    fprintf (stdout, "%s\n", self->epilog);
}

private int argparse_parse (argparse_t *self, int argc, const char **argv) {
  self->argc = argc - 1;
  self->argv = argv + 1;
  self->out  = argv;

  argparse_options_check (self->options);

  for (; self->argc; self->argc--, self->argv++) {
    const char *arg = self->argv[0];
    int opt_has_no_dash = 0;
    if (arg[0] != '-' || !arg[1]) {
      if (self->flags & ARGPARSE_STOP_AT_NON_OPTION) {
        goto end;
      }

      if (!arg[1]) {  /* extension */
        for (int i = 0; self->options[i].type != ARGPARSE_OPT_END; i++) {
          if (self->options[i].short_name == arg[0] &&
            self->options[i].flags & SHORT_OPT_HAS_NO_DASH) {
            opt_has_no_dash = 1;
            break;
          }
        }
      }

      if (!opt_has_no_dash) {
        // if it's not option or is a single char '-', copy verbatim
        self->out[self->cpidx++] = self->argv[0];
        continue;
      }
    }

    // short option
    if (arg[1] != '-') {
      self->optvalue = arg + 1 - opt_has_no_dash;
      switch (argparse_short_opt (self, self->options)) {
        case -1:
          return -1;

         case -2:
           goto unknown;
      }

      while (self->optvalue) {
        switch (argparse_short_opt (self, self->options)) {
          case -1:
            return -1;

          case -2:
            goto unknown;
        }
      }

      continue;
    }

    // if '--' presents
    if (!arg[2]) {
      self->argc--;
      self->argv++;
      break;
    }

    // long option
    switch (argparse_long_opt (self, self->options)) {
      case -1:
        return -1;

      case -2:
        goto unknown;
    }
    continue;

unknown:
    fprintf (stderr, "error: unknown option `%s`\n", self->argv[0]);
    argparse_usage (self);
    return -1;
  }

end:
  memmove (self->out + self->cpidx, self->argv,
           self->argc * sizeof(*self->out));
  self->out[self->cpidx + self->argc] = NULL;

  return self->cpidx + self->argc;
}

private int argparse_help_cb (argparse_t *self, const argparse_option_t *option) {
  (void)option;
  argparse_usage (self);
  return -1;
}

private Class (argparse) __init_argparse__ (void) {
  return ClassInit (argparse,
    .self = SelfInit (argparse,
      .init = argparse_init,
      .exec =  argparse_parse
    )
  );
}

private string_t *this_parse_command (Class (this) *this, char *bytes) {
  (void) this;
  string_t *com = String.new (256);
  char *sp = bytes;
  while (*sp) {
    if (*sp isnot ':')
      String.append_byte (com, *sp);
    else {
      if (*(sp+1) isnot ':')
        String.append_byte (com, *sp);
      else {
        String.append_byte (com, ' ');
        sp++;
      }
    }
    sp++;
  }

  return com;
}

#if HAS_TCC
private void __tcc_free (tcc_t **thisp) {
  if (NULL is thisp) return;

  tcc_t *this = *thisp;
  ifnot (NULL is this->handler) {
    tcc_delete (this->handler);
    this->handler = NULL;
  }

  free (this);
  thisp = NULL;
}

private tcc_t *__tcc_new (void) {
  tcc_t *this = AllocType (tcc);
  this->handler = tcc_new ();
  return this;
}

private int tcc_set_path (tcc_t *this, char *path, int type) {
  switch (type) {
    case TCC_CONFIG_TCC_DIR:
      tcc_set_lib_path (this->handler, path);
      return 0;

    case TCC_ADD_INC_PATH:
      return tcc_add_include_path (this->handler, path);

    case TCC_ADD_SYS_INC_PATH:
      return tcc_add_sysinclude_path (this->handler, path);

    case TCC_ADD_LPATH:
      return tcc_add_library_path (this->handler, path);

    case TCC_ADD_LIB:
      return tcc_add_library (this->handler, path);

    case TCC_SET_OUTPUT_PATH:
      return tcc_output_file (this->handler, path);

    default:
      return NOTOK;
    }

  return NOTOK;
}

private void __tcc_set_options (tcc_t *this, char *opt) {
  tcc_set_options (this->handler, opt);
}

private int __tcc_set_output_type (tcc_t *this, int type) {
  return (this->retval = tcc_set_output_type (this->handler, type));
}

private void tcc_set_error_handler (tcc_t *this, void *obj, TCCErrorFunc cb) {
  tcc_set_error_func (this->handler, obj, cb);
}

private int __tcc_compile_string (tcc_t *this, char *src) {
  return (this->retval = tcc_compile_string (this->handler, src));
}

private int tcc_compile_file (tcc_t *this, char *file) {
  return (this->retval = tcc_add_file (this->handler, file));
}

private int __tcc_run (tcc_t *this, int argc, char **argv) {
  return (this->retval = tcc_run (this->handler, argc, argv));
}

private int __tcc_relocate (tcc_t *this, void *mem) {
  return (this->retval = tcc_relocate (this->handler, mem));
}

private tcc_T __init_tcc__ (void) {
  return ClassInit (tcc,
    .self = SelfInit (tcc,
      .free = __tcc_free,
      .new = __tcc_new,
      .compile_string = __tcc_compile_string,
      .compile_file = tcc_compile_file,
      .run = __tcc_run,
      .relocate = __tcc_relocate,
      .set = SubSelfInit (tcc, set,
        .path = tcc_set_path,
        .output_type = __tcc_set_output_type,
        .options = __tcc_set_options,
        .error_handler = tcc_set_error_handler
      )
    )
  );
}

private void c_tcc_error_cb (void *obj, const char *msg) {
  (void) obj;
  ed_t *ed = E.get.current (THIS_E);
  Msg.write (ed, "====- Tcc Error Message -====\n");
  Msg.write (ed, (char *) msg);
}

private int c_tcc_string_add_lnums_cb (Vstring_t *str, char *tok, void *obj) {
  (void) str;
  ed_t *ed = E.get.current (THIS_E);
  int *lnr = (int *) obj;
  Ed.append.message_fmt (ed, "%d|%s", ++(*lnr), tok);
  return OK;
}

private void c_tcc_string_add_lnums (char *src) {
  Vstring_t unused;
  int lnr = 0;
  Cstring.chop (src, '\n', &unused, c_tcc_string_add_lnums_cb, &lnr);
}

private int c_tcc_string (buf_t **thisp, char *src) {
  (void) thisp;
  ed_t *ed = E.get.current (THIS_E);
  tcc_t *this = Tcc.new ();

  Tcc.set.error_handler (this, NULL, c_tcc_error_cb);
  Tcc.set.output_type (this, TCC_OUTPUT_MEMORY);

  int retval = NOTOK;
  if (NOTOK is (retval = Tcc.compile_string (this, src))) {
failed:
    Ed.append.message (ed, "Failed to compile string\n");
    c_tcc_string_add_lnums (src);
    goto theend;
  }

  char *argv[] = {"libved_module"};
  if (NOTOK is (retval = Tcc.run (this, 1, argv)))
    goto failed;

theend:
  Tcc.free (&this);
  Ed.append.message_fmt (ed, "exitstatus: %d\n", retval);
  return retval;
}

private int __tcc_compile__ (buf_t **thisp, string_t *src) {
  ed_t *ed = E.get.current (THIS_E);
  term_t *term = Ed.get.term (ed);
  Term.reset (term);
  int exit_code = c_tcc_string (thisp, src->bytes);
  String.free (src);
  Term.set_mode (term, 'r');
  Input.get (term);
  Term.set (term);
  Ed.draw.current_win (ed);

  if (NOTOK is exit_code)
    Ed.messages (ed, thisp, NOT_AT_EOF);

  return exit_code;
}

#endif /* HAS_TCC */

/* Math Expr */
#define MATH_OK            OK
#define MATH_NOTOK         NOTOK
#define MATH_BASE_ERROR    2000
#define MATH_COMPILE_NOREF (MATH_BASE_ERROR + 1)
#define MATH_EVAL_NOREF    (MATH_BASE_ERROR + 2)
#define MATH_INTERP_NOREF  (MATH_BASE_ERROR + 3)
#define MATH_COMPILE_ERROR (MATH_BASE_ERROR + 4)
#define MATH_LAST_ERROR    MATH_COMPILE_ERROR
#define MATH_OUT_OF_BOUNDS_ERROR MATH_LAST_ERROR + 1

private void math_free (math_t **thisp) {
  String.free ((*thisp)->data);
  String.free ((*thisp)->lang);
  String.free ((*thisp)->error_string);

  ifnot (NULL is (*thisp)->free)
    (*thisp)->free ((*thisp));

  free (*thisp);
  thisp = NULL;
}

private math_t *math_new (char *lang, MathCompile_cb compile,
    MathEval_cb eval, MathInterp_cb interp, MathFree_cb free_ref) {
  math_t *this = AllocType (math);
  this->retval = this->error = MATH_OK;
  this->lang = String.new_with (lang);
  this->error_string = String.new (8);
  this->compile = compile;
  this->eval = eval;
  this->interp = interp;
  this->free = free_ref;
  return this;
}

private char *math_strerror (math_t *this, int error) {
  if (error > MATH_LAST_ERROR)
    this->error = MATH_OUT_OF_BOUNDS_ERROR;
  else
    this->error = error;

  char *math_errors[] = {
    "NULL Function Reference (compile)",
    "NULL Function Reference (eval)",
    "NULL Function Reference (interp)",
    "Compilation ERROR",
    "Evaluation ERROR",
    "Interpretation ERROR",
    "INTERNAL ERROR, NO SUCH_ERROR, ERROR IS OUT OF BOUNDS"};

  String.append (this->error_string, math_errors[this->error - MATH_BASE_ERROR - 1]);

  ifnot (NULL is this->strerror) return this->strerror (this, error);

  return this->error_string->bytes;
}

private int math_compile (math_t *this) {
  if (NULL is this->compile) {
    this->retval = MATH_NOTOK;
    math_strerror (this, MATH_COMPILE_NOREF);
    return MATH_NOTOK;
  }

  return this->compile (this);
}

private int math_eval (math_t *this) {
  if (NULL is this->eval) {
    this->retval = MATH_NOTOK;
    math_strerror (this, MATH_EVAL_NOREF);
    return MATH_NOTOK;
  }

  return this->eval (this);
}

private int math_interp (math_t *this) {
  if (NULL is this->interp) {
    this->retval = MATH_NOTOK;
    math_strerror (this, MATH_INTERP_NOREF);
    return MATH_NOTOK;
  }

  return this->interp (this);
}

public math_T __init_math__ (void) {
  return ClassInit (math,
    .self = SelfInit (math,
      .new = math_new,
      .free = math_free,
      .compile = math_compile,
      .interp = math_interp,
      .eval = math_eval,
      .strerror = math_strerror
    )
  );
}

/* proc */

#define PIPE_READ_END  0
#define PIPE_WRITE_END 1

private void proc_free_argv (proc_t *this) {
  if (NULL is $my(argv))
    return;

  for (int i = 0; i <= $my(argc); i++)
    free ($my(argv)[i]);
  free ($my(argv));
}

private void proc_free (proc_t *this) {
  ifnot (this) return;
  proc_free_argv (this);
  if (NULL isnot $my(stdin_buf))
    free ($my(stdin_buf));

  free (this->prop);
  free (this);
}

private int proc_output_to_stream (buf_t *this, FILE *stream, fp_t *fp) {
  (void) this;
  char *line = NULL;
  size_t len = 0;
  while (-1 isnot getline (&line, &len, fp->fp))
    fprintf (stream, "%s\r", line);

  ifnot (NULL is line) free (line);
  return 0;
}

private proc_t *proc_new (void) {
  proc_t *this= AllocType (proc);
  $myprop = AllocProp (proc);
  $my(pid) = -1;
  $my(stdin_buf) = NULL;
  $my(dup_stdin) = 0;
  $my(read_stdout) = 0;
  $my(read_stderr) = 0;
  $my(read) = proc_output_to_stream;
  $my(argc) = 0;
  $my(argv) = NULL;
  $my(reset_term) = 0;
  $my(prompt_atend) = 1;
  return this;
}

private int proc_wait (proc_t *this) {
  if (-1 is $my(pid)) return NOTOK;
  $my(status) = 0;
  waitpid ($my(pid), &$my(status), 0);
 // waitpid ($my(pid), &$my(status), WNOHANG|WUNTRACED);
  if (WIFEXITED ($my(status)))
    return WEXITSTATUS ($my(status));
  return -1;
}

private int proc_read (proc_t *this) {
  int retval = NOTOK;

  if ($my(read_stdout)) {
    if ($my(read) isnot NULL and $my(buf) isnot NULL) {
      fp_t fp = (fp_t) {.fp = fdopen ($my(stdout_fds)[PIPE_READ_END], "r")};
      retval = $my(read) ($my(buf), stdout, &fp);
      fclose (fp.fp);
    }
  }

  if ($my(read_stderr)) {
    if ($my(read) isnot NULL and $my(buf) isnot NULL) {
      fp_t fp = (fp_t) {.fp = fdopen ($my(stderr_fds)[PIPE_READ_END], "r")};
      retval = $my(read) ($my(buf), stderr, &fp);
      fclose (fp.fp);
    }
  }

  return retval;
}

private char **proc_parse (proc_t *this, char *com) {
  char *sp = com;

  char *tokbeg;
  $my(argv) = Alloc (sizeof (char *));
  size_t len;

  while (*sp) {
    while (*sp and *sp is ' ') sp++;
    ifnot (*sp) break;

    if (*sp is '&' and *(sp + 1) is 0) {
      $my(is_bg) = 1;
      break;
    }

    tokbeg = sp;

    if (*sp is '"') {
      sp++;
      tokbeg++;

parse_quoted:
      while (*sp and *sp isnot '"') sp++;
      ifnot (*sp) goto theerror;
      if (*(sp - 1) is '\\') goto parse_quoted;
      len = (size_t) (sp - tokbeg);
      sp++;
      goto add_arg;
    }

    while (*sp and *sp isnot ' ') sp++;
    ifnot (*sp) {
      if (*(sp - 1) is '&') {
        $my(is_bg) = 1;
        sp--;
      }
    }

    len = (size_t) (sp - tokbeg);

add_arg:
    $my(argc)++;
    $my(argv) = Realloc ($my(argv), sizeof (char *) * ($my(argc) + 1));
    $my(argv)[$my(argc)-1] = Alloc (len + 1);
    Cstring.cp ($my(argv)[$my(argc)-1], len + 1, tokbeg, len);
    ifnot (*sp) break;
    sp++;
  }

  $my(argv)[$my(argc)] = (char *) NULL;
  return $my(argv);

theerror:
  proc_free_argv (this);
  return NULL;
}

private void proc_close_pipe (int num, ...) {
  va_list ap;
  int *p;
  int is_open;
  va_start (ap, num);
  is_open = va_arg (ap, int);
  p = va_arg (ap, int *);
  if (is_open) {
    close (p[0]);
    close (p[1]);
  }
  va_end (ap);
}

private int proc_open (proc_t *this) {
  if (NULL is this) return NOTOK;
  ifnot ($my(argc)) return NOTOK;

  if ($my(dup_stdin)) {
    if (-1 is pipe ($my(stdin_fds)))
      return NOTOK;
  }

  if ($my(read_stdout)) {
    if (-1 is pipe ($my(stdout_fds))) {
      proc_close_pipe (1, $my(dup_stdin), $my(stdin_fds));
      return NOTOK;
    }
  }

  if ($my(read_stderr)) {
    if (-1 is pipe ($my(stderr_fds))) {
      proc_close_pipe (1,
          $my(dup_stdin), $my(stdin_fds),
          $my(read_stdout), $my(stdout_fds));
      return NOTOK;
    }
  }

  if (-1 is ($my(pid) = fork ())) {
    proc_close_pipe (1,
       $my(dup_stdin), $my(stdin_fds),
       $my(read_stdout), $my(stdout_fds),
       $my(read_stderr), $my(stderr_fds));
    return NOTOK;
  }

  /* this code seems to prevent from execution many of interactive applications
   * which is desirable (many, but not all), but this looks to work for commands
   * that need to be executed and in the case of :r! to read from standard output
   */

  ifnot ($my(pid)) {
    setpgid (0, 0);
    setsid ();
    if ($my(read_stderr)) {
      dup2 ($my(stderr_fds)[PIPE_WRITE_END], fileno (stderr));
      close ($my(stderr_fds)[PIPE_READ_END]);
    }

    if ($my(read_stdout)) {
      close ($my(stdout_fds)[PIPE_READ_END]);
      dup2 ($my(stdout_fds)[PIPE_WRITE_END], fileno (stdout));
    }

    if ($my(dup_stdin)) {
      dup2 ($my(stdin_fds)[PIPE_READ_END], STDIN_FILENO);
      ifnot (NULL is $my(stdin_buf)) {
        int ign = write ($my(stdin_fds)[PIPE_WRITE_END], $my(stdin_buf), $my(stdin_buf_size));
        (void) ign;
      }

      close ($my(stdin_fds)[PIPE_WRITE_END]);
    }

    execvp ($my(argv)[0], $my(argv));
    $my(sys_errno) = errno;
    _exit (1);
  }

  if ($my(dup_stdin)) close ($my(stdin_fds)[PIPE_READ_END]);
  if ($my(read_stdout)) close ($my(stdout_fds)[PIPE_WRITE_END]);
  if ($my(read_stderr)) close ($my(stderr_fds)[PIPE_WRITE_END]);

  return $my(pid);
}

private int proc_exec (proc_t *this, char *com) {
  int retval = 0;

  if ($my(reset_term))
    Term.reset ($my(term));

  proc_parse (this, com);

  if (NOTOK is proc_open (this)) goto theend;

  proc_read (this);

  retval = proc_wait (this);

theend:
  if($my(reset_term))
    if ($my(prompt_atend)) {
      Term.set_mode ($my(term), 'r');
      Input.get ($my(term));
      Term.set ($my(term));
    }

  return retval;
}

private proc_T __init_proc__ (void) {
  return ClassInit (proc,
    .self = SelfInit (proc,
      .new = proc_new,
      .free = proc_free,
      .wait = proc_wait,
      .parse = proc_parse,
      .read = proc_read,
      .exec = proc_exec
    )
  );
}

private int ex_ed_sh_popen (ed_t *ed, buf_t *buf, char *com,
  int redir_stdout, int redir_stderr, PopenRead_cb read_cb) {

  int retval = NOTOK;
  proc_t *this = proc_new ();
  $my(read_stderr) =  redir_stderr;
  $my(read_stdout) = redir_stdout;
  $my(dup_stdin) = 0;
  $my(buf) = buf;
  ifnot (NULL is read_cb)
    $my(read) = read_cb;
  else
    if (redir_stdout)
      $my(read) = Buf.read.from_fp;

  proc_parse (this, com);
  term_t *term = Ed.get.term (ed);

  ifnot (redir_stdout)
    Term.reset (term);

  if (NOTOK is proc_open (this)) goto theend;
  proc_read (this);
  retval = proc_wait (this);

theend:
  ifnot (redir_stdout) {
    Term.set_mode (term, 'r');
    Input.get (term);
    Term.set (term);
  }

  proc_free (this);
  win_t *w = Ed.get.current_win (ed);
  int idx = Win.get.current_buf_idx (w);
  Win.set.current_buf (w, idx, DONOT_DRAW);
  Win.draw (w);
  return retval;
}

/* spell */

/* The algorithms for transforming the `word', except the case handling are based
 * on the checkmate_spell project at: https://github.com/syb0rg/checkmate
 *
 * Almost same code at: https://github.com/marcelotoledo/spelling_corrector
 *
 * Copyright  (C)  2007  Marcelo Toledo <marcelo@marcelotoledo.com>
 *
 * Version: 1.0
 * Keywords: spell corrector
 * Author: Marcelo Toledo <marcelo@marcelotoledo.com>
 * Maintainer: Marcelo Toledo <marcelo@marcelotoledo.com>
 * URL: http://marcelotoledo.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

 /* The original idea is from Peter Norvig at: http://norvig.com/ */

 /* The word database i used for a start, is from:
  * https://github.com/first20hours/google-10000-english
  * Data files are derived from the Google Web Trillion Word Corpus, as described
  * by Thorsten Brants and Alex Franz
  * http://googleresearch.blogspot.com/2006/08/all-our-n-gram-are-belong-to-you.html
  * and distributed by the Linguistic Data Consortium:
  * http://www.ldc.upenn.edu/Catalog/CatalogEntry.jsp?catalogId=LDC2006T13.
  * Subsets of this corpus distributed by Peter Novig:
  * http://norvig.com/ngrams/
  * Corpus editing and cleanup by Josh Kaufman.
  */

#define SPELL_MIN_WORD_LEN 4

#define SPELL_DONOT_CLEAR_DICTIONARY 0
#define SPELL_CLEAR_DICTIONARY 1

#define SPELL_DONOT_CLEAR_IGNORED 0
//#define SPELL_CLEAR_IGNORED       1

#define SPELL_WORD_ISNOT_CORRECT -1
#define SPELL_WORD_IS_CORRECT 0
#define SPELL_WORD_IS_IGNORED 1

#define SPELL_CHANGED_WORD 1
#define SPELL_OK     0
#define SPELL_ERROR -1

#define SPELL_NOTWORD Notword "012345678#:`$_"
#define SPELL_NOTWORD_LEN (Notword_len + 14)

private string_t *spell_get_dictionary (void) {
  return ((Prop (this) *) __This__->prop)->spell.dic_file;
}

private int spell_get_num_entries (void) {
  return ((Prop (this) *) __This__->prop)->spell.num_entries;
}

private spelldic_t *spell_get_current_dic (void) {
  return ((Prop (this) *) __This__->prop)->spell.current_dic;
}

private void spell_set_current_dic (spelldic_t *dic) {
  ((Prop (this) *) __This__->prop)->spell.current_dic = dic;
}

private void spell_clear (spell_t *spell, int clear_ignored) {
  if (NULL is spell) return;
  String.clear (spell->tmp);
  Vstring.clear (spell->words);
  Vstring.clear (spell->guesses);
  Vstring.clear (spell->messages);
  if (clear_ignored) Imap.clear (spell->ign_words);
}

private void spell_free (spell_t *spell, int clear_dic) {
  if (NULL is spell) return;
  String.free (spell->tmp);
  Vstring.free (spell->words);
  Vstring.free (spell->guesses);
  Vstring.free (spell->messages);

  Imap.free (spell->ign_words);

  if (SPELL_CLEAR_DICTIONARY is clear_dic) {
    Imap.free (spell->dic);
    spell_set_current_dic (NULL);
  } else
    spell_set_current_dic (spell->dic);

  free (spell);
}

private void spell_deletion (spell_t *spell) {
  for (size_t i = 0; i < spell->word_len; i++) {
    String.clear (spell->tmp);
    size_t ii = 0;
    for (; ii < i; ii++) String.append_byte (spell->tmp, spell->word[ii]);

    ssize_t len = spell->word_len - (i + 1);
    for (int i_ = 0; i_ < len; i_++)
      String.append_byte (spell->tmp, spell->word[++ii]);

    Vstring.add.sort_and_uniq (spell->words, spell->tmp->bytes);
  }
}

private void spell_transposition (spell_t *spell) {
  for (size_t i = 0; i < spell->word_len - 1; i++) {
    String.clear (spell->tmp);
    size_t ii = 0;
    for (; ii < i; ii++) String.append_byte (spell->tmp, spell->word[ii]);

    String.append_byte (spell->tmp, spell->word[i+1]);
    String.append_byte (spell->tmp, spell->word[i]);
    ii++;

    ssize_t len = spell->word_len - (i + 2);
    for (int i_ = 0; i_ <= len; i_++)
      String.append_byte (spell->tmp, spell->word[++ii]);

    Vstring.add.sort_and_uniq (spell->words, spell->tmp->bytes);
  }
}

private void spell_alteration(spell_t *spell) {
  for (size_t i = 0; i < spell->word_len; i++) {
    for (size_t j = 0; j < 26; j++) {
      String.clear (spell->tmp);
      size_t ii = 0;
      for (; ii < i; ii++) String.append_byte (spell->tmp, spell->word[ii]);

      String.append_byte (spell->tmp, 'a' + j);

      ssize_t len = spell->word_len - (i + 1);

      for (int i_ = 0; i_ < len; i_++)
        String.append_byte (spell->tmp, spell->word[++ii]);

      Vstring.add.sort_and_uniq (spell->words, spell->tmp->bytes);
    }
  }
}

private void spell_insertion (spell_t *spell) {
  for (size_t i = 0; i <= spell->word_len; i++) {
    for (size_t j = 0; j < 26; j++) {
      String.clear (spell->tmp);
      size_t ii = 0;
      for (; ii < i; ii++) String.append_byte (spell->tmp, spell->word[ii]);

      String.append_byte (spell->tmp, 'a' + j);
      for (size_t i_ = 0; i_ < spell->word_len - i; i_++)
        String.append_byte (spell->tmp, spell->word[i + i_]);

      Vstring.add.sort_and_uniq (spell->words, spell->tmp->bytes);
    }
  }
}

private int spell_case (spell_t *spell) {
  char buf[spell->word_len + 1];
  int retval = Ustring.change_case (buf, spell->word, spell->word_len, TO_LOWER);
  ifnot (retval) return SPELL_WORD_ISNOT_CORRECT;
  if (Imap.key_exists (spell->dic, buf)) return SPELL_WORD_IS_CORRECT;
  Vstring.add.sort_and_uniq (spell->words, buf);
  return SPELL_WORD_ISNOT_CORRECT;
}

private int spell_guess (spell_t *spell) {
  if (SPELL_WORD_IS_CORRECT is spell_case (spell))
    return SPELL_WORD_IS_CORRECT;

  spell_clear (spell, SPELL_DONOT_CLEAR_IGNORED);
  spell_deletion (spell);
  spell_transposition (spell);
  spell_alteration (spell);
  spell_insertion (spell);

  vstring_t *that = spell->words->head;
  while (that) {
    if (Imap.key_exists (spell->dic, that->data->bytes))
      Vstring.current.append_with (spell->guesses, that->data->bytes);
    that = that->next;
  }

  return SPELL_WORD_ISNOT_CORRECT;
}

private int spell_correct (spell_t *spell) {
  if (spell->word_len < spell->min_word_len or
      spell->word_len >= MAXLEN_WORD)
    return SPELL_WORD_IS_IGNORED;

  if (Imap.key_exists (spell->dic, spell->word)) return SPELL_WORD_IS_CORRECT;
  if (Imap.key_exists (spell->ign_words, spell->word)) return SPELL_WORD_IS_IGNORED;

  return spell_guess (spell);
}

private void spell_add_word_to_dictionary (spell_t *spell, char *word) {
  FILE *fp = fopen (spell->dic_file->bytes, "a+");
  fprintf (fp, "%s\n", word);
  fclose (fp);
}

private int spell_file_readlines_cb (Vstring_t *unused, char *line, size_t len,
                                                         int lnr, void *obj) {
  (void) unused; (void) lnr; (void) len;
  spell_t *spell = (spell_t *) obj;
  Imap.set_with_keylen (spell->dic, Cstring.trim.end (line, '\n'));
                    // this untill an inner getline()
  spell->num_dic_words++;
  return 0;
}

private int spell_read_dictionary (spell_t *spell) {
  Imap.clear (spell->dic);
  Vstring_t unused;
  File.readlines (spell->dic_file->bytes, &unused, spell_file_readlines_cb,
      (void *) spell);
  return spell->retval;
}

private int spell_init_dictionary (spell_t *spell, string_t *dic, int num_words, int force) {
  if (NULL is dic) return SPELL_ERROR;

  spelldic_t *current_dic = spell_get_current_dic ();

  if (current_dic isnot NULL and NO_FORCE is force) {
    spell->dic = current_dic;
    spell->dic_file = dic;
    return SPELL_OK;
  }

  if (-1 is access (dic->bytes, F_OK|R_OK)) {
    spell->retval = SPELL_ERROR;
    Vstring.append_with_fmt (spell->messages,
        "dictionary is not readable: |%s|\n" "errno: %d, error: %s",
        dic->bytes, errno, Error.string (E.get.current (THIS_E), errno));
    return spell->retval;
  }

  spell->dic_file = dic;
  spell->dic = Imap.new (num_words);
  spell_read_dictionary (spell);
  spell_set_current_dic (spell->dic);
  return SPELL_OK;
}

private spell_t *spell_new (void) {
  spell_t *spell = AllocType (spell);
  spell->tmp = String.new_with ("");
  spell->words = Vstring.new ();
  spell->ign_words = Imap.new (100);
  spell->guesses = Vstring.new ();
  spell->messages = Vstring.new ();
  spell->min_word_len = SPELL_MIN_WORD_LEN;
  return spell;
}

public spell_T __init_spell__ (void) {
  string_t *dic = String.new_with_fmt ("%s/spell/spell.txt",
      E.get.env (THIS_E, "data_dir")->bytes);

  return ClassInit (spell,
    .self = SelfInit (spell,
      .free = spell_free,
      .clear = spell_clear,
      .new = spell_new,
      .init_dictionary = spell_init_dictionary,
      .add_word_to_dictionary = spell_add_word_to_dictionary,
      .correct = spell_correct
    ),
   .current_dic = NULL,
   .dic_file = dic,
   .num_entries = 10000
  );
}

public void __deinit_spell__ (spell_T *this) {
  ifnot (NULL is this->current_dic) Imap.free (this->current_dic);
  String.free (this->dic_file);
}

private utf8 __spell_question__ (spell_t *spell, buf_t **thisp,
        Action_t **action, int fidx, int lidx, bufiter_t *iter) {
  ed_t *ed = E.get.current (THIS_E);
  char prefix[fidx + 1];
  char lpart[iter->line->num_bytes - lidx];

  Cstring.substr (prefix, fidx, iter->line->bytes, iter->line->num_bytes, 0);
  Cstring.substr (lpart, iter->line->num_bytes - lidx - 1, iter->line->bytes,
     iter->line->num_bytes, lidx + 1);

  string_t *quest = String.new (512);

  String.append_fmt (quest,
    "Spelling [%s] at line %d and %d index\n%s%s%s\n",
     spell->word, iter->idx + 1, fidx, prefix, spell->word, lpart);

   ifnot (spell->guesses->num_items)
     String.append (quest, "Cannot find matching words and there are no suggestions\n");
   else
     String.append (quest, "Suggestions: (enter number to accept one as correct)\n");

  int charslen = 5 + spell->guesses->num_items;
  utf8 chars[charslen];
  chars[0] = 'A'; chars[1] = 'a'; chars[2] = 'c'; chars[3] = 'i'; chars[4] = 'q';
  vstring_t *it = spell->guesses->head;
  for (int j = 1; j <= spell->guesses->num_items; j++) {
    String.append_fmt (quest, "%d: %s\n", j, it->data->bytes);
    chars[4+j] = '0' + j;
    it = it->next;
  }

  String.append (quest,
      "Choises:\n"
      "a[ccept] word as correct and add it to the dictionary\n"
      "A[ccept] word as correct just for this session\n"
      "c[ansel] operation and continue with the next\n"
      "i[nput]  correct word by getting input\n"
      "q[uit]   quit operation\n");

  utf8 c = Ed.question (ed, quest->bytes, chars, charslen);
  String.free (quest);

  it = spell->guesses->head;
  switch (c) {
    case 'c': return SPELL_OK;
    case 'a':
      Spell.add_word_to_dictionary (spell, spell->word);
      Imap.set_with_keylen (spell->dic, spell->word);
      return SPELL_OK;

    case 'q': return SPELL_ERROR;

    case 'A':
      Imap.set_with_keylen (spell->ign_words, spell->word);
      return SPELL_OK;

    case 'i': {
      string_t *inp = Buf.input_box (*thisp, Buf.get.current_video_row (*thisp) - 1,
      Buf.get.current_video_col (*thisp), 0, spell->word);
      ifnot (inp->num_bytes) {
        String.free (inp);
        return SPELL_OK;
      }

      Buf.action.set_with (*thisp, *action, REPLACE_LINE, iter->idx,
          iter->line->bytes, iter->line->num_bytes);
      String.replace_numbytes_at_with (iter->line, spell->word_len, fidx, inp->bytes);
      String.free (inp);
      Buf.set.modified (*thisp);
      return SPELL_CHANGED_WORD;
    }

    default: {
      Buf.action.set_with (*thisp, *action, REPLACE_LINE, iter->idx,
          iter->line->bytes, iter->line->num_bytes);
      it = spell->guesses->head;
      for (int k = '1'; k < c; k++) it = it->next;
      String.replace_numbytes_at_with (iter->line, spell->word_len, fidx,
          it->data->bytes);
      Buf.set.modified (*thisp);
      return SPELL_CHANGED_WORD;
    }
  }
  return SPELL_OK;
}

private int __spell_word__ (buf_t **thisp, int fidx, int lidx,
                                  bufiter_t *iter, char *word) {
  int retval = NOTOK;
  ed_t *ed = E.get.current (THIS_E);

  spell_t *spell = Spell.new ();

  if (SPELL_ERROR is Spell.init_dictionary (spell, spell_get_dictionary (),
      spell_get_num_entries (), NO_FORCE)) {
    Msg.send (ed, COLOR_RED, spell->messages->head->data->bytes);
    Spell.free (spell, SPELL_CLEAR_DICTIONARY);
    return NOTOK;
  }

  Action_t *action = Buf.action.new (*thisp);
  Buf.action.set_current (*thisp, action, REPLACE_LINE);

  int len = lidx - fidx + 1;

  char lword[len + 1];
  int i = 0;
  while (i < len and NULL isnot Cstring.byte_in_str (SPELL_NOTWORD, word[i])) {
    fidx++;
    len--;
    i++;
  }

  int j = 0;
  int orig_len = len;
  len = 0;
  while (i < orig_len and NULL is Cstring.byte_in_str (SPELL_NOTWORD, word[i])) {
    lword[j++] = word[i++];
    len++;
  }

  lword[j] = '\0';

  if (i isnot len) {
    i = len - 1;
    while (i >= 0 and NULL isnot Cstring.byte_in_str (SPELL_NOTWORD, word[i--])) {
      lidx--;
      len--;
    }
  }

  if (len < (int) spell->min_word_len) goto theend;

  spell->word_len = len;
  Cstring.cp (spell->word, MAXLEN_WORD, lword, len);

  retval = Spell.correct (spell);

  if (retval >= SPELL_WORD_IS_CORRECT) {
    retval = OK;
    goto theend;
  }

  retval = __spell_question__ (spell, thisp, &action, fidx, lidx, iter);

theend:
  if (retval is SPELL_CHANGED_WORD) {
    Buf.action.push (*thisp, action);
    Buf.draw (*thisp);
    retval = SPELL_OK;
  } else
    Buf.action.free (*thisp, action);

  Spell.free (spell, SPELL_DONOT_CLEAR_DICTIONARY);
  return retval;
}

private int __buf_spell__ (buf_t **thisp, rline_t *rl) {
  int range[2];
  int edit = Rline.arg.exists (rl, "edit");
  if (edit) {
    win_t *w = Buf.get.parent (*thisp);
    Win.edit_fname (w, thisp, spell_get_dictionary ()->bytes, 0, 0, 1, 0);
    return OK;
  }

  ed_t *ed = E.get.current(THIS_E);

  int retval = Rline.get.buf_range (rl, *thisp, range);
  if (NOTOK is retval) {
    range[0] = Buf.get.current_row_idx (*thisp);
    range[1] = range[0];
  }

  int count = range[1] - range[0] + 1;

  spell_t *spell = Spell.new ();
  if (SPELL_ERROR is Spell.init_dictionary (spell, spell_get_dictionary (),
      spell_get_num_entries (), NO_FORCE)) {
    Msg.send (ed, COLOR_RED, spell->messages->head->data->bytes);
    Spell.free (spell, SPELL_CLEAR_DICTIONARY);
    return NOTOK;
  }

  Action_t *action = Buf.action.new (*thisp);
  Buf.action.set_current (*thisp, action, REPLACE_LINE);

  int buf_changed = 0;

  char word[MAXLEN_WORD];

  bufiter_t *iter = Buf.iter.new (*thisp, range[0]);

  int i = 0;
  while (iter and i++ < count) {
    int fidx = 0; int lidx = -1;
    string_t *line = iter->line;
    char *tmp = NULL;
    for (;;) {
      int cur_idx = lidx + 1 + (tmp isnot NULL);
      tmp = Cstring.extract_word_at (line->bytes, line->num_bytes,
          word, MAXLEN_WORD, SPELL_NOTWORD, SPELL_NOTWORD_LEN, cur_idx, &fidx, &lidx);

      if (NULL is tmp) {
        if (lidx >= (int) line->num_bytes - 1)
          goto itnext;
        continue;
      }

      int len = lidx - fidx + 1;
      if (len < (int) spell->min_word_len or len >= MAXLEN_WORD)
        continue;

      spell->word_len = len;
      Cstring.cp (spell->word, MAXLEN_WORD, word, len);

      retval = Spell.correct (spell);

      if (retval >= SPELL_WORD_IS_CORRECT) continue;

      retval = __spell_question__ (spell, thisp, &action, fidx, lidx, iter);
      if (SPELL_ERROR is retval) goto theend;
      if (SPELL_CHANGED_WORD is retval) {
        retval = SPELL_OK;
        buf_changed = 1;
      }
    }
itnext:
    iter = Buf.iter.next (*thisp, iter);
  }

theend:
  if (buf_changed) {
    Buf.action.push (*thisp, action);
    Buf.draw (*thisp);
  } else
    Buf.action.free (*thisp, action);

  Buf.iter.free (*thisp, iter);
  Spell.free (spell, SPELL_DONOT_CLEAR_DICTIONARY);
  return retval;
}

/* Math Expr */

private int __ex_math_expr_interp__ (math_t *expr) {
  int err = 0;

  ed_t *ed = E.get.current (THIS_E);
  expr->ff_obj = (te_expr *) te_compile (expr->data->bytes, 0, 0, &err);

  ifnot (expr->ff_obj) {
    buf_t **thisp = (buf_t **) expr->i_obj;
    Math.strerror (expr, MATH_COMPILE_ERROR);
    String.append_fmt (expr->error_string, "\n%s\n%*s^\nError near here",
        expr->data->bytes, err-1, "");
    Ed.append.message_fmt (ed, expr->error_string->bytes);
    Ed.messages (ed, thisp, NOT_AT_EOF);
    expr->retval = MATH_NOTOK;
    return NOTOK;
  }

  expr->val.double_v = te_eval (expr->ff_obj);
  Ed.append.message_fmt (ed, "Result:\n%f\n", expr->val.double_v);
  char buf[256]; buf[0] = '\0';
  snprintf (buf, 256, "%f", expr->val.double_v);
  Ed.reg.set (ed, 'M', CHARWISE, buf, NORMAL_ORDER);
  Msg.send_fmt (ed, COLOR_NORMAL, "Result =  %f (stored to 'M' register)", expr->val.double_v);
  te_free (expr->ff_obj);

  return OK;
}

private int __ex_math_expr_evaluate__ (buf_t **thisp, char *bytes) {
  math_t *expr = Math.new ("Math", NULL_REF, NULL_REF, __ex_math_expr_interp__, NULL_REF);
  expr->i_obj = thisp;
  expr->data = String.new_with (bytes);
  int retval = Math.interp (expr);
  Math.free (&expr);
  return retval;
}

/* callbacks */

private int __ex_word_actions_cb__ (buf_t **thisp, int fidx, int lidx,
                      bufiter_t *it, char *word, utf8 c, char *action) {
  (void) fidx; (void) lidx; (void) action; (void) it; (void) word; (void) thisp;

  int retval = NO_CALLBACK_FUNCTION;
  (void) retval; // gcc complains here for no reason
  switch (c) {
    case 'S':
      retval = __spell_word__ (thisp, fidx, lidx, it, word);
      break;

    default:
      break;
   }

  return retval;
}

private void __ex_add_word_actions__ (ed_t *this) {
  utf8 chr[] = {'S'};
  Ed.set.word_actions (this, chr, 1, "Spell word", __ex_word_actions_cb__);
}

private int __ex_lw_mode_cb__ (buf_t **thisp, int fidx, int lidx, Vstring_t *vstr, utf8 c, char *action) {
  (void) vstr; (void) action;

  int retval = NO_CALLBACK_FUNCTION;

  switch (c) {
    case 'S': {
      rline_t *rl = Ed.rline.new (E.get.current (THIS_E));
      string_t *str = String.new_with_fmt ("spell --range=%d,%d",
         fidx + 1, lidx + 1);
      Rline.set.visibility (rl, NO);
      Rline.set.line (rl, str->bytes, str->num_bytes);
      String.free (str);
      Rline.parse (rl, *thisp);
      if (SPELL_OK is (retval = __buf_spell__ (thisp, rl)))
        Rline.history.push (rl);
      else
        Rline.free (rl);
    }
      break;

    case 'm': {
      string_t *expression = Vstring.join (vstr, "\n");
      retval = __ex_math_expr_evaluate__ (thisp, expression->bytes);
      String.free (expression);
    }
      break;

#ifdef HAS_TCC
    case 'C': {
      return __tcc_compile__ (thisp, Vstring.join (vstr, "\n"));
      }
      break;
#endif

    default:
      retval = NO_CALLBACK_FUNCTION;
  }

  return retval;
}

private void __ex_add_lw_mode_actions__ (ed_t *this) {
  int num_actions = 2;
#if HAS_TCC
  num_actions++;
#endif

  utf8 chars[] = {
#if HAS_TCC
  'C',
#endif
  'S', 'm'};

  char actions[] =
#if HAS_TCC
    "Compile lines with tcc\n"
#endif
     "Spell line[s]\n"
     "math expression";

  Ed.set.lw_mode_actions (this, chars, num_actions, actions, __ex_lw_mode_cb__);
}

private int __ex_cw_mode_cb__ (buf_t **thisp, int fidx, int lidx, string_t *str, utf8 c, char *action) {
  int retval = NO_CALLBACK_FUNCTION;
  switch (c) {
    case 'S': {
        bufiter_t *iter = Buf.iter.new (*thisp, -1);
        retval = __ex_word_actions_cb__ (thisp, fidx, lidx, iter, str->bytes, c, action);
        Buf.iter.free (*thisp, iter);
      }
      break;

    case 'm':
      debug_append ("stt |%s|\n",                str->bytes);
      retval = __ex_math_expr_evaluate__ (thisp, str->bytes);
      debug_append ("ret %d\n", retval);
      break;

    default:
      break;
  }

  return retval;
}

private void __ex_add_cw_mode_actions__ (ed_t *this) {
  int num_actions = 2;
  utf8 chars[] = {'S', 'm'};
  char actions[] =
    "Spell selected\n"
    "math expression";

  Ed.set.cw_mode_actions (this, chars, num_actions, actions, __ex_cw_mode_cb__);
}

private int __ex_file_mode_cb__ (buf_t **thisp, utf8 c, char *action) {
  (void) action;
  int retval = NO_CALLBACK_FUNCTION;

  switch (c) {
    case 'S': {
      int flags = Buf.get.flags (*thisp);
      if (0 is (flags & BUF_IS_SPECIAL) and
          0 is Cstring.eq (Buf.get.basename (*thisp), UNAMED)) {
        rline_t *rl = Ed.rline.new (E.get.current (THIS_E));
        string_t *str = String.new_with ("spell --range=%");
        Rline.set.visibility (rl, NO);
        Rline.set.line (rl, str->bytes, str->num_bytes);
        String.free (str);
        Rline.parse (rl, *thisp);
        if (SPELL_OK is (retval = __buf_spell__ (thisp, rl)))
          Rline.history.push (rl);
        else
          Rline.free (rl);
       }
    }
      break;

#if HAS_TCC
    case 'C': {
      int flags = Buf.get.flags (*thisp);
      if (0 is (flags & BUF_IS_SPECIAL) and
          0 is Cstring.eq (Buf.get.basename (*thisp), UNAMED)) {
        Vstring_t *lines = File.readlines (Buf.get.fname (*thisp), NULL, NULL, NULL);
        ifnot (NULL is lines) {
          retval = __tcc_compile__ (thisp, Vstring.join (lines, "\n"));
          Vstring.free (lines);
        } else
          retval = NOTOK;
      }
    }
    break;
#endif

    default:
      retval = NO_CALLBACK_FUNCTION;
  }

  return retval;
}

private void __ex_add_file_mode_actions__ (ed_t *this) {
  int num_actions = 1;
#if HAS_TCC
  num_actions++;
#endif

  utf8 chars[] = {
#if HAS_TCC
  'C',
#endif
  'S'};

  char actions[] =
#if HAS_TCC
 "Compile file with tcc compiler\n"
#endif
 "Spell check this file";

  Ed.set.file_mode_actions (this, chars, num_actions, actions, __ex_file_mode_cb__);
}

private string_t *__ex_buf_serial_info__ (bufinfo_t *info) {
  string_t *sinfo = String.new_with ("BUF_INFO_STRUCTURE\n");
  String.append_fmt (sinfo,
    "fname       : \"%s\"\n"
    "cwd         : \"%s\"\n"
    "parent name : \"%s\"\n"
    "at frame    : %d\n"
    "num bytes   : %zd\n"
    "num lines   : %zd\n"
    "cur idx     : %d\n"
    "is writable : %d\n",
    info->fname, info->cwd, info->parents_name, info->at_frame,
    info->num_bytes, info->num_lines, info->cur_idx, info->is_writable);

  return sinfo;
}

private string_t *__ex_win_serial_info__ (wininfo_t *info) {
  string_t *sinfo = String.new_with ("WIN_INFO_STRUCTURE\n");
  String.append_fmt (sinfo,
    "name         : \"%s\"\n"
    "ed name      : \"%s\"\n"
    "num buf      : %zd\n"
    "num frames   : %d\n"
    "cur buf idx  : %d\n"
    "cur buf name : \"%s\"\n"
    "buf names    :\n",
    info->name, info->parents_name, info->num_items, info->num_frames,
    info->cur_idx, info->cur_buf);

  for (size_t i = 0; i < info->num_items; i++)
    String.append_fmt (sinfo, "%12d : \"%s\"\n", i + 1, info->buf_names[i]);

  return sinfo;
}

private string_t *__ex_ed_serial_info__ (edinfo_t *info) {
  string_t *sinfo = String.new_with ("ED_INFO_STRUCTURE\n");
  String.append_fmt (sinfo,
    "name         : \"%s\"\n"
    "normal win   : %zd\n"
    "special win  : %d\n"
    "cur win idx  : %d\n"
    "cur win name : \"%s\"\n"
    "win names    :\n",
    info->name, info->num_items, info->num_special_win, info->cur_idx,
    info->cur_win);

  for (size_t i = 0; i < info->num_items; i++)
    String.append_fmt (sinfo, "%12d : \"%s\"\n", i + 1, info->win_names[i]);
  return sinfo;
}

private int __ex_com_info__ (buf_t **thisp, rline_t *rl) {
  (void) thisp; (void) rl;
  ed_t *ced = E.get.current (THIS_E);

  int
    buf = Rline.arg.exists (rl, "buf"),
    win = Rline.arg.exists (rl, "win"),
    ed  = Rline.arg.exists (rl, "ed");

  ifnot (buf + win + ed) buf = 1;

  Ed.append.toscratch (ced, CLEAR, "");

  if (buf) {
    bufinfo_t *binfo = Buf.get.info.as_type (*thisp);
    string_t *sbinfo = __ex_buf_serial_info__ (binfo);
    Ed.append.toscratch (ced, DONOT_CLEAR, sbinfo->bytes);
    String.free (sbinfo);
    Buf.free.info (*thisp, &binfo);
  }

  if (win) {
    win_t *cw = Ed.get.current_win (ced);
    wininfo_t *winfo = Win.get.info.as_type (cw);
    string_t *swinfo = __ex_win_serial_info__ (winfo);
    Ed.append.toscratch (ced, DONOT_CLEAR, swinfo->bytes);
    String.free (swinfo);
    Win.free_info (cw, &winfo);
  }

  if (ed) {
    edinfo_t *einfo = Ed.get.info.as_type (ced);
    string_t *seinfo = __ex_ed_serial_info__ (einfo);
    Ed.append.toscratch (ced, DONOT_CLEAR, seinfo->bytes);
    String.free (seinfo);
    Ed.free_info (ced, &einfo);
  }

  Ed.scratch (ced, thisp, NOT_AT_EOF);

  return OK;
}

private int __ex_rline_cb__ (buf_t **thisp, rline_t *rl, utf8 c) {
  (void) thisp; (void) c;
  int retval = RLINE_NO_COMMAND;
  string_t *com = Rline.get.command (rl);

  if (Cstring.eq (com->bytes, "@info")) {
    retval = __ex_com_info__ (thisp, rl);
    goto theend;
  } else if (Cstring.eq (com->bytes, "spell")) {
    retval = __buf_spell__ (thisp, rl);
  }

theend:
  String.free (com);
  return retval;
}

private void __ex_add_rline_commands__ (ed_t *this) {
  int num_commands = 1;
  char *commands[] = {"@info", NULL};
  int num_args[] = {0, 0};
  int flags[] = {0, 0};
  Ed.append.rline_commands (this, commands, num_commands, num_args, flags);
  Ed.append.command_arg (this, "@info", "--buf", 5);
  Ed.append.command_arg (this, "@info", "--win", 5);
  Ed.append.command_arg (this, "@info", "--ed", 5);

  Ed.append.rline_command (this, "spell", 1, RL_ARG_RANGE);
  Ed.append.command_arg (this, "spell", "--edit", 6);

  Ed.set.rline_cb (this, __ex_rline_cb__);
}

private void __init_ext__ (Type (ed) *this) {
  __ex_add_rline_commands__ (this);
  __ex_add_cw_mode_actions__ (this);
  __ex_add_lw_mode_actions__ (this);
  __ex_add_word_actions__ (this);
  __ex_add_file_mode_actions__ (this);

  Ed.sh.popen = ex_ed_sh_popen;

#if HAS_USER_EXTENSIONS
  __init_usr__ (this);
#endif

#if HAS_LOCAL_EXTENSIONS
  __init_local__ (this);
#endif

  Ed.history.read (this);
  Ed.set.at_exit_cb (this, Ed.history.write);
}

private void __deinit_ext__ (void) {
#if HAS_USER_EXTENSIONS
  __deinit_usr__ ();
#endif

#if HAS_LOCAL_EXTENSIONS
  __deinit_local__ ();
#endif

  __deinit_spell__ ( &(((Prop (this) *) __This__->prop))->spell);
}

#if HAS_PROGRAMMING_LANGUAGE

private Lstate *__init_lstate__ (const char *src, int argc, const char **argv) {
  Lstate *this = initVM (0, src, argc, argv);
  __init_led__ (this);
  return this;
}

private void __deinit_lstate__ (Lstate **thisp) {
  if (NULL is *thisp) return;
  freeVM (*thisp);
  *thisp = NULL;
}

private Class (l) *__init_l__ (int num_states) {
  Class (l) *this =  Alloc (sizeof (Class (l)));

  Self (l) l = SelfInit (l,
    .init = __init_lstate__,
    .deinit = __deinit_lstate__,
    .compile = interpret,
    .newString = copyString,
    .defineFun = defineNative,
    .defineProp = defineNativeProperty,
    .vmsize = vm_sizeof,
    .table = SubSelfInit (l, table,
      .get = SubSelfInit (ltable, get,
        .globals = vm_get_globals,
        .module = vm_get_module_table,
        .value = vm_table_get_value
      )
    ),
    .module = SubSelfInit (lmodule, get,
      .get = vm_module_get
   )
  );

  __L__ = this;
  __L__->self = l;
  __L__->num_states = num_states;
  __L__->cur_state = 0;
  __L__->states = Alloc (L.vmsize () * __L__->num_states);
  L_CUR_STATE = L.init ("__global__", 0, NULL);
  return __L__;
}

public void __deinit_l__ (Class (l) **thisp) {
  if (NULL is *thisp) return;
  Class (l) *this = *thisp;

  for (int i = 0; i < this->num_states; i++)
    __deinit_lstate__ (&this->states[i]);

  free (this->states);
  free (this);
  *thisp = NULL;
}
#endif /* HAS_PROGRAMMING_LANGUAGE */

private void __init_self__ (Class (this) *this) {
  this->self = AllocSelf (this);

  ((Self (this) *) this->self)->parse_command = this_parse_command;
  ((Self (this) *) this->self)->argparse = __init_argparse__ ().self;
  ((Self (this) *) this->self)->proc = __init_proc__ ().self;
  ((Self (this) *) this->self)->math = __init_math__().self;
}

/* Surely not perfect handler. Never have the chance to test since
 * my constant environ is fullscreen terminals. 
 */
private void sigwinch_handler (int sig) {
  (void) sig;
  ed_t *ed = E.get.head (THIS_E);
  int cur_idx = E.get.current_idx (THIS_E);

  while (ed) {
    Ed.set.screen_size (ed);
    win_t *w = Ed.get.win_head (ed);
    while (w) {
      Ed.readjust.win_size (ed, w);
      w = Ed.get.win_next (ed, w);
    }

    ed = E.set.next (THIS_E);
  }

  E.set.current (THIS_E, cur_idx);
}

/* one idea is to hold with a question, to give some time to the
 * user to free resources and retry; in that case the signature
 * should change to an int as return value plus the Alloc* macros */
mutable public void __alloc_error_handler__ (int err, size_t size,
                           char *file, const char *func, int line) {
  fprintf (stderr, "MEMORY_ALLOCATION_ERROR\n");
  fprintf (stderr, "File: %s\nFunction: %s\nLine: %d\n", file, func, line);
  fprintf (stderr, "Size: %zd\n", size);

  if (err is INTEGEROVERFLOW_ERROR)
    fprintf (stderr, "Error: Integer Overflow Error\n");
  else
    fprintf (stderr, "Error: Not Enouch Memory\n");

  ifnot (NULL is __This__) __deinit_this__ (&__This__);

  exit (1);
}
private int __initialize__ (void) {
  /* I do not know the way to read from stdin and at the same time to
   * initialize and use the terminal state, when we are the end of the pipe */

  /* 
    if (0 is isatty (fileno (stdout)) or 0 is isatty (fileno (stdin))) {
      tostderr ("Not a controlled terminal\n");
      return NOTOK;
    }

    Update: Tue 23 Jun 2020, first handle of this, as the first thing to check at
    the main() function.
  */

  setlocale (LC_ALL, "");
  AllocErrorHandler = __alloc_error_handler__;
  return OK;
}

public Class (this) *__init_this__ (void) {
  E_T *__e__;
  if (NOTOK is __initialize__ () or
      NULL is (__e__ = __init_ed__ (MYNAME)))
    return NULL;

  Class (this) *this = AllocClass (this);

  __This__ = this;
  __This__->__E__ = __e__;
  __This__->__E__->__This__ =  __This__;

  __init_self__ (__This__);

  ((Prop (this) *) this->prop)->spell =  __init_spell__ ();
  ((Self (this) *) this->self)->spell = ((Prop (this) *) __This__->prop)->spell.self;

#if HAS_TCC
  ((Prop (this) *) this->prop)->tcc =  __init_tcc__ ();
  ((Self (this) *) this->self)->tcc = ((Prop (this) *) __This__->prop)->tcc.self;
#endif

#if HAS_PROGRAMMING_LANGUAGE
  __init_l__ (1);
#endif

  return __This__;
}

public void __deinit_this__ (Class (this) **thisp) {
  if (*thisp is NULL) return;

  Class (this) *this = *thisp;

  __deinit_ed__ (&this->__E__);

#if HAS_PROGRAMMING_LANGUAGE
  __deinit_l__ (&__L__);
#endif

  free (this->prop);
  free (this->self);
  free (this);


  *thisp = NULL;
}

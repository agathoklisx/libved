#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <locale.h>
#include <errno.h>

#include "libved.h"
#include "libved+.h"
#include "lib/lib+.h"

public Class (This) *__THIS__ = NULL;
public Self (This)  *__SELF__ = NULL;

#if HAS_PROGRAMMING_LANGUAGE
public Class (L)    *__L__    = NULL;
#endif

#include "handlers/sigwinch_handler.c"
#include "handlers/alloc_err_handler.c"

#if HAS_SHELL_COMMANDS
  #include "ext/if_has_shell.c"
#endif

#if HAS_USER_EXTENSIONS
  #include "usr/usr.c"
#endif

#if HAS_LOCAL_EXTENSIONS
  #include "local/local.c"
#endif

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
  ed_t *ced = E(get.current);

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
  Ed.set.rline_cb (this, __ex_rline_cb__);
}

private void __init_ext__ (Type (ed) *this) {
  __ex_add_rline_commands__ (this);

#if HAS_SHELL_COMMANDS
  Ed.sh.popen = ext_ed_sh_popen;
#endif

#if HAS_USER_EXTENSIONS
  __init_usr__ (this);
#endif

#if HAS_LOCAL_EXTENSIONS
  __init_local__ (this);
#endif

#ifdef HAS_HISTORY
  Ed.history.read (this);
  Ed.set.at_exit_cb (this, Ed.history.write);
#endif
}

private void __deinit_ext__ (void) {
#if HAS_USER_EXTENSIONS
  __deinit_usr__ ();
#endif

#if HAS_LOCAL_EXTENSIONS
  __deinit_local__ ();
#endif
}

/* Argparse:
  https://github.com/cofyc/argparse

  forked commit: fafc503d23d077bda40c29e8a20ea74707452721
  (HEAD -> master, origin/master, origin/HEAD)
*/

/**
 * Copyright (C) 2012-2015 Yecheng Fu <cofyc.jackson at gmail dot com>
 * All rights reserved.
 *
 * Use of this source code is governed by a MIT-style license that can be found
 * in the LICENSE file.
 */

#define OPT_UNSET 1
#define OPT_LONG  (1 << 1)

private const char *prefix_skip (const char *str, const char *prefix) {
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

    rest = prefix_skip (self->argv[0] + 2, options->long_name);
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

      rest = prefix_skip (self->argv[0] + 2 + 3, options->long_name);
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

private string_t *this_parse_command (Class (This) *this, char *bytes) {
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

private void e_set_at_exit_cb (Class (This) *this, EAtExit_cb cb) {
  __E.set.at_exit_cb (this->__E__, cb);
}

private void e_set_at_init_cb (Class (This) *this, EdAtInit_cb cb) {
  __E.set.at_init_cb (this->__E__, cb);
}

private int e_get_num (Class (This) *this) {
  return __E.get.num (this->__E__);
}

private int e_get_prev_idx (Class (This) *this) {
  return __E.get.prev_idx (this->__E__);
}

private int e_get_current_idx (Class (This) *this) {
  return __E.get.current_idx (this->__E__);
}

private ed_t *e_get_current (Class (This) *this) {
  return __E.get.current (this->__E__);
}

private ed_t *e_get_head (Class (This) *this) {
  return __E.get.head (this->__E__);
}

private ed_t *e_get_next (Class (This) *this, ed_t *ed) {
  return __E.get.next (this->__E__, ed);
}

private string_t *e_get_env (Class (This) *this, char *name) {
  return __E.get.env (this->__E__, name);
}

private Class(I) *e_get_i_class (Class (This) *this) {
  return __E.get.iclass (this->__E__);
}

private int e_get_state (Class (This) *this) {
  return __E.get.state (this->__E__);
}

private ed_t * e_set_current (Class (This) *this, int idx) {
  return __E.set.current (this->__E__, idx);
}

private ed_t *e_set_next (Class (This) *this) {
   return __E.set.next (this->__E__);
}

private ed_t *e_init (Class (This) *this, EdAtInit_cb cb) {
  return __E.init (this->__E__, cb);
}

private ed_t* e_new (Class (This) *this, ED_INIT_OPTS opts) {
  return __E.new (this->__E__, opts);
}

private int e_main (Class (This) *this, buf_t *buf) {
  return __E.main (this->__E__, buf);
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

public Class (L) *__init_l__ (int num_states) {
  Class (L) *this =  Alloc (sizeof (Class (L)));
  this->self = __SELF__->l;
  __L__ = this;

  this->num_states = num_states;
  this->states = Alloc (L.vmsize () * this->num_states);
  this->cur_state = 0;
  return this;
}

public void __deinit_l__ (Class (L) **thisp) {
  if (NULL is *thisp) return;
  Class (L) *this = *thisp;

  for (int i = 0; i < this->num_states; i++)
    __deinit_lstate__ (&this->states[i]);

  free (this->states);
  free (this);
  *thisp = NULL;
}

#endif /* HAS_PROGRAMMING_LANGUAGE */

private void __init_self__ (Class (This) *this) {
  this->self = AllocSelf (This);

  ((Self (This) *) this->self)->parse_command = this_parse_command;

  SubSelf (This, argparse) argparse = SubSelfInit (This, argparse,
    .init = argparse_init,
    .exec =  argparse_parse
  );
  ((Self (This) *) this->self)->argparse = argparse;

  SubSelf (This, e) e = SubSelfInit (This, e,
    .init = e_init,
    .new = e_new,
    .main = e_main,
    .set = SubSelfInit (Thise, set,
      .at_exit_cb = e_set_at_exit_cb,
      .at_init_cb = e_set_at_init_cb,
      .current = e_set_current,
      .next = e_set_next
    ),
    .get = SubSelfInit (Thise, get,
      .num = e_get_num,
      .prev_idx = e_get_prev_idx,
      .state = e_get_state,
      .current = e_get_current,
      .current_idx = e_get_current_idx,
      .head = e_get_head,
      .next = e_get_next,
      .env = e_get_env,
      .iclass = e_get_i_class
    )
  );

  ((Self (This) *) this->self)->e = e;

#if HAS_PROGRAMMING_LANGUAGE
  SubSelf (This, l) l = SubSelfInit (This, l,
    .init = __init_lstate__,
    .deinit = __deinit_lstate__,
    .compile = interpret,
    .newString = copyString,
    .defineFun = defineNative,
    .defineProp = defineNativeProperty,
    .vmsize = vm_sizeof,
    .table = SubSelfInit (Thisl, table,
      .get = SubSelfInit (Thisltable, get,
        .globals = vm_get_globals,
        .module = vm_get_module_table,
        .value = vm_table_get_value
      )
    ),
    .module = SubSelfInit (Thislmodule, get,
      .get = vm_module_get
   )
  );

  ((Self (This) *) this->self)->l = l;
#endif

#if HAS_SHELL_COMMANDS
  Self (proc) p = __init_proc__ ().self;
  ((Self (This) *) this->self)->proc = p;
#endif

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

public Class (This) *__init_this__ (void) {
  E_T *__e__;
  if (NOTOK is __initialize__ () or
      NULL is (__e__ = __init_ed__ (MYNAME)))
    return NULL;

  Class (This) *this = AllocClass (This);

  __THIS__ = this;
  __THIS__->__E__ = __e__;
  __THIS__->__E__->__THIS__ =  __THIS__;

  __init_self__ (__THIS__);
  __SELF__ = __THIS__->self;

#if HAS_PROGRAMMING_LANGUAGE
  __init_l__ (1);
  ((Prop (This) *) $myprop)->__L__ = __L__;
  L_CUR_STATE = L.init ("__global__", 0, NULL);
#endif

  return __THIS__;
}

public void __deinit_this__ (Class (This) **thisp) {
  if (*thisp is NULL) return;

  Class (This) *this = *thisp;

  __deinit_ed__ (&this->__E__);

  free (__SELF__);

#if HAS_PROGRAMMING_LANGUAGE
  __deinit_l__ (&__L__);
#endif

  free (this->prop);
  free (this);

  *thisp = NULL;
}

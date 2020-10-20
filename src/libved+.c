#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#ifndef __APPLE__
#include <pty.h>
#else
#include <util.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <locale.h>
#include <grp.h>
#include <pwd.h>
#include <time.h>
#include <limits.h>
#include <math.h>
#include <errno.h>

#include "libved.h"
#include "libved+.h"
#include "lib/lib+.h"

public Class (this) *__This__ = NULL;

#ifdef HAS_USER_EXTENSIONS
  #include "usr/usr.c"
#endif

#ifdef HAS_LOCAL_EXTENSIONS
  #include "local/local.c"
#endif

#ifdef HAS_PROGRAMMING_LANGUAGE
  #include "lib/lai/led.c"
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
          if (self->flags & ARGPARSE_DONOT_EXIT_ON_UNKNOWN) {
            self->out[self->cpidx++] = self->argv[0];
            continue;
          }

          goto unknown;
      }

      while (self->optvalue) {
        switch (argparse_short_opt (self, self->options)) {
          case -1:
            return -1;

          case -2:
            if (self->flags & ARGPARSE_DONOT_EXIT_ON_UNKNOWN) {
             self->out[self->cpidx++] = self->argv[0];
             continue;
            }

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
        if (self->flags & ARGPARSE_DONOT_EXIT_ON_UNKNOWN) {
          self->out[self->cpidx++] = self->argv[0];
          continue;
        }

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

public int argparse_help_cb (argparse_t *self, const argparse_option_t *option) {
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

private string_t *sys_get_env (Class (sys) *this, char *name) {
  if (Cstring.eq (name, "sysname"))
    String.replace_with_len ($my(shared_str), $my(env)->sysname->bytes,
      $my(env)->sysname->num_bytes);
  else if (Cstring.eq (name, "battery_dir"))
    String.replace_with_len ($my(shared_str), $my(env)->battery_dir->bytes,
      $my(env)->battery_dir->num_bytes);
  else if (Cstring.eq (name, "man_exec"))
    String.replace_with_len ($my(shared_str), $my(env)->man_exec->bytes,
      $my(env)->man_exec->num_bytes);
  else
    String.clear ($my(shared_str));

  return $my(shared_str);
}

private string_t *__readlink__ (char *obj) {
  string_t *link = String.new (PATH_MAX);

  int loops = 0;
readthelink:
  link->num_bytes = readlink (obj, link->bytes, link->mem_size);
  if (NOTOK is (ssize_t) link->num_bytes)
    return link;

  if (link->num_bytes is link->mem_size and loops++ is 0) {
    String.reallocate (link, (link->mem_size / 2));
    goto readthelink;
  }

  link->bytes[link->num_bytes] = '\0'; // readlink() does not append the nullbyte
  return link;
}

private int sys_stat (buf_t **thisp, char *obj) {
  ed_t *ed = E.get.current (THIS_E);

  struct stat st;
  if (NOTOK is lstat (obj, &st)) {
    Msg.error (ed, "failed to lstat() file, %s", Error.string (ed, errno));
    return NOTOK;
  }

  Ed.append.toscratch (ed, CLEAR, "==- stat output -==");

  int islink = S_ISLNK (st.st_mode);
  string_t *link = NULL;

  if (islink) {
    link = __readlink__ (obj);
    if (NOTOK is (ssize_t) link->num_bytes) {
      Msg.error (ed, "readlink(): %s", Error.string (ed, errno));
      Ed.append.toscratch_fmt (ed, DONOT_CLEAR, "  File: %s", obj);
    } else
      Ed.append.toscratch_fmt (ed, DONOT_CLEAR, "  File: %s -> %s", obj, link->bytes);
  } else
    Ed.append.toscratch_fmt (ed, DONOT_CLEAR, "  File: %s", obj);

theoutput:
  Ed.append.toscratch_fmt (ed, DONOT_CLEAR, "  Size: %ld,  Blocks: %ld,  I/O Block: %ld",
      st.st_size, st.st_blocks, st.st_blksize);
  Ed.append.toscratch_fmt (ed, DONOT_CLEAR, "Device: %ld,  Inode: %ld,  Links: %d",
      st.st_dev, st.st_ino, st.st_nlink);

  char mode_string[16];
  Vsys.stat.mode_to_string (mode_string, st.st_mode);
  char mode_oct[8]; snprintf (mode_oct, 8, "%o", st.st_mode);
  struct passwd *pswd = getpwuid (st.st_uid);
  struct group *grp = getgrgid (st.st_gid);
  Ed.append.toscratch_fmt (ed, DONOT_CLEAR,
  	"Access: (%s/%s), Uid: (%ld / %s), Gid: (%d / %s)\n",
     mode_oct+2, mode_string, st.st_uid,
    (NULL is pswd ? "NONE" : pswd->pw_name), st.st_gid,
    (NULL is grp  ? "NONE" : grp->gr_name));
  time_t atm = (long int) st.st_atime;
  Ed.append.toscratch_fmt (ed, DONOT_CLEAR, "       Last Access: %s", Cstring.trim.end (ctime (&atm), '\n'));
  time_t mtm = (long int) st.st_mtime;
  Ed.append.toscratch_fmt (ed, DONOT_CLEAR, " Last Modification: %s", Cstring.trim.end (ctime (&mtm), '\n'));
  time_t ctm = (long int) st.st_ctime;
  Ed.append.toscratch_fmt (ed, DONOT_CLEAR, "Last Status Change: %s\n", Cstring.trim.end (ctime (&ctm), '\n'));

  if (islink and NULL isnot link) {
    islink = 0;
    ifnot (Path.is_absolute (link->bytes)) {
      char *dname = Path.dirname (obj);
      String.prepend_fmt (link, "%s/", dname);
      free (dname);
    }
    obj = link->bytes;
    Ed.append.toscratch (ed, DONOT_CLEAR, "==- Link info -==");
    Ed.append.toscratch_fmt (ed, DONOT_CLEAR, "  File: %s", obj);
    if (OK is stat (link->bytes, &st)) goto theoutput;
  }

  String.free (link);
  Ed.scratch (ed, thisp, NOT_AT_EOF);
  return OK;
}

/* this prints to the message line (through an over simplistic way and only
 * on Linux systems) the battery status and capacity */
private int sys_battery_info (char *buf, int should_print) {
  ed_t *ed = E.get.current (THIS_E);

  int retval = NOTOK;

  ifnot (Cstring.eq_n ("Linux", Sys.get.env (__SYS__, "sysname")->bytes, 5)) {
    Msg.error (ed, "battery function implemented for Linux");
    return NOTOK;
  }

  string_t *battery_dir = Sys.get.env (__SYS__, "battery_dir");
  ifnot (battery_dir->num_bytes) {
    Msg.error (ed, "battery directory hasn't been defined");
    return NOTOK;
  }

  dirlist_t *dlist = Dir.list (battery_dir->bytes, 0);
  char *cap = NULL;
  char *status = NULL;

  if (NULL is dlist) return NOTOK;

  vstring_t *it = dlist->list->head;
  while (it) {
    ifnot (Cstring.cmp_n ("BAT", it->data->bytes, 3)) goto foundbat;
    it = it->next;
    }

  goto theend;

/* funny goto's (i like them (the goto's i mean)) */
foundbat:;
  /* some maybe needless verbosity */
  char dir[64];
  snprintf (dir, 64, "%s/%s/", battery_dir->bytes, it->data->bytes);
  size_t len = bytelen (dir);
  Cstring.cp (dir + len, 64 - len, "capacity", 8);
  FILE *fp = fopen (dir, "r");
  if (NULL is fp) goto theend;

  size_t clen = 0;
  ssize_t nread = getline (&cap, &clen, fp);
  if (-1 is nread) goto theend;

  cap[nread - 1] = '\0';
  fclose (fp);

  dir[len] = '\0';
  Cstring.cp (dir + len, 64 - len, "status", 6);
  fp = fopen (dir, "r");
  if (NULL is fp) goto theend;

/* here clen it should be zero'ed because on the second call the application
 * segfaults (compiled with gcc and clang and at the first call with tcc);
 * this is when the code tries to free both char *variables arguments to getline();
 * this is as (clen) possibly interpeted as the length of the buffer
 */
  clen = 0;

  nread = getline (&status, &clen, fp);
  if (-1 is nread) goto theend;

  status[nread - 1] = '\0';
  fclose (fp);

  retval = OK;

  if (should_print)
    Msg.send_fmt (ed, COLOR_YELLOW, "[Battery is %s, remaining %s%%]",
        status, cap);

  ifnot (NULL is buf) snprintf (buf, 64, "[Battery is %s, remaining %s%%]",
      status, cap);

theend:
  ifnot (NULL is cap) free (cap);
  ifnot (NULL is status) free (status);
  dlist->free (dlist);
  return retval;
}

private int sys_man (buf_t **bufp, char *word, int section) {
  string_t *man_exec = Sys.get.env (__SYS__, "man_exec");
  if (NULL is man_exec) return NOTOK;

  if (NULL is word) return NOTOK;

  ed_t *ed = E.get.current (THIS_E);

  int retval = NOTOK;
  string_t *com;

  buf_t *this = Ed.get.scratch_buf (ed);
  Buf.clear (this);

  if (File.exists (word)) {
    if (Path.is_absolute (word))
      com = String.new_with_fmt ("%s %s", man_exec->bytes, word);
    else {
      char *cwdir = Dir.current ();
      com = String.new_with_fmt ("%s %s/%s", man_exec->bytes, cwdir, word);
      free (cwdir);
    }

    retval = Ed.sh.popen (ed, this, com->bytes, 1, 1, NULL);
    goto theend;
  }

  int sections[9]; for (int i = 0; i < 9; i++) sections[i] = 0;
  int def_sect = 2;

  section = ((section <= 0 or section > 8) ? def_sect : section);
  com = String.new_with_fmt ("%s -s %d %s", man_exec->bytes,
     section, word);

  int total_sections = 0;
  for (int i = 1; i < 9; i++) {
    sections[section] = 1;
    total_sections++;
    retval = Ed.sh.popen (ed, this, com->bytes, 1, 1, NULL);
    ifnot (retval) break;

    while (sections[section] and total_sections < 8) {
      if (section is 8) section = 1;
      else section++;
    }

    String.replace_with_fmt (com, "%s -s %d %s", man_exec->bytes,
        section, word);
  }

theend:
  String.free (com);

  Ed.scratch (ed, bufp, 0);
  Buf.substitute (this, ".\b", "", GLOBAL, NO_INTERACTIVE, 0,
      Buf.get.num_lines (this) - 1);
  Buf.normal.bof (this, DRAW);
  return (retval > 0 ? NOTOK : OK);
}

private int sys_mkdir (char *dir, mode_t mode, int verbose, int parents) {
  ed_t *ed = E.get.current (THIS_E);

  int retval = OK;
  char *dname = NULL;

  ifnot (parents)
    goto makethisdir;

  if (Cstring.eq (dir, "."))
    return OK;

  dname = Path.dirname (dir);

  if (Cstring.eq (dname, "/"))
    goto theend;

  if ((retval = Sys.mkdir (dname, mode, verbose, parents)) isnot OK)
    goto theend;

makethisdir:
  if (mkdir (dir, mode) isnot OK) {
    if (errno isnot EEXIST) {
      Msg.error (ed, "failed to create directory %s, %s", dir, Error.string (ed, errno));
      retval = -1;
      goto theend;
    }

    if (Dir.is_directory (dir))
      Msg.send_fmt (ed, COLOR_WARNING, "directory `%s' exists", dir);
    else
      Msg.error (ed, "Not a directory `%s'", dir);

    goto theend;
  }

  if (verbose) {
    struct stat st;
    if (NOTOK is stat (dir, &st)) {
      Msg.error (ed, "failed to stat directory `%s', %s", dir, Error.string (ed, errno));
      retval = NOTOK;
      goto theend;
    }

    char mode_string[16];
    Vsys.stat.mode_to_string (mode_string, st.st_mode);
    char mode_oct[8]; snprintf (mode_oct, 8, "%o", st.st_mode);

    Msg.send_fmt (ed, COLOR_YELLOW, "created directory `%s', with mode: %s (%s)",
       dir, mode_oct + 1, mode_string);
  }

theend:
  ifnot (NULL is dname)
    free (dname);

  return retval;
}

private Class (sys) *__init_sys__ (void) {
  Class (sys) *this = AllocClass (sys);
  $my(env) = AllocType (sysenv);

  struct utsname u;
  if (-1 is uname (&u))
    $my(env)->sysname = String.new_with ("unknown");
  else
    $my(env)->sysname = String.new_with (u.sysname);

  if (Cstring.eq_n ($my(env)->sysname->bytes, "Linux", 5))
    $my(env)->battery_dir = String.new_with ("/sys/class/power_supply");
  else
    $my(env)->battery_dir = String.new (0);

  $my(env)->man_exec = Vsys.which ("man", E.get.env (THIS_E, "path")->bytes);
  if (NULL is $my(env)->man_exec)
    $my(env)->man_exec = String.new (0);

  $my(shared_str) = String.new (8);

  this->self = SelfInit (sys,
    .get = SubSelfInit (sys, get,
      .env = sys_get_env
    ),
    .mkdir = sys_mkdir,
    .man = sys_man,
    .battery_info = sys_battery_info,
    .stat = sys_stat
  );

   return this;
}

private void __deinit_sys__ (Class (sys) **thisp) {
  if (*thisp is NULL) return;

  Class (sys) *this = *thisp;

  String.free ($my(env)->sysname);
  String.free ($my(env)->battery_dir);
  String.free ($my(env)->man_exec);

  free ($my(env));

  String.free ($my(shared_str));
  free (this->prop);
  free (this);
  *thisp = NULL;
}

#ifdef HAS_TCC
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

/*
 * TINYEXPR - Tiny recursive descent parser and evaluation engine in C
 *
 * Copyright (c) 2015-2018 Lewis Van Winkle
 *
 * http://CodePlea.com
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgement in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/* COMPILE TIME OPTIONS */

/* Exponentiation associativity:
For a^b^c = (a^b)^c and -a^b = (-a)^b do nothing.
For a^b^c = a^(b^c) and -a^b = -(a^b) uncomment the next line.*/
/* #define TE_POW_FROM_RIGHT */

/* Logarithms
For log = base 10 log do nothing
For log = natural log uncomment the next line. */
/* #define TE_NAT_LOG */

#ifndef NAN
#define NAN (0.0/0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif

typedef struct te_expr {
    int type;
    union {double value; const double *bound; const void *function;};
    void *parameters[1];
} te_expr;

enum {
    TE_VARIABLE = 0,

    TE_FUNCTION0 = 8, TE_FUNCTION1, TE_FUNCTION2, TE_FUNCTION3,
    TE_FUNCTION4, TE_FUNCTION5, TE_FUNCTION6, TE_FUNCTION7,

    TE_CLOSURE0 = 16, TE_CLOSURE1, TE_CLOSURE2, TE_CLOSURE3,
    TE_CLOSURE4, TE_CLOSURE5, TE_CLOSURE6, TE_CLOSURE7,

    TE_FLAG_PURE = 32
};

typedef struct te_variable {
    const char *name;
    const void *address;
    int type;
    void *context;
} te_variable;

typedef double (*te_fun2)(double, double);

enum {
    TOK_NULL = TE_CLOSURE7+1, TOK_ERROR, TOK_END, TOK_SEP,
    TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_VARIABLE, TOK_INFIX
};

enum {TE_CONSTANT = 1};

typedef struct te_state {
    const char *start;
    const char *next;
    int type;
    union {double value; const double *bound; const void *function;};
    void *context;

    const te_variable *lookup;
    int lookup_len;
} te_state;

#define TYPE_MASK(TYPE) ((TYPE)&0x0000001F)

#define IS_PURE(TYPE) (((TYPE) & TE_FLAG_PURE) != 0)
//#define TE_IS_FUNCTION(TYPE) (((TYPE) & TE_FUNCTION0) != 0)
#define TE_IS_CLOSURE(TYPE) (((TYPE) & TE_CLOSURE0) != 0)
#define ARITY(TYPE) ( ((TYPE) & (TE_FUNCTION0 | TE_CLOSURE0)) ? ((TYPE) & 0x00000007) : 0 )
#define NEW_EXPR(type, ...) new_expr((type), (const te_expr*[]){__VA_ARGS__})

private te_expr *new_expr(const int type, const te_expr *parameters[]) {
    const int arity = ARITY(type);
    const int psize = sizeof(void*) * arity;
    const int size = (sizeof(te_expr) - sizeof(void*)) + psize + (TE_IS_CLOSURE(type) ? sizeof(void*) : 0);
    te_expr *ret = malloc(size);
    memset(ret, 0, size);
    if (arity && parameters) {
        memcpy(ret->parameters, parameters, psize);
    }
    ret->type = type;
    ret->bound = 0;
    return ret;
}

private void te_free(te_expr *);
private void te_free_parameters(te_expr *n) {
    if (!n) return;
    switch (TYPE_MASK(n->type)) {
        case TE_FUNCTION7: case TE_CLOSURE7: te_free(n->parameters[6]);     /* Falls through. */
        case TE_FUNCTION6: case TE_CLOSURE6: te_free(n->parameters[5]);     /* Falls through. */
        case TE_FUNCTION5: case TE_CLOSURE5: te_free(n->parameters[4]);     /* Falls through. */
        case TE_FUNCTION4: case TE_CLOSURE4: te_free(n->parameters[3]);     /* Falls through. */
        case TE_FUNCTION3: case TE_CLOSURE3: te_free(n->parameters[2]);     /* Falls through. */
        case TE_FUNCTION2: case TE_CLOSURE2: te_free(n->parameters[1]);     /* Falls through. */
        case TE_FUNCTION1: case TE_CLOSURE1: te_free(n->parameters[0]);
    }
}

private void te_free(te_expr *n) {
    if (!n) return;
    te_free_parameters(n);
    free(n);
}

private double te_add(double a, double b) {return a + b;}
private double te_sub(double a, double b) {return a - b;}
private double te_mul(double a, double b) {return a * b;}
private double te_divide(double a, double b) {return a / b;}
private double negate(double a) {return -a;}
private double comma(double a, double b) {(void)a; return b;}
private double pi(void) {return 3.14159265358979323846;}
private double e(void) {return 2.71828182845904523536;}
private double fac(double a) {/* simplest version of fac */
    if (a < 0.0)
        return NAN;
    if (a > UINT_MAX)
        return INFINITY;
    unsigned int ua = (unsigned int)(a);
    unsigned long int result = 1, i;
    for (i = 1; i <= ua; i++) {
        if (i > ULONG_MAX / result)
            return INFINITY;
        result *= i;
    }
    return (double)result;
}

private double ncr(double n, double r) {
    if (n < 0.0 || r < 0.0 || n < r) return NAN;
    if (n > UINT_MAX || r > UINT_MAX) return INFINITY;
    unsigned long int un = (unsigned int)(n), ur = (unsigned int)(r), i;
    unsigned long int result = 1;
    if (ur > un / 2) ur = un - ur;
    for (i = 1; i <= ur; i++) {
        if (result > ULONG_MAX / (un - ur + i))
            return INFINITY;
        result *= un - ur + i;
        result /= i;
    }
    return result;
}
private double npr(double n, double r) {return ncr(n, r) * fac(r);}

private const te_variable functions[] = {
    /* must be in alphabetical order */
    {"abs", fabs,     TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"acos", acos,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"asin", asin,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan", atan,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan2", atan2,  TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"ceil", ceil,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cos", cos,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cosh", cosh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"e", e,          TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"exp", exp,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"fac", fac,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"floor", floor,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ln", log,       TE_FUNCTION1 | TE_FLAG_PURE, 0},
#ifdef TE_NAT_LOG
    {"log", log,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
#else
    {"log", log10,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
#endif
    {"log10", log10,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ncr", ncr,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"npr", npr,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"pi", pi,        TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"pow", pow,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"sin", sin,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sinh", sinh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sqrt", sqrt,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tan", tan,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tanh", tanh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {0, 0, 0, 0}
};

private const te_variable *find_builtin(const char *name, int len) {
    int imin = 0;
    int imax = sizeof(functions) / sizeof(te_variable) - 2;

    /*Binary search.*/
    while (imax >= imin) {
        const int i = (imin + ((imax-imin)/2));
        int c = strncmp(name, functions[i].name, len);
        if (!c) c = '\0' - functions[i].name[len];
        if (c == 0) {
            return functions + i;
        } else if (c > 0) {
            imin = i + 1;
        } else {
            imax = i - 1;
        }
    }

    return 0;
}

private const te_variable *find_lookup(const te_state *s, const char *name, int len) {
    int iters;
    const te_variable *var;
    if (!s->lookup) return 0;

    for (var = s->lookup, iters = s->lookup_len; iters; ++var, --iters) {
        if (strncmp(name, var->name, len) == 0 && var->name[len] == '\0') {
            return var;
        }
    }
    return 0;
}

private void next_token(te_state *s) {
    s->type = TOK_NULL;

    do {

        if (!*s->next){
            s->type = TOK_END;
            return;
        }

        /* Try reading a number. */
        if ((s->next[0] >= '0' && s->next[0] <= '9') || s->next[0] == '.') {
            s->value = strtod(s->next, (char**)&s->next);
            s->type = TOK_NUMBER;
        } else {
            /* Look for a variable or builtin function call. */
            if (s->next[0] >= 'a' && s->next[0] <= 'z') {
                const char *start;
                start = s->next;
                while ((s->next[0] >= 'a' && s->next[0] <= 'z') || (s->next[0] >= '0' && s->next[0] <= '9') || (s->next[0] == '_')) s->next++;

                const te_variable *var = find_lookup(s, start, s->next - start);
                if (!var) var = find_builtin(start, s->next - start);

                if (!var) {
                    s->type = TOK_ERROR;
                } else {
                    switch(TYPE_MASK(var->type))
                    {
                        case TE_VARIABLE:
                            s->type = TOK_VARIABLE;
                            s->bound = var->address;
                            break;

                        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:         /* Falls through. */
                        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:         /* Falls through. */
                            s->context = var->context;                                                  /* Falls through. */

                        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:     /* Falls through. */
                        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:     /* Falls through. */
                            s->type = var->type;
                            s->function = var->address;
                            break;
                    }
                }

            } else {
                /* Look for an operator or special character. */
                switch (s->next++[0]) {
                    case '+': s->type = TOK_INFIX; s->function = te_add; break;
                    case '-': s->type = TOK_INFIX; s->function = te_sub; break;
                    case '*': s->type = TOK_INFIX; s->function = te_mul; break;
                    case '/': s->type = TOK_INFIX; s->function = te_divide; break;
                    case '^': s->type = TOK_INFIX; s->function = pow; break;
                    case '%': s->type = TOK_INFIX; s->function = fmod; break;
                    case '(': s->type = TOK_OPEN; break;
                    case ')': s->type = TOK_CLOSE; break;
                    case ',': s->type = TOK_SEP; break;
                    case ' ': case '\t': case '\n': case '\r': break;
                    default: s->type = TOK_ERROR; break;
                }
            }
        }
    } while (s->type == TOK_NULL);
}


private te_expr *te_list(te_state *s);
private te_expr *expr(te_state *s);
private te_expr *power(te_state *s);

private te_expr *base(te_state *s) {
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr *ret;
    int arity;

    switch (TYPE_MASK(s->type)) {
        case TOK_NUMBER:
            ret = new_expr(TE_CONSTANT, 0);
            ret->value = s->value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(TE_VARIABLE, 0);
            ret->bound = s->bound;
            next_token(s);
            break;

        case TE_FUNCTION0:
        case TE_CLOSURE0:
            ret = new_expr(s->type, 0);
            ret->function = s->function;
            if (TE_IS_CLOSURE(s->type)) ret->parameters[0] = s->context;
            next_token(s);
            if (s->type == TOK_OPEN) {
                next_token(s);
                if (s->type != TOK_CLOSE) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }
            break;

        case TE_FUNCTION1:
        case TE_CLOSURE1:
            ret = new_expr(s->type, 0);
            ret->function = s->function;
            if (TE_IS_CLOSURE(s->type)) ret->parameters[1] = s->context;
            next_token(s);
            ret->parameters[0] = power(s);
            break;

        case TE_FUNCTION2: case TE_FUNCTION3: case TE_FUNCTION4:
        case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        case TE_CLOSURE2: case TE_CLOSURE3: case TE_CLOSURE4:
        case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            arity = ARITY(s->type);

            ret = new_expr(s->type, 0);
            ret->function = s->function;
            if (TE_IS_CLOSURE(s->type)) ret->parameters[arity] = s->context;
            next_token(s);

            if (s->type != TOK_OPEN) {
                s->type = TOK_ERROR;
            } else {
                int i;
                for(i = 0; i < arity; i++) {
                    next_token(s);
                    ret->parameters[i] = expr(s);
                    if(s->type != TOK_SEP) {
                        break;
                    }
                }
                if(s->type != TOK_CLOSE || i != arity - 1) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }

            break;

        case TOK_OPEN:
            next_token(s);
            ret = te_list(s);
            if (s->type != TOK_CLOSE) {
                s->type = TOK_ERROR;
            } else {
                next_token(s);
            }
            break;

        default:
            ret = new_expr(0, 0);
            s->type = TOK_ERROR;
            ret->value = NAN;
            break;
    }

    return ret;
}


private te_expr *power(te_state *s) {
    /* <power>     =    {("-" | "+")} <base> */
    int sign = 1;
    while (s->type == TOK_INFIX && (s->function == te_add || s->function == te_sub)) {
        if (s->function == te_sub) sign = -sign;
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) {
        ret = base(s);
    } else {
        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, base(s));
        ret->function = negate;
    }

    return ret;
}

#ifdef TE_POW_FROM_RIGHT
private te_expr *factor(te_state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);

    int neg = 0;
    te_expr *insertion = 0;

    if (ret->type == (TE_FUNCTION1 | TE_FLAG_PURE) && ret->function == negate) {
        te_expr *se = ret->parameters[0];
        free(ret);
        ret = se;
        neg = 1;
    }

    while (s->type == TOK_INFIX && (s->function == pow)) {
        te_fun2 t = s->function;
        next_token(s);

        if (insertion) {
            /* Make exponentiation go right-to-left. */
            te_expr *insert = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, insertion->parameters[1], power(s));
            insert->function = t;
            insertion->parameters[1] = insert;
            insertion = insert;
        } else {
            ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, power(s));
            ret->function = t;
            insertion = ret;
        }
    }

    if (neg) {
        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, ret);
        ret->function = negate;
    }

    return ret;
}
#else
private te_expr *factor(te_state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);

    while (s->type == TOK_INFIX && (s->function == pow)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, power(s));
        ret->function = t;
    }

    return ret;
}
#endif

private te_expr *term(te_state *s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);

    while (s->type == TOK_INFIX && (s->function == te_mul || s->function == te_divide || s->function == fmod)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, factor(s));
        ret->function = t;
    }

    return ret;
}


private te_expr *expr(te_state *s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);

    while (s->type == TOK_INFIX && (s->function == te_add || s->function == te_sub)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, term(s));
        ret->function = t;
    }

    return ret;
}

private te_expr *te_list(te_state *s) {
    /* <list>      =    <expr> {"," <expr>} */
    te_expr *ret = expr(s);

    while (s->type == TOK_SEP) {
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, expr(s));
        ret->function = comma;
    }

    return ret;
}


#define TE_FUN(...) ((double(*)(__VA_ARGS__))n->function)
#define M(e) te_eval(n->parameters[e])

private double te_eval(const te_expr *n) {
    if (!n) return NAN;

    switch(TYPE_MASK(n->type)) {
        case TE_CONSTANT: return n->value;
        case TE_VARIABLE: return *n->bound;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void)();
                case 1: return TE_FUN(double)(M(0));
                case 2: return TE_FUN(double, double)(M(0), M(1));
                case 3: return TE_FUN(double, double, double)(M(0), M(1), M(2));
                case 4: return TE_FUN(double, double, double, double)(M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(double, double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void*)(n->parameters[0]);
                case 1: return TE_FUN(void*, double)(n->parameters[1], M(0));
                case 2: return TE_FUN(void*, double, double)(n->parameters[2], M(0), M(1));
                case 3: return TE_FUN(void*, double, double, double)(n->parameters[3], M(0), M(1), M(2));
                case 4: return TE_FUN(void*, double, double, double, double)(n->parameters[4], M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(void*, double, double, double, double, double)(n->parameters[5], M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(void*, double, double, double, double, double, double)(n->parameters[6], M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(void*, double, double, double, double, double, double, double)(n->parameters[7], M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        default: return NAN;
    }

}

#undef TE_FUN
#undef M

private void optimize(te_expr *n) {
    /* Evaluates as much as possible. */
    if (n->type == TE_CONSTANT) return;
    if (n->type == TE_VARIABLE) return;

    /* Only optimize out functions flagged as pure. */
    if (IS_PURE(n->type)) {
        const int arity = ARITY(n->type);
        int known = 1;
        int i;
        for (i = 0; i < arity; ++i) {
            optimize(n->parameters[i]);
            if (((te_expr*)(n->parameters[i]))->type != TE_CONSTANT) {
                known = 0;
            }
        }
        if (known) {
            const double value = te_eval(n);
            te_free_parameters(n);
            n->type = TE_CONSTANT;
            n->value = value;
        }
    }
}


private te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) {
    te_state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;

    next_token(&s);
    te_expr *expr = te_list(&s);

    if (s.type != TOK_END) {
        te_free(expr);
        if (error) {
            *error = (s.next - s.start);
            if (*error == 0) *error = 1;
        }
        return 0;
    } else {
        optimize(expr);
        if (error) *error = 0;
        return expr;
    }
}

private double te_interp(const char *expression, int *error) {
    te_expr *n = te_compile(expression, 0, 0, error);
    double ret;
    if (n) {
        ret = te_eval(n);
        te_free(n);
    } else {
        ret = NAN;
    }
    return ret;
}

private void pn (const te_expr *n, int depth) {
    int i, arity;
    printf("%*s", depth, "");

    switch(TYPE_MASK(n->type)) {
    case TE_CONSTANT: printf("%f\n", n->value); break;
    case TE_VARIABLE: printf("bound %p\n", n->bound); break;

    case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
    case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
    case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
    case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
         arity = ARITY(n->type);
         printf("f%d", arity);
         for(i = 0; i < arity; i++) {
             printf(" %p", n->parameters[i]);
         }
         printf("\n");
         for(i = 0; i < arity; i++) {
             pn(n->parameters[i], depth + 1);
         }
         break;
    }
}

private void te_print(const te_expr *n) {
    pn(n, 0);
}

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
  $my(is_bg) = 0;
  return this;
}

private int proc_wait (proc_t *this) {
  if (-1 is $my(pid)) return NOTOK;
  $my(status) = 0;
  waitpid ($my(pid), &$my(status), 0);
 // waitpid ($my(pid), &$my(status), WNOHANG|WUNTRACED);
  if (WIFEXITED ($my(status)))
    $my(retval) = WEXITSTATUS ($my(status));
  else
    $my(retval) = -1;

  return $my(retval);
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
      dup2 ($my(stdout_fds)[PIPE_WRITE_END], fileno (stdout));
      close ($my(stdout_fds)[PIPE_READ_END]);
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

private void proc_set_stdin (proc_t *this, char *buf, size_t size) {
  if (NULL is buf) return;
  $my(dup_stdin) = 1;
  $my(stdin_buf) = Alloc (size + 1);
  $my(stdin_buf_size) = size;
  Cstring.cp ($my(stdin_buf), size + 1, buf, size);
}

private int proc_exec (proc_t *this, char *com) {
  int retval = 0;

  if ($my(reset_term))
    Term.reset ($my(term));

  proc_parse (this, com);

  if (NOTOK is proc_open (this)) goto theend;

  proc_read (this);

  ifnot ($my(is_bg))
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
      .exec = proc_exec,
      .set_stdin = proc_set_stdin
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
    if (redir_stdout or redir_stderr)
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
        Action_t **Action, int fidx, int lidx, bufiter_t *iter) {
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

      Buf.Action.set_with (*thisp, *Action, REPLACE_LINE, iter->idx,
          iter->line->bytes, iter->line->num_bytes);
      String.replace_numbytes_at_with (iter->line, spell->word_len, fidx, inp->bytes);
      String.free (inp);
      Buf.set.modified (*thisp);
      return SPELL_CHANGED_WORD;
    }

    default: {
      Buf.Action.set_with (*thisp, *Action, REPLACE_LINE, iter->idx,
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

  Action_t *Action = Buf.Action.new (*thisp);
  Buf.Action.set_with_current (*thisp, Action, REPLACE_LINE);

  int len = lidx - fidx + 1;

  char lword[len + 1];
  int i = 0;
  while (i < len and NULL isnot Cstring.byte.in_str (SPELL_NOTWORD, word[i])) {
    fidx++;
    len--;
    i++;
  }

  int j = 0;
  int orig_len = len;
  len = 0;
  while (i < orig_len and NULL is Cstring.byte.in_str (SPELL_NOTWORD, word[i])) {
    lword[j++] = word[i++];
    len++;
  }

  lword[j] = '\0';

  if (i isnot len) {
    i = len - 1;
    while (i >= 0 and NULL isnot Cstring.byte.in_str (SPELL_NOTWORD, word[i--])) {
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

  retval = __spell_question__ (spell, thisp, &Action, fidx, lidx, iter);

theend:
  if (retval is SPELL_CHANGED_WORD) {
    Buf.undo.push (*thisp, Action);
    Buf.draw (*thisp);
    retval = SPELL_OK;
  } else
    Buf.Action.free (*thisp, Action);

  Spell.free (spell, SPELL_DONOT_CLEAR_DICTIONARY);
  return retval;
}

private int __buf_spell__ (buf_t **thisp, rline_t *rl) {
  int range[2];
  int edit = Rline.arg.exists (rl, "edit");
  if (edit) {
    win_t *w = Buf.get.my_parent (*thisp);
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

  Action_t *Action = Buf.Action.new (*thisp);
  Buf.Action.set_with_current (*thisp, Action, REPLACE_LINE);

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

      retval = __spell_question__ (spell, thisp, &Action, fidx, lidx, iter);
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
    Buf.undo.push (*thisp, Action);
    Buf.draw (*thisp);
  } else
    Buf.Action.free (*thisp, Action);

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

    case 'm':
      retval = Sys.man (thisp, word, -1);
      break;

    default:
      break;
   }

  return retval;
}

private void __ex_add_word_actions__ (ed_t *this) {
  ifnot (getuid ()) return;
  int num_actions = 2;
  utf8 chars[] = {'S', 'm'};
  char actions[] =
    "Spell word\n"
    "man page";

  Ed.set.word_actions (this, chars, num_actions, actions, __ex_word_actions_cb__);
}

private int __ex_lw_mode_cb__ (buf_t **thisp, int fidx, int lidx, Vstring_t *vstr, utf8 c, char *action) {
  (void) vstr; (void) action;

  int retval = NO_CALLBACK_FUNCTION;

  if (Cstring.eq_n (action, "Math", 4)) c = 'm';

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
  ifnot (getuid ()) return;

  int num_actions = 2;
#ifdef HAS_TCC
  num_actions++;
#endif

  utf8 chars[] = {
#ifdef HAS_TCC
  'C',
#endif
  'S', 'm'};

  char actions[] =
#ifdef HAS_TCC
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
      retval = __ex_math_expr_evaluate__ (thisp, str->bytes);
      break;

    default:
      break;
  }

  return retval;
}

private void __ex_add_cw_mode_actions__ (ed_t *this) {
  ifnot (getuid ()) return;

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
          0 is Cstring.eq (Buf.get.basename (*thisp), UNNAMED)) {
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

#ifdef HAS_TCC
    case 'C': {
      int flags = Buf.get.flags (*thisp);
      if (0 is (flags & BUF_IS_SPECIAL) and
          0 is Cstring.eq (Buf.get.basename (*thisp), UNNAMED)) {
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
  ifnot (getuid ()) return;

  int num_actions = 1;

#ifdef HAS_TCC
  num_actions++;
#endif

  utf8 chars[] = {
#ifdef HAS_TCC
    'C',
#endif
    'S'};

  char actions[] =
#ifdef HAS_TCC
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
    ifnot (getuid ()) goto theend;
    retval = __buf_spell__ (thisp, rl);

  } else if (Cstring.eq (com->bytes, "`mkdir")) {
    Vstring_t *dirs = Rline.get.arg_fnames (rl, 1);
    if (NULL is dirs) goto theend;

    int is_verbose = Rline.arg.exists (rl, "verbose");
    int parents = Rline.arg.exists (rl, "parents");
    string_t *mode_s = Rline.get.anytype_arg (rl, "mode");

    mode_t def_mode = S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH; // 0755
    mode_t mode = (NULL is mode_s ? def_mode : (uint) strtol (mode_s->bytes, NULL, 8));

    retval = Sys.mkdir (dirs->tail->data->bytes, mode, is_verbose, parents);
    Vstring.free (dirs);

  } else if (Cstring.eq (com->bytes, "`man")) {
    Vstring_t *names = Rline.get.arg_fnames (rl, 1);
    if (NULL is names) goto theend;

    string_t *section = Rline.get.anytype_arg (rl, "section");
    int sect_id = (NULL is section ? 0 : atoi (section->bytes));

    retval = Sys.man (thisp, names->head->data->bytes, sect_id);
    Vstring.free (names);
  } else if (Cstring.eq (com->bytes, "`battery")) {
    retval = Sys.battery_info (NULL, 1);
  } else if (Cstring.eq (com->bytes, "`stat")) {
    Vstring_t *fnames = Rline.get.arg_fnames (rl, 1);
    if (NULL is fnames) goto theend;
    retval = Sys.stat (thisp, fnames->head->data->bytes);
    Vstring.free (fnames);
  }

theend:
  String.free (com);
  return retval;
}

private void __ex_add_rline_commands__ (ed_t *this) {
  uid_t uid = getuid ();

  int num_commands = 5;
  char *commands[] = {"@info", "`mkdir", "`man", "`battery", "`stat", NULL};
  int num_args[] = {0, 2, 1, 0, 1, 0};
  int flags[] = {0, RL_ARG_FILENAME|RL_ARG_VERBOSE, RL_ARG_FILENAME, 0,
      RL_ARG_FILENAME, 0};

  Ed.append.rline_commands (this, commands, num_commands, num_args, flags);
  Ed.append.command_arg (this, "@info", "--buf", 5);
  Ed.append.command_arg (this, "@info", "--win", 5);
  Ed.append.command_arg (this, "@info", "--ed",  4);

  Ed.append.command_arg (this, "`mkdir", "--mode=", 7);
  Ed.append.command_arg (this, "`mkdir", "--parents", 9);

  Ed.append.command_arg (this, "`man", "--section=", 10);

  if (uid) {
    Ed.append.rline_command (this, "spell", 1, RL_ARG_RANGE);
    Ed.append.command_arg (this, "spell",  "--edit", 6);
  }

  Ed.set.rline_cb (this, __ex_rline_cb__);
}

char __ex_balanced_pairs[] = "()[]{}";
char *__ex_NULL_ARRAY[] = {NULL};

char *make_filenames[] = {"Makefile", NULL};
char *make_extensions[] = {".Makefile", NULL};
char *make_keywords[] = {
  "ifeq I", "ifneq I", "endif I", "else I", "ifdef I", NULL};

char *sh_extensions[] = {".sh", ".bash", NULL};
char *sh_shebangs[] = {"#!/bin/sh", "#!/bin/bash", NULL};
char  sh_operators[] = "+:-%*><=|&()[]{}!$/`?";
char *sh_keywords[] = {
  "if I", "else I", "elif I", "then I", "fi I", "while I", "for I", "break I",
  "done I", "do I", "case I", "esac I", "in I", "EOF I", NULL};
char sh_singleline_comment[] = "#";

char *zig_extensions[] = {".zig", NULL};
char zig_operators[] = "+:-*^><=|&~.()[]{}/";
char zig_singleline_comment[] = "//";
char *zig_keywords[] = {
  "const V", "@import V", "pub I", "fn I", "void T", "try I",
  "else I", "if I", "while I", "true V", "false V", "Self V", "@This V",
  "return I", "u8 T", "struct T", "enum T", "var V", "comptime T",
  "switch I", "continue I", "for I", "type T", "void T", "defer I",
  "orelse I", "errdefer I", "undefined T", "threadlocal T", NULL};

char *lua_extensions[] = {".lua", NULL};
char *lua_shebangs[] = {"#!/bin/env lua", "#!/usr/bin/lua", NULL};
char  lua_operators[] = "+:-*^><=|&~.()[]{}!@/";
char *lua_keywords[] = {
  "do I", "if I", "while I", "else I", "elseif I", "nil I",
  "local I",  "self V", "require V", "return V", "and V",
  "then I", "end I", "function V", "or I", "in V",
  "repeat I", "for I",  "goto I", "not I", "break I",
  "setmetatable F", "getmetatable F", "until I",
  "true I", "false I", NULL
};

char lua_singleline_comment[] = "--";
char lua_multiline_comment_start[] = "--[[";
char lua_multiline_comment_end[] = "]]";

char *lai_extensions[] = {".lai", ".du", ".yala", NULL};
char *lai_shebangs[] = {"#!/bin/env lai", "#!/usr/bin/lai", "#!/usr/bin/yala", "#!/usr/bin/dictu", NULL};
char  lai_operators[] = "+:-*^><=|&~.()[]{}!@/";
char *lai_keywords[] = {
  "beg I", "end I", "if I", "while I", "else I", "for I", "do I", "orelse I",
  "is I", "isnot I", "nil E", "not I", "var V", "const V", "return V", "and I",
  "or I", "self F", "this V", "then I", "def F",  "continue I", "break I", "init I", "class T",
  "trait T", "true V", "false E", "import T", "as T", "hasAttribute F", "getAttribute F",
  "setAttribute F", "super V", "type T", "set F", "assert E", "with F", "forever I",
  "use T", "elseif I", "static T",
   NULL};

char lai_singleline_comment[] = "//";
char lai_multiline_comment_start[] = "/*";
char lai_multiline_comment_end[] = "*/";

char *md_extensions[] = {".md", NULL};

char *diff_extensions[] = {".diff", ".patch", NULL};

private char *__ex_diff_syn_parser (buf_t *this, char *line, int len, int idx, row_t *row) {
  (void) idx; (void) row;

  ifnot (len) return line;

  int color = HL_NORMAL;

  if (Cstring.eq_n (line, "--- ", 4)) {
    color = HL_IDENTIFIER;
    goto theend;
  }

  if (Cstring.eq_n (line, "+++ ", 4)) {
    color = HL_NUMBER;
    goto theend;
  }

  if (line[0] is  '-') {
    color = HL_VISUAL;
    goto theend;
  }

  if (line[0] is  '+') {
    color = HL_STRING_DELIM;
    goto theend;
  }

  if (Cstring.eq_n (line, "diff ", 5) or Cstring.eq_n (line, "index ", 6)
      or Cstring.eq_n (line, "@@ ", 3)) {
    color = HL_COMMENT;
    goto theend;
  }

theend:;
  string_t *shared = Buf.get.shared_str (this);
  String.replace_with_fmt (shared, "%s%s%s", TERM_MAKE_COLOR(color), line, TERM_COLOR_RESET);
  Cstring.cp (line, MAXLEN_LINE, shared->bytes, shared->num_bytes);
  return line;
}

private ftype_t *__ex_diff_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this, Ed.syn.get_ftype_idx (E.get.current (THIS_E), "diff"),
    QUAL(FTYPE, .tabwidth = 0, .tab_indents = 0));
  return ft;
}

private char *__ex_syn_parser (buf_t *this, char *line, int len, int idx, row_t *row) {
  return Buf.syn.parser (this, line, len, idx, row);
}

private string_t *__ex_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E.get.current (THIS_E), "autoindent_default");
  return autoindent_fun (this, row);
}

private string_t *__ex_c_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E.get.current (THIS_E), "autoindent_c");
  return autoindent_fun (this, row);
}

private string_t *__i_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E.get.current (THIS_E), "autoindent_c");
  return autoindent_fun (this, row);
}

private ftype_t *__ex_lai_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this, Ed.syn.get_ftype_idx (E.get.current (THIS_E), "lai"),
    QUAL(FTYPE, .autoindent = __ex_c_ftype_autoindent, .tabwidth = 2, .tab_indents = 1));
  return ft;
}

private ftype_t *__ex_lua_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this, Ed.syn.get_ftype_idx (E.get.current (THIS_E), "lua"),
    QUAL(FTYPE, .autoindent = __ex_c_ftype_autoindent, .tabwidth = 2, .tab_indents = 1));
  return ft;
}

private ftype_t *__ex_make_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E.get.current (THIS_E), "make"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __ex_ftype_autoindent));
  return ft;
}

private ftype_t *__ex_sh_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E.get.current (THIS_E), "sh"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __ex_ftype_autoindent));
  return ft;
}

private ftype_t *__ex_zig_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E.get.current (THIS_E), "zig"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __ex_c_ftype_autoindent));
  return ft;
}

private ftype_t *__ex_md_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E.get.current (THIS_E), "md"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __ex_c_ftype_autoindent, .clear_blanklines = 0));
  return ft;
}

/* really minimal and incomplete support */
syn_t ex_syn[] = {
  {
    "make", make_filenames, make_extensions, __ex_NULL_ARRAY,
    make_keywords, sh_operators,
    sh_singleline_comment, NULL, NULL, NULL,
    HL_STRINGS, HL_NUMBERS,
    __ex_syn_parser, __ex_make_syn_init, 0, 0, NULL, NULL, NULL,
  },
  {
    "sh", __ex_NULL_ARRAY, sh_extensions, sh_shebangs,
    sh_keywords, sh_operators,
    sh_singleline_comment, NULL, NULL, NULL,
    HL_STRINGS, HL_NUMBERS,
    __ex_syn_parser, __ex_sh_syn_init, 0, 0, NULL, NULL, __ex_balanced_pairs,
  },
  {
    "zig", __ex_NULL_ARRAY, zig_extensions, __ex_NULL_ARRAY, zig_keywords, zig_operators,
    zig_singleline_comment, NULL, NULL, NULL, HL_STRINGS, HL_NUMBERS,
    __ex_syn_parser, __ex_zig_syn_init, 0, 0, NULL, NULL, __ex_balanced_pairs
  },
  {
    "lua", __ex_NULL_ARRAY, lua_extensions, lua_shebangs, lua_keywords, lua_operators,
    lua_singleline_comment, lua_multiline_comment_start, lua_multiline_comment_end,
    NULL, HL_STRINGS, HL_NUMBERS,
    __ex_syn_parser, __ex_lua_syn_init, 0, 0, NULL, NULL, __ex_balanced_pairs,
  },
  {
    "lai", __ex_NULL_ARRAY, lai_extensions, lai_shebangs, lai_keywords, lai_operators,
    lai_singleline_comment, lai_multiline_comment_start, lai_multiline_comment_end,
    NULL, HL_STRINGS, HL_NUMBERS,
    __ex_syn_parser, __ex_lai_syn_init, 0, 0, NULL, NULL, __ex_balanced_pairs,
  },
  {
    "diff", __ex_NULL_ARRAY, diff_extensions, __ex_NULL_ARRAY,
    __ex_NULL_ARRAY, NULL, NULL, NULL, NULL,
    NULL, HL_STRINGS_NO, HL_NUMBERS_NO,
    __ex_diff_syn_parser, __ex_diff_syn_init, 0, 0, NULL, NULL, NULL
  },
  {
    "md", __ex_NULL_ARRAY, md_extensions, __ex_NULL_ARRAY,
    __ex_NULL_ARRAY, NULL, NULL, NULL, NULL,
    NULL, HL_STRINGS_NO, HL_NUMBERS_NO,
    __ex_syn_parser, __ex_md_syn_init, 0, 0, NULL, NULL, NULL
  }
};

public void __init_ext__ (Type (ed) *this) {
  __ex_add_rline_commands__ (this);
  __ex_add_cw_mode_actions__ (this);
  __ex_add_lw_mode_actions__ (this);
  __ex_add_word_actions__ (this);
  __ex_add_file_mode_actions__ (this);

  Ed.sh.popen = ex_ed_sh_popen;

#ifdef HAS_USER_EXTENSIONS
  __init_usr__ (this);
#endif

#ifdef HAS_LOCAL_EXTENSIONS
  __init_local__ (this);
#endif

  Ed.history.read (this);
  Ed.set.at_exit_cb (this, Ed.history.write);

  for (size_t i = 0; i < ARRLEN(ex_syn); i++)
    Ed.syn.append (this, ex_syn[i]);
}

public void __deinit_ext__ (void) {
#ifdef HAS_USER_EXTENSIONS
  __deinit_usr__ ();
#endif

#ifdef HAS_LOCAL_EXTENSIONS
  __deinit_local__ ();
#endif

  if (getuid ())
    __deinit_spell__ ( &(((Prop (this) *) __This__->prop))->spell);
}

#ifdef HAS_PROGRAMMING_LANGUAGE

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
public void sigwinch_handler (int sig) {
  signal (sig, sigwinch_handler);

  int cur_idx = E.get.current_idx (THIS_E);

  ed_t *ed = E.get.head (THIS_E);

  while (ed) {
    Ed.set.screen_size (ed);
    ifnot (OK is Ed.check_sanity (ed)) {
      __deinit_this__ (&__This__);
      exit (1);
    }

    win_t *w = Ed.get.win_head (ed);
    while (w) {
      Ed.readjust.win_size (ed, w);
      w = Ed.get.win_next (ed, w);
    }

    ifnot (E.get.next (THIS_E, ed))
      break;

    ed = E.set.next (THIS_E);
  }

  ed = E.set.current (THIS_E, cur_idx);
  win_t *w = Ed.get.current_win (ed);
#ifdef HAS_TEMPORARY_WORKAROUND
  buf_t * buf = Win.get.current_buf (w);
  Buf.draw (buf);
#else
  Win.draw (w);
#endif
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

  uid_t uid = getuid ();
  if (uid) {
    ((Prop (this) *) this->prop)->spell =  __init_spell__ ();
    ((Self (this) *) this->self)->spell = ((Prop (this) *) __This__->prop)->spell.self;
  }

  ((Prop (this) *) this->prop)->sys =  __init_sys__ ();
  ((Self (this) *) this->self)->sys = ((Prop (this) *) __This__->prop)->sys->self;

#ifdef HAS_TCC
  if (uid) {
    ((Prop (this) *) this->prop)->tcc =  __init_tcc__ ();
    ((Self (this) *) this->self)->tcc = ((Prop (this) *) __This__->prop)->tcc.self;
  }
#endif

#ifdef HAS_PROGRAMMING_LANGUAGE
  if (uid)
    __init_l__ (1);
#endif

  return __This__;
}

public void __deinit_this__ (Class (this) **thisp) {
  if (*thisp is NULL) return;

  Class (this) *this = *thisp;

  __deinit_sys__ (&((Prop (this) *) this->prop)->sys);

  __deinit_ed__ (&this->__E__);

#ifdef HAS_PROGRAMMING_LANGUAGE
  uid_t uid = getuid ();
  if (uid)
    __deinit_l__ (&__L__);
#endif

  free (this->prop);
  free (this->self);
  free (this);

  *thisp = NULL;
}

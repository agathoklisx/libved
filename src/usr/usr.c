#ifndef SYS_NAME
#error "SYS_NAME is not defined"
#else
#if defined(SYS_IS_LINUX)
#define SYS_BATTERY_DIR "/sys/class/power_supply"
#else
#define SYS_BATTERY_DIR
#endif /* SYS_IS_LINUX */
#endif /* SYS_NAME */

#include <sys/stat.h> /* for mkdir() */

#include "../lib/utf8/is_utf8.c"

#if HAS_JSON
#include "../lib/json/json.c"

static json_T JsonClass;
#define Json JsonClass.self
#endif

NewType (uenv,
  string_t *man_exec;
  string_t *elinks_exec;
);

static uenv_t *Uenv = NULL;

/* user sample extension[s] (some personal) and basic system command[s],
that since explore[s] the API, this unit is also a vehicle to understand
the needs and establish this application layer */

#if HAS_SPELL
#include "../ext/if_has_spell.c"
#endif

#ifdef WORD_LEXICON_FILE
#include "../ext/if_has_lexicon.c"
#endif

private int sys_man (buf_t **bufp, char *word, int section) {
  if (NULL is Uenv->man_exec) return NOTOK;
  if (NULL is word) return NOTOK;

  int retval = NOTOK;
  string_t *com;

  buf_t *this = Ed.get.scratch_buf ($myed);
  Buf.clear (this);

  if (File.exists (word)) {
    if (Path.is_absolute (word))
      com = String.new_with_fmt ("%s %s", Uenv->man_exec->bytes, word);
    else {
      char *cwdir = Dir.current ();
      com = String.new_with_fmt ("%s %s/%s", Uenv->man_exec->bytes, cwdir, word);
      free (cwdir);
    }

    retval = Ed.sh.popen ($myed, this, com->bytes, 1, 1, NULL);
    goto theend;
  }

  int sections[9]; for (int i = 0; i < 9; i++) sections[i] = 0;
  int def_sect = 2;

  section = ((section <= 0 or section > 8) ? def_sect : section);
  com = String.new_with_fmt ("%s -s %d %s", Uenv->man_exec->bytes,
     section, word);

  int total_sections = 0;
  for (int i = 1; i < 9; i++) {
    sections[section] = 1;
    total_sections++;
    retval = Ed.sh.popen ($myed, this, com->bytes, 1, 1, NULL);
    ifnot (retval) break;

    while (sections[section] and total_sections < 8) {
      if (section is 8) section = 1;
      else section++;
    }

    String.replace_with_fmt (com, "%s -s %d %s", Uenv->man_exec->bytes,
        section, word);
  }

theend:
  String.free (com);

  Ed.scratch ($myed, bufp, 0);
  Buf.substitute (this, ".\b", "", GLOBAL, NO_INTERACTIVE, 0,
      Buf.get.num_lines (this) - 1);
  Buf.normal.bof (this, DRAW);
  return retval;
}

/* the callback function that is called on 'W' in normal mode */
private int __u_word_actions_cb__ (buf_t **thisp, int fidx, int lidx,
                                      bufiter_t *it, char *word, utf8 c) {
  (void) fidx; (void) lidx;
  int retval = NOTOK;
  (void) retval; // gcc complains here for no reason
  switch (c) {
    case 'm':
      retval = sys_man (thisp, word, -1);
      break;

#ifdef WORD_LEXICON_FILE
    case 't':
      retval = __translate_word__ (thisp, word);
      if (0 is retval)
        Msg.send_fmt ($myed, COLOR_RED, "Nothing matched the pattern [%s]", word);
      else if (0 < retval)
        Ed.scratch ($myed, thisp, NOT_AT_EOF);
      retval = (retval > 0 ? OK : NOTOK);
      break;
#endif

#if HAS_SPELL
    case 'S':
      retval = __spell_word__ (thisp, fidx, lidx, it, word);
      break;
#endif

    default:
      break;
   }

  return retval;
}

private void __u_add_word_actions__ (ed_t *this) {
  utf8 chr[] = {'m'};
  Ed.set.word_actions (this, chr, 1, "man page", __u_word_actions_cb__);
#if HAS_SPELL
  chr[0] = 'S';
  Ed.set.word_actions (this, chr, 1, "Spell word", __u_word_actions_cb__);
#endif
#ifdef WORD_LEXICON_FILE
  chr[0] =  't';
  Ed.set.word_actions (this, chr, 1, "translate word", __u_word_actions_cb__);
#endif
}

/* this function extends standard defined commands with a `battery command
 * this prints to the message line (through an over simplistic way and only
 * on Linux systems) the battery status and capacity */
private int sys_battery_info (char *buf, int should_print) {
  int retval = NOTOK;

  /* use the SYS_NAME defined in the Makefile and avoid uname() for now */
  ifnot (Cstring.eq ("Linux", SYS_NAME)) {
    Msg.error ($myed, "battery function implemented for Linux");
    return NOTOK;
  }

  dirlist_t *dlist = Dir.list (SYS_BATTERY_DIR, 0);
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
  snprintf (dir, 64, "%s/%s/", SYS_BATTERY_DIR, it->data->bytes);
  size_t len = bytelen (dir);
  strcpy (dir + len, "capacity");
  FILE *fp = fopen (dir, "r");
  if (NULL is fp) goto theend;

  size_t clen = 0;
  ssize_t nread = getline (&cap, &clen, fp);
  if (-1 is nread) goto theend;

  cap[nread - 1] = '\0';
  fclose (fp);

  dir[len] = '\0';
  strcpy (dir + len, "status");
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
    Msg.send_fmt ($myed, COLOR_YELLOW, "[Battery is %s, remaining %s%%]",
        status, cap);

  ifnot (NULL is buf) snprintf (buf, 64, "[Battery is %s, remaining %s%%]",
      status, cap);

theend:
  ifnot (NULL is cap) free (cap);
  ifnot (NULL is status) free (status);
  dlist->free (dlist);
  return retval;
}

private int __validate_utf8_cb__ (vstr_t *unused, char *line, size_t len,
                                                      int lnr, void *obj) {
  (void) unused;
  int *retval = (int *) obj;
  char *message;
  int num_faultbytes;
  int cur_idx = 0;
  char *bytes = line;
  size_t orig_len = len;
  size_t index;

check_utf8:
  index = is_utf8 ((unsigned char *) bytes, len, &message, &num_faultbytes);

  ifnot (index) return OK;

  Ed.append.toscratch_fmt ($myed, DONOT_CLEAR,
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

private int __file_validate_utf8__ (buf_t **thisp, char *fname) {
  (void) thisp;
  int retval = NOTOK;
  ifnot (File.exists (fname)) {
    Msg.send_fmt ($myed, COLOR_RED, "%s doesn't exists", fname);
    return retval;
  }

  ifnot (File.is_readable (fname)) {
    Msg.send_fmt ($myed, COLOR_RED, "%s is not readable", fname);
    return retval;
  }

  buf_t *this = Ed.get.scratch_buf ($myed);
  Buf.clear (this);

  vstr_t unused;
  retval = OK;
  File.readlines (fname, &unused, __validate_utf8_cb__, &retval);

  if (retval is NOTOK) Ed.scratch ($myed, thisp, NOT_AT_EOF);

  return OK;
}

private int __validate_utf8__ (buf_t **thisp, rline_t *rl) {
  int range[2];
  int retval = Rline.get.range (rl, *thisp, range);
  if (NOTOK is retval) {
    range[0] = Buf.get.row.current_col_idx (*thisp);
    range[1] = range[0];
  }

  int count = range[1] - range[0] + 1;

  buf_t *this = Ed.get.scratch_buf ($myed);
  Buf.clear (this);

  vstr_t unused;
  bufiter_t *iter = Buf.iter.new (*thisp, range[0]);
  int i = 0;

  retval = OK;

  while (iter and i++ < count) {
    __validate_utf8_cb__ (&unused, iter->line->bytes, iter->line->num_bytes,
         iter->idx + 1, &retval);
    iter = Buf.iter.next (*thisp, iter);
  }

  Buf.iter.free (*thisp, iter);
  if (retval is NOTOK) Ed.scratch ($myed, thisp, NOT_AT_EOF);
  return retval;
}

private int __u_lw_mode_cb__ (buf_t **thisp, int fidx, int lidx, vstr_t *vstr, utf8 c) {
  (void) vstr;
  int retval = NOTOK;
  switch (c) {
#if HAS_SPELL
    case 'S': {
      rline_t *rl = Rline.new ($myed);
      string_t *str = String.new_with_fmt ("~spell --range=%d,%d",
         fidx + 1, lidx + 1);
      Rline.set.line (rl, str->bytes, str->num_bytes);
      String.free (str);
      Rline.parse (*thisp, rl);
      if (SPELL_OK is (retval = __buf_spell__ (thisp, rl)))
        Rline.history.push (rl);
      else
        Rline.free (rl);
    }
#endif

    case 'v': {
      rline_t *rl = Rline.new ($myed);
      string_t *str = String.new_with_fmt ("ignore --range=%d,%d",
         fidx + 1, lidx + 1);
      Rline.set.line (rl, str->bytes, str->num_bytes);
      String.free (str);
      Rline.parse (*thisp, rl);
      __validate_utf8__ (thisp, rl);
      Rline.free (rl);
    }
      break;
  }

  return retval;
}

private void __u_add_lw_mode_actions__ (ed_t *this) {
  int num_actions = 1;
#if HAS_SPELL
  num_actions++;
  utf8 chars[] = {'S', 'v'};
  char actions[] = "Spell line[s]\nvalidate utf8";
#else
  utf8 chars[] = {'v'};
  char actions[] = "validate utf8";
#endif

  Ed.set.lw_mode_actions (this, chars, num_actions, actions, __u_lw_mode_cb__);
}

private int __u_cw_mode_cb__ (buf_t **thisp, int fidx, int lidx, string_t *str, utf8 c) {
  int retval = NOTOK;
  switch (c) {
#if HAS_SPELL
    case 'S': {
      bufiter_t *iter = Buf.iter.new (*thisp, -1);
      return __u_word_actions_cb__ (thisp, fidx, lidx, iter, str->bytes, c);
    }
#endif

  }
  return retval;
}

private void __u_add_cw_mode_actions__ (ed_t *this) {
  utf8 chars[] = {'S'};
  char actions[] = "Spell selected";
  Ed.set.cw_mode_actions (this, chars, 1, actions, __u_cw_mode_cb__);
}
          /* user defined commands and|or actions */
private void __u_add_rline_user_commands__ (ed_t *this) {
/* user defined commands can begin with '~': associated in mind with '~' as $HOME */
  int num_commands = 2;
  char *commands[3] = {"~battery", "@validate_utf8", NULL};
  int num_args[] = {0, 0, 0}; int flags[] = {0, 0, 0};
  Ed.append.rline_commands (this, commands, num_commands, num_args, flags);

#if HAS_SPELL
  Ed.append.rline_command (this, "~spell", 1, RL_ARG_RANGE);
#endif

#ifdef WORD_LEXICON_FILE
  Ed.append.rline_command (this, "~translate", 0, 0);
#endif
}

private int sys_mkdir (char *dir, mode_t mode, int verbose) {
  if (OK is mkdir (dir, mode)) {
    if (verbose) {
      struct stat st;
      if (NOTOK is stat (dir, &st)) {
        Msg.error ($myed, "failed to stat directory, %s", Error.string ($myed, errno));
        return NOTOK;
      }

      char mode_string[16];
      Vsys.stat.mode_to_string (mode_string, st.st_mode);
      char mode_oct[8]; snprintf (mode_oct, 8, "%o", st.st_mode);

      Msg.send_fmt ($myed, COLOR_YELLOW, "created directory `%s', with mode: %s (%s)",
          dir, mode_oct + 1, mode_string);
    }

    return OK;
  }

  Msg.error ($myed, "failed to create directory, (%s)", Error.string ($myed, errno));
  return NOTOK;
}

private void __u_add_rline_sys_commands__ (ed_t *this) {
 /* sys defined commands can begin with '`': associated with shell syntax */
  int num_commands = 2;
  char *commands[] = {"`mkdir", "`man", NULL};
  int num_args[] = {3, 0, 0};
  int flags[] = {RL_ARG_FILENAME|RL_ARG_VERBOSE, 0, 0};
  Ed.append.rline_commands (this, commands, num_commands, num_args, flags);
  Ed.append.command_arg (this, "`man", "--section=", 10);
  Ed.append.command_arg (this, "`mkdir", "--mode=", 7);
}

/* this is the callback function that is called on the extended commands */
private int __u_rline_cb__ (buf_t **thisp, rline_t *rl, utf8 c) {
  (void) thisp; (void) c;
  int retval = NOTOK;
  string_t *com = Rline.get.command (rl);

  if (Cstring.eq (com->bytes, "~battery")) {
    retval = sys_battery_info (NULL, 1);

  } else if (Cstring.eq (com->bytes, "`mkdir")) {
    vstr_t *dirs = Rline.get.arg_fnames (rl, 1);
    if (NULL is dirs) goto theend;

    int is_verbose = Rline.arg.exists (rl, "verbose");

    mode_t def_mode = S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH; // 0755

    string_t *mode_s = Rline.get.anytype_arg (rl, "mode");

    mode_t mode = (NULL is mode_s ? def_mode : (uint) strtol (mode_s->bytes, NULL, 8));

    retval = sys_mkdir (dirs->tail->data->bytes, mode, is_verbose);
    Vstring.free (dirs);

  } else if (Cstring.eq (com->bytes, "`man")) {
    vstr_t *names = Rline.get.arg_fnames (rl, 1);
    if (NULL is names) goto theend;

    string_t *section = Rline.get.anytype_arg (rl, "section");
    int sect_id = (NULL is section ? 0 : atoi (section->bytes));

    retval = sys_man (thisp, names->head->data->bytes, sect_id);
    Vstring.free (names);

  } else if (Cstring.eq (com->bytes, "@validate_utf8")) {
    vstr_t *words = Rline.get.arg_fnames (rl, 1);
    if (NULL is words) goto theend;

    retval = __file_validate_utf8__ (thisp, words->head->data->bytes);
    Vstring.free (words);

#ifdef WORD_LEXICON_FILE
  } else if (Cstring.eq (com->bytes, "~translate")) {
    vstr_t *words = Rline.get.arg_fnames (rl, 1);
    if (NULL is words) goto theend;

    retval = __translate_word__ (thisp, words->head->data->bytes);
    if (0 is retval)
       Msg.send_fmt ($myed, COLOR_RED, "Nothing matched the pattern [%s]",
           words->head->data->bytes);
      else if (0 < retval)
        Ed.scratch ($myed, thisp, NOT_AT_EOF);
    Vstring.free (words);
    retval = (retval > 0 ? OK : NOTOK);
#endif

#if HAS_SPELL
  } else if (Cstring.eq (com->bytes, "~spell")) {
    retval = __buf_spell__ (thisp, rl);
#endif
  }

theend:
  String.free (com);
  return retval;
}

private void __u_add_rline_commands__ (ed_t *this) {
  __u_add_rline_sys_commands__ (this);
  __u_add_rline_user_commands__ (this);
  Ed.set.rline_cb (this, __u_rline_cb__);
}

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
char  sh_singleline_comment[] = "#";
char *_u_NULL_ARRAY[] = {NULL};

private char *__u_syn_parser (buf_t *this, char *line, int len, int idx, row_t *row) {
  return Buf.syn.parser (this, line, len, idx, row);
}

private string_t *__u_ftype_autoindent (buf_t *this, row_t *row) {
  return Buf.ftype.autoindent (this, row);
}

private ftype_t *__u_make_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.init (this, Ed.syn.get_ftype_idx ($myed, "make"), __u_ftype_autoindent);
  ft->tabwidth = 4;
  ft->tab_indents = 0;
  return ft;
}

private ftype_t *__u_sh_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.init (this, Ed.syn.get_ftype_idx ($myed, "sh"), __u_ftype_autoindent);
  ft->tabwidth = 4;
  ft->tab_indents = 0;
  return ft;
}

syn_t u_syn[] = {
  {
    "make", make_filenames, make_extensions, _u_NULL_ARRAY,
    make_keywords, sh_operators,
    sh_singleline_comment, NULL, NULL, NULL,
    HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_make_syn_init, 0, NULL, NULL,
  },
  {
    "sh", _u_NULL_ARRAY, sh_extensions, sh_shebangs,
    sh_keywords, sh_operators,
    sh_singleline_comment, NULL, NULL, NULL,
    HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_sh_syn_init, 0, NULL, NULL,
  },
};

private int __u_proc_popen_open_link_cb (buf_t *this, fp_t *fp) {
  (void) this; (void) fp;
  return 0;
}

private int __u_open_link_on_browser (buf_t *this) {
  if (NULL is Uenv->elinks_exec) return NOTOK;

  string_t *str = NULL;
  string_t *com = NULL;
  regexp_t *re = NULL;

  str =  Buf.get.row.current_bytes (this);
  char link[str->num_bytes + 1];
  /* from slre sources (plus file) */
  char *pat = "(((https?|file):///?)[^\\s/'\"<>]+/?[^\\s'\"<>]*)";
  int flags = RE_IGNORE_CASE;
  re = Re.new (pat, flags, RE_MAX_NUM_CAPTURES, Re.compile);
  int retval = Re.exec (re, str->bytes, str->num_bytes);

  if (retval < 0) goto theerror;

  int idx = Buf.get.row.current_col_idx (this);
  if (idx < re->match_idx or re->match_idx + re->match_len < idx)
    goto theerror;

  for (int i = 0; i < re->match_len; i++)
    link[i] = re->match_ptr[i];

   link[re->match_len] = '\0';

   /* this seems unneccecary and might give troubles */
   // com = String.new_with_fmt ("%s -remote \"ping()\"", Lenv->elinks_exec->bytes);

   com = String.new_with_fmt ("%s -remote \"openURL(%s, new-tab)\"",
      Uenv->elinks_exec->bytes, link);

   retval = Ed.sh.popen ($myed, this, com->bytes, 1, 0, __u_proc_popen_open_link_cb);
   goto theend;

theerror:
  retval = NOTOK;

theend:
  String.free (com);
  Re.free (re);
  return (retval isnot 0 ? NOTOK : OK);
}

/* extend the actions on 'g' in normal mode */
private int __u_on_normal_g (buf_t **thisp, utf8 c) {
  switch (c) {
    case 'b':
      return __u_open_link_on_browser (*thisp);
   default: break;
   }

  return NOTOK;
}

private void __init_usr__ (ed_t *this) {
  /* as a first sample, extend the actions on current word, triggered by 'W' */
  __u_add_word_actions__ (this);
  /* extend commands */
  __u_add_rline_commands__ (this);
  /* extend visual [lc]wise mode */
  __u_add_lw_mode_actions__ (this);
  __u_add_cw_mode_actions__ (this);

  Ed.set.on_normal_g_cb (this, __u_on_normal_g);

#if HAS_SPELL
  Intmap = __init_int_map__ ();
  SpellClass = __init_spell__ ();
#endif

#if HAS_JSON
  JsonClass = __init_json__ ();
#endif

  Uenv = AllocType (uenv);
  string_t *path = Venv.get (this, "path");
  Uenv->man_exec = Vsys.which ("man", path->bytes);
  Uenv->elinks_exec = Vsys.which ("elinks", path->bytes);

  Ed.syn.append (this, u_syn[0]);
  Ed.syn.append (this, u_syn[1]);
}

private void __deinit_usr__ (ed_t *this) {
  (void) this;
  String.free (Uenv->man_exec);
  String.free (Uenv->elinks_exec);
  free (Uenv);

#if HAS_JSON
  __deinit_json__ (&JsonClass);
#endif

#if HAS_SPELL
  __deinit_spell__ (&SpellClass);
#endif
}

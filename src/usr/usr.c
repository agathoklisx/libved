
#include <sys/stat.h> /* for mkdir() */
#include <time.h>
#include <pwd.h>
#include <grp.h>

NewType (uenv,
  string_t *man_exec;
  string_t *elinks_exec;
);

static uenv_t *Uenv = NULL;

/* user sample extensions and basic system command[s], that since explore[s] the API,
 * this unit is being used as a vehicle to understand the needs and establish this
 * application layer */

#include "../lib/sys/com/man.c"
#include "../lib/sys/com/battery.c"
#include "../lib/sys/com/mkdir.c"
#include "../lib/sys/com/stat.c"

/* the callback function that is called on 'W' in normal mode */
private int __u_word_actions_cb__ (buf_t **thisp, int fidx, int lidx,
                                      bufiter_t *it, char *word, utf8 c, char *action) {
  (void) fidx; (void) lidx; (void) action; (void) it; (void) word; (void) thisp;

  int retval = NOTOK;
  (void) retval; // gcc complains here for no reason
  switch (c) {
    case 'm':
      retval = sys_man (thisp, word, -1);
      break;

    default:
      break;
   }

  return retval;
}

private void __u_add_word_actions__ (ed_t *this) {
  utf8 chr[] = {'m'};
  Ed.set.word_actions (this, chr, 1, "man page", __u_word_actions_cb__);
}

#ifdef HAS_PROGRAMMING_LANGUAGE
private int __interpret__ (buf_t **thisp, char *bytes) {
  (void) thisp;
  ed_t *ed = E.get.current (THIS_E);
  term_t *term = Ed.get.term (ed);
  Term.reset (term);
  InterpretResult retval = L.compile (L_CUR_STATE, bytes);
  (void) retval;
  Term.set_mode (term, 'r');
  Input.get (term);
  Term.set (term);
  win_t *w = Ed.get.current_win (ed);
  int idx = Win.get.current_buf_idx (w);
  Win.set.current_buf (w, idx, DONOT_DRAW);
  Win.draw (w);
  return OK;
}
#endif /* HAS_PROGRAMMING_LANGUAGE */

private int __u_proc_popen_open_link_cb (buf_t *this, FILE *stream, fp_t *fp) {
  (void) this; (void) fp; (void) stream;
  return 0;
}

private int __u_open_link_on_browser__ (buf_t *this, char *link) {
   /* this seems unnecessary and might give troubles */
   // com = String.new_with_fmt ("%s -remote \"ping()\"", Uenv->elinks_exec->bytes);

  string_t *com = String.new_with_fmt ("%s -remote \"openURL(%s, new-tab)\"",
      Uenv->elinks_exec->bytes, link);

  int retval = Ed.sh.popen (E.get.current (THIS_E), this, com->bytes, 1, 0, __u_proc_popen_open_link_cb);
  String.free (com);
  return retval;
}

private int __u_file_mode_cb__ (buf_t **thisp, utf8 c, char *action) {
  (void) action;
  int retval = NO_CALLBACK_FUNCTION;
  switch (c) {
    case 'B': {
      int flags = Buf.get.flags (*thisp);
      if (0 is (flags & BUF_IS_SPECIAL) and
          0 is Cstring.eq (Buf.get.basename (*thisp), UNAMED))
        retval = __u_open_link_on_browser__ (*thisp, Buf.get.fname (*thisp));
      }
      break;

#ifdef HAS_PROGRAMMING_LANGUAGE
    case 'I': {
      int flags = Buf.get.flags (*thisp);
      if (0 is (flags & BUF_IS_SPECIAL) and
          0 is Cstring.eq (Buf.get.basename (*thisp), UNAMED)) {
        Vstring_t *lines = File.readlines (Buf.get.fname (*thisp), NULL, NULL, NULL);
        ifnot (NULL is lines) {
          retval = __interpret__ (thisp, Vstring.join (lines, "\n")->bytes);
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

private void __u_add_file_mode_actions__ (ed_t *this) {
  int num_actions = 1;
#ifdef HAS_PROGRAMMING_LANGUAGE
  num_actions++;
#endif

  utf8 chars[] = {
#ifdef HAS_PROGRAMMING_LANGUAGE
    'I',
#endif
    'B'};
  char actions[] =
#ifdef HAS_PROGRAMMING_LANGUAGE
    "Interpret file with Dictu\n"
#endif
    "Browser: Open file in the text browser (elinks)";
  Ed.set.file_mode_actions (this, chars, num_actions, actions, __u_file_mode_cb__);
}

private int __u_lw_mode_cb__ (buf_t **thisp, int fidx, int lidx, Vstring_t *vstr, utf8 c, char *action) {
  (void) vstr; (void) fidx; (void) lidx;

  int retval = NO_CALLBACK_FUNCTION;
  if (Cstring.eq_n (action, "Math", 4)) c = 'm';

  switch (c) {

#ifdef HAS_PROGRAMMING_LANGUAGE
    case 'I': {
      string_t *expression = Vstring.join (vstr, "\n");
      String.prepend (expression, "import Datetime;\n");
      retval = __interpret__ (thisp, expression->bytes);
      String.free (expression);
    }
      break;
#endif

    default:
      retval = NO_CALLBACK_FUNCTION;
  }

  return retval;
}

private void __u_add_lw_mode_actions__ (ed_t *this) {
  int num_actions = 0;

#ifdef HAS_PROGRAMMING_LANGUAGE
  num_actions++;
#endif

ifnot (num_actions) return;

  utf8 chars[] = {'I'};
  char actions[] = "Interpret line[s] with Dictu\n";
  actions[bytelen (actions) - 1] = '\0';

  Ed.set.lw_mode_actions (this, chars, num_actions, actions, __u_lw_mode_cb__);
}

/* this is the callback function that is called on the extended commands */
private int __u_rline_cb__ (buf_t **thisp, rline_t *rl, utf8 c) {
  (void) c;
  int retval = NOTOK;
  string_t *com = Rline.get.command (rl);

  if (Cstring.eq (com->bytes, "`battery")) {
    retval = sys_battery_info (NULL, 1);

  } else if (Cstring.eq (com->bytes, "`mkdir")) {
    Vstring_t *dirs = Rline.get.arg_fnames (rl, 1);
    if (NULL is dirs) goto theend;

    int is_verbose = Rline.arg.exists (rl, "verbose");

    mode_t def_mode = S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH; // 0755

    string_t *mode_s = Rline.get.anytype_arg (rl, "mode");

    mode_t mode = (NULL is mode_s ? def_mode : (uint) strtol (mode_s->bytes, NULL, 8));

    retval = sys_mkdir (dirs->tail->data->bytes, mode, is_verbose);
    Vstring.free (dirs);

  } else if (Cstring.eq (com->bytes, "`man")) {
    Vstring_t *names = Rline.get.arg_fnames (rl, 1);
    if (NULL is names) goto theend;

    string_t *section = Rline.get.anytype_arg (rl, "section");
    int sect_id = (NULL is section ? 0 : atoi (section->bytes));

    retval = sys_man (thisp, names->head->data->bytes, sect_id);
    Vstring.free (names);

  } else if (Cstring.eq (com->bytes, "`stat")) {
    Vstring_t *fnames = Rline.get.arg_fnames (rl, 1);
    if (NULL is fnames) goto theend;
    retval = sys_stat (thisp, fnames->head->data->bytes);
    Vstring.free (fnames);

  } else
    retval = RLINE_NO_COMMAND;

theend:
  String.free (com);
  return retval;
}

private void __u_add_rline_commands__ (ed_t *this) {
 /* sys defined commands can begin with '`': associated with shell syntax */
  int num_commands = 4;
  char *commands[] = {"`mkdir", "`man", "`stat", "`battery", NULL};
  int num_args[] = {3, 0, 1, 0, 0};
  int flags[] = {RL_ARG_FILENAME|RL_ARG_VERBOSE, 0, RL_ARG_FILENAME, 0, 0};
  Ed.append.rline_commands (this, commands, num_commands, num_args, flags);
  Ed.append.command_arg (this, "`man", "--section=", 10);
  Ed.append.command_arg (this, "`mkdir", "--mode=", 7);

  Ed.set.rline_cb (this, __u_rline_cb__);
}

char __u_balanced_pairs[] = "()[]{}";
char *__u_NULL_ARRAY[] = {NULL};

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

char *diff_extensions[] = {".diff", ".patch", NULL};

private char *__u_diff_syn_parser (buf_t *this, char *line, int len, int idx, row_t *row) {
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

private ftype_t *__u_diff_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this, Ed.syn.get_ftype_idx (E.get.current (THIS_E), "diff"),
    QUAL(FTYPE, .tabwidth = 0, .tab_indents = 0));
  return ft;
}

private char *__u_syn_parser (buf_t *this, char *line, int len, int idx, row_t *row) {
  return Buf.syn.parser (this, line, len, idx, row);
}

private string_t *__u_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E.get.current (THIS_E), "autoindent_default");
  return autoindent_fun (this, row);
}

private string_t *__u_c_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E.get.current (THIS_E), "autoindent_c");
  return autoindent_fun (this, row);
}

private string_t *__i_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E.get.current (THIS_E), "autoindent_c");
  return autoindent_fun (this, row);
}

private ftype_t *__u_lai_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this, Ed.syn.get_ftype_idx (E.get.current (THIS_E), "lai"),
    QUAL(FTYPE, .autoindent = __u_c_ftype_autoindent, .tabwidth = 2, .tab_indents = 1));
  return ft;
}

private ftype_t *__u_lua_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this, Ed.syn.get_ftype_idx (E.get.current (THIS_E), "lua"),
    QUAL(FTYPE, .autoindent = __u_c_ftype_autoindent, .tabwidth = 2, .tab_indents = 1));
  return ft;
}

private ftype_t *__u_make_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E.get.current (THIS_E), "make"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __u_ftype_autoindent));
  return ft;
}

private ftype_t *__u_sh_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E.get.current (THIS_E), "sh"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __u_ftype_autoindent));
  return ft;
}

private ftype_t *__u_zig_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E.get.current (THIS_E), "zig"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __u_c_ftype_autoindent));
  return ft;
}

/* really minimal and incomplete support */
syn_t u_syn[] = {
  {
    "make", make_filenames, make_extensions, __u_NULL_ARRAY,
    make_keywords, sh_operators,
    sh_singleline_comment, NULL, NULL, NULL,
    HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_make_syn_init, 0, 0, NULL, NULL, NULL,
  },
  {
    "sh", __u_NULL_ARRAY, sh_extensions, sh_shebangs,
    sh_keywords, sh_operators,
    sh_singleline_comment, NULL, NULL, NULL,
    HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_sh_syn_init, 0, 0, NULL, NULL, __u_balanced_pairs,
  },
  {
    "zig", __u_NULL_ARRAY, zig_extensions, __u_NULL_ARRAY, zig_keywords, zig_operators,
    zig_singleline_comment, NULL, NULL, NULL, HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_zig_syn_init, 0, 0, NULL, NULL, __u_balanced_pairs
  },
  {
    "lua", __u_NULL_ARRAY, lua_extensions, lua_shebangs, lua_keywords, lua_operators,
    lua_singleline_comment, lua_multiline_comment_start, lua_multiline_comment_end,
    NULL, HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_lua_syn_init, 0, 0, NULL, NULL, __u_balanced_pairs,
  },
  {
    "lai", __u_NULL_ARRAY, lai_extensions, lai_shebangs, lai_keywords, lai_operators,
    lai_singleline_comment, lai_multiline_comment_start, lai_multiline_comment_end,
    NULL, HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_lai_syn_init, 0, 0, NULL, NULL, __u_balanced_pairs,
  },
  {
    "diff", __u_NULL_ARRAY, diff_extensions, __u_NULL_ARRAY,
    __u_NULL_ARRAY, NULL, NULL, NULL, NULL,
    NULL, HL_STRINGS_NO, HL_NUMBERS_NO,
    __u_diff_syn_parser, __u_diff_syn_init, 0, 0, NULL, NULL, NULL
  }
};

private int __u_open_link_on_browser (buf_t *this) {
  if (NULL is Uenv->elinks_exec) return NOTOK;

  string_t *str = NULL;
  regexp_t *re = NULL;

  str =  Buf.get.row.current_bytes (this);
  char link[str->num_bytes + 1];
  /* from slre sources (plus file) */
  char *pat = "(((https?|file):///?)[^\\s/'\"<>]+/?[^\\s'\"<>]*)";
  int flags = RE_IGNORE_CASE;
  re = Re.new (pat, flags, RE_MAX_NUM_CAPTURES, Re.compile);
  int retval = Re.exec (re, str->bytes, str->num_bytes);

  if (retval < 0) goto theerror;

  int idx = Buf.get.current_col_idx (this);
  if (idx < re->match_idx or re->match_idx + re->match_len < idx)
    goto theerror;

  for (int i = 0; i < re->match_len; i++)
    link[i] = re->match_ptr[i];

   link[re->match_len] = '\0';

   retval = __u_open_link_on_browser__ (this, link);
   goto theend;

theerror:
  retval = NOTOK;

theend:
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

  return NO_CALLBACK_FUNCTION;
}

#ifdef HAS_PROGRAMMING_LANGUAGE

private int __u_expr_register_cb__ (ed_t *this, buf_t *buf, int regidx) {
  (void) this; (void) buf; (void) regidx;

  Msg.send (this, COLOR_NORMAL, "Expression Register");

  rline_t *rl = Ed.rline.new (this);
  Rline.set.prompt_char (rl, '=');
  Rline.edit (rl);
  string_t *line = Rline.get.line (rl);
  Rline.free (rl);
  ifnot (line->num_bytes) {
    String.free (line);
    return NOTOK;
  }

  String.prepend (line, "var __val__ = ");
  String.trim_end (line, '\n');
  String.prepend (line, "import Datetime;\n");
  String.trim_end (line, ' ');

  ifnot (line->bytes[line->num_bytes - 1] is ';')
    String.append_byte (line, ';');

  InterpretResult retval = L.compile (L_CUR_STATE, line->bytes);
  String.free (line);

  if (retval isnot INTERPRET_OK)
    return NOTOK;

  ObjString *var = L.newString (L_CUR_STATE, "__val__", 7);
  Table *table = L.table.get.module (L_CUR_STATE, "main", 4);

  if (NULL is table) return NOTOK;

  Value value;
  if (NULL is L.table.get.value (L_CUR_STATE, table, var, &value))
    return NOTOK;

  if (IS_STRING(value) or IS_INSTANCE(value)) {
    char *cstr = AS_CSTRING(value);
    Ed.reg.setidx (this, regidx, CHARWISE, cstr, NORMAL_ORDER);
  } else if (IS_NUMBER (value)) {
    double num = AS_NUMBER(value);
    char cstr[32];
    snprintf (cstr, 32, "%f", num);
    Ed.reg.setidx (this, regidx, CHARWISE, cstr, NORMAL_ORDER);
  } else
    return NOTOK;

  return OK;
}

private void __u_add_expr_register_cb__ (ed_t *this) {
  Ed.set.expr_register_cb (this, __u_expr_register_cb__);
}
#endif /* HAS_PROGRAMMING_LANGUAGE */

private void __init_usr__ (ed_t *this) {
  if (NULL is Uenv) {
    Uenv = AllocType (uenv);
    string_t *path = E.get.env (THIS_E, "path");
    Uenv->man_exec = Vsys.which ("man", path->bytes);
    Uenv->elinks_exec = Vsys.which ("elinks", path->bytes);
  }

  /* as a first sample, extend the actions on current word, triggered by 'W' */
  __u_add_word_actions__ (this);
  /* extend commands */
  __u_add_rline_commands__ (this);
  /* extend visual [lc]wise mode */
  __u_add_lw_mode_actions__ (this);

  __u_add_file_mode_actions__ (this);

#ifdef HAS_PROGRAMMING_LANGUAGE
  __u_add_expr_register_cb__ (this);
#endif

  Ed.set.on_normal_g_cb (this, __u_on_normal_g);

#ifdef HAS_JSON
  JsonClass = __init_json__ ();
#endif

  for (size_t i = 0; i < ARRLEN(u_syn); i++)
    Ed.syn.append (this, u_syn[i]);
}

private void __deinit_usr__ (void) {
  if (NULL is Uenv) return;

  String.free (Uenv->man_exec);
  String.free (Uenv->elinks_exec);
  free (Uenv);

#ifdef HAS_JSON
  __deinit_json__ (&JsonClass);
#endif
}

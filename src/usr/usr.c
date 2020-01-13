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
#include <time.h>
#include <pwd.h>
#include <grp.h>

#include "../lib/utf8/is_utf8.c"

#if HAS_JSON
#include "../lib/json/json.c"

static json_T JsonClass;
#define  Json JsonClass.self
#endif

#if HAS_EXPR
#include "../modules/tinyexpr/tinyexpr.c"
#include "../ext/if_has_expr.c"

static expr_T ExprClass;
#define  Expr ExprClass.self
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

#include "../lib/sys/com/man.c"
#include "../lib/sys/com/battery.c"
#include "../lib/sys/com/mkdir.c"
#include "../lib/sys/com/stat.c"

#include "../lib/fun/callbacks/validate_utf8.c"
#include "../lib/fun/validate_utf8.c"

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

#ifdef WORD_LEXICON_FILE
    case 't':
      retval = __translate_word__ (thisp, word);
      if (0 is retval)
        Msg.send_fmt (E(get.current), COLOR_RED, "Nothing matched the pattern [%s]", word);
      else if (0 < retval)
        Ed.scratch (E(get.current), thisp, NOT_AT_EOF);
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

#if HAS_EXPR
private int __u_math_expr_interp__ (expr_t *expr) {
  int err = 0;

  ed_t *ed = E(get.current);
  expr->ff_obj = (te_expr *) te_compile (expr->data->bytes, 0, 0, &err);

  ifnot (expr->ff_obj) {
    buf_t **thisp = (buf_t **) expr->i_obj;
    Expr.strerror (expr, EXPR_COMPILE_ERROR);
    String.append_fmt (expr->error_string, "\n%s\n%*s^\nError near here",
        expr->data->bytes, err-1, "");
    Ed.append.message_fmt (ed, expr->error_string->bytes);
    Ed.messages (ed, thisp, NOT_AT_EOF);
    expr->retval = EXPR_NOTOK;
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

private int __math_expr_evaluate__ (buf_t **thisp, char *bytes) {
  expr_t *expr = Expr.new ("Math", NULL_REF, NULL_REF, __u_math_expr_interp__, NULL_REF);
  expr->i_obj = thisp;
  expr->data = String.new_with (bytes);
  int retval = Expr.interp (expr);
  Expr.free (&expr);
  return retval;
}
#endif // HAS_EXPR

private int __u_proc_popen_open_link_cb (buf_t *this, fp_t *fp) {
  (void) this; (void) fp;
  return 0;
}

private int __u_open_link_on_browser__ (buf_t *this, char *link) {
   /* this seems unnecessary and might give troubles */
   // com = String.new_with_fmt ("%s -remote \"ping()\"", Uenv->elinks_exec->bytes);

  string_t *com = String.new_with_fmt ("%s -remote \"openURL(%s, new-tab)\"",
      Uenv->elinks_exec->bytes, link);

  int retval = Ed.sh.popen (E(get.current), this, com->bytes, 1, 0, __u_proc_popen_open_link_cb);
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

    default:
      retval = NO_CALLBACK_FUNCTION;
  }
  return retval;
}

private void __u_add_file_mode_actions__ (ed_t *this) {
  int num_actions = 1;
  utf8 chars[] = {'B'};
  char actions[] = "Browser: Open file in the text browser (elinks)";
  Ed.set.file_mode_actions (this, chars, num_actions, actions, __u_file_mode_cb__);
}

private int __u_lw_mode_cb__ (buf_t **thisp, int fidx, int lidx, vstr_t *vstr, utf8 c, char *action) {
  (void) vstr; (void) action;

  int retval = NO_CALLBACK_FUNCTION;
  switch (c) {
#if HAS_SPELL
    case 'S': {
      rline_t *rl = Rline.new (E(get.current));
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

#ifdef HAS_EXPR
    case 'm': {
      string_t *expression = Vstring.join (vstr, "\n");
      retval = __math_expr_evaluate__ (thisp, expression->bytes);
      String.free (expression);
    }
      break;
#endif

    case 'v': {
      rline_t *rl = Rline.new (E(get.current));
      string_t *str = String.new_with_fmt ("ignore --range=%d,%d",
         fidx + 1, lidx + 1);
      Rline.set.line (rl, str->bytes, str->num_bytes);
      String.free (str);
      Rline.parse (*thisp, rl);
      __validate_utf8__ (thisp, rl);
      Rline.free (rl);
      retval = OK;
    }
      break;

    default:
      retval = NO_CALLBACK_FUNCTION;
  }

  return retval;
}

private void __u_add_lw_mode_actions__ (ed_t *this) {
  int num_actions = 1;
#if HAS_SPELL
  num_actions++;
#endif

#if HAS_EXPR
  num_actions++;
#endif

  utf8 chars[] = {
#if HAS_SPELL
  'S',
#endif
#if HAS_EXPR
  'm',
#endif
  'v'};

  char actions[] =
#if HAS_SPELL
     "Spell line[s]\n"
#endif
#if HAS_EXPR
     "math expression\n"
#endif
     "validate utf8";

  Ed.set.lw_mode_actions (this, chars, num_actions, actions, __u_lw_mode_cb__);
}

private int __u_cw_mode_cb__ (buf_t **thisp, int fidx, int lidx, string_t *str, utf8 c, char *action) {
  int retval = NO_CALLBACK_FUNCTION;
  switch (c) {
#if HAS_SPELL
    case 'S': {
      bufiter_t *iter = Buf.iter.new (*thisp, -1);
      retval = __u_word_actions_cb__ (thisp, fidx, lidx, iter, str->bytes, c, action);
      Buf.iter.free (*thisp, iter);
      break;
    }
#endif

#if HAS_EXPR
    case 'm': {
      retval = __math_expr_evaluate__ (thisp, str->bytes);
      break;
    }
#endif
    default:
      retval = NO_CALLBACK_FUNCTION;
  }
  return retval;
}

private void __u_add_cw_mode_actions__ (ed_t *this) {
  int num_actions = 0;

#if HAS_SPELL
  num_actions++;
#endif

#if HAS_EXPR
  num_actions++;
#endif

  utf8 chars[] = {
#if HAS_SPELL
  'S',
#endif
#if HAS_EXPR
  'm'
#endif
  };

 char actions[] = ""
#if HAS_SPELL
   "Spell selected\n"
#endif
#if HAS_EXPR
  "math expression"
#endif
   ;

  Ed.set.cw_mode_actions (this, chars, num_actions, actions, __u_cw_mode_cb__);
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
  Ed.append.command_arg (this, "~spell", "--edit", 6);
#endif

#ifdef WORD_LEXICON_FILE
  Ed.append.rline_command (this, "~translate", 0, 0);
#endif
}

private void __u_add_rline_sys_commands__ (ed_t *this) {
 /* sys defined commands can begin with '`': associated with shell syntax */
  int num_commands = 3;
  char *commands[] = {"`mkdir", "`man", "`stat", NULL};
  int num_args[] = {3, 0, 1, 0};
  int flags[] = {RL_ARG_FILENAME|RL_ARG_VERBOSE, 0, RL_ARG_FILENAME, 0};
  Ed.append.rline_commands (this, commands, num_commands, num_args, flags);
  Ed.append.command_arg (this, "`man", "--section=", 10);
  Ed.append.command_arg (this, "`mkdir", "--mode=", 7);
}

/* this is the callback function that is called on the extended commands */
private int __u_rline_cb__ (buf_t **thisp, rline_t *rl, utf8 c) {
  (void) c;
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

  } else if (Cstring.eq (com->bytes, "`stat")) {
    vstr_t *fnames = Rline.get.arg_fnames (rl, 1);
    if (NULL is fnames) goto theend;
    retval = sys_stat (thisp, fnames->head->data->bytes);
    Vstring.free (fnames);

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
       Msg.send_fmt (E(get.current), COLOR_RED, "Nothing matched the pattern [%s]",
           words->head->data->bytes);
      else if (0 < retval)
        Ed.scratch (E(get.current), thisp, NOT_AT_EOF);
    Vstring.free (words);
    retval = (retval > 0 ? OK : NOTOK);
#endif

#if HAS_SPELL
  } else if (Cstring.eq (com->bytes, "~spell")) {
    retval = __buf_spell__ (thisp, rl);
#endif
  } else
    retval = RLINE_NO_COMMAND;

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
char sh_singleline_comment[] = "#";
char sh_balanced_pairs[] = "()[]{}";
char *_u_NULL_ARRAY[] = {NULL};

private char *__u_syn_parser (buf_t *this, char *line, int len, int idx, row_t *row) {
  return Buf.syn.parser (this, line, len, idx, row);
}

private string_t *__u_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E(get.current), "autoindent_default");
  return autoindent_fun (this, row);
}

private ftype_t *__u_make_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E(get.current), "make"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __u_ftype_autoindent));
  return ft;
}

private ftype_t *__u_sh_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E(get.current), "sh"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __u_ftype_autoindent));
  return ft;
}

/* really minimal and incomplete support */
syn_t u_syn[] = {
  {
    "make", make_filenames, make_extensions, _u_NULL_ARRAY,
    make_keywords, sh_operators,
    sh_singleline_comment, NULL, NULL, NULL,
    HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_make_syn_init, 0, 0, NULL, NULL, NULL,
  },
  {
    "sh", _u_NULL_ARRAY, sh_extensions, sh_shebangs,
    sh_keywords, sh_operators,
    sh_singleline_comment, NULL, NULL, NULL,
    HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_sh_syn_init, 0, 0, NULL, NULL, sh_balanced_pairs,
  },
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

  int idx = Buf.get.row.current_col_idx (this);
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

private void __init_usr__ (ed_t *this) {
  if (NULL is Uenv) {
    Uenv = AllocType (uenv);
    string_t *path = Venv.get (this, "path");
    Uenv->man_exec = Vsys.which ("man", path->bytes);
    Uenv->elinks_exec = Vsys.which ("elinks", path->bytes);
  }

  /* as a first sample, extend the actions on current word, triggered by 'W' */
  __u_add_word_actions__ (this);
  /* extend commands */
  __u_add_rline_commands__ (this);
  /* extend visual [lc]wise mode */
  __u_add_lw_mode_actions__ (this);
  __u_add_cw_mode_actions__ (this);

  __u_add_file_mode_actions__ (this);

  Ed.set.on_normal_g_cb (this, __u_on_normal_g);

#if HAS_SPELL
  Intmap = __init_int_map__ ();
  SpellClass = __init_spell__ ();
#endif

#if HAS_JSON
  JsonClass = __init_json__ ();
#endif

#if HAS_EXPR
  ExprClass = __init_expr__ ();
#endif

  Ed.syn.append (this, u_syn[0]);
  Ed.syn.append (this, u_syn[1]);
}

private void __deinit_usr__ (void) {
  String.free (Uenv->man_exec);
  String.free (Uenv->elinks_exec);
  free (Uenv);

#if HAS_SPELL
  __deinit_spell__ (&SpellClass);
#endif

#if HAS_JSON
  __deinit_json__ (&JsonClass);
#endif

#if HAS_EXPR
  __deinit_expr__ (&ExprClass);
#endif

}

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

#if HAS_EXPR
#include "../lib/tinyexpr/tinyexpr.c"
#include "../ext/if_has_expr.c"

static expr_T ExprClass;
#define  Expr ExprClass.self
#endif

#if HAS_TCC

static tcc_T TccClass;
#define Tcc TccClass.self

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
        Msg.send_fmt (E(get.current), COLOR_ERROR, "Nothing matched the pattern [%s]", word);
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
#endif /* HAS_EXPR */

#ifdef HAS_RUNTIME_INTERPRETER
private int __interpret__ (buf_t **thisp, char *bytes) {
  (void) thisp;
  ed_t *ed = E(get.current);
  term_t *term = Ed.get.term (ed);
  Term.reset (term);
  InterpretResult retval = interpret (__L__->states[__L__->cur_state], bytes);
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
#endif /* HAS_RUNTIME_INTERPRETER */

#ifdef HAS_TCC
private void c_tcc_error_cb (void *obj, const char *msg) {
  (void) obj;
  ed_t *ed = E(get.current);
  Msg.write (ed, "====- Tcc Error Message -====\n");
  Msg.write (ed, (char *) msg);
}

private int c_tcc_string_add_lnums_cb (vstr_t *str, char *tok, void *obj) {
  (void) str;
  ed_t *ed = E(get.current);
  int *lnr = (int *) obj;
  Ed.append.message_fmt (ed, "%d|%s", ++(*lnr), tok);
  return OK;
}

private void c_tcc_string_add_lnums (char *src) {
  vstr_t unused;
  int lnr = 0;
  Cstring.chop (src, '\n', &unused, c_tcc_string_add_lnums_cb, &lnr);
}

private int c_tcc_string (buf_t **thisp, char *src) {
  (void) thisp;
  ed_t *ed = E(get.current);
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
  ed_t *ed = E(get.current);
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

private int __u_proc_popen_open_link_cb (buf_t *this, FILE *stream, fp_t *fp) {
  (void) this; (void) fp; (void) stream;
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

#if HAS_RUNTIME_INTERPRETER
    case 'I': {
      int flags = Buf.get.flags (*thisp);
      if (0 is (flags & BUF_IS_SPECIAL) and
          0 is Cstring.eq (Buf.get.basename (*thisp), UNAMED)) {
        vstr_t *lines = File.readlines (Buf.get.fname (*thisp), NULL, NULL, NULL);
        ifnot (NULL is lines) {
          retval = __interpret__ (thisp, Vstring.join (lines, "\n")->bytes);
          Vstring.free (lines);
        } else
          retval = NOTOK;
      }
    }
    break;
#endif

#if HAS_TCC
    case 'C': {
      int flags = Buf.get.flags (*thisp);
      if (0 is (flags & BUF_IS_SPECIAL) and
          0 is Cstring.eq (Buf.get.basename (*thisp), UNAMED)) {
        vstr_t *lines = File.readlines (Buf.get.fname (*thisp), NULL, NULL, NULL);
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

private void __u_add_file_mode_actions__ (ed_t *this) {
  int num_actions = 1;
#if HAS_RUNTIME_INTERPRETER
  num_actions++;
#endif
#if HAS_TCC
  num_actions++;
#endif

  utf8 chars[] = {
#if HAS_RUNTIME_INTERPRETER
  'I',
#endif
#if HAS_TCC
  'C',
#endif
 'B'};
  char actions[] =
#if HAS_RUNTIME_INTERPRETER
 "Interpret: Interpret file\n"
#endif
#if HAS_TCC
 "Compile file with tcc compiler\n"
#endif
 "Browser: Open file in the text browser (elinks)";
  Ed.set.file_mode_actions (this, chars, num_actions, actions, __u_file_mode_cb__);
}

private int __u_lw_mode_cb__ (buf_t **thisp, int fidx, int lidx, vstr_t *vstr, utf8 c, char *action) {
  (void) vstr;

  int retval = NO_CALLBACK_FUNCTION;
  if (Cstring.eq_n (action, "Math", 4)) c = 'm';

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

#ifdef HAS_TCC
    case 'C': {
      return __tcc_compile__ (thisp, Vstring.join (vstr, "\n"));
    }
      break;
#endif

#ifdef HAS_EXPR
    case 'm': {
      string_t *expression = Vstring.join (vstr, "\n");
      retval = __math_expr_evaluate__ (thisp, expression->bytes);
      String.free (expression);
    }
      break;
#endif

#ifdef HAS_RUNTIME_INTERPRETER
    case 'I': {
      string_t *expression = Vstring.join (vstr, "\n");
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
#if HAS_SPELL
  num_actions++;
#endif

#if HAS_TCC
  num_actions++;
#endif

#if HAS_EXPR
  num_actions++;
#endif

#if HAS_RUNTIME_INTERPRETER
  num_actions++;
#endif

ifnot (num_actions) return;

  utf8 chars[] = {
#if HAS_SPELL
  'S',
#endif
#if HAS_TCC
  'C',
#endif
#if HAS_RUNTIME_INTERPRETER
  'I',
#endif
#if HAS_EXPR
  'm'
#endif
  };

  char actions[] =
#if HAS_SPELL
     "Spell line[s]\n"
#endif
#if HAS_TCC
  "Compile lines with tcc\n"
#endif
#if HAS_RUNTIME_INTERPRETER
     "Interpret\n"
#endif
#if HAS_EXPR
     "math expression\n"
#endif
    ;
  actions[bytelen (actions) - 1] = '\0';
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
  Ed.append.rline_command (this, "~battery", 0, 0);

#if HAS_SPELL
  Ed.append.rline_command (this, "~spell", 1, RL_ARG_RANGE);
  Ed.append.command_arg (this, "~spell", "--edit", 6);
#endif

#ifdef WORD_LEXICON_FILE
  Ed.append.rline_command (this, "~translate", 0, 0);
  Ed.append.command_arg (this, "~translate", "--edit", 6);
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

#ifdef WORD_LEXICON_FILE
  } else if (Cstring.eq (com->bytes, "~translate")) {

    int edit = Rline.arg.exists (rl, "edit");
    if (edit) {
      retval = __edit_lexicon__ (thisp);
      goto theend;
    }

    vstr_t *words = Rline.get.arg_fnames (rl, 1);
    if (NULL is words) goto theend;

    retval = __translate_word__ (thisp, words->head->data->bytes);
    if (0 is retval)
       Msg.send_fmt (E(get.current), COLOR_ERROR, "Nothing matched the pattern [%s]",
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

private int i_save_image (buf_t **thisp, rline_t *rl) {
  int retval = NOTOK;
  win_t *w = Buf.get.parent (*thisp);
  int cbidx = Win.get.current_buf_idx (w);

  char *fn = NULL;
  string_t *fn_arg = Rline.get.anytype_arg (rl, "as");
  ifnot (NULL is fn_arg) {
    fn = fn_arg->bytes;
  } else
    fn = Path.basename (Buf.get.basename (*thisp));

  string_t *img = String.new_with (fn);
  char *extname = Path.extname (img->bytes);

  size_t exlen = bytelen (extname);
  if (exlen) {
    if (exlen isnot img->num_bytes) {
      char *p = img->bytes + img->num_bytes - 1;
      while (*p isnot '.') {
        p--;
        String.clear_at (img, img->num_bytes - 1);
      }
    } else  // .file
      String.append_byte (img, '.');
  } else
    String.append_byte (img, '.');

  ifnot (Path.is_absolute (img->bytes)) {
    String.prepend (img, "/profiles/");
    String.prepend (img, E(get.env, "data_dir")->bytes);
  }

  String.append (img, "i");

  FILE *fp = fopen (img->bytes, "w");
  if (NULL is fp) goto theend;

  String.clear (img);

  String.append (img,
     "var flags = 0\n"
     "var frame_zero = 0\n"
     "var draw = 1\n"
     "var donot_draw = 0\n");

  int g_num_buf = 0;
  int g_num_win = 0;
  int g_num_ed = 0;

  ed_t *ed = E(get.head);

  while (ed isnot NULL) {
    ifnot (g_num_ed) {
      String.append (img, "var ");
    }

    g_num_ed++;

    int num_win = Ed.get.num_win (ed, NO_COUNT_SPECIAL);
    String.append_fmt (img, "ed = ed_new (%d)\n", num_win);

    ifnot (g_num_win) {
      String.append (img, "var ");
      g_num_win++;
    }

    String.append (img, "cwin = ed_get_current_win (ed)\n");

    int l_num_win = 0;
    win_t *cwin = Ed.get.win_head (ed);

    while (cwin) {
      if (Win.isit.special_type (cwin)) goto next_win;

      if (l_num_win)
        String.append (img, "cwin = ed_get_win_next (ed, cwin)\n\n");

      l_num_win++;

      buf_t *buf = Win.get.buf_head (cwin);
      while (buf) {
        if (Buf.isit.special_type (buf)) goto next_buf;
        char *bufname = Buf.get.fname (buf);

        ifnot (g_num_buf) {
          String.append (img, "var ");
          g_num_buf++;
         }

         String.append (img, "buf = win_buf_init (cwin, frame_zero, flags)\n");
         String.append_fmt (img, "buf_init_fname (buf, \"%s\")\n", bufname);
         char *ftype_name = Buf.get.ftype_name  (buf);
         String.append_fmt (img, "buf_set_ftype (buf, \"%s\")\n", ftype_name);
         int cur_row_idx = Buf.get.cur_idx (buf);
         String.append_fmt (img, "buf_set_row_idx (buf, %d)\n", cur_row_idx);
         String.append (img, "win_append_buf (cwin, buf)\n");

next_buf:
        buf = Win.get.buf_next (cwin, buf);
      }

next_win:
      cwin = Ed.get.win_next (ed, cwin);
    }

    ed = E(get.next, ed);
    ifnot (NULL is ed)
      String.append (img, "ed = set_ed_next ()\n");
  }

  int idx = E(get.current_idx);
  String.append_fmt (img, "ed = set_ed_by_idx (%d)\n", idx);
  String.append (img, "cwin = ed_get_current_win (ed)\n");
  String.append_fmt (img, "win_set_current_buf (cwin, %d, donot_draw)\n", cbidx);
  String.append (img, "win_draw (cwin)\n");

  fprintf (fp, "%s\n", img->bytes);
  fclose (fp);
  retval = OK;

theend:
  String.free (img);
  return retval;
}

private int i_rline_cb (buf_t **thisp, rline_t *rl, utf8 c) {
  (void) c;
  int retval = RLINE_NO_COMMAND;
  string_t *com = Rline.get.command (rl);

  if (Cstring.eq (com->bytes, "@save_image")) {
    retval = i_save_image (thisp, rl);
  }

  String.free (com);
  return retval;
}

private void __u_add_rline_commands__ (ed_t *this) {
  __u_add_rline_sys_commands__ (this);
  __u_add_rline_user_commands__ (this);
  Ed.set.rline_cb (this, __u_rline_cb__);

  Ed.append.rline_command (this, "@save_image", 0, 0);
  Ed.append.command_arg (this,   "@save_image", "--as=", 5);
  Ed.set.rline_cb (this, i_rline_cb);
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
char *i_extensions[] = {".i", NULL};
char *i_shebangs[] = {"#!/bin/env i", NULL};
char i_operators[] = "+:-*^><=|&~.()[]{}/";
char *i_keywords[] = {
    "while I", "print F", "var V",
    "if I", "for I", "else I", "return I", "func I",
    NULL,
};

char i_singleline_comment[] = "#";

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
char *lai_shebangs[] = {"#!/bin/env lai", "#!/usr/bin/lai", "#!/usr/bin/yala", "#!/usr/bin/doctu", NULL};
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

private char *__u_syn_parser (buf_t *this, char *line, int len, int idx, row_t *row) {
  return Buf.syn.parser (this, line, len, idx, row);
}

private string_t *__u_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E(get.current), "autoindent_default");
  return autoindent_fun (this, row);
}

private string_t *__u_c_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E(get.current), "autoindent_c");
  return autoindent_fun (this, row);
}

private string_t *__i_ftype_autoindent (buf_t *this, row_t *row) {
  FtypeAutoIndent_cb autoindent_fun = Ed.get.callback_fun (E(get.current), "autoindent_c");
  return autoindent_fun (this, row);
}

private ftype_t *__u_lai_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this, Ed.syn.get_ftype_idx (E(get.current), "lai"),
    QUAL(FTYPE, .autoindent = __u_c_ftype_autoindent, .tabwidth = 2, .tab_indents = 1));
  return ft;
}

private ftype_t *__u_lua_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.set (this, Ed.syn.get_ftype_idx (E(get.current), "lua"),
    QUAL(FTYPE, .autoindent = __u_c_ftype_autoindent, .tabwidth = 2, .tab_indents = 1));
  return ft;
}

private ftype_t *__u_make_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E(get.current), "make"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __u_ftype_autoindent));
  return ft;
}

private ftype_t *__u_sh_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E(get.current), "sh"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __u_ftype_autoindent));
  return ft;
}

private ftype_t *__u_zig_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this,  Ed.syn.get_ftype_idx (E(get.current), "zig"),
    QUAL(FTYPE, .tabwidth = 4, .tab_indents = 0, .autoindent = __u_c_ftype_autoindent));
  return ft;
}

private ftype_t *__u_i_syn_init (buf_t *this) {
  ftype_t *ft = Buf.ftype.set (this, Ed.syn.get_ftype_idx (E(get.current), "i"),
    QUAL(FTYPE, .autoindent = __u_c_ftype_autoindent, .tabwidth = 2, .tab_indents = 1));
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
    "i", __u_NULL_ARRAY, i_extensions, i_shebangs, i_keywords, i_operators,
    i_singleline_comment, NULL, NULL, NULL, HL_STRINGS, HL_NUMBERS,
    __u_syn_parser, __u_i_syn_init, 0, 0, NULL, NULL, __u_balanced_pairs
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
    string_t *path = E(get.env, "path");
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

  ImapClass = __init_int_map__ ();

#if HAS_SPELL
  SpellClass = __init_spell__ ();
#endif

#if HAS_JSON
  JsonClass = __init_json__ ();
#endif

#if HAS_EXPR
  ExprClass = __init_expr__ ();
#endif

#if HAS_TCC
  TccClass = __init_tcc__ ();
#endif

  for (size_t i = 0; i < ARRLEN(u_syn); i++)
    Ed.syn.append (this, u_syn[i]);
}

private void __deinit_usr__ (void) {
  if (NULL is Uenv) return;

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

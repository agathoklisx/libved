
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
#include <errno.h>

#include <libved.h>
#include <libved+.h>

#ifdef HAS_JSON
#include "../lib/json/json.c"
#endif

NewType (uenv,
  string_t *elinks_exec;
);

static uenv_t *Uenv = NULL;

/* user sample extensions and basic system command[s], that since explore[s] the API,
 * this unit has being used initially, as a vehicle to understand the needs and establish this
 * application layer */

/* the callback function that is called on 'W' in normal mode */
private int __u_word_actions_cb__ (buf_t **thisp, int fidx, int lidx,
                                      bufiter_t *it, char *word, utf8 c, char *action) {
  (void) fidx; (void) lidx; (void) action; (void) it; (void) word; (void) thisp; (void) c;
  return NO_CALLBACK_FUNCTION;
}

private void __u_add_word_actions__ (ed_t *this) {
  (void) this;
  return;
}

#ifdef HAS_PROGRAMMING_LANGUAGE
private int __interpret__ (buf_t **thisp, char *bytes) {
  (void) thisp;
  ed_t *ed = E.get.current (THIS_E);
  term_t *term = Ed.get.term (ed);
  Term.reset (term);
  DictuInterpretResult retval = L.compile (L_CUR_STATE, "libved", bytes);
  (void) retval;
  fprintf (stdout, "\nHit any key to continue\n");
  Term.set_mode (term, 'r');
  Cursor.hide (term);
  Input.get (term);
  Cursor.show (term);
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

  int flags = ED_PROC_READ_STDOUT;

  int retval = Ed.sh.popen (E.get.current (THIS_E), this, com->bytes, flags, __u_proc_popen_open_link_cb);
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
          0 is Cstring.eq (Buf.get.basename (*thisp), UNNAMED))
        retval = __u_open_link_on_browser__ (*thisp, Buf.get.fname (*thisp));
      }
      break;

#ifdef HAS_PROGRAMMING_LANGUAGE
    case 'I': {
      int flags = Buf.get.flags (*thisp);
      if (0 is (flags & BUF_IS_SPECIAL) and
          0 is Cstring.eq (Buf.get.basename (*thisp), UNNAMED)) {
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
  ifnot (getuid ()) return;

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
  (void) thisp; (void) vstr; (void) fidx; (void) lidx; (void) action;

  int retval = NO_CALLBACK_FUNCTION;

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
  if (getuid ())
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
  (void) thisp; (void) rl; (void) c;
  return RLINE_NO_COMMAND;
}

private void __u_add_rline_commands__ (ed_t *this) {
  (void) this;
  return;
}

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

  DictuInterpretResult retval = L.compile (L_CUR_STATE, "libved", line->bytes);
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
  Ed.set.expr_reg_cb (this, __u_expr_register_cb__);
}
#endif /* HAS_PROGRAMMING_LANGUAGE */

private void __init_usr__ (ed_t *this) {
  if (NULL is Uenv) {
    Uenv = AllocType (uenv);
    string_t *path = E.get.env (THIS_E, "path");
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
  if (getuid ())
    __u_add_expr_register_cb__ (this);
#endif

  Ed.set.on_normal_g_cb (this, __u_on_normal_g);

#ifdef HAS_JSON
  JsonClass = __init_json__ ();
#endif
}

private void __deinit_usr__ (void) {
  if (NULL is Uenv) return;

  String.free (Uenv->elinks_exec);
  free (Uenv);

#ifdef HAS_JSON
  __deinit_json__ (&JsonClass);
#endif
}

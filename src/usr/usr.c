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

NewType (uenv,
  string_t *man_exec;
);

static uenv_t *Uenv = NULL;

/* user sample extension[s] (mostly personal) and basic system command[s],
that since explore[s] the API, this unit is also a vehicle to understand
the needs and establish this application layer */

#if HAS_SPELL
#include "../lib/map/int_map.h"
static   intmap_T Intmap;
#define  Imap Intmap.self

#include "../lib/spell/spell.c"
static   spell_T SpellClass;
#define  Spell SpellClass.self

#define SPELL_NOTWORD Notword "012345678#:`$_"
#define SPELL_NOTWORD_LEN (Notword_len + 14)
#define SPELL_CHANGED_WORD 1

private utf8 __spell_question__ (spell_t *spell, buf_t **thisp,
       action_t **action, int fidx, int lidx, bufiter_t *iter) {
  char prefix[fidx + 1];
  char lpart[iter->line->num_bytes - lidx];
  Cstring.substr (prefix, fidx, iter->line->bytes, iter->line->num_bytes, 0);
  Cstring.substr (lpart, iter->line->num_bytes - lidx - 1, iter->line->bytes,
     iter->line->num_bytes, lidx + 1);
/*
  string_t *quest = String.new_with_fmt (
  "Spelling [%s] at line %d\n%s" COLOR_FMT "%s" COLOR_RESET "%s\n"
  "Suggestions: (enter number to accept one as correct)\n",
  spell->word, iter->idx + 1, prefix, COLOR_RED, spell->word, lpart);
*/

  string_t *quest = String.new_with_fmt (
    "Spelling [%s] at line %d and %d index\n%s%s%s\n"
    "Suggestions: (enter number to accept one as correct)\n",
    spell->word, iter->idx + 1, fidx, prefix, spell->word, lpart);
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
      "Other Choises:\na[ccept] word as correct and add it to the dictionary\n"
      "A[ccept] word as correct just for this session\n"
      "c[ansel] operation and continue with the next\n"
      "i[nput]  correct word by getting input\n"
      "q[uit]   quit operation\n");

  utf8 c = Ed.question ($myed, quest->bytes, chars, charslen);
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

  spell_t *spell = Spell.new ();
  if (SPELL_ERROR is Spell.init_dictionary (spell, SPELL_DICTIONARY, SPELL_DICTIONARY_NUM_ENTRIES, NO_FORCE)) {
    Msg.send ($myed, COLOR_RED, spell->messages->head->data->bytes);
    Spell.free (spell, SPELL_CLEAR_DICTIONARY);
    return NOTOK;
  }

  action_t *action = Buf.action.new (*thisp);
  Buf.action.set_current (*thisp, action, REPLACE_LINE);

  int len = lidx - fidx + 1;
  if (len < (int) spell->min_word_len) goto theend;

  strcpy (spell->word, word);
  spell->word_len = len;

  retval = Spell.correct (spell);

  if (retval >= SPELL_WORD_IS_CORRECT) {
    retval = OK;
    goto theend;
  }

  ifnot (spell->guesses->num_items) {
    retval = NOTOK;
    goto theend;
  }

  retval = __spell_question__ (spell, thisp, &action, fidx, lidx, iter);

theend:
  if (retval is SPELL_CHANGED_WORD) {
    Buf.action.push (*thisp, action);
    Buf.draw (*thisp);
  } else
    Buf.action.free (*thisp, action);

  Spell.free (spell, SPELL_DONOT_CLEAR_DICTIONARY);
  return retval;
}

private int __buf_spell__ (buf_t **thisp, rline_t *rl) {
  int range[2];
  int retval = Rline.get.range (rl, *thisp, range);
  if (NOTOK is retval) {
    range[0] = Buf.row.get_current_line_idx (*thisp);
    range[1] = range[0];
  }

  int count = range[1] - range[0] + 1;

  spell_t *spell = Spell.new ();
  if (SPELL_ERROR is Spell.init_dictionary (spell, SPELL_DICTIONARY, SPELL_DICTIONARY_NUM_ENTRIES, NO_FORCE))
    {
    Msg.send ($myed, COLOR_RED, spell->messages->head->data->bytes);
    Spell.free (spell, SPELL_CLEAR_DICTIONARY);
    return NOTOK;
    }

  action_t *action = Buf.action.new (*thisp);
  Buf.action.set_current (*thisp, action, REPLACE_LINE);

  int buf_changed = 0;

  char word[MAXWORD];

  bufiter_t *iter = Buf.iter.new (*thisp, range[0]);

  int i = 0;
  while (iter and i++ < count) {
    int fidx = 0; int lidx = -1;
    string_t *line = iter->line;
    char *tmp = NULL;
    for (;;) {
      int cur_idx = lidx + 1 + (tmp isnot NULL);
      tmp = Cstring.extract_word_at (line->bytes, line->num_bytes,
          word, MAXWORD, SPELL_NOTWORD, SPELL_NOTWORD_LEN, cur_idx, &fidx, &lidx);

      if (NULL is tmp) {
        if (lidx >= (int) line->num_bytes - 1)
          goto itnext;
        continue;
      }

      int len = lidx - fidx + 1;
      if (len < (int) spell->min_word_len or len >= MAXWORD)
        continue;

      strcpy (spell->word, word);
      spell->word_len = len;

      retval = Spell.correct (spell);

      if (retval >= SPELL_WORD_IS_CORRECT) continue;
      ifnot (spell->guesses->num_items) continue;
      retval = __spell_question__ (spell, thisp, &action, fidx, lidx, iter);
      if (SPELL_ERROR is retval) goto theend;
      if (SPELL_CHANGED_WORD) buf_changed = 1;
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
  spell_free (spell, SPELL_DONOT_CLEAR_DICTIONARY);
  return retval;
}

private int __u_lw_mode_cb__ (buf_t **thisp, int fidx, int lidx, vstr_t *vstr, utf8 c) {
  (void) vstr;
  int retval = NOTOK;
  switch (c) {
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
    break;

  }
  return retval;
}

private void __u_add_lw_mode_actions__ (ed_t *this) {
  utf8 chars[] = {'S'};
  char actions[] = "Spell line[s]";
  Ed.set.lw_mode_actions (this, chars, 1, actions, __u_lw_mode_cb__);
}
#endif

          /* user defined commands and|or actions */

/* this function that extends normal mode, performs a simple search on a
 * lexicon defined file for 'word', and then prints the matched lines to
 * the scratch buffer (this buffer can be closed with 'q' (as in a pager)):
 * requires the WORD_LEXICON_FILE to be defined with a way; my way is to
 * compile the distribution through a shell script, that invokes `make`
 * with my specific definitions
 */
private int ved_translate_word (buf_t **thisp, char *word) {
  (void) thisp;
  char *lex = NULL;

#ifndef WORD_LEXICON_FILE
  Msg.error ($myed, "%s(): lexikon has not been defined", __func__);
  return NOTOK;
#else
  lex = WORD_LEXICON_FILE;
#endif

  Msg.send_fmt ($myed, COLOR_YELLOW, "translating [%s]", word);

  ifnot (File.exists (lex)) {
    Msg.error ($myed, "%s(): lexicon has not been found", __func__);
    return NOTOK;
  }

  FILE *fp = fopen (lex, "r");
  if (NULL is fp) {
    Msg.error ($myed, Error.string ($myed, errno));
    return NOTOK;
  }

  regexp_t *re = Re.new (word, 0, RE_MAX_NUM_CAPTURES, Re.compile);
  size_t len;
  char *line = NULL;
  int nread;
  int match = 0;

  toscratch ($myed, word, 1);
  toscratch ($myed, "=================", 0);

  while (-1 isnot (nread = getline (&line, &len, fp)))
    if (0 <= Re.exec (re, line, nread)) {
      match++;
      toscratch ($myed, line, 0);
      Re.reset_captures (re);
    }

  fclose (fp);
  Re.free (re);
  if (line isnot NULL) free (line);

  return match;
}

private int sys_man (buf_t **bufp, char *word, int section) {
  if (NULL is Uenv->man_exec) return NOTOK;
  if (NULL is word) return NOTOK;

  int retval = NOTOK;
  int sections[9]; for (int i = 0; i < 9; i++) sections[i] = 0;
  int def_sect = 2;

  section = ((section <= 0 or section > 8) ? def_sect : section);
  string_t *com = String.new_with_fmt ("%s -s %d %s", Uenv->man_exec->bytes,
     section, word);

  buf_t *this = Ed.get.scratch_buf ($myed);
  Buf.clear (this);

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

  String.free (com);

  Ed.scratch ($myed, bufp, 0);
  Buf.substitute (this, ".\b", "", GLOBAL, NO_INTERACTIVE, 0,
      Buf.get.num_lines (this) - 1);
  Buf.normal.bof (this);
  return retval;
}


/* the callback function that is called on 'W' in normal mode */
private int __u_word_actions_cb__ (buf_t **thisp, int fidx, int lidx,
                                      bufiter_t *it, char *word, utf8 c) {
  (void) fidx; (void) lidx;
  int retval = 0;
  switch (c) {
    case 't':
      ifnot (retval = ved_translate_word (thisp, word))
        Msg.send_fmt ($myed, COLOR_RED, "Nothing matched the pattern [%s]", word);
      else if (OK is retval)
        Ed.scratch ($myed, thisp, 0);
      return retval;

      case 'm':
        return sys_man (thisp, word, -1);

#if HAS_SPELL
      case 'S':
        return __spell_word__ (thisp, fidx, lidx, it, word);
#endif
    default:
      break;
   }

  return OK;
}

private void __u_add_word_actions__ (ed_t *this) {
  int num_commands = 2;
#if HAS_SPELL
  num_commands++;
  utf8 chars[] = {'t', 'm', 'S'};
  char actions[] = "translate word\nman page\nSpell word";
#else
  utf8 chars[] = {'t', 'm'};
  char actions[] = "translate word\nman page";
#endif
  Ed.set.word_actions (this, chars, num_commands, actions, __u_word_actions_cb__);
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

private void __u_add_rline_user_commands__ (ed_t *this) {
/* user defined commands can begin with '~': associated in mind with '~' as $HOME */
  int num_commands = 1;
#if HAS_SPELL
  num_commands++;
  char *commands[3] = {"~battery", "~spell", NULL};
  int num_args[] = {0, 1, 0}; int flags[] = {0, RL_ARG_RANGE, 0};
#else
  char *commands[2] = {"~battery", NULL};
  int num_args[] = {0, 0}; int flags[] = {0, 0};
#endif

  Ed.append.rline_commands (this, commands, num_commands, num_args, flags);
}

private int sys_mkdir (char *dir, mode_t mode, int verbose) {
  ifnot (mkdir (dir, mode)) {
    if (verbose)
      Msg.send_fmt ($myed, COLOR_YELLOW, "created directory `%s', with mode: 0%o",
          dir, mode);
    return OK;
  }

/* to do: handle errno */
  return NOTOK;
}

private void __u_add_rline_sys_commands__ (ed_t *this) {
 /* sys defined commands can begin with '`': associated with shell syntax */
  int num_commands = 2;
  char *commands[3] = {"`mkdir", "`man", NULL};
  int num_args[] = {2, 1, 0}; int flags[] = {RL_ARG_FILENAME|RL_ARG_VERBOSE, 0, 0};
  Ed.append.rline_commands (this, commands, num_commands, num_args, flags);
  Ed.append.command_arg (this, "`man", "--section=");
}

/* this is the callback function that is called on the extended commands */
private int __u_rline_cb__ (buf_t **thisp, rline_t *rl, utf8 c) {
  (void) thisp; (void) c;
  int retval = NOTOK;
  string_t *com = Rline.get.command (rl);

  if (Cstring.eq (com->bytes, "~battery"))
    retval = sys_battery_info (NULL, 1);
  else if (Cstring.eq (com->bytes, "`mkdir")) {
    vstr_t *dirs = Rline.get.arg_fnames (rl, 1);
    if (NULL is dirs) goto theend;
    int is_verbose = Rline.arg.exists (rl, "verbose");
    retval = sys_mkdir (dirs->tail->data->bytes, S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH, is_verbose);
    Vstring.free (dirs);
  } else if (Cstring.eq (com->bytes, "`man")) {
    vstr_t *names = Rline.get.arg_fnames (rl, 1);
    if (NULL is names) goto theend;
    string_t *section = Rline.get.anytype_arg (rl, "section");
    int sect_id = (NULL is section ? 0 : atoi (section->bytes));
    retval = sys_man (thisp, names->head->data->bytes, sect_id);
    Vstring.free (names);
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

private void __init_usr__ (ed_t *this) {
  /* as a first sample, extend the actions on current word, triggered by 'W' */
  __u_add_word_actions__ (this);
  /* extend commands */
  __u_add_rline_commands__ (this);
  /* extend visual lw mode */
#if HAS_SPELL
  __u_add_lw_mode_actions__ (this);
#endif

  Uenv = AllocType (uenv);
  string_t *path = Ed.venv.get (this, "path");
  Uenv->man_exec = Ed.vsys.which ("man", path->bytes);
#if HAS_SPELL
  Intmap = __init_int_map__ ();
  SpellClass = __init_spell__ ();
#endif
}

private void __deinit_usr__ (ed_t *this) {
  (void) this;
  String.free (Uenv->man_exec);
  free (Uenv);
#if HAS_SPELL
  __deinit_spell__ (&SpellClass);
#endif
}
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

NewType (uenv,
  string_t *man_exec;
  string_t *elinks_exec;
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

private utf8 __spell_question__ (spell_t *spell, buf_t **thisp,
        action_t **action, int fidx, int lidx, bufiter_t *iter) {
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

  strcpy (spell->word, lword);
  spell->word_len = len;

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
  int retval = Rline.get.range (rl, *thisp, range);
  if (NOTOK is retval) {
    range[0] = Buf.get.row.current_col_idx (*thisp);
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
  spell_free (spell, SPELL_DONOT_CLEAR_DICTIONARY);
  return retval;
}

private int __u_word_actions_cb__ (buf_t **, int, int, bufiter_t *, char *, utf8);

#endif


/* this function that extends normal mode, performs a simple search on a
 * lexicon defined file for 'word', and then prints the matched lines to
 * the scratch buffer (this buffer can be closed with 'q' (as in a pager)):
 * requires the WORD_LEXICON_FILE to be defined with a way; my way is to
 * compile the distribution through a shell script, that invokes `make`
 * with my specific definitions
 */
private int __translate_word__ (buf_t **thisp, char *word) {
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

  Ed.append.toscratch ($myed, CLEAR, word);
  Ed.append.toscratch ($myed, DONOT_CLEAR, "=================");

  while (-1 isnot (nread = getline (&line, &len, fp)))
    if (0 <= Re.exec (re, line, nread)) {
      match++;
      Ed.append.toscratch ($myed, DONOT_CLEAR, line);
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
  int retval = 0;
  switch (c) {
    case 't':
      retval = __translate_word__ (thisp, word);
      if (0 is retval)
        Msg.send_fmt ($myed, COLOR_RED, "Nothing matched the pattern [%s]", word);
      else if (0 < retval)
        Ed.scratch ($myed, thisp, NOT_AT_EOF);
      return (retval > 0 ? OK : NOTOK);

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
  int num_commands = 3;
#if HAS_SPELL
  num_commands++;
  char *commands[5] = {"~battery", "~spell", "~translate", "@validate_utf8", NULL};
  int num_args[] = {0, 1, 0, 0, 0}; int flags[] = {0, RL_ARG_RANGE, 0, 0, 0};
#else
  char *commands[4] = {"~battery", "~translate", "@validate_utf8", NULL};
  int num_args[] = {0, 0, 0, 0}; int flags[] = {0, 0, 0, 0};
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
  } else if (Cstring.eq (com->bytes, "@validate_utf8")) {
    vstr_t *words = Rline.get.arg_fnames (rl, 1);
    if (NULL is words) goto theend;
    retval = __file_validate_utf8__ (thisp, words->head->data->bytes);
    Vstring.free (words);
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

  Uenv = AllocType (uenv);
  string_t *path = Ed.venv.get (this, "path");
  Uenv->man_exec = Ed.vsys.which ("man", path->bytes);
  Uenv->elinks_exec = Ed.vsys.which ("elinks", path->bytes);

  Ed.syn.append (this, u_syn[0]);
  Ed.syn.append (this, u_syn[1]);
}

private void __deinit_usr__ (ed_t *this) {
  (void) this;
  String.free (Uenv->man_exec);
  String.free (Uenv->elinks_exec);
  free (Uenv);

#if HAS_SPELL
  __deinit_spell__ (&SpellClass);
#endif
}

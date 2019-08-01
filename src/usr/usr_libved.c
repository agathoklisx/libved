/* user sample extension[s] (mostly personal), that since explore[s] the API,
 * this unit is also a vehicle to understand the needs and establish it */

/* a simple search on a lexicon for 'word', that prints the matched lines to
 * the scratch buffer (it can be closed quickly with 'q' (as a pager)):
 * requires the WORD_LEXICON_FILE to be defined with a way; my way is to
 * compile the distribution through a shell script, that invokes `make`
 * with my specific definitions
 */
private int ved_translate_word (buf_t **thisp, char *word) {
  (void) thisp;
  char *lex = NULL;

#ifndef WORD_LEXICON_FILE
  My(Msg).error ($myed, "%s(): lexikon has not been defined", __func__);
  return NOTOK;
#else
  lex = WORD_LEXICON_FILE;
#endif

  My(Msg).send_fmt ($myed, COLOR_YELLOW, "translating %s", word);

  ifnot (My(File).exists (lex)) {
    My(Msg).error ($myed, "%s(): lexicon has not been found", __func__);
    return NOTOK;
  }

  FILE *fp = fopen (lex, "r");
  if (NULL is fp) {
    My(Msg).error ($myed, My(Error).string ($myed, errno));
    return NOTOK;
  }

  regexp_t *re = My(Re).new (word, 0, RE_MAX_NUM_CAPTURES, My(Re).compile);
  size_t len;
  char *line = NULL;
  int nread;
  int match = 0;

  toscratch ($myed, "", 1);

  while (-1 isnot (nread = getline (&line, &len, fp)))
    if (0 <= My(Re).exec (re, line, nread)) {
      match++;
      toscratch ($myed, line, 0);
      My(Re).reset_captures (re);
    }

  fclose (fp);
  My(Re).free (re);
  if (line isnot NULL) free (line);

  return match;
}

/* the callback function when user press 'W' in normal mode */
private int __word_actions_cb__ (buf_t **thisp, char *word, utf8 c) {
  int retval = 0;

  switch (c) {
    case 't':
      ifnot (retval = ved_translate_word (thisp, word))
        My(Msg).send ($myed, COLOR_RED, "Nothing matched the pattern");
      else if (NOTOK isnot retval)
        Ed.scratch ($myed, thisp, 0);
      return 0;

    default:
      (void) thisp;
      break;
   }

  return 0;
}

/* this prints to the message line (through an over simplistic way) the battery
 * status and capacity (works for Linux) */
private int sys_battery_info (char *buf, int should_print) {
  int retval = NOTOK;

  /* use the SYS_NAME defined in the Makefile and avoid uname() for now */
  ifnot (My(Cstring).eq ("Linux", SYS_NAME)) {
    My(Msg).error ($myed, "battery function implemented for Linux");
    return NOTOK;
  }

/* not to be taken as a bad practice though obviously it is (it should be changed) */
#define SYS_BATTERY_DIR "/sys/class/power_supply"

  dirlist_t *dlist = My(Dir).list (SYS_BATTERY_DIR, 0);
  char *cap = NULL;
  char *status = NULL;

  if (NULL is dlist) return NOTOK;

  vstring_t *it = dlist->list->head;
  while (it) {
    ifnot (My(Cstring).cmp_n ("BAT", it->data->bytes, 3)) goto foundbat;
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
    My(Msg).send_fmt ($myed, COLOR_YELLOW, "[Battery is %s, remaining %s%%]",
        status, cap);

  ifnot (NULL is buf) snprintf (buf, 64, "[Battery is %s, remaining %s%%]",
      status, cap);

theend:
  ifnot (NULL is cap) free (cap);
  ifnot (NULL is status) free (status);
  dlist->free (dlist);
  return retval;
}

/* this is the callback function that is called on the extended by
 * the user commands
 */
private int __rline_cb__ (buf_t **thisp, rline_t *rl, utf8 c) {
  (void) thisp; (void) c;
  int retval = NOTOK;
  string_t *com = My(Rline).get.command (rl);

  if (My(Cstring).eq (com->bytes, "~battery"))
    retval = sys_battery_info (NULL, 1);

  My(String).free (com);
  return retval;
}

private void __add_rline_commands__ (ed_t *this) {
/* user defined commands can begin with '~': associated in mind with '~' as $HOME */
  char *commands[2] = {"~battery", NULL};
  int num_args[] = {0, 0}; int flags[] = {0, 0};

  Ed.append.rline_commands (this, commands, 1, num_args, flags);
  Ed.set.rline_cb (this, __rline_cb__);
}

private void __add_word_actions__ (ed_t *this) {
  utf8 chars[] = {'t'};
  char actions[] = "translate word\n";
  Ed.set.word_actions (this, chars, 1, actions, __word_actions_cb__);
}

private void __init_usr__ (ed_t *this) {
  /* as a first sample, extend the actions on current word, triggered by 'W' */
  __add_word_actions__ (this);
  /* extend commands */
  __add_rline_commands__ (this);
}

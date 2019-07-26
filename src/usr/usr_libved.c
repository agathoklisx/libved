/* user extension[s] */

private int ved_translate_word (buf_t **thisp, char *word) {
  (void) thisp;
  char *lex = NULL;

#ifndef WORD_LEXIKON_FILE
  My(Msg).error ($myed, "%s(): lexikon has not been defined", __func__);
  return NOTOK;
#else
  lex = WORD_LEXIKON_FILE;
#endif

  My(Msg).send_fmt ($myed, COLOR_YELLOW, "translating %s", word);

  ifnot (My(File).exists (lex)) {
    My(Msg).error ($myed, "%s(): lexikon has not been found", __func__);
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

private int word_actions_cb (buf_t **thisp, char *word, utf8 c) {
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

private void __init_usr__ (ed_t *this) {
/* as a sample, expand the actions on current word, triggered by 'W' */
  utf8 chars[] = {'t'};
  char actions[] = "translate word\n";
  Ed.set.word_actions (this, chars, 1, actions, word_actions_cb);

}

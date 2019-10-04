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

 /* The word database i use is from:
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
#define SPELL_CLEAR_IGNORED       1

#define SPELL_WORD_ISNOT_CORRECT -1
#define SPELL_WORD_IS_CORRECT 0
#define SPELL_WORD_IS_IGNORED 1

#define SPELL_CHANGED_WORD 1
#define SPELL_OK     0
#define SPELL_ERROR -1

NewType (spell,
 char
     word[MAXWORD],
     *dictionary;

  int
    retval;

  utf8 c;

  size_t
    num_dic_words,
    min_word_len,
    word_len;

  string_t *tmp;

  intmap_t
    *dic,
    *ign_words;

  vstr_t
    *words,
    *guesses,
    *messages;
);

typedef intmap_t spelldic_t;
static spelldic_t *CURRENT_DIC = NULL;

NewSelf (spell,
  spell_t *(*new) (void);
  void
    (*free) (spell_t *, int),
    (*clear) (spell_t *, int),
    (*add_word_to_dictionary) (spell_t *, char *);

  int
    (*init_dictionary) (spell_t *, char *, int, int),
    (*correct) (spell_t *);
);

NewClass (spell,
  Self (spell) self;
);

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

  if (clear_dic) {
    Imap.free (spell->dic);
    CURRENT_DIC = NULL;
  }

  free (spell->dictionary);
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
  char buf[spell->word_len];
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
      Vstring.cur.append_with (spell->guesses, that->data->bytes);
    that = that->next;
  }

  return SPELL_WORD_ISNOT_CORRECT;
}

private int spell_correct (spell_t *spell) {
  if (spell->word_len < spell->min_word_len or
      spell->word_len >= MAXWORD)
    return SPELL_WORD_IS_IGNORED;

  if (Imap.key_exists (spell->dic, spell->word)) return SPELL_WORD_IS_CORRECT;
  if (Imap.key_exists (spell->ign_words, spell->word)) return SPELL_WORD_IS_IGNORED;

  return spell_guess (spell);
}

private void spell_add_word_to_dictionary (spell_t *spell, char *word) {
  FILE *fp = fopen (spell->dictionary, "a+");
  fprintf (fp, "%s\n", word);
  fclose (fp);
}

private int spell_file_readlines_cb (vstr_t *unused, char *line, size_t len,
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
  vstr_t unused;
  File.readlines (spell->dictionary, &unused, spell_file_readlines_cb,
      (void *) spell);
  return spell->retval;
}

private int spell_init_dictionary (spell_t *spell, char *dic, int num_words, int force) {
  if (NULL is dic) return SPELL_ERROR;

  if (CURRENT_DIC isnot NULL and 0 is force) {
    spell->dic = CURRENT_DIC;
    return SPELL_OK;
  }

  if (-1 is access (dic, F_OK|R_OK)) {
    spell->retval = SPELL_ERROR;
    Vstring.append_with_fmt (spell->messages,
        "dictionary is not readable: |%s|\n" "errno: %d, error: %s",
        dic, errno, Error.string ($myed, errno));
    return spell->retval;
  }

  ifnot (NULL is spell->dictionary) free (spell->dictionary);

  spell->dictionary = Cstring.dup (dic, bytelen (dic));
  spell->dic = Imap.new (num_words);
  spell_read_dictionary (spell);
  CURRENT_DIC = spell->dic;
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
  return ClassInit (spell,
    .self = SelfInit (spell,
      .free = spell_free,
      .clear = spell_clear,
      .new = spell_new,
      .init_dictionary = spell_init_dictionary,
      .add_word_to_dictionary = spell_add_word_to_dictionary,
      .correct = spell_correct
     )
   );
}

public void __deinit_spell__ (spell_T *spell) {
  (void) spell;
  ifnot (NULL is CURRENT_DIC) Imap.free (CURRENT_DIC);
}

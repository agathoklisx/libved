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
  return (retval > 0 ? NOTOK : OK);
}

